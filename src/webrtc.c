/**************************************************
 * 
 *  二次封装webrtc旗下的vad、aec/aecm、ns/nsx、agc模块接口
 * 
 **************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "webrtc.h"

/* ==================== VAD 人声识别模块 ==================== */
#if (WMIX_WEBRTC_VAD)
#include "webrtc_vad.h"

#define VAD_AGGRESSIVE 3 // 效果激进程度 0~3

typedef struct
{
    VadInst *handle;
    int chn;
    int freq;
    int intervalMs;
    int pkgFrame; //转换时每包帧数,等于chn*freq/1000*20ms
    int reduce;   //消音减益
} Vad_Struct;

/*
 *  初始化
 * 
 *  param:
 *      chn <in> : 声道数
 *      freq <in> : 8000, 16000, 32000
 *      intervalMs <int> : 分包间隔 10ms, 20ms
 *  return: 
 *      fp指针
 */
void *vad_init(int chn, int freq, int intervalMs)
{
    Vad_Struct *vs;
    if(freq > 32000)
        return NULL;
    vs = (Vad_Struct *)calloc(1, sizeof(Vad_Struct));
    if (WebRtcVad_Create(&vs->handle) == 0)
    {
        if (WebRtcVad_Init(vs->handle) == 0)
        {
            if (WebRtcVad_set_mode(vs->handle, VAD_AGGRESSIVE) == 0)
            {
                vs->chn = chn;
                vs->freq = freq;
                //采样间隔只能 10ms 或 20ms
                if(intervalMs%20 == 0)
                    vs->intervalMs = 20;
                else
                    vs->intervalMs = 10;
                vs->pkgFrame = freq / 1000 * vs->intervalMs;
                vs->reduce = 4; //默认消音
                printf("vad_init: chn/%d freq/%d pkgFrame/%d\r\n", chn, freq, vs->pkgFrame);
                return vs;
            }
#ifdef WMIX_WEBRTC_DEBUG
            else
                printf("WebRtcVad_set_mode failed !!\r\n");
#endif
        }
#ifdef WMIX_WEBRTC_DEBUG
        else
            printf("WebRtcVad_Init failed !!\r\n");
#endif
        WebRtcVad_Free(vs->handle);
    }
#ifdef WMIX_WEBRTC_DEBUG
    else
        printf("WebRtcVad_Create failed !!\r\n");
#endif
    free(vs);
    return NULL;
}

/*
 *  录音数据处理,在无人说话时主动降噪
 * 
 *  param:
 *      frame <in/out> : 录音数据
 *      frameLen <in> : 帧数(每帧chn*2字节), 必须为 chn*freq/1000*10ms 的倍数
 */
void vad_process(void *fp, int16_t *frame, int frameLen)
{
    Vad_Struct *vs = fp;
    int cChn, cLen, cPkg, cReduce, ret;
    int16_t *pFrame;
    int32_t tmp32;
    int realFrameLen;

    //实际 frame 的 int16_t 字数
    realFrameLen = frameLen * vs->chn;

    pFrame = frame;
    cPkg = realFrameLen;
    //多声道数据合并为单声道)
    if (vs->chn > 1)
    {
        for (cLen = cPkg = 0; cLen < realFrameLen;)
        {
            //取当前帧所有声道数据的总和
            for (tmp32 = cChn = 0; cChn < vs->chn; cChn++)
                tmp32 += frame[cLen++];
            //合并为一个声道的数据
            pFrame[cPkg++] = (int16_t)(tmp32 / vs->chn);
        }
    }

    //转换数据
    for (cLen = 0; cLen < cPkg; cLen += vs->pkgFrame)
    {
        ret = WebRtcVad_Process(vs->handle, vs->freq, pFrame, vs->pkgFrame);
        if (ret < 0)
        {
#ifdef WMIX_WEBRTC_DEBUG
            printf("WebRtcVad_Process failed !!, ret %d \r\n", ret);
#endif
            return;
        }
        else if (ret == 0)
        {
            // reduce 逐渐增大,直至完全消声
            if (vs->reduce < 4)
                vs->reduce += 1;
        }
        else
        {
            // reduce 逐渐恢复到0,直至最大音量
            if (vs->reduce > 0)
                vs->reduce -= 1;
        }
        // 声音根据 reduce 的大小右移(声音变小)
        for (cReduce = cLen; cReduce < vs->pkgFrame; cReduce++)
            pFrame[cReduce] >>= vs->reduce;
    }

    //单声道恢复为多声道, 这里要倒着拷贝
    if (vs->chn > 1)
    {
        for (cLen = realFrameLen -1, cPkg -= 1; cLen >= 0; cPkg--)
            for (cChn = 0; cChn < vs->chn; cChn++)
                frame[cLen--] = pFrame[cPkg];
    }
}

