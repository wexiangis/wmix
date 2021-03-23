/**************************************************
 * 
 *  二次封装speex接口
 * 
 **************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "speexType.h"

#if (MAKE_SPEEX_BETA3)

#include "speex/speex_echo.h"

/* ==================== AEC 回声消除 ==================== */

typedef struct
{
    void *st;
    int chn;
    int freq;
    int intervalMs;
    int pkgFrame; //转换时每包帧数,等于chn*freq/1000*20ms
    int16_t *in[2];
    int16_t *out[2];
    int16_t *far;
    bool *debug;
} SpxAec_Struct;

/*
 *  初始化
 * 
 *  param:
 *      chn <in> : 声道数
 *      freq <in> : 8000, 16000
 *      intervalMs <int> : 分包间隔 10ms, 20ms
 *      delayms <int> : 录播音估计延时间隔
 *      debug : debug标志指针,不使用则置NULL
 *  return:
 *      fp指针
 */
void *spx_aec_init(int chn, int freq, int intervalMs, int delayms, bool *debug)
{
    SpxAec_Struct *sas;
    if (freq > 16000 || freq % 8000 != 0)
        return NULL;
    sas = (SpxAec_Struct *)calloc(1, sizeof(SpxAec_Struct));
    sas->debug = debug;
    sas->chn = chn;
    sas->freq = freq;
    //采样间隔只能 10ms 或 20ms
    if (freq <= 8000)
    {
        if (intervalMs % 20 == 0)
            sas->intervalMs = 20;
        else
            sas->intervalMs = 10;
    }
    else
        sas->intervalMs = 10;
    sas->pkgFrame = freq / 1000 * sas->intervalMs; //必须10ms每包
    //初始化
    sas->st = speex_echo_state_init(sas->pkgFrame, delayms);
    if (!sas->st)
    {
        if (debug && (*debug))
            printf("speex_echo_state_init failed !! \r\n");
        free(sas);
        return NULL;
    }
    //单声道
    sas->in[0] = (int16_t *)calloc(sas->pkgFrame, sizeof(int16_t));
    sas->out[0] = (int16_t *)calloc(sas->pkgFrame, sizeof(int16_t));
    sas->far = (int16_t *)calloc(sas->pkgFrame, sizeof(int16_t));
    //双声道
    if (chn > 1)
    {
        sas->in[1] = (int16_t *)calloc(sas->pkgFrame, sizeof(int16_t));
        sas->out[1] = (int16_t *)calloc(sas->pkgFrame, sizeof(int16_t));
    }
    if (debug && (*debug))
        printf("spx_aec_init: chn/%d freq/%d intervalMs/%d pkgFrame/%d x %d\r\n", chn, freq, sas->intervalMs, sas->pkgFrame, chn);
    return sas;
}

/*
 *  二合一回声消除
 * 
 *  param:
 *      frameFar <in> : 远端音频数据(即录音数据)
 *      frameNear <in> : 近端数据(即将要播放的音频数据)
 *      frameOut <out> : 处理好的播音数据
 *      frameNum <in> : 帧数(每帧chn*2字节), 必须为 chn*freq/1000*10ms 的倍数
 *  return:
 *      0/OK
 *      -1/failed
 */
int spx_aec_process(void *fp, int16_t *frameFar, int16_t *frameNear, int16_t *frameOut, int frameNum)
{
    SpxAec_Struct *sas = (SpxAec_Struct *)fp;
    int cLen, cPkg, cChn;
    int realFrameLen, realPkgFrame;

    //实际 frameFar 的 int16_t 字数
    realFrameLen = frameNum * sas->chn;

    //实际每包数据的 int16_t 字数
    realPkgFrame = sas->pkgFrame * sas->chn;

    for (cLen = 0; cLen < realFrameLen; cLen += realPkgFrame)
    {
        //装载数据,把 int6_t 转为 int16_t (双声道时,把左声提取到ns->in[0])
        for (cPkg = 0; cPkg < sas->pkgFrame; cPkg++)
        {
            //取左声道数据
            sas->far[cPkg] = (int16_t)(*frameFar++);
            sas->in[0][cPkg] = (int16_t)(*frameNear++);
            if (sas->chn > 1)
                sas->in[1][cPkg] = sas->in[0][cPkg];
            //丢弃其它声道数据
            for (cChn = 1; cChn < sas->chn; cChn++)
            {
                frameFar++;
                frameNear++;
            }
        }
        //开始处理
        speex_echo_cancellation(
            sas->st,
            (const spx_int16_t *)sas->in[0],  //仅单声道
            (const spx_int16_t *)sas->out[0], //仅单声道
            (spx_int16_t *)sas->out[0]);      //仅单声道
        //提取输出数据
        for (cPkg = 0; cPkg < sas->pkgFrame; cPkg++)
            for (cChn = 0; cChn < sas->chn; cChn++)
                *frameOut++ = (int16_t)sas->out[0][cPkg];
    }

    return 0;
}

void spx_aec_release(void *fp)
{
    SpxAec_Struct *sas = (SpxAec_Struct *)fp;
    speex_echo_state_destroy(sas->st);
    free(sas->in[0]);
    free(sas->out[0]);
    if (sas->chn > 1)
    {
        free(sas->in[1]);
        free(sas->out[1]);
    }
    if (sas->debug && (*sas->debug))
        printf("spx_aec_release\r\n");
    free(sas);
}

#endif // #if (MAKE_SPEEX_BETA3)