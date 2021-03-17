/*
 *  平台对接文件
 */
#ifndef _HI3516_PLAT_H_
#define _HI3516_PLAT_H_

#include <stdint.h>

void *hi3516_ao_init(int chn, int freq);
void *hi3516_ai_init(int chn, int freq);

int hi3516_ao_write(void *objAo, uint8_t *data, int len);
int hi3516_ai_read(void *objAi, uint8_t *data, int len);

void hi3516_ao_vol_set(void *objAo, int vol);
void hi3516_ai_vol_set(void *objAi, int vol);

int hi3516_ao_vol_get(void *objAo);
int hi3516_ai_vol_get(void *objAi);

void hi3516_ao_exit(void *objAo);
void hi3516_ai_exit(void *objAi);

uint8_t *hi3516_ao_buffer(void *objAo, int *retSize);
uint8_t *hi3516_ai_buffer(void *objAi, int *retSize);

#endif
