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
    if(WebRtcVad_Create(&vs->handle) == 0){
        if(WebRtcVad_Init(vs->handle) == 0){
            if(WebRtcVad_set_mode(vs->handle, VAD_AGGRESSIVE) == 0){
                vs->chn = chn;
                vs->freq = freq;
                vs->framePerPkg = chn*freq/1000*20;
                return vs;
            }
        }
        WebRtcVad_Free(vs->handle);
    }
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
        pFrame = calloc(frameLen, sizeof(int16_t));
        for(cLen = 0; cLen < frameLen; cLen++){
            ret = cLen*vs->chn;
            for(tmp32 = cChn = 0; cChn < vs->chn; cChn++)
                tmp32 += frame[ret+cChn];
            tmp32 /= vs->chn;
            pFrame[cLen] = (int16_t)(tmp32/vs->chn);
        }
    }
    //转换数据
    for(cLen = 0; cLen < frameLen;){
        ret = WebRtcVad_Process(vs->handle, vs->freq, pFrame, vs->framePerPkg);
        if(ret < 0)
            return;
        else if(ret == 0){
            // reduce 逐渐增大,直至完全消声
            if(vs->reduce < 8)
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
        cLen += vs->framePerPkg;
    }
    //单声道恢复为多声道
    if(vs->chn == 1)
        ;
    else{
        for(cLen = 0; cLen < frameLen; cLen++){
            ret = cLen*vs->chn;
            for(cChn = 0; cChn < vs->chn; cChn++)
                frame[ret+cChn] = pFrame[cLen];
        }
        free(pFrame);
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
}

#endif
/* ==================== AEC 回声消除 ==================== */
#if(WMIX_WEBRTC_AEC)
#include "echo_cancellation.h"

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
    return NULL;
}

/*
 *  消除录音回声
 * 
 *  param:
 *      frameIn <in/out> : 录音数据
 *      frameOut <in> : 播音数据
 *      frameLen <in> : 帧数(每帧chn*2字节), 必须为 chn*freq/1000*20ms 的倍数
 *  return:
 *      0/OK
 *      -1/failed
 */
int aec_process(void *fp, int16_t *frameIn, int16_t *frameOut, int frameLen)
{
    return 0;
}
/*
 *  内存回收 
 */
void aec_release(void *fp)
{
    ;
}

#endif
/* ==================== AECM 回声消除移动版 ==================== */
#if(WMIX_WEBRTC_AECM)
#include "echo_control_mobile.h"

/*
 *  初始化
 * 
 *  param:
 *      chn <in> : 声道数
 *      freq <in> : 8000, 16000, 32000, 48000
 *  return:
 *      fp指针
 */
void* aecm_init(int chn, int freq)
{
    return 0;
}

/*
 *  消除录音回声
 * 
 *  param:
 *      frameIn <in/out> : 录音数据
 *      frameOut <in> : 播音数据
 *      frameLen <in> : 帧数(每帧chn*2字节), 必须为 chn*freq/1000*20ms 的倍数
 *  return:
 *      0/OK
 *      -1/failed
 */
int aecm_process(void *fp, int16_t *frameIn, int16_t *frameOut, int frameLen)
{
    return 0;
}

/*
 *  内存回收 
 */
void aecm_release(void *fp)
{
    ;
}

#endif
/* ==================== NS 噪音抑制 ==================== */
#if(WMIX_WEBRTC_NS)
#include "noise_suppression.h"
#include "noise_suppression_x.h"

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
    return 0;
}

/*
 *  噪音抑制
 * 
 *  param:
 *      frame <in/out> : 录音数据
 *      frameLen <in> : 帧数(每帧chn*2字节), 必须为 chn*freq/1000*20ms 的倍数
 */
void ns_process(void *fp, int16_t *frame, int frameLen)
{
    ;
}

/*
 *  内存回收 
 */
void ns_release(void *fp)
{
    ;
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

