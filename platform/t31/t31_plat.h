/*
 *  平台对接文件
 */
#ifndef _T31_PLAT_H_
#define _T31_PLAT_H_

#include <stdint.h>

void *t31_ao_init(int chn, int freq);
void *t31_ai_init(int chn, int freq);

int t31_ao_write(void *objAo, uint8_t *data, int len);
int t31_ai_read(void *objAi, uint8_t *data, int len);

void t31_ao_vol_set(void *objAo, int vol);
void t31_ai_vol_set(void *objAi, int vol);

int t31_ao_vol_get(void *objAo);
int t31_ai_vol_get(void *objAi);

void t31_ao_exit(void *objAo);
void t31_ai_exit(void *objAi);

#endif
