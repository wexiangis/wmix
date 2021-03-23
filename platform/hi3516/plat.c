/*
 *  平台对接文件
 */
#include <stdio.h>
#include <stdint.h>

void plat_ao_vol_set(void *objAo, int vol)
{
    ;
}

void plat_ai_vol_set(void *objAi, int vol)
{
    ;
}

int plat_ao_vol_get(void *objAo)
{
    return 0;
}

int plat_ai_vol_get(void *objAi)
{
    return 0;
}

void *plat_ao_init(int chn, int freq)
{
    return NULL;
}

void *plat_ai_init(int chn, int freq)
{
    return NULL;
}

int plat_ao_write(void *objAo, uint8_t *data, int len)
{
    return 0;
}

int plat_ai_read(void *objAi, uint8_t *data, int len)
{
    return 0;
}

void plat_ao_exit(void *objAo)
{
    ;
}

void plat_ai_exit(void *objAi)
{
    ;
}