/*
 *  内存回收 
 */
void vad_release(void *fp)
{
    Vad_Struct *vs = fp;
    WebRtcVad_Free(vs->handle);
    free(vs);
#ifdef WMIX_WEBRTC_DEBUG
    printf("vad_release\r\n");
#endif
}

#endif
/* ==================== AEC 回声消除 ==================== */
#if (WMIX_WEBRTC_AEC)

#include "echo_cancellation.h"
#include "echo_control_mobile.h"

// #undef WMIX_WEBRTC_AEC // do this switch to AECM

#ifdef WMIX_WEBRTC_AEC
#define AEC_FRAME_TYPE float
#define WebRtcAecX_Create(x) WebRtcAec_Create(x)
#define WebRtcAecX_Init(a, b, c) WebRtcAec_Init(a, b, c)
#define WebRtcAecX_set_config(x, y) WebRtcAec_set_config(x, y)
#define WebRtcAecX_BufferFarend(a, b, c) WebRtcAec_BufferFarend(a, b, c)
#define WebRtcAecX_Process(a, b, c, d, e, f, g) WebRtcAec_Process(a, b, c, d, e, f, g)
#define WebRtcAecX_Free(x) WebRtcAec_Free(x)
#else
#define AEC_FRAME_TYPE int16_t
#define WebRtcAecX_Create(x) WebRtcAecm_Create(x)
#define WebRtcAecX_Init(a, b, c) WebRtcAecm_Init(a, b)
#define WebRtcAecX_set_config(x, y) (0)
#define WebRtcAecX_BufferFarend(a, b, c) WebRtcAecm_BufferFarend(a, b, c)
#define WebRtcAecX_Process(a, b, c, d, e, f) WebRtcAecm_Process(a, b, c, d, e, f)
#define WebRtcAecX_Free(x) WebRtcAecm_Free(x)
#endif

typedef struct
{
    void *aecInst;
    int chn;
    int freq;
    int intervalMs;
    int pkgFrame; //转换时每包帧数,等于chn*freq/1000*20ms
    AEC_FRAME_TYPE *in[2];
    AEC_FRAME_TYPE *out[2];
    AEC_FRAME_TYPE *far;
} Aec_Struct;

/*
 *  初始化
 * 
 *  param:
 *      chn <in> : 声道数
 *      freq <in> : 8000, 16000, 32000
 *      intervalMs <int> : 分包间隔 10ms, 20ms
 *  return:
 *      fp指针
 */
