/**************************************************
 * 
 *  二次封装webrtc旗下的vad、aec/aecm、ns/nsx、agc模块接口
 * 
 **************************************************/
#ifndef _WEBRTC_H_
#define _WEBRTC_H_

#include <stdint.h>
#include <stdbool.h>

//接收来自Makefile的传参,没有定义则自己定义
#ifndef MAKE_WEBRTC_VAD
#define MAKE_WEBRTC_VAD 1
#endif

#ifndef MAKE_WEBRTC_AEC
#define MAKE_WEBRTC_AEC 1
#endif

#ifndef MAKE_WEBRTC_NS
#define MAKE_WEBRTC_NS 1
#endif

#ifndef MAKE_WEBRTC_AGC
#define MAKE_WEBRTC_AGC 1
#endif

/* ==================== VAD 人声识别模块 ==================== */

#if (MAKE_WEBRTC_VAD)
void *vad_init(int chn, int freq, int intervalMs, bool *debug);
void vad_process(void *fp, int16_t *frame, int frameNum);
void vad_release(void *fp);
#endif

/* ==================== AEC 回声消除 ==================== */

#if (MAKE_WEBRTC_AEC)
void *aec_init(int chn, int freq, int intervalMs, bool *debug);
int aec_setFrameFar(void *fp, int16_t *frameFar, int frameNum);
int aec_process(void *fp, int16_t *frameNear, int16_t *frameOut, int frameNum, int delayms);
int aec_process2(void *fp, int16_t *frameFar, int16_t *frameNear, int16_t *frameOut, int frameNum, int delayms);
void aec_release(void *fp);
#endif

/* ==================== NS 噪音抑制 ==================== */

#if (MAKE_WEBRTC_NS)
void *ns_init(int chn, int freq, bool *debug);
void ns_process(void *fp, int16_t *frame, int16_t *frameOut, int frameNum);
void ns_release(void *fp);
#endif

/* ==================== AGC 自动增益 ==================== */

#if (MAKE_WEBRTC_AGC)
void *agc_init(int chn, int freq, int intervalMs, int value, bool *debug);
int agc_process(void *fp, int16_t *frame, int16_t *frameOut, int frameNum);
void agc_addition(void *fp, uint8_t value);
void agc_release(void *fp);
#endif

#endif
