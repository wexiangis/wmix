/*
 *  平台类型全局定义
 */
#ifndef _WMIX_PLAT_H_
#define _WMIX_PLAT_H_

#include <stdint.h>

typedef enum
{
    WPT_ALSA = 0, // default
    WPT_HI3516 = 1,
    WPT_T31 = 2,
} PLATFORM_TYPE;

//选择平台
#define WMIX_PLATFORM 0

/* ---------- 不同平台的统一接口定义 ---------- */

/*
 *  平台初始化接口
 *  函数原型:
 *      void *ao_init(int chn, int freq);
 *      void *ai_init(int chn, int freq);
 *  参数:
 *      chn: 通道(声道)数, 1或者2
 *      freq: 频率,8000,16000,11025,22050,32000,44100,48000
 *  返回: 控制结构指针(obj),NULL失败
 */
typedef void *(*PLATFORM_INIT)(int, int, int *);

/*
 *  音频数据读/写接口
 *  函数原型:
 *      int ao_write(void *objAo, uint8_t *data, int len)
 *      int ai_read(void *objAi, uint8_t *data, int len)
 *  参数:
 *      obj: 初始化时获得的指针
 *      data: 音频数据
 *      len: 数据量,必定为一帧(chn*2字节)的整数倍
 *  返回: 实际读/写字节数,小于0异常失败
 */
typedef int (*PLATFORM_RW)(void *, uint8_t *, int);

/*
 *  录播音量设置
 *  函数原型:
 *      void ao_vol_set(void *objAo, int vol);
 *      void ai_vol_set(void *objAi, int vol);
 *      int ao_vol_get(void *objAo);
 *      int ai_vol_get(void *objAi);
 *  参数:
 *      vol: 录播音音量,格式0~100
 *  返回: 当前音量
 */
typedef void (*PLATFORM_VOL_SET)(void *, int);
typedef int (*PLATFORM_VOL_GET)(void *);

/*
 *  平台释放接口
 *  函数原型:
 *      void ao_exit(void *objAo);
 *      void ai_exit(void *objAi);
 */
typedef void (*PLATFORM_EXIT)(void *);

/* ---------- 平台区别 ---------- */

#if(WMIX_PLATFORM == WPT_HI3516)

//使用内置aec,anr,hpf模块时,单声道
#define WMIX_CHANNELS 1
#define WMIX_SAMPLE 16
#define WMIX_FREQ 8000

#elif(WMIX_PLATFORM == WPT_T31)

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

#endif

#endif