void *aec_init(int chn, int freq, int intervalMs)
{
    Aec_Struct *as;
    if(freq > 32000)
        return NULL;
    as = (Aec_Struct *)calloc(1, sizeof(Aec_Struct));
    AecConfig config = {
        .nlpMode = kAecNlpModerate,
        .skewMode = kAecFalse,
        .metricsMode = kAecFalse,
        .delay_logging = kAecFalse,
    };
    if (WebRtcAecX_Create(&as->aecInst) == 0)
    {
        if (WebRtcAecX_Init(as->aecInst, freq, freq) == 0)
        {
            if (WebRtcAecX_set_config(as->aecInst, config) == 0)
            {
                as->chn = chn;
                as->freq = freq;
                //采样间隔只能 10ms 或 20ms
                if(intervalMs%20 == 0)
                    as->intervalMs = 20;
                else
                    as->intervalMs = 10;
                as->pkgFrame = freq / 1000 * as->intervalMs; //必须10ms每包
                //单声道
                as->in[0] = (AEC_FRAME_TYPE *)calloc(as->pkgFrame, sizeof(AEC_FRAME_TYPE));
                as->out[0] = (AEC_FRAME_TYPE *)calloc(as->pkgFrame, sizeof(AEC_FRAME_TYPE));
                //双声道
                if (chn > 1)
                {
                    as->in[1] = (AEC_FRAME_TYPE *)calloc(as->pkgFrame, sizeof(AEC_FRAME_TYPE));
                    as->out[1] = (AEC_FRAME_TYPE *)calloc(as->pkgFrame, sizeof(AEC_FRAME_TYPE));
                }
                as->far = (AEC_FRAME_TYPE *)calloc(as->pkgFrame, sizeof(AEC_FRAME_TYPE));
                printf("aec_init: chn/%d freq/%d pkgFrame/%d x %d\r\n", chn, freq, as->pkgFrame, chn);
                return as;
            }
#ifdef WMIX_WEBRTC_DEBUG
            else
                printf("WebRtcAecX_set_config failed !!\r\n");
#endif
        }
#ifdef WMIX_WEBRTC_DEBUG
        else
            printf("WebRtcAecX_Init failed !!\r\n");
#endif
        WebRtcAecX_Free(as->aecInst);
    }
#ifdef WMIX_WEBRTC_DEBUG
    else
        printf("WebRtcAecX_Create failed !!\r\n");
#endif
    free(as);
    return NULL;
}

/*
 *  输入远端音频数据(即录音数据)
 * 
 *  param:
 *      frameFar <in> : 远端音频数据(即录音数据)
 *      frameLen <in> : 帧数(每帧chn*2字节), 必须为 chn*freq/1000*10ms 的倍数
 *  return:
 *      0/OK
 *      -1/failed
 */
int aec_setFrameFar(void *fp, int16_t *frameFar, int frameLen)
{
    Aec_Struct *as = fp;
    int ret;
    int cLen, cPkg, cChn;
    int realFrameLen, realPkgFrame;
    int32_t temp32;

    //实际 frameFar 的 int16_t 字数
    realFrameLen = frameLen * as->chn;

    //实际每包数据的 int16_t 字数
    realPkgFrame = as->pkgFrame * as->chn;

    for (cLen = 0; cLen < realFrameLen; cLen += realPkgFrame)
    {
        //装载数据,把 int6_t 转为 AEC_FRAME_TYPE (双声道时,把左右合并到ns->far)
        for (cPkg = 0; cPkg < as->pkgFrame; cPkg++)
        {
            for (cChn = temp32 = 0; cChn < as->chn; cChn++)
                temp32 += (*frameFar++);
            //合并多声道数据
            as->far[cPkg] = (AEC_FRAME_TYPE)(temp32/as->chn);
        }
        //开始处理
        ret = WebRtcAecX_BufferFarend(
            as->aecInst,
            (const AEC_FRAME_TYPE *)as->far,
            (int16_t)as->pkgFrame);
        if (ret != 0)
        {
#ifdef WMIX_WEBRTC_DEBUG
            printf("WebRtcAecX_BufferFarend failed !!, ret %d \r\n", ret);
#endif
            return ret;
        }
    }
    return 0;
}

/*
 *  根据 aec_setFrameFar() 的远端数据处理近端数据(即将要播放的音频数据)
 * 
 *  param:
 *      frameNear <in> : 近端数据(即将要播放的音频数据)
 *      frameOut <out> : 处理好的播音数据
 *      frameLen <in> : 帧数(每帧chn*2字节), 必须为 chn*freq/1000*10ms 的倍数
 *      delayms <in> : 录播音估计延时间隔
 *  return:
 *      0/OK
 *      -1/failed
 */
