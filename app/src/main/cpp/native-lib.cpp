#include <jni.h>
#include <string>
#include <unistd.h>
#include "header.h"

AVCodecContext *codec_ctx;
const AVCodec *codec;
AVFormatContext *out_ctx;
AVPacket *pkt;
AVFrame *frame;
AVCodecParameters *parameters;
int ret;
AVStream *out_stream;
int64_t pts = 0;
int real_width = 480, real_height = 320;
int fps = 30;
uint16_t frameCount = 0;

const char *rtmp_path;

bool isFront;

void rotateI420(jbyte *src_i420_data, jint width, jint height, jbyte *dst_i420_data, jint degree) {
    jint src_i420_y_size = width * height;
    jint src_i420_u_size = (width >> 1) * (height >> 1);

    jbyte *src_i420_y_data = src_i420_data;
    jbyte *src_i420_u_data = src_i420_data + src_i420_y_size;
    jbyte *src_i420_v_data = src_i420_data + src_i420_y_size + src_i420_u_size;

    jbyte *dst_i420_y_data = dst_i420_data;
    jbyte *dst_i420_u_data = dst_i420_data + src_i420_y_size;
    jbyte *dst_i420_v_data = dst_i420_data + src_i420_y_size + src_i420_u_size;

    if (degree == libyuv::kRotate90 || degree == libyuv::kRotate270 ||
        degree == libyuv::kRotate180 || degree == libyuv::kRotate0) {
        libyuv::I420Rotate((const uint8_t *) src_i420_y_data, width,
                           (const uint8_t *) src_i420_u_data, width >> 1,
                           (const uint8_t *) src_i420_v_data, width >> 1,
                           (uint8_t *) dst_i420_y_data, height,
                           (uint8_t *) dst_i420_u_data, height >> 1,
                           (uint8_t *) dst_i420_v_data, height >> 1,
                           width, height,
                           (libyuv::RotationMode) degree);
    }
}

void mirrorI420(uint8_t *src_i420_data, jint width, jint height, uint8_t *dst_i420_data) {
    jint src_i420_y_size = width * height;
    jint src_i420_u_size = (width >> 1) * (height >> 1);

    uint8_t *src_i420_y_data = src_i420_data;
    uint8_t *src_i420_u_data = src_i420_data + src_i420_y_size;
    uint8_t *src_i420_v_data = src_i420_data + src_i420_y_size + src_i420_u_size;

    uint8_t *dst_i420_y_data = dst_i420_data;
    uint8_t *dst_i420_u_data = dst_i420_data + src_i420_y_size;
    uint8_t *dst_i420_v_data = dst_i420_data + src_i420_y_size + src_i420_u_size;

    libyuv::I420Mirror((const uint8_t *) src_i420_y_data, width,
                       (const uint8_t *) src_i420_u_data, width >> 1,
                       (const uint8_t *) src_i420_v_data, width >> 1,
                       (uint8_t *) dst_i420_y_data, width,
                       (uint8_t *) dst_i420_u_data, width >> 1,
                       (uint8_t *) dst_i420_v_data, width >> 1,
                       width, height);
}

