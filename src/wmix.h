
#ifndef _WMIX_H_
#define _WMIX_H_

#include <stdarg.h>
#include "wmixConf.h"

//wav编码
#include "wav.h"
//g711和pcm互转编码
#include "g711codec.h"
//rtp协议和数据发收
#include "rtp.h"
//webrtc接口二次封装
#include "webrtc.h"
//speex接口二次封装
#include "speexType.h"
//mp3编码
#include "mp3Type.h"
//aac编码
#include "aacType.h"

//不带参数 和 带参数
#define WMIX_ERR(fmt)           fprintf(stderr, "%s(%d): "fmt, __func__, __LINE__)
#define WMIX_ERR2(fmt, argv...) fprintf(stderr, "%s(%d): "fmt, __func__, __LINE__, ##argv)

//第三方程序调用入口
void wmix_start(void);
//初始化
WMix_Struct *wmix_init(void);
void wmix_exit(WMix_Struct *wmix);

// 信号事件接收,主要是识别ctrl+c结束时完成收尾工作
void wmix_signal(int signo);

//快速写文件
void wmix_write_file(char *file, const char* format, ...);

//往混音器载入音频数据
WMix_Point wmix_load_data(
    WMix_Struct *wmix,
    WMix_Point src,
    uint32_t srcU8Len,
    uint16_t freq,
    uint8_t channels,
    uint8_t sample,
    WMix_Point head,
    uint8_t reduce,
    uint32_t *tick);

//抛线程工具 或 用于加载 wmix_thread_xxx
void wmix_load_thread(
    WMix_Struct *wmix,
    long flag,
    uint8_t *param,
    size_t paramLen,
    void *callback);

//用于加载 wmix_task_xxx
void wmix_load_task(WMixThread_Param *wmtp);

//用 wmix_load_thread() 加载的功能模块
void wmix_thread_fifo_pcm_play(WMixThread_Param *wmtp);
void wmix_thread_fifo_pcm_record(WMixThread_Param *wmtp);
void wmix_thread_fifo_g711a_record(WMixThread_Param *wmtp);
void wmix_thread_fifo_aac_record(WMixThread_Param *wmtp);
void wmix_thread_record_wav(WMixThread_Param *wmtp);
#if (MAKE_AAC)
void wmix_thread_record_aac(WMixThread_Param *wmtp);
void wmix_thread_rtp_send_aac(WMixThread_Param *wmtp);
void wmix_thread_rtp_recv_aac(WMixThread_Param *wmtp);
#endif
void wmix_thread_rtp_send_pcma(WMixThread_Param *wmtp);
void wmix_thread_rtp_recv_pcma(WMixThread_Param *wmtp);

//用 wmix_load_task() 加载的功能模块
void wmix_task_play_wav(
    WMix_Struct *wmix,
    char *wavPath,
    int msg_fd,
    uint8_t reduce,
    uint8_t interval,
    uint8_t repeat);
#if (MAKE_AAC)
void wmix_task_play_aac(
    WMix_Struct *wmix,
    char *aacPath,
    int msg_fd,
    uint8_t reduce,
    uint8_t interval,
    uint8_t repeat);
#endif
#if (MAKE_MP3)
void wmix_task_play_mp3(
    WMix_Struct *wmix,
    char *mp3Path,
    int msg_fd,
    uint8_t reduce,
    uint8_t interval,
    uint8_t repeat);
#endif

//单通道8000Hz共享内存数据,len和返回长度都按int16计算长度
int16_t wmix_mem_read_1x8000(int16_t *dat, int16_t len, int16_t *addr, bool wait);
//原始录音共享内存数据,len和返回长度都按int16计算长度
int16_t wmix_mem_read_origin(int16_t *dat, int16_t len, int16_t *addr, bool wait);
//单通道8000Hz共享内存数据,len和返回长度都按int16计算长度
int16_t wmix_mem_write_1x8000(int16_t *dat, int16_t len);
//原始录音共享内存数据,len和返回长度都按int16计算长度
int16_t wmix_mem_write_origin(int16_t *dat, int16_t len);

//知道输入长度,计算缩放后输出长度(注意长度必须2倍数)
uint32_t wmix_len_of_out(
    uint8_t inChn, uint16_t inFreq,
    uint32_t inLen,
    uint8_t outChn, uint16_t outFreq);
//知道输出长度,计算缩需要的输入长度(注意长度必须2倍数)
uint32_t wmix_len_of_in(
    uint8_t inChn, uint16_t inFreq,
    uint8_t outChn, uint16_t outFreq,
    uint32_t outLen);
//缩放,返回输出长度(注意长度必须2倍数)
uint32_t wmix_pcm_zoom(
    uint8_t inChn, uint16_t inFreq,
    uint8_t *in, uint32_t inLen,
    uint8_t outChn, uint16_t outFreq,
    uint8_t *out);

#endif //end of file