int aec_process(void *fp, int16_t *frameNear, int16_t *frameOut, int frameLen, int delayms)
{
    Aec_Struct *as = fp;
    int ret;
    int cLen, cPkg, cChn;
    int realFrameLen, realPkgFrame;
    int32_t temp32;

    //实际 frameFar 的 int16_t 字数
    realFrameLen = frameLen * as->chn;

    //实际每包数据的 int16_t 字数
    realPkgFrame = as->pkgFrame * as->chn;

    for (cLen = 0; cLen < realFrameLen; cLen += realPkgFrame)
    {
        //装载数据,把 int6_t 转为 AEC_FRAME_TYPE (双声道时,把左右声合并到ns->in[0])
        for (cPkg = 0; cPkg < as->pkgFrame; cPkg++)
        {
            for (cChn = temp32 = 0; cChn < as->chn; cChn++)
                temp32 += (*frameNear++);
            //合并多声道数据
            as->in[0][cPkg] = as->in[1][cPkg] = (AEC_FRAME_TYPE)(temp32/as->chn);
        }
        //开始处理
#ifdef WMIX_WEBRTC_AEC
        ret = WebRtcAecX_Process(
            as->aecInst,
            (const AEC_FRAME_TYPE *const *)as->in, //注意这里in和下面out是 AEC_FRAME_TYPE *in[2] 指针(即左右声道数据)
            1,//as->chn,//只支持单声道
            (AEC_FRAME_TYPE *const *)as->out,
            as->pkgFrame,
            delayms,
            0);
#else
        ret = WebRtcAecX_Process(
            as->aecInst,
            (const AEC_FRAME_TYPE *)as->in[0], //仅单声道
            NULL,
            (AEC_FRAME_TYPE *)as->out[0], //仅单声道
            as->pkgFrame,
            delayms);
#endif
        if (ret != 0)
        {
#ifdef WMIX_WEBRTC_DEBUG
            printf("WebRtcAecX_Process failed !!, ret %d \r\n", ret);
#endif
            return ret;
        }
        //提取输出数据
        for (cPkg = 0; cPkg < as->pkgFrame; cPkg++)
            for (cChn = 0; cChn < as->chn; cChn++)
                *frameOut++ = (int16_t)as->out[0][cPkg];
    }

    return 0;
}

/*
 *  二合一回声消除
 * 
 *  param:
 *      frameFar <in> : 远端音频数据(即录音数据)
 *      frameNear <in> : 近端数据(即将要播放的音频数据)
 *      frameOut <out> : 处理好的播音数据
 *      frameLen <in> : 帧数(每帧chn*2字节), 必须为 chn*freq/1000*10ms 的倍数
 *      delayms <in> : 录播音估计延时间隔
 *  return:
 *      0/OK
 *      -1/failed
 */
