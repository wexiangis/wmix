/*
 *  平台类型全局定义
 */
#ifndef _WMIX_PLAT_H_
#define _WMIX_PLAT_H_

#include <stdint.h>

//平台方案
#define PLATFORM_ALSA   0 // default
#define PLATFORM_HI3516 1
#define PLATFORM_T31    2

//选择平台
#ifndef WMIX_PLATFORM
#define WMIX_PLATFORM PLATFORM_ALSA
#endif

/* ---------- 平台区别 ---------- */

/*
 *  回声消除,时间间隔估算,必须是 WMIX_INTERVAL_MS 的倍数
 *  不同设备、结构回声时间不一样,这里测算的400ms
 */

#if (WMIX_PLATFORM == PLATFORM_HI3516)

#include "hi3516_init.h"
//使用内置aec,anr,hpf模块时,单声道
#define WMIX_CHANNELS 1
#define WMIX_SAMPLE 16
#define WMIX_FREQ 8000
//基本接口
#define wmix_ao_init hi3516_ao_init
#define wmix_ai_init hi3516_ai_init
#define wmix_ao_write hi3516_ao_write
#define wmix_ai_read hi3516_ai_read
#define wmix_ao_vol_set hi3516_ao_vol_set
#define wmix_ai_vol_set hi3516_ai_vol_set
#define wmix_ao_vol_get hi3516_ao_vol_get
#define wmix_ai_vol_get hi3516_ai_vol_get
#define wmix_ao_exit hi3516_ao_exit
#define wmix_ai_exit hi3516_ai_exit
//回声时延
#define AEC_INTERVAL_MS 760

#elif (WMIX_PLATFORM == PLATFORM_T31)

#include "t31_init.h"
//内置aec,单声道
#define WMIX_CHANNELS 1
#define WMIX_SAMPLE 16
#define WMIX_FREQ 8000
//基本接口
#define wmix_ao_init t31_ao_init
#define wmix_ai_init t31_ai_init
#define wmix_ao_write t31_ao_write
#define wmix_ai_read t31_ai_read
#define wmix_ao_vol_set t31_ao_vol_set
#define wmix_ai_vol_set t31_ai_vol_set
#define wmix_ao_vol_get t31_ao_vol_get
#define wmix_ai_vol_get t31_ai_vol_get
#define wmix_ao_exit t31_ao_exit
#define wmix_ai_exit t31_ai_exit
//回声时延
#define AEC_INTERVAL_MS 0

#else //default alsa

#include "alsa_init.h"
/*  注意事项:
 *    1.频率分 8000,16000,32000,48000 和 11025,22050,44100 两个系列,
 *    有些硬件由于时钟配置原因,使用不适配的频率可能导致录播音变声(听起来变了个人);
 *    2.过高的频率和44100系列频率不被webrtc支持,具体支持情况看readme.
 *    3.aec回声消除cpu占用过高(即使树莓派P4B),建议在单声道8000Hz下使用
 */
#define WMIX_CHANNELS 1
#define WMIX_SAMPLE 16
#define WMIX_FREQ 8000
//基本接口
#define wmix_ao_init alsa_ao_init
#define wmix_ai_init alsa_ai_init
#define wmix_ao_write alsa_ao_write
#define wmix_ai_read alsa_ai_read
#define wmix_ao_vol_set alsa_ao_vol_set
#define wmix_ai_vol_set alsa_ai_vol_set
#define wmix_ao_vol_get alsa_ao_vol_get
#define wmix_ai_vol_get alsa_ai_vol_get
#define wmix_ao_exit alsa_ao_exit
#define wmix_ai_exit alsa_ai_exit
//回声时延
#define AEC_INTERVAL_MS 400

#endif

/* ---------- 接收来自Makefile的宏定义 ---------- */

#ifdef MAKE_MP3
#define WMIX_MP3 MAKE_MP3
#else
#define WMIX_MP3 1
#endif

#ifdef MAKE_AAC
#define WMIX_AAC MAKE_AAC
#else
#define WMIX_AAC 1
#endif

#ifdef MAKE_FFT_SAMPLE
#define WMIX_FFT_SAMPLE MAKE_FFT_SAMPLE
#else
#define WMIX_FFT_SAMPLE 1024
#endif

//end of file
#endif
