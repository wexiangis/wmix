/*
 *  平台对接文件
 */
#include <stdio.h>
#include <stdint.h>

void hi3516_ao_vol_set(void *objAo, int vol)
{
    ;
}

void hi3516_ai_vol_set(void *objAi, int vol)
{
    ;
}

int hi3516_ao_vol_get(void *objAo)
{
    return 0;
}

int hi3516_ai_vol_get(void *objAi)
{
    return 0;
}

void *hi3516_ao_init(int chn, int freq)
{
    return NULL;
}

void *hi3516_ai_init(int chn, int freq)
{
    return NULL;
}

int hi3516_ao_write(void *objAo, uint8_t *data, int len)
{
    return 0;
}

int hi3516_ai_read(void *objAi, uint8_t *data, int len)
{
    return 0;
}

void hi3516_ao_exit(void *objAo)
{
    ;
}

void hi3516_ai_exit(void *objAi)
{
    ;
}

uint8_t *hi3516_ao_buffer(void *objAo, int *retSize)
{
    return NULL;
}

uint8_t *hi3516_ai_buffer(void *objAi, int *retSize)
{
    return NULL;
}
