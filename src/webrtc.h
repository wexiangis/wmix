/**************************************************
 * 
 *  二次封装webrtc旗下的vad、aec/aecm、ns/nsx、agc模块接口
 * 
 **************************************************/
#ifndef _WEBRTC_H_
#define _WEBRTC_H_

#define WMIX_WEBRTC_DEBUG

/* ---------- 接收来自Makefile的宏定义 ---------- */
/* ----------   若要强制使能,手动置1   ---------- */

#ifdef MAKE_WEBRTC_VAD
#define WMIX_WEBRTC_VAD MAKE_WEBRTC_VAD
#else
#define WMIX_WEBRTC_VAD 1
#endif

#ifdef MAKE_WEBRTC_AEC
#define WMIX_WEBRTC_AEC MAKE_WEBRTC_AEC
#else
#define WMIX_WEBRTC_AEC 1
#endif

#ifdef MAKE_WEBRTC_NS
#define WMIX_WEBRTC_NS MAKE_WEBRTC_NS
#else
#define WMIX_WEBRTC_NS 1
#endif

#ifdef MAKE_WEBRTC_AGC
#define WMIX_WEBRTC_AGC MAKE_WEBRTC_AGC
#else
#define WMIX_WEBRTC_AGC 1
#endif

#include <stdint.h>

/* ==================== VAD 人声识别模块 ==================== */

#if (WMIX_WEBRTC_VAD)
void *vad_init(int chn, int freq, int intervalMs);
void vad_process(void *fp, int16_t *frame, int frameNum);
void vad_release(void *fp);
#endif

/* ==================== AEC 回声消除 ==================== */

#if (WMIX_WEBRTC_AEC)
void *aec_init(int chn, int freq, int intervalMs);
int aec_setFrameFar(void *fp, int16_t *frameFar, int frameNum);
int aec_process(void *fp, int16_t *frameNear, int16_t *frameOut, int frameNum, int delayms);
int aec_process2(void *fp, int16_t *frameFar, int16_t *frameNear, int16_t *frameOut, int frameNum, int delayms);
int aec_process3(void *fp, int16_t *frameFar, int16_t *frameNear, int16_t *frameOut, int frameNum, float reduce);
void aec_release(void *fp);
#endif

/* ==================== NS 噪音抑制 ==================== */

#if (WMIX_WEBRTC_NS)
void *ns_init(int chn, int freq);
void ns_process(void *fp, int16_t *frame, int16_t *frameOut, int frameNum);
void ns_release(void *fp);
#endif

/* ==================== AGC 自动增益 ==================== */

#if (WMIX_WEBRTC_AGC)
void *agc_init(int chn, int freq, int intervalMs, int value);
int agc_process(void *fp, int16_t *frame, int16_t *frameOut, int frameNum);
void agc_addition(void *fp, uint8_t value);
void agc_release(void *fp);
#endif

#endif
