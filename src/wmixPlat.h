/*
 *  平台类型及其接口全局对接
 */
#ifndef _WMIXPLAT_H_
#define _WMIXPLAT_H_

#ifndef MAKE_MATH_FFT
#define MAKE_MATH_FFT 1024
#endif

// ---------- 平台对接 ----------
#include "plat.h"
//基本参数
#define WMIX_CHN PLAT_CHN
#define WMIX_SAMPLE PLAT_SAMPLE
#define WMIX_FREQ PLAT_FREQ
//回声时延ms
#define AEC_INTERVALMS PLAT_AEC_INTERVALMS
//所谓correct,就是在放置播放指针时,超前当前播放指针一定量,以保证完整播放音频
#define VIEW_PLAY_CORRECT PLAT_PLAY_CORRECT
//基本接口
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
