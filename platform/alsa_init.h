#ifndef _ALSA_INIT_H_
#define _ALSA_INIT_H_

#include <stdint.h>

void *alsa_ao_init(int chn, int freq);
void *alsa_ai_init(int chn, int freq);

int alsa_ao_write(void *objAo, uint8_t *data, int len);
int alsa_ai_read(void *objAi, uint8_t *data, int len);

void alsa_ao_vol_set(void *objAo, int vol);
void alsa_ai_vol_set(void *objAi, int vol);

int alsa_ao_vol_get(void *objAo);
int alsa_ai_vol_get(void *objAi);

void alsa_ao_exit(void *objAo);
void alsa_ai_exit(void *objAi);

#endif
