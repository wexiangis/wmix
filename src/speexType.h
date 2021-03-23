/**************************************************
 * 
 *  二次封装speex接口
 * 
 **************************************************/
#ifndef _SPEEXLIB_H_
#define _SPEEXLIB_H_

#include <stdint.h>
#include <stdbool.h>

//接收来自Makefile的传参,没有定义则自己定义
#ifndef MAKE_SPEEX_BETA3
#define MAKE_SPEEX_BETA3 1
#endif

#if (MAKE_SPEEX_BETA3)

/* ==================== AEC 回声消除 ==================== */

void *spx_aec_init(int chn, int freq, int intervalMs, int delayms, bool *debug);
int spx_aec_process(void *fp, int16_t *frameFar, int16_t *frameNear, int16_t *frameOut, int frameNum);
void spx_aec_release(void *fp);

#endif // #if (MAKE_SPEEX_BETA3)

#endif // end of file
