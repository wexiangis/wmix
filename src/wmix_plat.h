/*
 *  平台类型及其接口全局对接
 */
#ifndef _WMIX_PLAT_H_
#define _WMIX_PLAT_H_

#include <stdint.h>

//平台方案
#define PLATFORM_ALSA   0 // 通用平台(ubuntu,ARM,树莓派等)
#define PLATFORM_HI3516 1 // 海思hi3516平台
#define PLATFORM_T31    2 // 君正T31平台

//接收来自Makefile传递的宏
#ifndef MAKE_PLATFORM
#define MAKE_PLATFORM PLATFORM_ALSA // 默认通用平台
#endif

#ifndef MAKE_MP3
#define MAKE_MP3 1
#endif

#ifndef MAKE_AAC
#define MAKE_AAC 1
#endif

#ifndef MAKE_MATH_FFT
#define MAKE_MATH_FFT 1024
#endif

// ========== 海思hi3516平台配置 ==========
#if (MAKE_PLATFORM == PLATFORM_HI3516)

#include "hi3516_plat.h"

#define WMIX_CHANNELS 1 //使用内置aec,anr,hpf模块时,单声道
#define WMIX_SAMPLE 16
#define WMIX_FREQ 8000

#define wmix_ao_init hi3516_ao_init //基本接口
#define wmix_ai_init hi3516_ai_init
#define wmix_ao_write hi3516_ao_write
#define wmix_ai_read hi3516_ai_read
#define wmix_ao_vol_set hi3516_ao_vol_set
#define wmix_ai_vol_set hi3516_ai_vol_set
#define wmix_ao_vol_get hi3516_ao_vol_get
#define wmix_ai_vol_get hi3516_ai_vol_get
#define wmix_ao_exit hi3516_ao_exit
#define wmix_ai_exit hi3516_ai_exit

#define AEC_INTERVAL_MS 760 //回声时延

// ========== 君正T31平台配置 ==========
#elif (MAKE_PLATFORM == PLATFORM_T31)

#include "t31_plat.h"

#define WMIX_CHANNELS 1 //内置aec,单声道
#define WMIX_SAMPLE 16
#define WMIX_FREQ 16000

#define wmix_ao_init t31_ao_init //基本接口
#define wmix_ai_init t31_ai_init
#define wmix_ao_write t31_ao_write
#define wmix_ai_read t31_ai_read
#define wmix_ao_vol_set t31_ao_vol_set
#define wmix_ai_vol_set t31_ai_vol_set
#define wmix_ao_vol_get t31_ao_vol_get
#define wmix_ai_vol_get t31_ai_vol_get
#define wmix_ao_exit t31_ao_exit
#define wmix_ai_exit t31_ai_exit

#define AEC_INTERVAL_MS 0 //回声时延

// ========== 通用平台(ubuntu,ARM,树莓派等) ==========
#else

#include "alsa_plat.h"
/*  注意事项:
 *    1.频率分 8000,16000,32000,48000 和 11025,22050,44100 两个系列,
 *    有些硬件由于时钟配置原因,使用不适配的频率可能导致录播音变声(听起来变了个人);
 *    2.过高的频率和44100系列频率不被webrtc支持,具体支持情况看readme.
 *    3.aec回声消除cpu占用过高(即使树莓派P4B),建议在单声道8000Hz下使用
 */
#define WMIX_CHANNELS 1
#define WMIX_SAMPLE 16
#define WMIX_FREQ 8000

#define wmix_ao_init alsa_ao_init //基本接口
#define wmix_ai_init alsa_ai_init
#define wmix_ao_write alsa_ao_write
#define wmix_ai_read alsa_ai_read
#define wmix_ao_vol_set alsa_ao_vol_set
#define wmix_ai_vol_set alsa_ai_vol_set
#define wmix_ao_vol_get alsa_ao_vol_get
#define wmix_ai_vol_get alsa_ai_vol_get
#define wmix_ao_exit alsa_ao_exit
#define wmix_ai_exit alsa_ai_exit

#define AEC_INTERVAL_MS 400 //回声时延

#endif

#endif //end of file
