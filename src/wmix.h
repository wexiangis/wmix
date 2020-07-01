/**************************************************
 * 
 *  服务端+客户端(或用wmix_user.h自己开发) 组建的混音器、音频托管工具
 * 
 **************************************************/
#ifndef _WMIX_H_
#define _WMIX_H_

//0/alsa 1/hi3516
#define WMIX_MODE 0

/* ---------- 接收来自Makefile的宏定义 ---------- */

#ifdef MAKE_MP3
#define WMIX_MP3 MAKE_MP3
#else
#define WMIX_MP3 1
#endif

#ifdef MAKE_AAC
#define WMIX_AAC MAKE_AAC
#else
#define WMIX_AAC 1
#endif

/* ---------- rtp ---------- */

//rtp发收同fd
#define RTP_ONE_SR 1

/* ---------- alsa ---------- */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#if (WMIX_MODE == 1)
#include "hiaudio.h"
#else
#include <alsa/asoundlib.h>
typedef struct SNDPCMContainer
{
    snd_pcm_t *handle;
    snd_output_t *log;
    snd_pcm_uframes_t chunk_size;
    snd_pcm_uframes_t buffer_size; //缓冲区大小
    snd_pcm_format_t format;
    uint16_t channels;
    size_t chunk_bytes; //每次读取到缓冲区的字节数
    size_t bits_per_sample;
    size_t bits_per_frame;

    uint8_t *data_buf;
} SNDPCMContainer_t;
#endif

/* ---------- wmix ---------- */

#include <pthread.h>
#include <sys/ipc.h>

#define WMIX_VERSION "V4.1 - 20200630"

#define WMIX_MSG_PATH "/tmp/wmix"
#define WMIX_MSG_PATH_CLEAR "rm -rf /tmp/wmix/*"
#define WMIX_MSG_PATH_AUTHORITY "chmod 777 /tmp/wmix -R"
#define WMIX_MSG_ID 'w'
#define WMIX_MSG_BUFF_SIZE 128

#define WMIX_INTERVAL_MS 20 //录播音包间隔ms, 必须10的倍数且>=10

#if (WMIX_MODE == 0)

#define WMIX_CHANNELS 2
#define WMIX_SAMPLE 16
#define WMIX_FREQ 8000

#else

//hiaudio只支持单声道
#define WMIX_CHANNELS 1
#define WMIX_SAMPLE 16
#define WMIX_FREQ 8000

#endif

#define WMIX_FRAME_SIZE (WMIX_CHANNELS * WMIX_SAMPLE / 8)

#define WMIX_FRAME_NUM (WMIX_FREQ / 1000 * WMIX_INTERVAL_MS)

#define WMIX_PKG_SIZE (WMIX_FRAME_SIZE * WMIX_FRAME_NUM)

typedef enum
{
    WMT_VOLUME = 1,           //设置音量
    WMT_PLYAY_MUTEX = 2,      //互斥播放文件
    WMT_PLAY_MIX = 3,         //混音播放文件
    WMT_FIFO_PLAY = 4,        //fifo播放wav流
    WMT_RESET = 5,            //复位
    WMT_FIFO_RECORD = 6,      //fifo录音wav流
    WMT_RECORD_WAV = 7,       //录音wav文件
    WMT_CLEAN_LIST = 8,       //清空播放列表
    WMT_PLAY_FIRST = 9,       //排头播放
    WMT_PLAY_LAST = 10,       //排尾播放
    WMT_RTP_SEND_PCMA = 11,   //rtp send pcma
    WMT_RTP_RECV_PCMA = 12,   //rtp recv pcma
    WMT_RECORD_AAC = 13,      //录音aac文件
    WMT_MEM_SW = 14,          //开/关 shmem
    WMT_WEBRTC_VAD_SW = 15,   //开/关 webrtc.vad 人声识别,录音辅助,没人说话时主动静音
    WMT_WEBRTC_AEC_SW = 16,   //开/关 webrtc.aec 回声消除
    WMT_WEBRTC_NS_SW = 17,    //开/关 webrtc.ns 噪音抑制(录音)
    WMT_WEBRTC_NS_PA_SW = 18, //开/关 webrtc.ns 噪音抑制(播音)
    WMT_WEBRTC_AGC_SW = 19,   //开/关 webrtc.agc 自动增益
    WMT_RW_TEST = 20,         //自收发测试

    WMT_LOG_SW = 100, //开关log
} WMIX_MSG_TYPE;

