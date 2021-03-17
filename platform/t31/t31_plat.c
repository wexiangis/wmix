/*
 *  平台对接文件
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <imp/imp_audio.h>
#include <imp/imp_log.h>

//获取 WMIX_FRAME_NUM 以制定最小缓存数据量
#include "wmix_conf.h"

#define T31_ERR(fmt)           fprintf(stderr, "%s: "fmt, __func__)
#define T31_ERR2(fmt, argv...) fprintf(stderr, "%s: "fmt, __func__, ##argv)

#define T31_AO_VOL_MAX 110
#define T31_AO_VOL_DIV 10
#define T31_AO_VOL_MIN 10

#define T31_AI_VOL_MAX 110
#define T31_AI_VOL_DIV 10
#define T31_AI_VOL_MIN 10

typedef struct
{
    int devID;  // 0
    int chnID;  // 0
    int aoVol;  // [-30~120],120-->30db
    int aoGain; // [0~31]-->[-18dB~28.5dB]
    IMPAudioIOAttr attr;
    IMPAudioFrame frame;
    IMPAudioOChnState state;
} T31_AO_Struct;

typedef struct {
    int devID;  // 0
    int chnID;  // 0
    int aiVol;  // [-30~120],120-->30db
    int aiGain; // [0~31]-->[-18dB~28.5dB]
    IMPAudioIOAttr attr;
    IMPAudioFrame frame;
    IMPAudioIChnParam param;
} T31_AI_Struct;

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
    return (((T31_AO_Struct *)objAo)->aoVol - T31_AO_VOL_MIN) / T31_AO_VOL_DIV;
}

int t31_ai_vol_get(void *objAi)
{
    return (((T31_AI_Struct *)objAi)->aiVol - T31_AI_VOL_MIN) / T31_AI_VOL_DIV;
}

void *t31_ao_init(int chn, int freq)
{
    int ret;
    T31_AO_Struct *tas = (T31_AO_Struct *)calloc(1, sizeof(T31_AO_Struct));

    tas->devID = 0;
    tas->chnID = 0;
    tas->aoVol = T31_AO_VOL_MAX;
    tas->aoGain = 20;

    tas->attr.samplerate = freq;
    tas->attr.bitwidth = AUDIO_BIT_WIDTH_16;
    tas->attr.soundmode = AUDIO_SOUND_MODE_MONO;
    tas->attr.frmNum = 20; // 数据没有发完情况下,最多缓存这么多份数据,即下面 numPerFrm x 该值
    tas->attr.numPerFrm = WMIX_FRAME_NUM;
    tas->attr.chnCnt = 1;

    ret = IMP_AO_SetPubAttr(tas->devID, &tas->attr);
    if (ret != 0)
    {
        T31_ERR2("set ao %d attr err: %d\n", tas->devID, ret);
        goto err;
    }

    memset(&tas->attr, 0, sizeof(tas->attr));
    ret = IMP_AO_GetPubAttr(tas->devID, &tas->attr);
    if (ret != 0)
    {
        T31_ERR2("get ao %d attr err: %d\n", tas->devID, ret);
        goto err;
    }
    printf("freq: %dHz, bit %d, chn %d, frmNum %d, numPerFrm %d, cc %d \r\n",
           tas->attr.samplerate,
           tas->attr.bitwidth,
           tas->attr.soundmode,
           tas->attr.frmNum,
           tas->attr.numPerFrm,
           tas->attr.chnCnt);

    ret = IMP_AO_Enable(tas->devID);
    if (ret != 0)
    {
        T31_ERR2("enable ao %d err\n", tas->devID);
        goto err;
    }

    ret = IMP_AO_EnableChn(tas->devID, tas->chnID);
    if (ret != 0)
    {
        T31_ERR("Audio play enable channel failed\n");
        goto err;
    }

    ret = IMP_AO_SetVol(tas->devID, tas->chnID, tas->aoVol);
    if (ret != 0)
    {
        T31_ERR("Audio Play set volume failed\n");
        goto err;
    }

    ret = IMP_AO_SetGain(tas->devID, tas->chnID, tas->aoGain);
    if (ret != 0)
    {
        T31_ERR("Audio Record Set Gain failed\n");
        goto err;
    }

    return tas;

err:
    free(tas);
    return NULL;
}

void *t31_ai_init(int chn, int freq)
{
    int ret;
    T31_AI_Struct *tas = (T31_AI_Struct *)calloc(1, sizeof(T31_AI_Struct));

    tas->devID = 0;
    tas->chnID = 0;
    tas->aiVol = T31_AI_VOL_MAX;
    tas->aiGain = 20;

    tas->attr.samplerate = freq;
    tas->attr.bitwidth = AUDIO_BIT_WIDTH_16;
    tas->attr.soundmode = AUDIO_SOUND_MODE_MONO;
    tas->attr.frmNum = 20; // 数据没有发完情况下,最多缓存这么多份数据,即下面 numPerFrm x 该值
    tas->attr.numPerFrm = WMIX_FRAME_NUM;
    tas->attr.chnCnt = 1;

    // AI参数配置(不依赖系统初始化?)
    ret = IMP_AI_SetPubAttr(tas->devID, &tas->attr);
    if (ret != 0)
    {
        T31_ERR2("set ai %d attr err: %d\n", tas->devID, ret);
        goto err;
    }

    // 读取检查
    memset(&tas->attr, 0, sizeof(tas->attr));
    ret = IMP_AI_GetPubAttr(tas->devID, &tas->attr);
    if (ret != 0)
    {
        T31_ERR2("get ai %d attr err: %d\n", tas->devID, ret);
        goto err;
    }
    printf("freq: %dHz, bit %d, chn %d, frmNum %d, numPerFrm %d, cc %d \r\n",
           tas->attr.samplerate,
           tas->attr.bitwidth,
           tas->attr.soundmode,
           tas->attr.frmNum,
           tas->attr.numPerFrm,
           tas->attr.chnCnt);

    ret = IMP_AI_Enable(tas->devID);
    if (ret != 0)
    {
        T31_ERR2("enable ai %d err\n", tas->devID);
        goto err;
    }

    ret = IMP_AI_SetChnParam(tas->devID, tas->chnID, &tas->param);
    if (ret != 0)
    {
        T31_ERR2("set ai %d channel %d attr err: %d\n", tas->devID, tas->chnID, ret);
        goto err;
    }

    // 读取检查
    memset(&tas->param, 0, sizeof(tas->param));
    ret = IMP_AI_GetChnParam(tas->devID, tas->chnID, &tas->param);
    if (ret != 0)
    {
        T31_ERR2("get ai %d channel %d attr err: %d\n", tas->devID, tas->chnID, ret);
        goto err;
    }
    printf("usrFrmDepth %d \r\n", tas->param.usrFrmDepth);

    ret = IMP_AI_EnableChn(tas->devID, tas->chnID);
    if (ret != 0)
    {
        T31_ERR("Audio Record enable channel failed\n");
        goto err;
    }

    ret = IMP_AI_SetVol(tas->devID, tas->chnID, tas->aiVol);
    if (ret != 0)
    {
        T31_ERR("Audio Record set volume failed\n");
        goto err;
    }

    // 读取检查
    ret = IMP_AI_GetVol(tas->devID, tas->chnID, &tas->aiVol);
    if (ret != 0)
    {
        T31_ERR("Audio Record get volume failed\n");
        goto err;
    }
    printf("aiVol %d \r\n", tas->aiVol);

    ret = IMP_AI_SetGain(tas->devID, tas->chnID, tas->aiGain);
    if (ret != 0)
    {
        T31_ERR("Audio Record Set Gain failed\n");
        goto err;
    }

    // 读取检查
    ret = IMP_AI_GetGain(tas->devID, tas->chnID, &tas->aiGain);
    if (ret != 0)
    {
        T31_ERR("Audio Record Get Gain failed\n");
        goto err;
    }

    return tas;

err:
    free(tas);
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
