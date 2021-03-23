/*
 *  平台对接文件
 */
#ifndef _PLAT_H_
#define _PLAT_H_

#include <stdint.h>

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