void init() {
    avformat_network_init();

    avformat_alloc_output_context2(&out_ctx, NULL, "flv", rtmp_path);
    codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!codec) {
        return;
    }
    codec_ctx = avcodec_alloc_context3(codec);
    if (!codec_ctx) {
        return;
    }
    if (!out_ctx) {
        fprintf(stderr, "Could not allocate output context\n");
        return;
    }
    //设置编码

    codec_ctx->codec_type = AVMEDIA_TYPE_VIDEO;
    codec_ctx->width = real_width;
    codec_ctx->height = real_height;
    codec_ctx->bit_rate = 1000000;
    codec_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
    codec_ctx->level = 50;
    codec_ctx->thread_count = 8;
    codec_ctx->profile = FF_PROFILE_H264_HIGH_444;
    codec_ctx->coded_height = real_height;
    codec_ctx->coded_width = real_width;
    codec_ctx->sample_aspect_ratio = (AVRational) {1, 1};
    codec_ctx->framerate = (AVRational) {fps, 1};
    codec_ctx->time_base = (AVRational) {1, fps};
    codec_ctx->gop_size = 8;
    codec_ctx->max_b_frames = 0;
    codec_ctx->has_b_frames = 0;
    codec_ctx->refs = 1;
    if (out_ctx->oformat->flags & AVFMT_GLOBALHEADER) {
        codec_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }
    AVDictionary *param = 0;
    if (codec_ctx->codec_id == AV_CODEC_ID_H264) {
        //固定设置
        codec_ctx->qmin = 10;
        codec_ctx->qmax = 51;
        codec_ctx->qcompress = (float) 0.6;
        //设置编码速度为慢，越慢质量越好 适中即可
        av_opt_set(codec_ctx->priv_data, "preset", "medium", 0);
        //设置0延迟编码
//        av_dict_set(&param, "preset", "superfast", 0);
//        av_dict_set(&param, "tune", "zerolatency", 0);
        av_opt_set(codec_ctx->priv_data, "tune", "zerolatency", 0);
    }

    //打开编码器
    if (avcodec_open2(codec_ctx, codec, NULL) < 0) {
        return;
    }

    out_stream = avformat_new_stream(out_ctx, NULL);
    if (!out_stream) {
        fprintf(stderr, "Could not add video stream\n");
        return;
    }

    // Set codec parameters
    out_stream->codecpar->codec_tag = 0;
    out_stream->codecpar->codec_id = AV_CODEC_ID_H264;
    out_stream->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    out_stream->time_base.num = 1;
    out_stream->time_base.den = fps;

    if (avcodec_parameters_from_context(out_stream->codecpar, codec_ctx) < 0) {
        LOGE("Failed av codec parameters_from_context");
        return;
    }

    AVDictionary *format_opts = NULL;
    av_dict_set(&format_opts, "rw_timeout", "1000000", 0); //设置超时时间,单位mcs
    // Open rtmp connection
    ret = avio_open2(&out_ctx->pb, out_ctx->url, AVIO_FLAG_READ_WRITE, NULL, &format_opts);
    if (ret < 0) {
        fprintf(stderr, "Could not open rtmp connection: %s\n", av_err2str(ret));
        return;
    }


    // Write flv header
    ret = avformat_write_header(out_ctx, NULL);
    if (ret < 0) {
        fprintf(stderr, "Could not write flv header: %s\n", av_err2str(ret));
        return;
    }

}


extern "C" JNIEXPORT jstring JNICALL
Java_com_example_rtmp_1demo1_MainActivity_stringFromJNI(
        JNIEnv *env,
        jobject /* this */) {
    std::string hello = "Hello from C++";
    return env->NewStringUTF(hello.c_str());
}
extern "C"
JNIEXPORT jbyteArray JNICALL
Java_com_example_rtmp_1demo1_MainActivity_libyuvI420Roate90(JNIEnv *env, jobject thiz,
                                                            jbyteArray i420, jint width,
                                                            jint height) {
    //申请目标与src数据大小的内存
    jbyte *bytess = env->GetByteArrayElements(i420, 0);
    jbyteArray dst_i420_a = env->NewByteArray(width * height * 3 / 2);
    jbyte *dst_i420 = env->GetByteArrayElements(dst_i420_a, 0);
    //旋转90度
    rotateI420(bytess, width, height, dst_i420, 270);
    //申请与拷贝目标数据 转换到java中
    jbyteArray array = env->NewByteArray(width * height * 3 / 2);
    env->SetByteArrayRegion(array, 0, width * height * 3 / 2, (jbyte *) dst_i420);
    //旋转后宽高应该置换
    real_width = height;
    real_height = width;
    return array;
}
int64_t index;

