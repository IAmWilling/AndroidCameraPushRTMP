//
// Created by mt220718 on 2023/3/27.
//

#ifndef ANDROID_VIDEOINTERCEPT_HEADER_H
#define ANDROID_VIDEOINTERCEPT_HEADER_H
#include "queue"
#include "thread"
extern "C" {


#include "libavcodec/avcodec.h"
#include "libavfilter/avfilter.h"
#include "libavformat/avformat.h"
#include "libavutil/avutil.h"
#include "libavutil/frame.h"
#include "libavutil/ffversion.h"
#include "libswresample/swresample.h"
#include "libswscale/swscale.h"
#include "libpostproc/postprocess.h"
#include "libavutil/imgutils.h"
#include "libavutil/time.h"
#include "libavutil/opt.h"
#include "libavfilter/buffersrc.h"
#include "libavfilter/buffersink.h"
#include "libyuv/convert.h"
#include <android/log.h>
#define TAG "yumi"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, TAG, __VA_ARGS__);
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__);
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__);
};
#endif //ANDROID_VIDEOINTERCEPT_HEADER_H