int aec_process2(void *fp, int16_t *frameFar, int16_t *frameNear, int16_t *frameOut, int frameLen, int delayms)
{
    Aec_Struct *as = fp;
    int ret;
    int cLen, cPkg, cChn;
    int realFrameLen, realPkgFrame;
    int32_t temp32, temp32s;

    //实际 frameFar 的 int16_t 字数
    realFrameLen = frameLen * as->chn;

    //实际每包数据的 int16_t 字数
    realPkgFrame = as->pkgFrame * as->chn;

    for (cLen = 0; cLen < realFrameLen; cLen += realPkgFrame)
    {
        //装载数据,把 int6_t 转为 AEC_FRAME_TYPE (双声道时,把左右声合并到ns->in[0])
        for (cPkg = 0; cPkg < as->pkgFrame; cPkg++)
        {
            //其它声道数据
            for (cChn = temp32 = temp32s = 0; cChn < as->chn; cChn++)
            {
                temp32 += (*frameFar++);
                temp32s += (*frameNear++);
            }
            //取左声道数据
            as->far[cPkg] = (AEC_FRAME_TYPE)(temp32/as->chn);
            as->in[0][cPkg] = as->in[1][cPkg] = (AEC_FRAME_TYPE)(temp32s/as->chn);
        }
        //开始处理
        ret = WebRtcAecX_BufferFarend(
            as->aecInst,
            (const AEC_FRAME_TYPE *)as->far,
            (int16_t)as->pkgFrame);
        if (ret != 0)
        {
#ifdef WMIX_WEBRTC_DEBUG
            printf("WebRtcAecX_BufferFarend failed !!, ret %d \r\n", ret);
#endif
            return ret;
        }
        //开始处理
#ifdef WMIX_WEBRTC_AEC
        ret = WebRtcAecX_Process(
            as->aecInst,
            (const AEC_FRAME_TYPE *const *)as->in, //注意这里in和下面out是 AEC_FRAME_TYPE *in[2] 指针(即左右声道数据)
            1,//as->chn,//只支持单声道
            (AEC_FRAME_TYPE *const *)as->out,
            as->pkgFrame,
            delayms,
            0);
#else
        ret = WebRtcAecX_Process(
            as->aecInst,
            (const AEC_FRAME_TYPE *)as->in[0], //仅单声道
            NULL,
            (AEC_FRAME_TYPE *)as->out[0], //仅单声道
            as->pkgFrame,
            delayms);
#endif
        if (ret != 0)
        {
#ifdef WMIX_WEBRTC_DEBUG
            printf("WebRtcAecX_Process failed !!, ret %d \r\n", ret);
#endif
            return ret;
        }
        //提取输出数据
        for (cPkg = 0; cPkg < as->pkgFrame; cPkg++)
            for (cChn = 0; cChn < as->chn; cChn++)
                *frameOut++ = (int16_t)as->out[0][cPkg];
    }

    return 0;
}

/*
 *  二合一回声消除
 * 
 *  param:
 *      frameFar <in> : 远端音频数据(即录音数据)
 *      frameNear <in> : 近端数据(即将要播放的音频数据)
 *      frameOut <out> : 处理好的播音数据
 *      frameLen <in> : 帧数(每帧chn*2字节), 必须为 chn*freq/1000*10ms 的倍数
 *      reduce <in> : frameFar乘上的倍数, 决定了消音强度, 推荐范围0.1 ~ 1.0
 *  return:
 *      0/OK
 *      -1/failed
 */
int aec_process3(void *fp, int16_t *frameFar, int16_t *frameNear, int16_t *frameOut, int frameLen, float reduce)
{
    Aec_Struct *as = fp;
    int cLen, cPkg, cChn;
    int realFrameLen, realPkgFrame;

    //实际 frameFar 的 int16_t 字数
    realFrameLen = frameLen * as->chn;

    //实际每包数据的 int16_t 字数
    realPkgFrame = as->pkgFrame * as->chn;

    for (cLen = 0; cLen < realFrameLen; cLen += realPkgFrame)
    {
        //装载数据,把 int6_t 转为 AEC_FRAME_TYPE (双声道时,把左右声拆分到ns->out[2])
        for (cPkg = 0; cPkg < as->pkgFrame; cPkg++)
        {
            for (cChn = 0; cChn < as->chn; cChn++)
            {
                as->in[cChn][cPkg] = (AEC_FRAME_TYPE)(*frameFar++);
                as->out[cChn][cPkg] = (AEC_FRAME_TYPE)(*frameNear++);
            }
        }
        // 把 as->out[2] 中的 as->in[2] (frameFar) 减去
        for (cPkg = 0; cPkg < as->pkgFrame; cPkg++)
            for (cChn = 0; cChn < as->chn; cChn++)
                as->out[cChn][cPkg] = as->out[cChn][cPkg] - (AEC_FRAME_TYPE)(as->in[cChn][cPkg]*reduce);
        //提取输出数据
        for (cPkg = 0; cPkg < as->pkgFrame; cPkg++)
            for (cChn = 0; cChn < as->chn; cChn++)
                *frameOut++ = (int16_t)as->out[cChn][cPkg];
    }

    return 0;
}

/*
 *  内存回收 
 */
void aec_release(void *fp)
{
    Aec_Struct *as = fp;
    WebRtcAecX_Free(as->aecInst);
    free(as->in[0]);
    free(as->out[0]);
    if (as->chn > 1)
    {
        free(as->in[1]);
        free(as->out[1]);
    }
    free(as->far);
    free(as);
#ifdef WMIX_WEBRTC_DEBUG
    printf("aec_release\r\n");
#endif
}

