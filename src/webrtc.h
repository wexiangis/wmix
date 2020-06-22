/**************************************************
 * 
 *  二次封装webrtc旗下的vad、aec/aecm、ns/nsx、agc模块接口
 * 
 **************************************************/
#ifndef _WEBRTC_H_
#define _WEBRTC_H_

#define WMIX_WEBRTC_DEBUG

/* ---------- 接收来自Makefile的宏定义 ---------- */

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

#ifdef MAKE_WEBRTC_AECM
#define WMIX_WEBRTC_AECM MAKE_WEBRTC_AECM
#else
#define WMIX_WEBRTC_AECM 1
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

void* vad_init(int chn, int freq, int intervalMs);
void vad_process(void *fp, int16_t *frame, int frameLen);
void vad_release(void *fp);

/* ==================== AEC 回声消除 ==================== */

void* aec_init(int chn, int freq, int intervalMs);
int aec_setFrameFar(void *fp, int16_t *frameFar, int frameLen);
int aec_process(void *fp, int16_t *frameNear, int16_t *frameOut, int frameLen);
void aec_release(void *fp);

/* ==================== NS 噪音抑制 ==================== */

void* ns_init(int chn, int freq, int intervalMs);
void ns_process(void *fp, int16_t *frame, int16_t *frameOut, int frameLen);
void ns_release(void *fp);

/* ==================== AGC 自动增益 ==================== */

void* agc_init(int chn, int freq, int intervalMs);
void agc_process(void *fp, int16_t *frame, int frameLen);
void agc_release(void *fp);

#endif
