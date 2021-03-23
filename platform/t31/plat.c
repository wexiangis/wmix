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
#include "wmixConf.h"

//不带参数 和 带参数
#define T31_ERR(fmt) fprintf(stderr, "%s(%d): " fmt, __func__, __LINE__)
#define T31_ERR2(fmt, argv...) fprintf(stderr, "%s(%d): " fmt, __func__, __LINE__, ##argv)

//音量最大值、分块和最小值(范围10格)
#define T31_AO_VOL_MAX 115
#define T31_AO_VOL_DIV 7
#define T31_AO_VOL_MIN 45

#define T31_AI_VOL_MAX 115
#define T31_AI_VOL_DIV 7
#define T31_AI_VOL_MIN 45

//ao和ai的devID不能一样
#define T31_AO_DEV_ID 0
#define T31_AO_CHN_ID 0

#define T31_AI_DEV_ID 1
#define T31_AI_CHN_ID 0

/*
 *  启用平台的aec功能
 *  注意事项:
 *  1.配置 /etc/webrtc_profile.ini 文件,添加并调试以下参数:
 *      [Set_Far_Frame]
 *      Frame_V=0.3
 *      [Set_Near_Frame]
 *      Frame_V=0.1
 *      delay_ms=150
 *  2.确保已拷贝 libaudioProcess.so 到文件系统
 *  3.在编译时加 -laudioProcess
 */
// #define T31_AEC_EN

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

typedef struct
{
    int devID;  // 1
    int chnID;  // 0
    int aiVol;  // [-30~120],120-->30db
    int aiGain; // [0~31]-->[-18dB~28.5dB]
    IMPAudioIOAttr attr;
    IMPAudioFrame frame;
    IMPAudioIChnParam param;
} T31_AI_Struct;

void plat_ao_vol_set(void *objAo, int vol)
{
    T31_AO_Struct *tas = (T31_AO_Struct *)objAo;

    if (vol > 10)
        tas->aoVol = 10 * T31_AO_VOL_DIV;
    else if (vol < 1)
        tas->aoVol = 0;
    else
        tas->aoVol = vol * T31_AO_VOL_DIV;

    if (IMP_AO_SetVol(tas->devID, tas->chnID, tas->aoVol) != 0)
    {
        T31_ERR("IMP_AO_SetVol failed\r\n");
        return;
    }
    if (IMP_AO_GetVol(tas->devID, tas->chnID, &tas->aoVol) != 0)
    {
        T31_ERR("IMP_AO_GetVol failed\r\n");
        return;
    }
}

void plat_ai_vol_set(void *objAi, int vol)
{
    T31_AI_Struct *tas = (T31_AI_Struct *)objAi;

    if (vol > 10)
        tas->aiVol = 10 * T31_AI_VOL_DIV;
    else if (vol < 1)
        tas->aiVol = 0;
    else
        tas->aiVol = vol * T31_AI_VOL_DIV;

    if (IMP_AI_SetVol(tas->devID, tas->chnID, tas->aiVol) != 0)
    {
        T31_ERR("IMP_AI_SetVol failed\r\n");
        return;
    }
    if (IMP_AI_GetVol(tas->devID, tas->chnID, &tas->aiVol) != 0)
    {
        T31_ERR("IMP_AI_GetVol failed\r\n");
        return;
    }
}

int plat_ao_vol_get(void *objAo)
{
    T31_AO_Struct *tas = (T31_AO_Struct *)objAo;
    if (tas->aoVol < T31_AO_VOL_MIN)
        return 0;
    else
        return (tas->aoVol - T31_AO_VOL_MIN) / T31_AO_VOL_DIV;
}

int plat_ai_vol_get(void *objAi)
{
    T31_AI_Struct *tas = (T31_AI_Struct *)objAi;
    if (tas->aiVol < T31_AI_VOL_MIN)
        return 0;
    else
        return (tas->aiVol - T31_AI_VOL_MIN) / T31_AI_VOL_DIV;
}