#endif
/* ==================== NS 噪音抑制 ==================== */
#if (WMIX_WEBRTC_NS)
#include "noise_suppression.h"
#include "noise_suppression_x.h"

// #define WMIX_WEBRTC_NSX // define this switch to NSX

#ifdef WMIX_WEBRTC_NSX
#define NSX_FRAME_TYPE short
#define WebRtcNsX_Create(x) WebRtcNsx_Create(x)
#define WebRtcNsX_Free(x) WebRtcNsx_Free(x)
#define WebRtcNsX_Init(x, y) WebRtcNsx_Init(x, y)
#define WebRtcNsX_set_policy(x, y) WebRtcNsx_set_policy(x, y)
#define WebRtcNsX_Analyze(x, y)
#define WebRtcNsX_Process(a, b, c, d) WebRtcNsx_Process(a, b, c, d)
#else
#define NSX_FRAME_TYPE float
#define WebRtcNsX_Create(x) WebRtcNs_Create(x)
#define WebRtcNsX_Free(x) WebRtcNs_Free(x)
#define WebRtcNsX_Init(x, y) WebRtcNs_Init(x, y)
#define WebRtcNsX_set_policy(x, y) WebRtcNs_set_policy(x, y)
#define WebRtcNsX_Analyze(x, y) WebRtcNs_Analyze(x, y)
#define WebRtcNsX_Process(a, b, c, d) WebRtcNs_Process(a, b, c, d)
#endif

#define NS_AGGRESSIVE 2 // 效果激进程度 0~2

typedef struct
{
#ifdef WMIX_WEBRTC_NSX
    NsxHandle *nsxInst;
#else
    NsHandle *nsxInst;
#endif
    int chn;
    int freq;
    int intervalMs;
    int pkgFrame; //转换时每包帧数,等于chn*freq/1000*20ms
    NSX_FRAME_TYPE *in[2];
    NSX_FRAME_TYPE *out[2];
} Ns_Struct;

/*
 *  初始化
 * 
 *  param:
 *      chn <in> : 声道数
 *      freq <in> : 8000, 16000, 32000
 *  return:
 *      fp指针
 */
void *ns_init(int chn, int freq)
{
    Ns_Struct *ns;
    if(freq > 32000)
        return NULL;
    ns = (Ns_Struct *)calloc(1, sizeof(Ns_Struct));
    if (WebRtcNsX_Create(&ns->nsxInst) == 0)
    {
        if (WebRtcNsX_Init(ns->nsxInst, freq) == 0)
        {
            if (WebRtcNsX_set_policy(ns->nsxInst, NS_AGGRESSIVE) == 0)
            {
                ns->chn = chn;
                ns->freq = freq;
                //必须10ms每包
                ns->intervalMs = 10;
                ns->pkgFrame = freq / 1000 * ns->intervalMs;
                //单声道
                ns->in[0] = (NSX_FRAME_TYPE *)calloc(ns->pkgFrame, sizeof(NSX_FRAME_TYPE));
                ns->out[0] = (NSX_FRAME_TYPE *)calloc(ns->pkgFrame, sizeof(NSX_FRAME_TYPE));
                //双声道
                if (chn > 1)
                {
                    ns->in[1] = (NSX_FRAME_TYPE *)calloc(ns->pkgFrame, sizeof(NSX_FRAME_TYPE));
                    ns->out[1] = (NSX_FRAME_TYPE *)calloc(ns->pkgFrame, sizeof(NSX_FRAME_TYPE));
                }
                printf("ns_init: chn/%d freq/%d pkgFrame/%d x %d\r\n", chn, freq, ns->pkgFrame, chn);
                return ns;
            }
#ifdef WMIX_WEBRTC_DEBUG
            else
                printf("WebRtcNs_set_policy failed !!\r\n");
#endif
        }
#ifdef WMIX_WEBRTC_DEBUG
        else
            printf("WebRtcNs_Init failed !!\r\n");
#endif
        WebRtcNsX_Free(ns->nsxInst);
    }
#ifdef WMIX_WEBRTC_DEBUG
    else
        printf("WebRtcNs_Create failed !!\r\n");
#endif
    free(ns);
    return NULL;
}

