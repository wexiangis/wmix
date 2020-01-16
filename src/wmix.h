#ifndef _WMIX_H_
#define _WMIX_H_

//0/alsa 1/hi3516
#define WMIX_MODE 0

//rtp发收同fd
#if(WMIX_MODE!=1)
#define RTP_ONE_SR 0
#else
#define RTP_ONE_SR 1
#endif

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#if(WMIX_MODE==1)
#include "hiaudio.h"
#else
#include <alsa/asoundlib.h>
typedef struct SNDPCMContainer {
    snd_pcm_t *handle;
    snd_output_t *log;
    snd_pcm_uframes_t chunk_size;
    snd_pcm_uframes_t buffer_size;//缓冲区大小
    snd_pcm_format_t format;
    uint16_t channels;
    size_t chunk_bytes;//每次读取到缓冲区的字节数
    size_t bits_per_sample;
    size_t bits_per_frame;

    uint8_t *data_buf;
} SNDPCMContainer_t;
#endif

//-------------------- Wav Mix --------------------------

#include <pthread.h>
#include <sys/ipc.h>

#define WMIX_VERSION "V3.3 - 20200105"

#define WMIX_MSG_PATH "/tmp/wmix"
#define WMIX_MSG_PATH_CLEAR "rm -rf /tmp/wmix/*"
#define WMIX_MSG_PATH_AUTHORITY "chmod 777 /tmp/wmix -R"
#define WMIX_MSG_ID   'w'
#define WMIX_MSG_BUFF_SIZE 128

#define WMIX_CHANNELS    2
#define WMIX_SAMPLE      16
#define WMIX_FREQ        44100

typedef struct{
    //type[0,7]:
    //      1/设置音量
    //      2/互斥播放文件
    //      3/混音播放文件
    //      4/fifo播放wav流
    //      5/复位
    //      6/fifo录音wav流
    //      7/录音wav文件
    //      8/清空播放列表
    //      9/排头播放
    //      10/排尾播放
    //      11/rtp send pcma
    //      12/rtp recv pcma
    //      13/录音aac文件
    //type[8,15]: reduce
    //type[16,23]: repeatInterval
    long type;
    //value(file/fifo): filePath + '\0' + msgPath
    //value(rtp): chn(1) + bitWidth(1) + freq(2) + port(2) + ip + '\0' + msgPath
    uint8_t value[WMIX_MSG_BUFF_SIZE];
}WMix_Msg;

//循环缓冲区大小
#define WMIX_BUFF_SIZE 131072//128K //262144//256K 524288//512K //1048576//1M

//多功能指针
typedef union
{
    int8_t *S8;
    uint8_t *U8;
    int16_t *S16;
    uint16_t *U16;
    int32_t *S32;
    uint32_t *U32;
}WMix_Point;

typedef struct{
    uint16_t head, tail;
}WMix_Queue;

typedef struct{
#if(WMIX_MODE!=1)
    SNDPCMContainer_t *playback;
#endif
    uint8_t *buff;//缓冲区
    WMix_Point start, end;//缓冲区头尾指针
    WMix_Point head, tail, vipWrite;//当前缓冲区读写指针
    // pthread_mutex_t lock;//互斥锁
    //
    uint8_t run;//全局正常运行标志
    uint8_t loopWord;//每个播放线程的循环标志都要与该值一致,否则循环结束,用于打断全局播放
    uint8_t loopWordRecord;
    uint32_t tick;//播放指针启动至今走过的字节数
    //
    uint32_t thread_sys;//线程计数 增加线程时+1 减少时-1 等于0时全部退出
    uint32_t thread_record;//线程计数 增加线程时+1 减少时-1 等于0时全部退出
    uint32_t thread_play;//线程计数 增加线程时+1 减少时-1 等于0时全部退出
    //
    key_t msg_key;
    int msg_fd;
    //
    uint8_t reduceMode;//背景消减模式
    bool debug;//打印log?
    WMix_Queue queue;//排队头尾标记
    uint32_t onPlayCount;//当前排队总数
}WMix_Struct;

//设置音量
int sys_volume_set(uint8_t count, uint8_t div);

//播放
#if(WMIX_MODE==1)
#define play_wav hiaudio_play_wav
#else
int record_wav(char *filename,uint32_t duration_time, uint8_t chn, uint8_t sample, uint16_t freq);
#endif

//-------------------- 混音方式播放 --------------------

//-- 支持混音范围: 44100Hz及以下频率,采样为16bit的音频 --

//初始化
WMix_Struct *wmix_init(void);

//关闭
void wmix_exit(WMix_Struct *wmix);

//载入音频数据 的方式播放
WMix_Point wmix_load_wavStream(
    WMix_Struct *wmix,
    WMix_Point src,
    uint32_t srcU8Len,
    uint16_t freq,
    uint8_t channels,
    uint8_t sample,
    WMix_Point head,
    uint8_t reduce);

//指定wav文件 的方式播放
void wmix_load_wav(
    WMix_Struct *wmix,
    char *wavPath,
    int msg_fd,
    uint8_t reduce,
    uint8_t repeatInterval);

#if(WMIX_MODE==1)
//指定aac文件 的方式播放
void wmix_load_aac(
    WMix_Struct *wmix,
    char *aacPath,
    int msg_fd,
    uint8_t reduce,
    uint8_t repeatInterval);
#endif

//指定mp3文件 的方式播放
void wmix_load_mp3(
    WMix_Struct *wmix,
    char *mp3Path,
    int msg_fd,
    uint8_t reduce,
    uint8_t repeatInterval);

#endif

