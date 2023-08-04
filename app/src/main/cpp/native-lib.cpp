#include <jni.h>
#include <string>
#include <unistd.h>
#include "header.h"

AVCodecContext *codec_ctx, *audio_codec_ctx;
const AVCodec *codec;
const AVCodec *audio_code;
AVFormatContext *out_ctx;
AVPacket *pkt, *audio_pkt;
AVFrame *frame, *audio_frame;
AVCodecParameters *parameters;
int ret;
AVStream *out_stream, *audio_stream;
int64_t pts = 0;
int real_width = 480, real_height = 640;
int fps = 30;
uint16_t frameCount = 0;
uint16_t audioCount = 0;
const char *rtmp_path;

bool isFront;

bool isFirstLoadInit = true;

//是否开始
bool is_start = false;
//推送包队列
std::queue<AVPacket *> push_video_queue;
std::queue<AVPacket *> push_audio_queue;

//推流线程
pthread_t push_thread;


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

void initAudioEncode() {
    int ret;
//    audio_code = avcodec_find_encoder(AV_CODEC_ID_AAC);
    audio_code = avcodec_find_encoder_by_name("libfdk_aac");
    if (!audio_code) {
        return;
    }
    audio_codec_ctx = avcodec_alloc_context3(audio_code);
    if (!audio_codec_ctx) {
        return;
    }
    //按照编码复杂度和音频质量从高到低的顺序排列
    int supportProfile[] = {
            FF_PROFILE_AAC_MAIN,
            FF_PROFILE_AAC_LTP,
            FF_PROFILE_AAC_LD,
            FF_PROFILE_AAC_HE_V2,
            FF_PROFILE_AAC_HE,
            FF_PROFILE_AAC_LOW,
            FF_PROFILE_AAC_SSR,
            FF_PROFILE_AAC_ELD
    };
    int supProfile = -1;
    //直接获取可支持的配置项
    for (const auto &item: supportProfile) {
        const char *profile = NULL;
        profile = avcodec_profile_name(audio_code->id, item);
        if (profile != NULL) {
            supProfile = item;
            break;
        }
    }

    //设置audio编码参数
    audio_codec_ctx->codec_type = AVMEDIA_TYPE_AUDIO;
    audio_codec_ctx->profile = supProfile;
    audio_codec_ctx->sample_fmt = AV_SAMPLE_FMT_S16;

    audio_codec_ctx->channel_layout = AV_CH_LAYOUT_MONO;
    audio_codec_ctx->channels = 1;
    audio_codec_ctx->bit_rate = 64000;
    audio_codec_ctx->sample_rate = 44100;
    audio_codec_ctx->thread_count = 8;
    ret = avcodec_open2(audio_codec_ctx, audio_code, NULL);
    if (ret < 0) {
        return;
    }
    audio_stream = avformat_new_stream(out_ctx, audio_code);
    if (!audio_stream) {
        return;
    }
    audio_stream->codecpar->codec_id = AV_CODEC_ID_AAC;
    audio_stream->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
    audio_stream->time_base.num = 1;
    audio_stream->time_base.den = fps;
    ret = avcodec_parameters_from_context(audio_stream->codecpar, audio_codec_ctx);
    if (ret < 0) {
        return;
    }
}

void initVideoEncode() {

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
    //设置video编码

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


    out_stream = avformat_new_stream(out_ctx, codec);
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
    out_stream->r_frame_rate = AVRational{fps, 1};
    if (avcodec_parameters_from_context(out_stream->codecpar, codec_ctx) < 0) {
        LOGE("Failed av codec parameters_from_context");
        return;
    }

}

void init() {
    avformat_network_init();
    avformat_alloc_output_context2(&out_ctx, NULL, "flv", rtmp_path);
    AVDictionary *format_opts = NULL;
    av_dict_set(&format_opts, "rw_timeout", "1000000", 0); //设置超时时间,单位mcs
    // Open rtmp connection
    ret = avio_open2(&out_ctx->pb, out_ctx->url, AVIO_FLAG_READ_WRITE, NULL, &format_opts);
    if (ret < 0) {
        fprintf(stderr, "Could not open rtmp connection: %s\n", av_err2str(ret));
        return;
    }
    initAudioEncode();
    initVideoEncode();
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
    codec_ctx->coded_width = real_height;
    return array;
}
int64_t index;