extern "C"
JNIEXPORT void JNICALL
Java_com_example_rtmp_1demo1_MainActivity_native_1ffmpeg_1push_1rtmp(JNIEnv *env, jobject thiz,
                                                                     jbyteArray data) {
    if (codec_ctx == nullptr) {
        init();
    }
    frameCount += 1;
    int y_size = real_height * real_width;
    jbyte *i420 = env->GetByteArrayElements(data, 0);
    frame = av_frame_alloc();
    frame->width = real_width;
    frame->height = real_height;
    frame->format = codec_ctx->pix_fmt;
    frame->pts = frameCount;
    uint8_t *i420_data_mirror = (uint8_t *) malloc(y_size * 3 / 2);
    if (isFront) {

        //水平镜像处理
        mirrorI420((uint8_t *) i420, real_width, real_height, i420_data_mirror);
        av_image_fill_arrays(
                frame->data, frame->linesize,
                i420_data_mirror, codec_ctx->pix_fmt, codec_ctx->width,
                codec_ctx->height, 1
        );

    } else {
        av_image_fill_arrays(
                frame->data, frame->linesize,
                (uint8_t *) i420, codec_ctx->pix_fmt, codec_ctx->width,
                codec_ctx->height, 1
        );
    }

    ret = avcodec_send_frame(codec_ctx, frame);
    if (ret < 0) {
        av_frame_free(&frame);
        return;
    }
    pkt = av_packet_alloc();
    while (avcodec_receive_packet(codec_ctx, pkt) >= 0) {
        pkt->pts = frameCount * (out_stream->time_base.den) / ((out_stream->time_base.num) * fps);
        pkt->dts = pkt->pts;
        pkt->duration = (out_stream->time_base.den) / ((out_stream->time_base.num) * fps);
        pkt->stream_index = out_stream->index;
        pkt->pos = -1;
        ret = av_interleaved_write_frame(out_ctx, pkt);
        if (ret < 0) {
            av_packet_free(&pkt);
            av_frame_free(&frame);
            av_free(i420_data_mirror);
            LOGD("Could not write frame: %s\n", av_err2str(ret));
            return;
        }
        av_packet_unref(pkt);
    }
    av_packet_free(&pkt);
    av_frame_free(&frame);
    av_free(i420_data_mirror);
}
extern "C"
JNIEXPORT void JNICALL
Java_com_example_rtmp_1demo1_MainActivity_native_1set_1rtmp_1path(JNIEnv *env, jobject thiz,
                                                                  jstring trim) {
    rtmp_path = env->GetStringUTFChars(trim, 0);
}
extern "C"
JNIEXPORT jbyteArray JNICALL
Java_com_example_rtmp_1demo1_MainActivity_native_1i420_1roate(JNIEnv *env, jobject thiz,
                                                              jbyteArray i420, jint width,
                                                              jint height, jint rote) {
    //申请目标与src数据大小的内存
    jbyte *bytess = env->GetByteArrayElements(i420, 0);
    jbyteArray dst_i420_a = env->NewByteArray(width * height * 3 / 2);
    jbyte *dst_i420 = env->GetByteArrayElements(dst_i420_a, 0);
    rotateI420(bytess, width, height, dst_i420, rote);
    //申请与拷贝目标数据 转换到java中
    jbyteArray array = env->NewByteArray(width * height * 3 / 2);
    env->SetByteArrayRegion(array, 0, width * height * 3 / 2, (jbyte *) dst_i420);
    //旋转后宽高应该置换
    real_width = height;
    real_height = width;
    return array;
}
extern "C"
JNIEXPORT void JNICALL
Java_com_example_rtmp_1demo1_MainActivity_native_1set_1camera_1position(JNIEnv *env, jobject thiz,
                                                                        jboolean is_front) {
    isFront = is_front;
}
extern "C"
JNIEXPORT void JNICALL
Java_com_example_rtmp_1demo1_MainActivity_native_1ffmpeg_1push_1rtmp_1stop(JNIEnv *env,
                                                                           jobject thiz) {
    av_write_trailer(out_ctx);
    avio_close(out_ctx->pb);
    avformat_free_context(out_ctx);
}