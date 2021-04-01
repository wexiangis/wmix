/*
 *  平台对接文件
 */
#ifndef _PLAT_H_
#define _PLAT_H_

#include <stdint.h>

/*  注意事项:
 *    1.频率分 8000,16000,32000,48000 和 11025,22050,44100 两个系列,
 *    有些硬件由于时钟配置原因,使用不适配的频率可能导致录播音变声(听起来变了个人);
 *    2.过高的频率和44100系列频率不被webrtc支持,具体支持情况看readme.
 *    3.aec回声消除cpu占用过高(即使树莓派P4B),建议在单声道8000Hz下使用
 */
#define PLAT_CHN 1
#define PLAT_SAMPLE 16
#define PLAT_FREQ 16000
//回声时延ms
#define PLAT_AEC_INTERVALMS 400
//所谓correct,就是在放置播放指针时,超前当前播放指针一定量,以保证完整播放音频
#define PLAT_PLAY_CORRECT (PLAT_CHN * PLAT_FREQ * 16 / 8 / 5)

void *plat_ao_init(int chn, int freq);
void *plat_ai_init(int chn, int freq);

int plat_ao_write(void *objAo, uint8_t *data, int len);
int plat_ai_read(void *objAi, uint8_t *data, int len);

void plat_ao_vol_set(void *objAo, int vol);
void plat_ai_vol_set(void *objAi, int vol);

int plat_ao_vol_get(void *objAo);
int plat_ai_vol_get(void *objAi);

void plat_ao_exit(void *objAo);
void plat_ai_exit(void *objAi);

#endif