typedef struct
{
    /*
     *  type[0,7]: see WMIX_MSG_TYPE
     *  type[8,15]: reduce
     *  type[16,23]: repeatInterval
     */
    long type;
    /*
     *  value(file/fifo): filePath + '\0' + msgPath
     *  value(rtp): chn(1) + bitWidth(1) + freq(2) + port(2) + ip + '\0' + msgPath
     */
    uint8_t value[WMIX_MSG_BUFF_SIZE];
} WMix_Msg;

//循环缓冲区大小
#define WMIX_BUFF_SIZE 262144 //256K //131072//128K //262144//256K 524288//512K //1048576//1M

//多功能指针
typedef union {
    int8_t *S8;
    uint8_t *U8;
    int16_t *S16;
    uint16_t *U16;
    int32_t *S32;
    uint32_t *U32;
} WMix_Point;

//webrtc modules
typedef enum
{
    WR_VAD = 0, //人声识别
    WR_AEC,     //回声消除
    WR_NS,      //噪音抑制(录音)
    WR_NS_PA,   //噪音抑制(播音)
    WR_AGC,     //自动增益

    WR_TOTAL,
} WEBRTC_MODULES;

//先进先出队列
typedef struct
{
    uint16_t head, tail;
} WMix_Queue;

typedef struct
{
#if (WMIX_MODE != 1)
    SNDPCMContainer_t *playback, *recordback;
#endif
    uint8_t *buff;         //缓冲区
    WMix_Point start, end; //缓冲区头尾指针
    WMix_Point head, tail; //当前缓冲区读写指针
    // pthread_mutex_t lock;//互斥锁
    //
    bool run;               //全局正常运行标志
    uint8_t loopWord;       //全局播放循环标志(每个播放线程的循环标志都要与该值一致,否则循环结束,用于打断全局播放)
    uint8_t loopWordRecord; //全局录音循环标志
    uint8_t loopWordFifo;   //全局fifo循环标志
    uint8_t loopWordRtp;    //全局rtp循环标志
    uint32_t tick;          //播放指针启动至今走过的字节数
    //
    uint32_t thread_sys;    //线程计数 增加线程时+1 减少时-1 等于0时全部退出
    uint32_t thread_record; //线程计数 增加线程时+1 减少时-1 等于0时全部退出
    uint32_t thread_play;   //线程计数 增加线程时+1 减少时-1 等于0时全部退出
    //
    bool playRun;   //指导 play_thread() 运行, thread_play=0 时暂停播放
    bool recordRun; //指导 wmix_shmem_write_circle() 运行, thread_record=0 时暂停播放
    int shmemRun;   //共享内存录音服务标志
    //
    key_t msg_key; //接收来自客户端的消息
    int msg_fd;    //客户端消息句柄
    //
    uint8_t reduceMode;   //背景消减模式
    bool debug;           //打印log?
    WMix_Queue queue;     //排队头尾标记
    uint32_t onPlayCount; //当前排队总数

    //webrtc modules
    int webrtcEnable[WR_TOTAL];
    void *webrtcPoint[WR_TOTAL];

    //自收发测试
    bool rwTest;
} WMix_Struct;

/* ---------- 原始的操作方式 ---------- */

//设置音量
int sys_volume_set(uint8_t count, uint8_t div);

//播放
#if (WMIX_MODE == 1)
#define play_wav hiaudio_play_wav
#else
int record_wav(char *filename, uint32_t duration_time, uint8_t chn, uint8_t sample, uint16_t freq);
#endif

/* ---------- 混音器主要操作 ---------- */

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
    uint8_t reduce,
    uint32_t *tick);

//指定wav文件 的方式播放
void wmix_load_wav(
    WMix_Struct *wmix,
    char *wavPath,
    int msg_fd,
    uint8_t reduce,
    uint8_t repeatInterval);

#if (WMIX_AAC)
//指定aac文件 的方式播放
void wmix_load_aac(
    WMix_Struct *wmix,
    char *aacPath,
    int msg_fd,
    uint8_t reduce,
    uint8_t repeatInterval);
#endif

#if (WMIX_MP3)
//指定mp3文件 的方式播放
void wmix_load_mp3(
    WMix_Struct *wmix,
    char *mp3Path,
    int msg_fd,
    uint8_t reduce,
    uint8_t repeatInterval);
#endif

#endif
