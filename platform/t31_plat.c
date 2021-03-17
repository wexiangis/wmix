/*
 *  平台对接文件
 */
#include <stdio.h>
#include <stdint.h>

void t31_ao_vol_set(void *objAo, int vol)
{
    ;
}

void t31_ai_vol_set(void *objAi, int vol)
{
    ;
}

int t31_ao_vol_get(void *objAo)
{
    return 0;
}

int t31_ai_vol_get(void *objAi)
{
    return 0;
}

void *t31_ao_init(int chn, int freq)
{
    return NULL;
}

void *t31_ai_init(int chn, int freq)
{
    return NULL;
}

int t31_ao_write(void *objAo, uint8_t *data, int len)
{
    return 0;
}

int t31_ai_read(void *objAi, uint8_t *data, int len)
{
    return 0;
}

void t31_ao_exit(void *objAo)
{
    ;
}

void t31_ai_exit(void *objAi)
{
    ;
}

uint8_t *alsa_ao_buffer(void *objAo, int *retSize)
{
    return NULL;
}
uint8_t *alsa_ai_buffer(void *objAi, int *retSize)
{
    return NULL;
}