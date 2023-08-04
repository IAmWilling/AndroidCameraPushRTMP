package com.example.rtmp_demo1;

import android.Manifest;
import android.app.Activity;
import android.content.pm.PackageManager;
import android.media.AudioFormat;
import android.media.AudioRecord;
import android.media.MediaRecorder;
import android.os.Build;
import android.util.Log;

import androidx.core.app.ActivityCompat;

public class AudioHelper implements Runnable {
    private static final int SAMPLE_RATE = 44100;
    private AudioRecord mAudioRecord;
    private Thread mAudioThread;
    private int mAudioRecordBufferSize;
    private boolean isStartRecording = false;
    private AudioDataCallback mAudioDataCallback;

    public AudioHelper(Activity activity) {
        this.mAudioThread = new Thread(this, "AudioThread");
        initAudioRecord(activity);
    }

    private void initAudioRecord(Activity activity) {
        this.mAudioRecordBufferSize = AudioRecord.getMinBufferSize(SAMPLE_RATE, AudioFormat.CHANNEL_IN_MONO, AudioFormat.ENCODING_PCM_16BIT);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            if (ActivityCompat.checkSelfPermission(activity, Manifest.permission.RECORD_AUDIO) != PackageManager.PERMISSION_GRANTED) {
                // TODO: Consider calling
                //    ActivityCompat#requestPermissions
                // here to request the missing permissions, and then overriding
                //   public void onRequestPermissionsResult(int requestCode, String[] permissions,
                //                                          int[] grantResults)
                // to handle the case where the user grants the permission. See the documentation
                // for ActivityCompat#requestPermissions for more details.
                return;
            }
            this.mAudioRecord = new AudioRecord.Builder()
                    .setAudioSource(MediaRecorder.AudioSource.MIC)
                    .setAudioFormat(new AudioFormat.Builder()
                            .setSampleRate(SAMPLE_RATE)
                            .setChannelMask(AudioFormat.CHANNEL_IN_MONO)
                            .setEncoding(AudioFormat.ENCODING_PCM_16BIT)
                            .build())
                    .setBufferSizeInBytes(this.mAudioRecordBufferSize)
                    .build();
        } else {
            this.mAudioRecord = new AudioRecord(MediaRecorder.AudioSource.MIC, SAMPLE_RATE, AudioFormat.CHANNEL_IN_MONO, AudioFormat.ENCODING_PCM_16BIT, this.mAudioRecordBufferSize);
        }
    }

    public void start() {
        if (!this.isStartRecording) {
            this.mAudioThread = new Thread(this, "AudioThread");
            this.mAudioThread.start();
        }
    }

    public void stop() {
        this.isStartRecording = false;
        this.mAudioRecord.stop();
    }

    public void setAudioDataCallback(AudioDataCallback callback) {
        this.mAudioDataCallback = callback;
    }


    @Override
    public void run() {
        this.mAudioRecord.startRecording();
        this.isStartRecording = true;
        byte[] data = new byte[this.mAudioRecordBufferSize];
        while (this.isStartRecording) {
            int res = this.mAudioRecord.read(data, 0, this.mAudioRecordBufferSize);
            if (res != AudioRecord.ERROR_INVALID_OPERATION && res != AudioRecord.ERROR_BAD_VALUE && res != AudioRecord.ERROR_DEAD_OBJECT) {
                if (this.mAudioDataCallback != null) {
                    // 计算采样数 16->ENCODING_PCM_16BIT
                    //采样位数
                    int bitsPerSample = 16;
                    //声道数
                    int channels = 1;
                    int bytesPerSample = bitsPerSample / 8;
                    int srcNbSamples = res / (bytesPerSample * channels);
                    Log.d("yumi","样本  count =  " + srcNbSamples);
                    this.mAudioDataCallback.onData(data, res, srcNbSamples);
                }
            }
        }
    }

    public interface AudioDataCallback {
        void onData(byte[] data, int length, int srcNbSamples);
    }
}