/*
 *  噪音抑制
 * 
 *  param:
 *      frame <in> : 音频数据,采样间隔必须10ms的整数倍数
 *      frameOut <out> : 输出数据
 *      frameLen <in> : 帧数(每帧chn*2字节), 必须为 chn*freq/1000*10ms 的倍数
 */
void ns_process(void *fp, int16_t *frame, int16_t *frameOut, int frameLen)
{
    Ns_Struct *ns = fp;
    int cLen, cPkg, cChn;
    int realFrameLen, realPkgFrame;

    //实际 frameFar 的 int16_t 字数
    realFrameLen = frameLen * ns->chn;

    //实际每包数据的 int16_t 字数
    realPkgFrame = ns->pkgFrame * ns->chn;

    for (cLen = 0; cLen < realFrameLen; cLen += realPkgFrame)
    {
        //装载数据,把 int6_t 转为 NSX_FRAME_TYPE (双声道时,把左右声拆分到ns->in[2])
        for (cPkg = 0; cPkg < ns->pkgFrame; cPkg++)
            for (cChn = 0; cChn < ns->chn; cChn++)
                ns->in[cChn][cPkg] = (NSX_FRAME_TYPE)(*frame++);
        //开始处理
        WebRtcNsX_Analyze(
            ns->nsxInst,
            (const NSX_FRAME_TYPE *)ns->in[0]); //提供左声道的数据
        WebRtcNsX_Process(
            ns->nsxInst,
            (const NSX_FRAME_TYPE *const *)ns->in, //注意这里in和下面out是 NSX_FRAME_TYPE *in[2] 指针(即左右声道数据)
            ns->chn,
            (NSX_FRAME_TYPE *const *)ns->out);
        //提取输出数据
        for (cPkg = 0; cPkg < ns->pkgFrame; cPkg++)
            for (cChn = 0; cChn < ns->chn; cChn++)
                *frameOut++ = (int16_t)ns->out[cChn][cPkg];
    }
}

/*
 *  内存回收 
 */
void ns_release(void *fp)
{
    Ns_Struct *ns = fp;
    WebRtcNsX_Free(ns->nsxInst);
    free(ns->in[0]);
    free(ns->out[0]);
    if (ns->chn > 1)
    {
        free(ns->in[1]);
        free(ns->out[1]);
    }
    free(ns);
#ifdef WMIX_WEBRTC_DEBUG
    printf("ns_release\r\n");
#endif
}

#endif
/* ==================== AGC 自动增益 ==================== */
#if (WMIX_WEBRTC_AGC)
#include "gain_control.h"

/*
 *  初始化
 * 
 *  param:
 *      chn <in> : 声道数
 *      freq <in> : 8000, 16000, 32000
 *      intervalMs <int> : 分包间隔 10ms, 20ms
 *  return:
 *      fp指针
 */
void *agc_init(int chn, int freq, int intervalMs)
{
    
    if(freq > 32000)
        return NULL;
    
    return NULL;
}

/*
 *  自动增益
 * 
 *  param:
 *      frame <in/out> : 录音数据
 *      frameLen <in> : 帧数(每帧chn*2字节), 必须为 chn*freq/1000*10ms 的倍数
 */
void agc_process(void *fp, int16_t *frame, int frameLen)
{
    ;
}

/*
 *  内存回收 
 */
void agc_release(void *fp)
{
    ;
}

