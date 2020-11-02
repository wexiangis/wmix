/**************************************************
 * 
 *  二次封装speex接口
 * 
 **************************************************/
#ifndef _SPEEXLIB_H_
#define _SPEEXLIB_H_

#include <stdint.h>
#include <stdbool.h>

/* ---------- 接收来自Makefile的宏定义 ---------- */
/* ----------   若要强制使能,手动置1   ---------- */

#ifdef MAKE_SPEEX_BETA3
#define WMIX_SPEEX_BETA3 MAKE_SPEEX_BETA3
#else
#define WMIX_SPEEX_BETA3 1
#endif

#if (WMIX_SPEEX_BETA3)

/* ==================== AEC 回声消除 ==================== */

void *spx_aec_init(int chn, int freq, int intervalMs, int delayms, bool *debug);
int spx_aec_process(void *fp, int16_t *frameFar, int16_t *frameNear, int16_t *frameOut, int frameNum);
void spx_aec_release(void *fp);

#endif

#endif