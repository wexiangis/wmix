#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "webrtc.h"

/* ==================== VAD 人声识别模块 ==================== */
#if(WMIX_WEBRTC_VAD)
#include "webrtc_vad.h"

#define VAD_AGGRESSIVE 3 // 效果激进程度 0~3

typedef struct{
    VadInst* handle;
    int chn;
    int freq;
    int framePerPkg;//转换时每包帧数,等于chn*freq/1000*20ms
    int reduce;//消音减益
}Vad_Struct;

/*
 *  初始化
 * 
 *  param:
 *      chn <in> : 声道数
 *      freq <in> : 8000, 16000, 32000, 48000
 *  return: 
 *      fp指针
 */
void* vad_init(int chn, int freq)
{
    Vad_Struct *vs = (Vad_Struct*)calloc(1, sizeof(Vad_Struct));
    if(WebRtcVad_Create(&vs->handle) == 0)
    {
        if(WebRtcVad_Init(vs->handle) == 0)
        {
            if(WebRtcVad_set_mode(vs->handle, VAD_AGGRESSIVE) == 0)
            {
                vs->chn = chn;
                vs->freq = freq;
                vs->framePerPkg = 1*freq/1000*20;
                vs->reduce = 4;//默认消音
                printf("vad_init: chn/%d freq/%d framePerPkg/%d\r\n", chn, freq, vs->framePerPkg);
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
 *      frameLen <in> : 帧数(每帧chn*2字节), 必须为 chn*freq/1000*20ms 的倍数
 */
void vad_process(void *fp, int16_t *frame, int frameLen)
{
    Vad_Struct *vs = fp;
    int cChn, cLen, cReduce, ret;
    int16_t *pFrame;
    int32_t tmp32;
    //多声道数据合并为单声道)
    if(vs->chn == 1)
        pFrame = frame;
    else{
        pFrame = frame;
        for(cLen = 0; cLen < frameLen; cLen++){
            ret = cLen*vs->chn;
            for(tmp32 = cChn = 0; cChn < vs->chn; cChn++)
                tmp32 += frame[ret+cChn];
            tmp32 /= vs->chn;
            pFrame[cLen] = (int16_t)(tmp32/vs->chn);
        }
    }
    //转换数据
    for(cLen = 0; cLen < frameLen;)
    {
        ret = WebRtcVad_Process(vs->handle, vs->freq, pFrame, vs->framePerPkg);
        if(ret < 0){
#ifdef WMIX_WEBRTC_DEBUG
            printf("WebRtcVad_Process failed !!, ret %d \r\n", ret);
#endif
            return;
        }
        else if(ret == 0){
            // reduce 逐渐增大,直至完全消声
            if(vs->reduce < 4)
                vs->reduce += 1;
        }
        else{
            // reduce 逐渐恢复到0,直至最大音量
            if(vs->reduce > 0)
                vs->reduce -= 1;
        }
        // 声音根据 reduce 的大小右移(声音变小)
        for(cReduce = cLen; cReduce < vs->framePerPkg; cReduce++)
            pFrame[cReduce] >>= vs->reduce;
        //
        cLen += vs->framePerPkg;
    }
    //单声道恢复为多声道
    if(vs->chn == 1)
        ;
    else{
        for(cLen = frameLen; cLen > 0; cLen--){
            ret = cLen*vs->chn;
            for(cChn = 0; cChn < vs->chn; cChn++)
                frame[ret-1-cChn] = pFrame[cLen-1];
        }
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
#if((WMIX_WEBRTC_AEC)||(WMIX_WEBRTC_AECM))

#include "echo_cancellation.h"
#include "echo_control_mobile.h"

typedef struct{
    void* aecInst;
    int chn;
    int freq;
    int framePerPkg;//转换时每包帧数,等于chn*freq/1000*20ms
}Aec_Struct;

/*
 *  初始化
 * 
 *  param:
 *      chn <in> : 声道数
 *      freq <in> : 8000, 16000, 32000, 48000
 *  return:
 *      fp指针
 */
void* aec_init(int chn, int freq)
{
    Aec_Struct *as = (Aec_Struct*)calloc(1, sizeof(Aec_Struct));
    AecConfig config = {
        .nlpMode = kAecNlpModerate,
        .skewMode = kAecFalse,
        .metricsMode = kAecFalse,
        .delay_logging = kAecFalse,
    };
    if(WebRtcAec_Create(&as->aecInst) == 0)
    {
        if(WebRtcAec_Init(as->aecInst, freq, freq) == 0)
        {
            if(1)//WebRtcAec_set_config(as->aecInst, config) == 0)
            {
                as->chn = chn;
                as->freq = freq;
                as->framePerPkg = 1*freq/1000*20;
                printf("aec_init: chn/%d freq/%d framePerPkg/%d\r\n", chn, freq, as->framePerPkg);
                return as;
            }
#ifdef WMIX_WEBRTC_DEBUG
            else
                printf("WebRtcAec_set_config failed !!\r\n");
#endif
        }
#ifdef WMIX_WEBRTC_DEBUG
        else
            printf("WebRtcAec_Init failed !!\r\n");
#endif
        WebRtcAec_Free(as->aecInst);
    }
#ifdef WMIX_WEBRTC_DEBUG
    else
        printf("WebRtcAec_Create failed !!\r\n");
#endif
    free(as);
    return NULL;
}

/*
 *  输入远端音频数据(即录音数据)
 * 
 *  param:
 *      frameFar <in> : 远端音频数据(即录音数据)
 *      frameLen <in> : 帧数(每帧chn*2字节), 必须为 chn*freq/1000*20ms 的倍数
 *  return:
 *      0/OK
 *      -1/failed
 */
int aec_setFrameFar(void *fp, int16_t *frameFar, int frameLen)
{
    Aec_Struct *as = fp;
    return WebRtcAec_BufferFarend(as->aecInst, (const float*)frameFar, (int16_t)frameLen);
}

/*
 *  根据 aec_setFrameFar() 的远端数据处理近端数据(即将要播放的音频数据)
 * 
 *  param:
 *      frameNear <in> : 近端数据(即将要播放的音频数据)
 *      frameOut <out> : 处理好的播音数据
 *      frameLen <in> : 帧数(每帧chn*2字节), 必须为 chn*freq/1000*20ms 的倍数
 *      intervalMs <in> : 采样间隔时间ms
 *  return:
 *      0/OK
 *      -1/failed
 */
int aec_process(void *fp, int16_t *frameNear, int16_t *frameOut, int frameLen, int intervalMs)
{
    Aec_Struct *as = fp;
    int count;
    // float *pFloat;
    // for(count = 0, pFloat = (float*)frameNear; count < frameLen; count++)
    //     pFloat[count] = (float)frameNear[count];
    return WebRtcAec_Process(
        as->aecInst,
        (const float* const*)frameNear, as->chn,
        (float* const*)frameOut, (int16_t)frameLen,
        intervalMs, 0);
}
/*
 *  内存回收 
 */
void aec_release(void *fp)
{
    Aec_Struct *as = fp;
    WebRtcAec_Free(as->aecInst);
    free(as);
#ifdef WMIX_WEBRTC_DEBUG
    printf("aec_release\r\n");
#endif
}

#endif
/* ==================== NS 噪音抑制 ==================== */
#if(WMIX_WEBRTC_NS)
#include "noise_suppression.h"
#include "noise_suppression_x.h"

#define NS_AGGRESSIVE 3 // 效果激进程度 0~3

typedef struct{
    NsHandle* NS_inst;
    int chn;
    int freq;
    int framePerPkg;//转换时每包帧数,等于chn*freq/1000*20ms
}Ns_Struct;

/*
 *  初始化
 * 
 *  param:
 *      chn <in> : 声道数
 *      freq <in> : 8000, 16000, 32000, 48000
 *  return:
 *      fp指针
 */
void* ns_init(int chn, int freq)
{
    Ns_Struct *ns = (Ns_Struct*)calloc(1, sizeof(Ns_Struct));
    if(WebRtcNs_Create(&ns->NS_inst) == 0)
    {
        if(WebRtcNs_Init(ns->NS_inst, freq) == 0)
        {
            if(WebRtcNs_set_policy(ns->NS_inst, NS_AGGRESSIVE) == 0)
            {
                ns->chn = chn;
                ns->freq = freq;
                ns->framePerPkg = 1*freq/1000*10;//必须10ms每包
                printf("ns_init: chn/%d freq/%d framePerPkg/%d\r\n", chn, freq, ns->framePerPkg);
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
        WebRtcNs_Free(ns->NS_inst);
    }
#ifdef WMIX_WEBRTC_DEBUG
    else
        printf("WebRtcNs_Create failed !!\r\n");
#endif
    free(ns);
    return NULL;
}

/*
 *  xxxx
 * 
 *  param:
 *      frame <in>: xxx
 */
void ns_setAnalyze(void *fp, int16_t *frame)
{
    Ns_Struct *ns = fp;
    WebRtcNs_Analyze(ns->NS_inst, (const float*)frame);
}

/*
 *  噪音抑制
 * 
 *  param:
 *      frame <in> : 录音数据
 *      frameOut <out> : 录音数据
 *      frameLen <in> : 帧数(每帧chn*2字节), 必须为 chn*freq/1000*20ms 的倍数
 */
void ns_process(void *fp, int16_t *frame, int16_t *frameOut, int frameLen)
{
    Ns_Struct *ns = fp;

    int count;
    // float *pFloat;

    for(count = 0; count < frameLen; count += ns->framePerPkg)
        WebRtcNs_Process(
            ns->NS_inst,
            (const float* const*)&frame[count],
            ns->chn,
            (float* const*)&frameOut[count]);

    // for(count = 0, pFloat = (float*)frame; count < frameLen; count++)
    //     pFloat[count] = (float)frame[count];

    // WebRtcNs_Process(
    //     ns->NS_inst, 
    //     (const float* const*)frame, 
    //     ns->chn, 
    //     (float* const*)frameOut);

    // for(count = 0, pFloat = (float*)frameOut; count < frameLen; count++)
    //     frameOut[count] = (int16_t)pFloat[count];
}

/*
 *  内存回收 
 */
void ns_release(void *fp)
{
    Ns_Struct *ns = fp;
    WebRtcNs_Free(ns->NS_inst);
    free(ns);
#ifdef WMIX_WEBRTC_DEBUG
    printf("ns_release\r\n");
#endif
}

#endif
/* ==================== AGC 自动增益 ==================== */
#if(WMIX_WEBRTC_AGC)
#include "gain_control.h"

/*
 *  初始化
 * 
 *  param:
 *      chn <in> : 声道数
 *      freq <in> : 8000, 16000, 32000, 48000
 *  return:
 *      fp指针
 */
void* agc_init(int chn, int freq)
{
    return 0;
}

/*
 *  自动增益
 * 
 *  param:
 *      frame <in/out> : 录音数据
 *      frameLen <in> : 帧数(每帧chn*2字节), 必须为 chn*freq/1000*20ms 的倍数
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
    NsHandle *NS_inst;
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
    ret = WebRtcNs_Create(&NS_inst);
    printf("WebRtcNs_Create: ret %d\r\n", ret);
    
    // 回收内存
    ret = WebRtcNs_Free(NS_inst);
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
    ret = WebRtcAec_Create(&aecInst);
    printf("WebRtcAec_Create: ret %d\r\n", ret);
    
    // 初始化句柄
    ret = WebRtcAec_Init(aecInst, 8000, 8000);
    printf("WebRtcAec_Init: ret %d\r\n", ret);
    
    // 回收内存
    ret = WebRtcAec_Free(aecInst);
    printf("WebRtcAec_Free: ret %d\r\n", ret);
    
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