void *plat_ao_init(int chn, int freq)
{
    T31_AO_Struct *tas = (T31_AO_Struct *)calloc(1, sizeof(T31_AO_Struct));

    tas->devID = T31_AO_DEV_ID;
    tas->chnID = T31_AO_CHN_ID;
    tas->aoVol = T31_AO_VOL_MAX;
    tas->aoGain = 30;

    tas->attr.samplerate = freq;
    tas->attr.bitwidth = AUDIO_BIT_WIDTH_16;
    tas->attr.soundmode = AUDIO_SOUND_MODE_MONO;
    tas->attr.frmNum = 20; // 数据没有发完情况下,最多缓存这么多份数据,即下面 numPerFrm x 该值
    tas->attr.numPerFrm = WMIX_FRAME_NUM;
    tas->attr.chnCnt = 1;

    if (IMP_AO_SetPubAttr(tas->devID, &tas->attr) != 0)
    {
        T31_ERR2("IMP_AO_SetPubAttr failed, devID %d\r\n", tas->devID);
        goto err;
    }

    memset(&tas->attr, 0, sizeof(tas->attr));
    if (IMP_AO_GetPubAttr(tas->devID, &tas->attr) != 0)
    {
        T31_ERR2("IMP_AO_GetPubAttr failed, devID %d\r\n", tas->devID);
        goto err;
    }
    printf("freq: %dHz, bit %d, chn %d, frmNum %d, numPerFrm %d, cc %d \r\n",
           tas->attr.samplerate,
           tas->attr.bitwidth,
           tas->attr.soundmode,
           tas->attr.frmNum,
           tas->attr.numPerFrm,
           tas->attr.chnCnt);

    if (IMP_AO_Enable(tas->devID) != 0)
    {
        T31_ERR2("IMP_AO_Enable failed, devID %d\r\n", tas->devID);
        goto err;
    }

    if (IMP_AO_EnableChn(tas->devID, tas->chnID) != 0)
    {
        T31_ERR("IMP_AO_EnableChn failed\r\n");
        goto err;
    }

    if (IMP_AO_SetVol(tas->devID, tas->chnID, tas->aoVol) != 0)
    {
        T31_ERR("IMP_AO_SetVol failed\r\n");
        goto err;
    }

    if (IMP_AO_GetVol(tas->devID, tas->chnID, &tas->aoVol) != 0)
    {
        T31_ERR("IMP_AO_GetVol failed\r\n");
        goto err;
    }

    if (IMP_AO_SetGain(tas->devID, tas->chnID, tas->aoGain) != 0)
    {
        T31_ERR("IMP_AO_SetGain failed\r\n");
        goto err;
    }

    if (IMP_AO_GetGain(tas->devID, tas->chnID, &tas->aoGain) != 0)
    {
        T31_ERR("IMP_AO_GetGain failed\r\n");
        goto err;
    }

    return tas;
err:
    free(tas);
    return NULL;
}

void *plat_ai_init(int chn, int freq)
{
    T31_AI_Struct *tas = (T31_AI_Struct *)calloc(1, sizeof(T31_AI_Struct));

    tas->devID = T31_AI_DEV_ID;
    tas->chnID = T31_AI_CHN_ID;
    tas->aiVol = T31_AI_VOL_MAX;
    tas->aiGain = 30;

    tas->attr.samplerate = freq;
    tas->attr.bitwidth = AUDIO_BIT_WIDTH_16;
    tas->attr.soundmode = AUDIO_SOUND_MODE_MONO;
    tas->attr.frmNum = 20; // 数据没有发完情况下,最多缓存这么多份数据,即下面 numPerFrm x 该值
    tas->attr.numPerFrm = WMIX_FRAME_NUM;
    tas->attr.chnCnt = 1;

    tas->param.usrFrmDepth = tas->attr.frmNum;

    // AI参数配置(不依赖系统初始化?)
    if (IMP_AI_SetPubAttr(tas->devID, &tas->attr) != 0)
    {
        T31_ERR2("IMP_AI_SetPubAttr failed, devID %d\r\n", tas->devID);
        goto err;
    }

    memset(&tas->attr, 0, sizeof(tas->attr));
    if (IMP_AI_GetPubAttr(tas->devID, &tas->attr) != 0)
    {
        T31_ERR2("IMP_AI_GetPubAttr failed, devID %d\r\n", tas->devID);
        goto err;
    }
    printf("freq: %dHz, bit %d, chn %d, frmNum %d, numPerFrm %d, cc %d \r\n",
           tas->attr.samplerate,
           tas->attr.bitwidth,
           tas->attr.soundmode,
           tas->attr.frmNum,
           tas->attr.numPerFrm,
           tas->attr.chnCnt);

    if (IMP_AI_Enable(tas->devID) != 0)
    {
        T31_ERR2("IMP_AI_Enable failed, devID %d\r\n", tas->devID);
        goto err;
    }

    if (IMP_AI_SetChnParam(tas->devID, tas->chnID, &tas->param) != 0)
    {
        T31_ERR2("IMP_AI_SetChnParam failed, devID %d chnID %d\r\n", tas->devID, tas->chnID);
        goto err;
    }

    memset(&tas->param, 0, sizeof(tas->param));
    if (IMP_AI_GetChnParam(tas->devID, tas->chnID, &tas->param) != 0)
    {
        T31_ERR2("IMP_AI_GetChnParam failed, devID %d chnID %d\r\n", tas->devID, tas->chnID);
        goto err;
    }
    printf("usrFrmDepth %d \r\n", tas->param.usrFrmDepth);

    if (IMP_AI_EnableChn(tas->devID, tas->chnID) != 0)
    {
        T31_ERR("IMP_AI_EnableChn failed\r\n");
        goto err;
    }

    if (IMP_AI_SetVol(tas->devID, tas->chnID, tas->aiVol) != 0)
    {
        T31_ERR("IMP_AI_SetVol failed\r\n");
        goto err;
    }

    if (IMP_AI_GetVol(tas->devID, tas->chnID, &tas->aiVol) != 0)
    {
        T31_ERR("IMP_AI_GetVol failed\r\n");
        goto err;
    }
    printf("aiVol %d \r\n", tas->aiVol);

    if (IMP_AI_SetGain(tas->devID, tas->chnID, tas->aiGain) != 0)
    {
        T31_ERR("IMP_AI_SetGain failed\r\n");
        goto err;
    }

    if (IMP_AI_GetGain(tas->devID, tas->chnID, &tas->aiGain) != 0)
    {
        T31_ERR("IMP_AI_GetGain failed\r\n");
        goto err;
    }

#ifdef T31_AEC_EN
    if (IMP_AI_EnableAec(tas->devID, tas->chnID, T31_AO_DEV_ID, T31_AO_CHN_ID) != 0)
    {
        T31_ERR2("IMP_AI_EnableAec failed, devID %d chnID %d\r\n", tas->devID, tas->chnID);
        goto err;
    }
#endif

    return tas;
err:
    free(tas);
    return NULL;
}

