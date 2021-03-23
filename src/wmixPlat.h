/*
 *  平台类型及其接口全局对接
 */
#ifndef _WMIXPLAT_H_
#define _WMIXPLAT_H_

//平台方案
#define PLATFORM_ALSA   0 // 通用平台(ubuntu,ARM,树莓派等)
#define PLATFORM_HI3516 1 // 海思plat_平台
#define PLATFORM_T31    2 // 君正T31平台

//接收来自Makefile传递的宏
#ifndef MAKE_PLATFORM
#define MAKE_PLATFORM PLATFORM_ALSA // 默认通用平台
#endif

#ifndef MAKE_MATH_FFT
#define MAKE_MATH_FFT 1024
#endif

// ========== 海思plat_平台配置 ==========
#if (MAKE_PLATFORM == PLATFORM_HI3516)

//使用内置aec,anr,hpf模块时,单声道
#define WMIX_CHANNELS 1
#define WMIX_SAMPLE 16
#define WMIX_FREQ 8000

#define AEC_INTERVAL_MS 760 //回声时延

// ========== 君正T31平台配置 ==========
#elif (MAKE_PLATFORM == PLATFORM_T31)

//内置aec,单声道
#define WMIX_CHANNELS 1
#define WMIX_SAMPLE 16
#define WMIX_FREQ 16000

#define AEC_INTERVAL_MS 0 //回声时延

// ========== 通用平台(ubuntu,ARM,树莓派等) ==========
#else

/*  注意事项:
 *    1.频率分 8000,16000,32000,48000 和 11025,22050,44100 两个系列,
 *    有些硬件由于时钟配置原因,使用不适配的频率可能导致录播音变声(听起来变了个人);
 *    2.过高的频率和44100系列频率不被webrtc支持,具体支持情况看readme.
 *    3.aec回声消除cpu占用过高(即使树莓派P4B),建议在单声道8000Hz下使用
 */
#define WMIX_CHANNELS 1
#define WMIX_SAMPLE 16
#define WMIX_FREQ 8000

#define AEC_INTERVAL_MS 400 //回声时延

#endif

//基本接口
#include "plat.h"
#define wmix_ao_init plat_ao_init
#define wmix_ai_init plat_ai_init
#define wmix_ao_write plat_ao_write
#define wmix_ai_read plat_ai_read
#define wmix_ao_vol_set plat_ao_vol_set
#define wmix_ai_vol_set plat_ai_vol_set
#define wmix_ao_vol_get plat_ao_vol_get
#define wmix_ai_vol_get plat_ai_vol_get
#define wmix_ao_exit plat_ao_exit
#define wmix_ai_exit plat_ai_exit

#endif //end of file
