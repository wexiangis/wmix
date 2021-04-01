/*
 *  平台对接文件
 */
#ifndef _PLAT_H_
#define _PLAT_H_

#include <stdint.h>

//内置aec,单声道
#define PLAT_CHN 1
#define PLAT_SAMPLE 16
#define PLAT_FREQ 8000
//回声时延ms
#define PLAT_AEC_INTERVALMS 0
//所谓correct,就是在放置播放指针时,超前当前播放指针一定量,以保证完整播放音频
#define PLAT_PLAY_CORRECT 0

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