int plat_ao_write(void *objAo, uint8_t *data, int len)
{
    T31_AO_Struct *tas = (T31_AO_Struct *)objAo;

    tas->frame.virAddr = (uint32_t *)data;
    tas->frame.len = len;

    //BLOCK阻塞, NOBLOCK非阻塞
    if (IMP_AO_SendFrame(tas->devID, tas->chnID, &tas->frame, BLOCK) != 0)
    {
        T31_ERR("IMP_AO_SendFrame error\r\n");
        return 0;
    }

    return len;
}

int plat_ai_read(void *objAi, uint8_t *data, int len)
{
    T31_AI_Struct *tas = (T31_AI_Struct *)objAi;

    if (IMP_AI_PollingFrame(tas->devID, tas->chnID, 500) != 0)
    {
        T31_ERR("IMP_AI_PollingFrame error\r\n");
        return 0;
    }

    //BLOCK阻塞, NOBLOCK非阻塞
    if (IMP_AI_GetFrame(tas->devID, tas->chnID, &tas->frame, BLOCK) != 0)
    {
        T31_ERR("IMP_AI_GetFrame error\r\n");
        return 0;
    }

    //长度检查
    if (len < tas->frame.len)
        T31_ERR2("warnning !! len(%d) < frame.len(%d) \r\n", len, tas->frame.len);
    else
        len = tas->frame.len;
    memcpy(data, tas->frame.virAddr, len);

    if (IMP_AI_ReleaseFrame(tas->devID, tas->chnID, &tas->frame) != 0)
    {
        T31_ERR("IMP_AI_ReleaseFrame error\r\n");
    }

    return len;
}

void plat_ao_exit(void *objAo)
{
    T31_AO_Struct *tas = (T31_AO_Struct *)objAo;

    if (!tas)
        return;

    // 等待缓冲区中的数据播放完毕
    // if (IMP_AO_FlushChnBuf(tas->devID, tas->chnID) != 0)
    //     T31_ERR("IMP_AO_FlushChnBuf error\r\n");

    if (IMP_AO_DisableChn(tas->devID, tas->chnID) != 0)
        T31_ERR("IMP_AO_DisableChn error\r\n");

    if (IMP_AO_Disable(tas->devID) != 0)
        T31_ERR("IMP_AO_Disable error\r\n");

    free(tas);
}

void plat_ai_exit(void *objAi)
{
    T31_AI_Struct *tas = (T31_AI_Struct *)objAi;

    if (!tas)
        return;

#ifdef T31_AEC_EN
    if (IMP_AI_DisableAec(tas->devID, tas->chnID) != 0)
        T31_ERR("IMP_AI_DisableAec error\r\n");
#endif

    if (IMP_AI_DisableChn(tas->devID, tas->chnID) != 0)
        T31_ERR("IMP_AI_DisableChn error\r\n");

    if (IMP_AI_Disable(tas->devID) != 0)
        T31_ERR("IMP_AI_Disable error\r\n");

    free(tas);
}
