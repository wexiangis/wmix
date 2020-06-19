/**************************************************
 * 
 *  二次封装webrtc旗下的vad、aec/aecm、ns、agc模块接口
 * 
 **************************************************/
#ifndef _WEBRTC_H_
#define _WEBRTC_H_

#ifdef MAKE_WEBRTC_VAD
#define WMIX_WEBRTC_VAD MAKE_WEBRTC_VAD
#else
#define WMIX_WEBRTC_VAD 0
#endif

#ifdef MAKE_WEBRTC_AEC
#define WMIX_WEBRTC_AEC MAKE_WEBRTC_AEC
#else
#define WMIX_WEBRTC_AEC 0
#endif

#ifdef MAKE_WEBRTC_AECM
#define WMIX_WEBRTC_AECM MAKE_WEBRTC_AECM
#else
#define WMIX_WEBRTC_AECM 0
#endif

#ifdef MAKE_WEBRTC_NS
#define WMIX_WEBRTC_NS MAKE_WEBRTC_NS
#else
#define WMIX_WEBRTC_NS 0
#endif

#ifdef MAKE_WEBRTC_AGC
#define WMIX_WEBRTC_AGC MAKE_WEBRTC_AGC
#else
#define WMIX_WEBRTC_AGC 0
#endif

#include <stdint.h>

/* ==================== VAD 人声识别模块 ==================== */

void* vad_init(int chn, int freq);
void vad_process(void *fp, int16_t *frame, int frameLen);
void vad_release(void *fp);

/* ==================== AEC 回声消除 ==================== */

void* aec_init(int chn, int freq);
int aec_process(void *fp, int16_t *frameIn, int16_t *frameOut, int frameLen);
void aec_release(void *fp);

/* ==================== AECM 回声消除移动版 ==================== */

void* aecm_init(int chn, int freq);
int aecm_process(void *fp, int16_t *frameIn, int16_t *frameOut, int frameLen);
void aecm_release(void *fp);

/* ==================== NS 噪音抑制 ==================== */

void* ns_init(int chn, int freq);
void ns_process(void *fp, int16_t *frame, int frameLen);
void ns_release(void *fp);

/* ==================== AGC 自动增益 ==================== */

void* agc_init(int chn, int freq);
void agc_process(void *fp, int16_t *frame, int frameLen);
void agc_release(void *fp);

#endif
