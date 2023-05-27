package com.example.rtmp_demo1;

import androidx.annotation.NonNull;
import androidx.appcompat.app.AppCompatActivity;
import androidx.camera.core.AspectRatio;
import androidx.camera.core.Camera;
import androidx.camera.core.CameraSelector;
import androidx.camera.core.ImageAnalysis;
import androidx.camera.core.ImageProxy;
import androidx.camera.core.Preview;
import androidx.camera.lifecycle.ProcessCameraProvider;
import androidx.camera.view.PreviewView;
import androidx.core.app.ActivityCompat;
import androidx.core.content.ContextCompat;
import androidx.lifecycle.LifecycleOwner;

import android.Manifest;
import android.content.pm.PackageManager;
import android.graphics.ImageFormat;
import android.graphics.Rect;
import android.media.Image;
import android.os.Bundle;
import android.util.Size;
import android.view.Surface;
import android.view.View;
import android.widget.TextView;

import com.example.rtmp_demo1.databinding.ActivityMainBinding;
import com.google.common.util.concurrent.ListenableFuture;

import java.nio.ByteBuffer;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.Executor;
import java.util.concurrent.Executors;

public class MainActivity extends AppCompatActivity {
    static {
        System.loadLibrary("jpeg");
        System.loadLibrary("turbojpeg");
        System.loadLibrary("yuv");
        System.loadLibrary("x264");
        System.loadLibrary("androidcamerapushrtmp");
        System.loadLibrary("avcodec");
        System.loadLibrary("avfilter");
        System.loadLibrary("avformat");
        System.loadLibrary("avutil");
        System.loadLibrary("swresample");
        System.loadLibrary("swscale");
        System.loadLibrary("postproc");
        System.loadLibrary("avdevice");
    }

    private ActivityMainBinding binding;
    private ListenableFuture<ProcessCameraProvider> cameraProviderFuture;
    private Executor executor = Executors.newSingleThreadExecutor();
    long count = 0;

    private boolean isPush = true;

    private boolean startPush = false;