extern "C"
JNIEXPORT void JNICALL
Java_com_example_rtmp_1demo1_MainActivity_native_1ffmpeg_1push_1rtmp(JNIEnv *env, jobject thiz,
                                                                     jbyteArray data, jlong time) {
    if (!codec_ctx) {
        return;
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
    int64_t calc_duration = (double) AV_TIME_BASE / av_q2d(out_stream->r_frame_rate);

    pkt = av_packet_alloc();
    while (avcodec_receive_packet(codec_ctx, pkt) >= 0) {
        pkt->pts = (double) (frameCount * calc_duration) /
                   (double) (av_q2d(out_stream->time_base) * AV_TIME_BASE);
//        pkt->pts = frameCount * (out_stream->time_base.den) / ((out_stream->time_base.num) * fps);
//        pkt->pts = (long)(time * AV_TIME_BASE);
        pkt->dts = pkt->pts;
//        pkt->duration = (out_stream->time_base.den) / ((out_stream->time_base.num) * fps);
        pkt->stream_index = out_stream->index;
        pkt->pos = -1;
        AVPacket *avPacket = av_packet_clone(pkt);
        //加入队列 线程中等待发送
        push_video_queue.push(avPacket);
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
//    real_width = height;
//    real_height = width;
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
extern "C"
JNIEXPORT void JNICALL
Java_com_example_rtmp_1demo1_MainActivity_native_1encode_1audio(JNIEnv *env, jobject thiz,
                                                                jbyteArray data,
                                                                jint src_nb_samples, jdouble time) {
    if (!audio_codec_ctx) {
        return;
    }
    audioCount += 1;
    int src_buf_size = env->GetArrayLength(data);
    jbyte *src_data = env->GetByteArrayElements(data, 0);
    // 创建指针数组
//    uint8_t *src_data_array[1] = {(uint8_t *) src_data};
//
//    // 转换为 uint8_t ** 类型的指针
//    uint8_t **src_data_ptr = src_data_array;
//    //传过来的是AV_SAMPLE_FMT_S16
//    SwrContext *swr_ctx = swr_alloc_set_opts(NULL,
//                                             audio_codec_ctx->channel_layout,
//                                             audio_codec_ctx->sample_fmt,
//                                             audio_codec_ctx->sample_rate,
//                                             audio_codec_ctx->channel_layout,
//                                             AV_SAMPLE_FMT_S16,
//                                             audio_codec_ctx->sample_rate,
//                                             0,
//                                             NULL);
//    if (!swr_ctx) {
//        return;
//    }
//    if (swr_init(swr_ctx) < 0) {
//        swr_free(&swr_ctx);
//        return;
//    }
    audio_frame = av_frame_alloc();
    audio_frame->format = audio_codec_ctx->sample_fmt;
    audio_frame->nb_samples =
            src_buf_size / (av_get_bytes_per_sample(AV_SAMPLE_FMT_S16) * audio_codec_ctx->channels);
    audio_frame->channels = audio_codec_ctx->channels;
//    audio_frame->ch_layout = audio_codec_ctx->ch_layout;
    audio_frame->sample_rate = audio_codec_ctx->sample_rate;
    audio_frame->channel_layout = audio_codec_ctx->channel_layout;
    audio_frame->pts = audioCount;

// Allocate the data buffer
//    int ret = av_frame_get_buffer(audio_frame, 0);
    ret = av_samples_alloc(audio_frame->data, nullptr, audio_codec_ctx->channels,
                           audio_frame->nb_samples,
                           audio_codec_ctx->sample_fmt, 0);

    int64_t audio_frame_duration =
            (double) AV_TIME_BASE * src_nb_samples / audio_codec_ctx->sample_rate;
    if (ret < 0) {
        // Handle error
        goto datafree;
    }
    ret = avcodec_fill_audio_frame(audio_frame, audio_codec_ctx->channels, AV_SAMPLE_FMT_S16,
                                   (uint8_t *) src_data, src_buf_size, 0);
    if (ret < 0) {
        goto datafree;
    }
// Copy the data
//    memcpy(audio_frame->data[0], src_data, src_buf_size);
//    // 分配输出缓冲区
//    ret = av_samples_alloc(audio_frame->data, nullptr, audio_codec_ctx->channels,
//                           audio_codec_ctx->frame_size,
//                           audio_codec_ctx->sample_fmt, 0);
//    int64_t dst_nb_samples =
//            av_rescale_rnd(src_nb_samples, audio_codec_ctx->sample_rate, audio_frame->sample_rate,
//                           AV_ROUND_UP);
//    if (ret < 0) {
//        LOGD("分配缓冲区错误 av_samples_alloc_array_and_samples error = ", av_err2str(ret));
//        goto datafree;
//    }
//
//    // 转换采样格式
//    ret = swr_convert(swr_ctx, audio_frame->data, dst_nb_samples,
//                      (const uint8_t **) src_data_ptr,
//                      src_nb_samples);
//    if (ret < 0) {
//        LOGD("转换采样格式错误 swr_convert error_str = ", av_err2str(ret));
//        goto datafree;
//    }


    ret = avcodec_send_frame(audio_codec_ctx, audio_frame);
    if (ret < 0) {
        LOGD("音频发送到编码器错误 avcodec_send_frame error = %s", av_err2str(ret));
        goto datafree;
    }


    audio_pkt = av_packet_alloc();
    while (avcodec_receive_packet(audio_codec_ctx, audio_pkt) >= 0) {
        audio_pkt->pts = (double) (audioCount * audio_frame_duration) /
                         (double) (av_q2d(audio_stream->time_base) * AV_TIME_BASE);
//        audio_pkt->pts =
//                audioCount * (audio_stream->time_base.den) / ((audio_stream->time_base.num) * fps);
        audio_pkt->dts = audio_pkt->pts;
//        audio_pkt->duration = (audio_stream->time_base.den) / ((audio_stream->time_base.num) * fps);
        audio_pkt->stream_index = audio_stream->index;
        audio_pkt->pos = -1;
        AVPacket *p = av_packet_clone(audio_pkt);
        push_audio_queue.push(p);
        av_packet_unref(audio_pkt);
    }
    datafree:
    av_frame_free(&audio_frame);
    av_packet_free(&audio_pkt);
//    av_freep(&dst_data[0]);
//    av_freep(&dst_data);
    env->ReleaseByteArrayElements(data, src_data, 0);
//    swr_free(&swr_ctx);
}

void *push_thread_rtmp(void *obj) {
    int ret = 0;
    while (is_start) {
        if (!push_video_queue.empty()) {
            AVPacket *video_pkt = push_video_queue.front();
            if (video_pkt != NULL) {
                ret = av_interleaved_write_frame(out_ctx, video_pkt);
                if (ret < 0) {
                    LOGD("video推流失败")
                } else {
                    LOGD("正在推流视频画面")
                }
                av_packet_free(&video_pkt);
            }
            push_video_queue.pop();
        }

        if (!push_audio_queue.empty()) {
            AVPacket *a_pkt = push_audio_queue.front();
            if (a_pkt != NULL) {
                LOGD("2")
                try {
                    ret = av_interleaved_write_frame(out_ctx, a_pkt);
                    if (ret < 0) {
                        LOGD("音频推流失败")
                    } else {
                        LOGD("正在推流音频数据")
                    }
                } catch (std::exception exception) {
                    LOGD("出现错误 %s", exception.what());
                }

                av_packet_free(&a_pkt);

            }
            push_audio_queue.pop();
        }
    }
    return nullptr;
}


extern "C"
JNIEXPORT void JNICALL
Java_com_example_rtmp_1demo1_MainActivity_native_1only_1init_1once(JNIEnv *env, jobject thiz,
                                                                   jint width, jint height) {
    if (isFirstLoadInit) {
        //翻转
        real_height = width;
        real_width = height;
        init();
        isFirstLoadInit = false;
    }

}
extern "C"
JNIEXPORT void JNICALL
Java_com_example_rtmp_1demo1_MainActivity_native_1set_1start_1flag(JNIEnv *env, jobject thiz,
                                                                   jboolean start) {
    is_start = start;
    //true启动线程 false线程直接关闭
    if (is_start) {
        pthread_create(&push_thread, NULL, push_thread_rtmp, NULL);
    }

}