#endif
/* ******************** example ********************

#include <stdio.h>
// vad
#include "webrtc_vad.h"
//aec
#include "echo_cancellation.h"
//aecm
#include "echo_control_mobile.h"
//ns
#include "noise_suppression.h"
#include "noise_suppression_x.h"
//agc
#include "gain_control.h"

void help(char *name){
    printf("\r\n"
        "Usage: \r\n"
        "\r\n"
        );
}

void main(int argc, char **argv){
    
    int ret = 0;
    
    VadInst* handle;
    void *aecInst;
    void *aecmInst;
    NsHandle *nsxInst;
    NsxHandle *nsxInst;
    void *agcInst;
    
    int16_t audio_frame[160] = {0};
    
    // ---------- agc ---------
    
    // 创建句柄
    ret = WebRtcAgc_Create(&agcInst);
    printf("WebRtcAgc_Create: ret %d\r\n", ret);
    
    // 回收内存
    ret = WebRtcAgc_Free(agcInst);
    printf("WebRtcAgc_Free: ret %d\r\n", ret);
    
    // ---------- ns ---------
    
    // 创建句柄
    ret = WebRtcNs_Create(&nsxInst);
    printf("WebRtcNs_Create: ret %d\r\n", ret);
    
    // 回收内存
    ret = WebRtcNs_Free(nsxInst);
    printf("WebRtcNs_Free: ret %d\r\n", ret);
    
    // ---------- nsx ---------
    
    // 创建句柄
    ret = WebRtcNsx_Create(&nsxInst);
    printf("WebRtcNsx_Create: ret %d\r\n", ret);
    
    // 回收内存
    ret = WebRtcNsx_Free(nsxInst);
    printf("WebRtcNsx_Free: ret %d\r\n", ret);
    
    // ---------- aecm ---------
    
    // 创建句柄
    ret = WebRtcAecm_Create(&aecmInst);
    printf("WebRtcAecm_Create: ret %d\r\n", ret);
    
    // 初始化句柄
    ret = WebRtcAecm_Init(aecmInst, 8000);
    printf("WebRtcAecm_Init: ret %d\r\n", ret);
    
    // 回收内存
    ret = WebRtcAecm_Free(aecmInst);
    printf("WebRtcAecm_Free: ret %d\r\n", ret);
    
    // ---------- aec ---------
    
    // 创建句柄
    ret = WebRtcAecX_Create(&aecInst);
    printf("WebRtcAecX_Create: ret %d\r\n", ret);
    
    // 初始化句柄
    ret = WebRtcAecX_Init(aecInst, 8000, 8000);
    printf("WebRtcAecX_Init: ret %d\r\n", ret);
    
    // 回收内存
    ret = WebRtcAecX_Free(aecInst);
    printf("WebRtcAecX_Free: ret %d\r\n", ret);
    
    // ---------- vad ---------
    
    // 创建句柄
    ret = WebRtcVad_Create(&handle);
    printf("WebRtcVad_Create: ret %d\r\n", ret);
    
    // 初始化句柄
    ret = WebRtcVad_Init(handle);
    printf("WebRtcVad_Init: ret %d\r\n", ret);
    
    // 设置模式
    // 0 (High quality) - 3 (Highly aggressive), 数字越大越激进
    ret = WebRtcVad_set_mode(handle, 0);
    printf("WebRtcVad_set_mode: ret %d\r\n", ret);

    // 数据处理
    // 8000Hz 20ms 采样间隔时, 每包160帧, 每帧2字节
    ret = WebRtcVad_Process(handle, 8000, audio_frame, 160);
    printf("WebRtcVad_Process: ret %d\r\n", ret);
    
    // 采样频率8000/16000/32000/48000Hz 和 每次解析帧数的组合 frame_length 是否合法
    // 即 WebRtcVad_Process(x,x,frame_length) 里面的 frame_length
    // 示例:
    //     8000Hz, 16bit, 间隔20ms, 则 frame_length = 8000/1000*20 = 160 frame (每帧2字节,用的 int16_t*)
    //     16000Hz, 16bit, 间隔30ms, 则 frame_length = 16000/1000*30 = 480 frame
    ret = WebRtcVad_ValidRateAndFrameLength(8000, 160);
    printf("WebRtcVad_ValidRateAndFrameLength: ret %d\r\n", ret);
    
    // 回收内存
    WebRtcVad_Free(handle);
}

******************** example ******************** */