    private int CAMERA_SHOOTING_POSITION = CameraSelector.LENS_FACING_FRONT;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        binding = ActivityMainBinding.inflate(getLayoutInflater());
        setContentView(binding.getRoot());
        if (ActivityCompat.checkSelfPermission(this, Manifest.permission.CAMERA) != PackageManager.PERMISSION_GRANTED) {
            ActivityCompat.requestPermissions(this, new String[]{Manifest.permission.CAMERA}, 0);
        }
        native_set_camera_position(CAMERA_SHOOTING_POSITION == CameraSelector.LENS_FACING_FRONT);
        binding.viewFinder.setImplementationMode(PreviewView.ImplementationMode.COMPATIBLE);
        cameraProviderFuture = ProcessCameraProvider.getInstance(this);
        //当cameraProviderFuture初始化完成后进行绑定预览view
        cameraProviderFuture.addListener(() -> {
            try {
                ProcessCameraProvider cameraProvider = cameraProviderFuture.get();
                bindPreview(cameraProvider);
            } catch (ExecutionException | InterruptedException e) {
            }
        }, ContextCompat.getMainExecutor(this));
        binding.push.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                
                if (binding.editText.getText().toString().trim().length() < 0) {
                    return;
                }
                if (isPush) {
                    native_set_rtmp_path(binding.editText.getText().toString().trim());
                    startPush = true;
                    binding.push.setText("结束");
                } else {
                    startPush = false;
                    binding.push.setText("推流");
                    native_ffmpeg_push_rtmp_stop();
                }
                isPush = !isPush;
            }
        });

        binding.select.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                if (CAMERA_SHOOTING_POSITION == CameraSelector.LENS_FACING_FRONT) {

                    CAMERA_SHOOTING_POSITION = CameraSelector.LENS_FACING_BACK;
                } else {

                    CAMERA_SHOOTING_POSITION = CameraSelector.LENS_FACING_FRONT;

                }
                try {
                    bindPreview(cameraProviderFuture.get());
                    native_set_camera_position(CAMERA_SHOOTING_POSITION == CameraSelector.LENS_FACING_FRONT);
                } catch (ExecutionException e) {
                    throw new RuntimeException(e);
                } catch (InterruptedException e) {
                    throw new RuntimeException(e);
                }
            }
        });

    }

    void bindPreview(ProcessCameraProvider cameraProvider) {
        Preview preview = new Preview.Builder().build();

        CameraSelector cameraSelector = new CameraSelector.Builder().requireLensFacing(CAMERA_SHOOTING_POSITION).build();


        preview.setSurfaceProvider(binding.viewFinder.getSurfaceProvider());
        ImageAnalysis imageAnalysis = new ImageAnalysis.Builder().setTargetResolution(new Size(640, 480)).setTargetRotation(Surface.ROTATION_270)
//                        .setTargetAspectRatio(AspectRatio.RATIO_16_9)
                .setOutputImageFormat(ImageAnalysis.OUTPUT_IMAGE_FORMAT_YUV_420_888).setBackpressureStrategy(ImageAnalysis.STRATEGY_KEEP_ONLY_LATEST).build();

        imageAnalysis.setAnalyzer(executor, new ImageAnalysis.Analyzer() {
            @Override
            public void analyze(@NonNull ImageProxy imageProxy) {
                Image image = imageProxy.getImage();
                int width = image.getWidth();
                int height = image.getHeight();
                if (startPush) {
                    //转换I420格式
                    byte[] i420 = getDataFromImage(image, COLOR_FormatI420);
                    //后置相机 270 90
                    //前置相机需要做镜面处理 90 270 【180 270 也不是不行】【270 270需要镜面】
                    //yuv旋转270 宽高调整
                    byte[] i420_Roate270 = native_i420_roate(i420, width, height, CAMERA_SHOOTING_POSITION == CameraSelector.LENS_FACING_FRONT ? 270 : 90);
                    native_ffmpeg_push_rtmp(i420_Roate270);
                }
                imageProxy.close();
            }
        });
        cameraProvider.unbindAll();
        cameraProvider.bindToLifecycle((LifecycleOwner) this, cameraSelector, preview, imageAnalysis);
    }

    private static final int COLOR_FormatI420 = 1;
    private static final int COLOR_FormatNV21 = 2;

    private static boolean isImageFormatSupported(Image image) {
        int format = image.getFormat();
        switch (format) {
            case ImageFormat.YUV_420_888:
            case ImageFormat.NV21:
            case ImageFormat.YV12:
                return true;
        }
        return false;
    }

    private byte[] convertYuvBuffer(Image image) {
        int w = image.getWidth();
        int h = image.getHeight();
        int len = w * h * 3 / 2;
        byte[] outBuffer = new byte[len];
        Image.Plane yPlane = image.getPlanes()[0]; //y
        ByteBuffer yBuffer = yPlane.getBuffer();//y
        Image.Plane uPlane = image.getPlanes()[1];//u
        ByteBuffer uBuffer = uPlane.getBuffer();//u
        Image.Plane vPlane = image.getPlanes()[2];//v
        ByteBuffer vBuffer = vPlane.getBuffer();//v
        int yPix = 0;
        int uPix = 0;
        int vPix = 0;
        //排列组合yuv
        for (int i = 0; i < len; i++) {
            if (i < w * h) {
                outBuffer[i] = yBuffer.get(yPix);
                yPix += yPlane.getPixelStride();
            } else if ((i >= w * h) && (i < w * h * 1.25)) {
                outBuffer[i] = uBuffer.get(uPix);
                uPix += uPlane.getPixelStride();
            } else if ((i >= w * h * 1.25) && (i < w * h * 1.5)) {
                outBuffer[i] = vBuffer.get(vPix);
                vPix += vPlane.getPixelStride();
            }
        }
        return outBuffer;
    }

    private static byte[] getDataFromImage(Image image, int colorFormat) {
        if (colorFormat != COLOR_FormatI420 && colorFormat != COLOR_FormatNV21) {
            throw new IllegalArgumentException("only support COLOR_FormatI420 " + "and COLOR_FormatNV21");
        }
        if (!isImageFormatSupported(image)) {
            throw new RuntimeException("can't convert Image to byte array, format " + image.getFormat());
        }
        Rect crop = image.getCropRect();
        int format = image.getFormat();
        int width = crop.width();
        int height = crop.height();
        Image.Plane[] planes = image.getPlanes();
        byte[] data = new byte[width * height * ImageFormat.getBitsPerPixel(format) / 8];
        byte[] rowData = new byte[planes[0].getRowStride()];
        int channelOffset = 0;
        int outputStride = 1;
        for (int i = 0; i < planes.length; i++) {
            switch (i) {
                case 0:
                    channelOffset = 0;
                    outputStride = 1;
                    break;
                case 1:
                    if (colorFormat == COLOR_FormatI420) {
                        channelOffset = width * height;
                        outputStride = 1;
                    } else if (colorFormat == COLOR_FormatNV21) {
                        channelOffset = width * height + 1;
                        outputStride = 2;
                    }
                    break;
                case 2:
                    if (colorFormat == COLOR_FormatI420) {
                        channelOffset = (int) (width * height * 1.25);
                        outputStride = 1;
                    } else if (colorFormat == COLOR_FormatNV21) {
                        channelOffset = width * height;
                        outputStride = 2;
                    }
                    break;
            }
            ByteBuffer buffer = planes[i].getBuffer();
            int rowStride = planes[i].getRowStride();
            int pixelStride = planes[i].getPixelStride();

            int shift = (i == 0) ? 0 : 1;
            int w = width >> shift;
            int h = height >> shift;
            buffer.position(rowStride * (crop.top >> shift) + pixelStride * (crop.left >> shift));
            for (int row = 0; row < h; row++) {
                int length;
                if (pixelStride == 1 && outputStride == 1) {
                    length = w;
                    buffer.get(data, channelOffset, length);
                    channelOffset += length;
                } else {
                    length = (w - 1) * pixelStride + 1;
                    buffer.get(rowData, 0, length);
                    for (int col = 0; col < w; col++) {
                        data[channelOffset] = rowData[col * pixelStride];
                        channelOffset += outputStride;
                    }
                }
                if (row < h - 1) {
                    buffer.position(buffer.position() + rowStride - length);
                }
            }
        }
        return data;
    }

    /**
     * RTMP推流地址
     *
     * @param trim
     */
    public native void native_set_rtmp_path(String trim);

    /**
     * i420旋转
     *
     * @param i420   frame
     * @param width
     * @param height
     * @param angle
     * @return
     */
    public native byte[] native_i420_roate(byte[] i420, int width, int height, int angle);

    /**
     * 推流
     *
     * @param data
     */
    public native void native_ffmpeg_push_rtmp(byte[] data);

    /**
     * 设置相机是前置还是后置
     *
     * @param isFront true前 false后
     */
    public native void native_set_camera_position(boolean isFront);

    /**
     * 结束推流
     */
    public native void native_ffmpeg_push_rtmp_stop();
}