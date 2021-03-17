
#ifndef _WMIX_H_
#define _WMIX_H_

#include "wmix_conf.h"

//第三方程序调用入口
void wmix_start(void);
//初始化
WMix_Struct *wmix_init(void);
void wmix_exit(WMix_Struct *wmix);

// 信号事件接收,主要是识别ctrl+c结束时完成收尾工作
void wmix_signal(int signo);

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

//抛线程工具
void wmix_load_thread(
    WMix_Struct *wmix,
    long flag,
    uint8_t *param,
    size_t paramLen,
    void *callback);

//任务线程加载函数
void wmix_load_task(
    WMixThread_Param *wmtp);

//可以用 wmix_load_thread() 加载的线程
void wmix_thread_play_wav_fifo(WMixThread_Param *wmtp);
void wmix_thread_record_wav_fifo(WMixThread_Param *wmtp);
void wmix_thread_record_wav(WMixThread_Param *wmtp);
#if (WMIX_AAC)
void wmix_thread_record_aac(WMixThread_Param *wmtp);
void wmix_thread_rtp_send_aac(WMixThread_Param *wmtp);
void wmix_thread_rtp_recv_aac(WMixThread_Param *wmtp);
#endif
void wmix_thread_rtp_send_pcma(WMixThread_Param *wmtp);
void wmix_thread_rtp_recv_pcma(WMixThread_Param *wmtp);

//可以用 wmix_load_task() 加载的线程
void wmix_task_play_wav(
    WMix_Struct *wmix,
    char *wavPath,
    int msg_fd,
    uint8_t reduce,
    uint8_t repeatInterval);
#if (WMIX_AAC)
void wmix_task_play_aac(
    WMix_Struct *wmix,
    char *aacPath,
    int msg_fd,
    uint8_t reduce,
    uint8_t repeatInterval);
#endif
#if (WMIX_MP3)
void wmix_task_play_mp3(
    WMix_Struct *wmix,
    char *mp3Path,
    int msg_fd,
    uint8_t reduce,
    uint8_t repeatInterval);
#endif

/*
 *  共享内存使用说明：
 *  "/tmp/wmix", 'O'： 单通道8000Hz客户端写入音频数据，这里读并播放
 *  "/tmp/wmix", 'I'： 单通道8000Hz这里录音并写入，客户端读取录音数据
 *  "/tmp/wmix", 'L'： 原始录音数据写入，客户端读取或混音器自己的录音线程取用
 */
//单通道8000Hz共享内存数据, len和返回长度都按int16计算长度
int16_t wmix_mem_read(int16_t *dat, int16_t len, int16_t *addr, bool wait);
//原始录音共享内存数据, len和返回长度都按int16计算长度
int16_t wmix_mem_read2(int16_t *dat, int16_t len, int16_t *addr, bool wait);
//单通道8000Hz共享内存数据, len和返回长度都按int16计算长度
int16_t wmix_mem_write(int16_t *dat, int16_t len);
//原始录音共享内存数据, len和返回长度都按int16计算长度
int16_t wmix_mem_write2(int16_t *dat, int16_t len);

/*
 *  把长度为ret的buff根据divPow放大/缩小为buffSize2长度的数据,
 *  数据所在地址仍为buff,其中chn为输出通道数,WMIX_CHANNELS为源通道数
 */
#if (WMIX_CHANNELS == 1)
#define RECORD_DATA_TRANSFER()                               \
    if (chn == 1)                                            \
    {                                                        \
        for (count = 0, src.U8 = dist.U8 = buff;             \
             count < ret; count += frame_size)               \
        {                                                    \
            if (divCount >= 1.0)                             \
            {                                                \
                src.U16++;                                   \
                divCount -= 1.0;                             \
            }                                                \
            else                                             \
            {                                                \
                *dist.U16++ = *src.U16++;                    \
                divCount += divPow;                          \
            }                                                \
        }                                                    \
        src.U8 = buff;                                       \
        buffSize2 = (size_t)(dist.U16 - src.U16) * 2;        \
    }                                                        \
    else                                                     \
    {                                                        \
        memcpy(&buff[ret], buff, ret);                       \
        for (count = 0, src.U8 = &buff[ret], dist.U8 = buff; \
             count < ret; count += frame_size)               \
        {                                                    \
            if (divCount >= 1.0)                             \
            {                                                \
                src.U16++;                                   \
                divCount -= 1.0;                             \
            }                                                \
            else                                             \
            {                                                \
                *dist.U16++ = *src.U16;                      \
                *dist.U16++ = *src.U16++;                    \
                divCount += divPow;                          \
            }                                                \
        }                                                    \
        src.U8 = buff;                                       \
        buffSize2 = (size_t)(dist.U16 - src.U16) * 2;        \
    }
#else
#define RECORD_DATA_TRANSFER()                        \
    if (chn == 1)                                     \
    {                                                 \
        for (count = 0, src.U8 = dist.U8 = buff;      \
             count < ret; count += frame_size)        \
        {                                             \
            if (divCount >= 1.0)                      \
            {                                         \
                src.U16++;                            \
                src.U16++;                            \
                divCount -= 1.0;                      \
            }                                         \
            else                                      \
            {                                         \
                *dist.U16++ = *src.U16++;             \
                src.U16++;                            \
                divCount += divPow;                   \
            }                                         \
        }                                             \
        src.U8 = buff;                                \
        buffSize2 = (size_t)(dist.U16 - src.U16) * 2; \
    }                                                 \
    else                                              \
    {                                                 \
        for (count = 0, src.U8 = dist.U8 = buff;      \
             count < ret; count += frame_size)        \
        {                                             \
            if (divCount >= 1.0)                      \
            {                                         \
                src.U32++;                            \
                divCount -= 1.0;                      \
            }                                         \
            else                                      \
            {                                         \
                *dist.U32++ = *src.U32++;             \
                divCount += divPow;                   \
            }                                         \
        }                                             \
        src.U8 = buff;                                \
        buffSize2 = (size_t)(dist.U32 - src.U32) * 4; \
    }
#endif

#endif //end of file
