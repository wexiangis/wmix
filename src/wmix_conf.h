/*
 *  混音器关键宏和结构体定义
 */
#ifndef _WMIX_CONF_H_
#define _WMIX_CONF_H_

#include <stdbool.h>
#include "wmix_plat.h"

//不带参数 和 带参数
#define WMIX_ERR(fmt)           fprintf(stderr, "%s(%d): "fmt, __func__, __LINE__)
#define WMIX_ERR2(fmt, argv...) fprintf(stderr, "%s(%d): "fmt, __func__, __LINE__, ##argv)

/* ---------- 需和客户端(程序)同步的信息 ---------- */

#define WMIX_VERSION "V5.5RC1 - 20210317"

#define WMIX_MSG_PATH "/tmp/wmix"
#define WMIX_MSG_PATH_CLEAR "rm -rf /tmp/wmix/*"
#define WMIX_MSG_PATH_AUTHORITY "chmod 777 /tmp/wmix -R"
#define WMIX_MSG_ID 'w'
#define WMIX_MSG_BUFF_SIZE 128

//录音共享内存循环缓冲区大小,必须和 wmix_user.c 中的一致
#define AI_CIRCLE_BUFF_LEN 10240

//客户端 发 服务端 消息类型
typedef enum
{
    WMT_VOLUME = 1,           //设置音量 (value[0]携带0~10)
    WMT_PLYAY_MUTEX = 2,      //互斥播放文件 (value格式见wmix_user.c)
    WMT_PLAY_MIX = 3,         //混音播放文件 (value格式见wmix_user.c)
    WMT_FIFO_PLAY = 4,        //fifo播放wav流 (value格式见wmix_user.c)
    WMT_RESET = 5,            //复位
    WMT_FIFO_RECORD = 6,      //fifo录音wav流 (value格式见wmix_user.c)
    WMT_RECORD_WAV = 7,       //录音wav文件 (value格式见wmix_user.c)
    WMT_CLEAN_LIST = 8,       //清空播放列表
    WMT_PLAY_FIRST = 9,       //排头播放 (value格式见wmix_user.c)
    WMT_PLAY_LAST = 10,       //排尾播放 (value格式见wmix_user.c)
    WMT_RTP_SEND_PCMA = 11,   //rtp send pcma (value格式见wmix_user.c)
    WMT_RTP_RECV_PCMA = 12,   //rtp recv pcma (value格式见wmix_user.c)
    WMT_RECORD_AAC = 13,      //录音aac文件 (value格式见wmix_user.c)
    WMT_MEM_SW = 14,          //开/关 shmem
    WMT_WEBRTC_VAD_SW = 15,   //开/关 webrtc.vad 人声识别,录音辅助,没人说话时主动静音
    WMT_WEBRTC_AEC_SW = 16,   //开/关 webrtc.aec 回声消除
    WMT_WEBRTC_NS_SW = 17,    //开/关 webrtc.ns 噪音抑制(录音)
    WMT_WEBRTC_NS_PA_SW = 18, //开/关 webrtc.ns 噪音抑制(播音)
    WMT_WEBRTC_AGC_SW = 19,   //开/关 webrtc.agc 自动增益
    WMT_RW_TEST = 20,         //自收发测试
    WMT_VOLUME_MIC = 21,      //设置录音音量 (value[0]携带0~10)
    WMT_VOLUME_AGC = 22,      //设置录音音量增益 (value[0]携带0~20)
    WMT_RTP_SEND_AAC = 23,    //rtp send pcma (value格式见wmix_user.c)
    WMT_RTP_RECV_AAC = 24,    //rtp recv pcma (value格式见wmix_user.c)
    WMT_CLEAN_ALL = 25,       //关闭所有播放、录音、fifo、rtp
    WMT_NOTE = 26,            //保存混音数据池的数据流到wav文件,写0关闭
    WMT_FFT = 27,             //输出幅频/相频图像到fb设备或bmp文件,写0关闭

    WMT_LOG_SW = 100,  //开关log
    WMT_INFO = 101,    //打印信息
    WMT_CONSOLE = 102, //重定向打印输出路径
    WMT_TOTAL,
} WMIX_MSG_TYPE;

//客户端(根据id) 发 服务端线程 控制类型
typedef enum
{
    //下列控制状态是互斥的,即设置一个就会清掉别的控制状态
    WCT_CLEAR = 1,   //清控制状态
    WCT_STOP = 2,    //结束线程
    WCT_RESET = 3,   //重置/重连(rtp)
    WCT_SILENCE = 4, //静音,使用0数据运行
    WCT_TOTAL,
} WMIX_CTRL_TYPE;

//消息载体格式
typedef struct
{
    /*
     *  type[0,7]: see WMIX_MSG_TYPE or WMIX_CTRL_TYPE
     *  type[8,15]: reduce
     *  type[16,23]: repeatInterval
     */
    long type;
    /*
     *  使用格式见wmix_user.c
     */
    uint8_t value[WMIX_MSG_BUFF_SIZE];
} WMix_Msg;

/* ---------- 音频参数 ---------- */

//1音量时的保底音量
//配置音量为[1~10]时,实际[WMIX_VOLUME_BASE+1~WMIX_VOLUME_BASE+10],0还是0
#define WMIX_VOLUME_BASE 5

//录播音包间隔ms,必须10的倍数且>=10
#define WMIX_INTERVAL_MS 20

//每帧字节数
#define WMIX_FRAME_SIZE (WMIX_CHANNELS * WMIX_SAMPLE / 8)

//按 WMIX_INTERVAL_MS 采样,每次采样帧数
#define WMIX_FRAME_NUM (WMIX_FREQ * WMIX_INTERVAL_MS / 1000)

//按 WMIX_INTERVAL_MS 采样,每次采样字节数
#define WMIX_PKG_SIZE (WMIX_FRAME_SIZE * WMIX_FRAME_NUM)

//播放循环缓冲区大小 1秒数据量
#define WMIX_BUFF_SIZE (WMIX_FRAME_SIZE * WMIX_FREQ * 1000 / 1000)

/*
 * 保存AEC 左(录音) 右(播音) 声道数据,用于校正AEC时延
 * 
 * 操作方法:
 *      wmixMsg xxx.wav -t 1 -v 8 //循环播放音频
 *      wmixMsg -aec 1            //开启回声消除
 *      wmixMsg -r /tmp/xxx.wav   //开录音5秒,此时不要说话,让设备干净的录到自己播出的声音
 * 
 * 文件aec.pcm怎么用?
 *      看其波形,左声道是录到的声音,右声道是播出的声音,会看到右声道数据会超前左声道,
 *      此时找到左右声道相似的波峰位置,两个位置的时差就是aec回声消除的时差
 */
// #define AEC_SYNC_SAVE_FILE "/tmp/aec.pcm"

//FIFO 循环缓冲区包数量
#define AEC_FIFO_PKG_NUM (AEC_INTERVAL_MS / WMIX_INTERVAL_MS + 2)

//录、播音同步,将把"录音线程"改为"心跳函数"内嵌到"播音线程"中
#define WMIX_RECORD_PLAY_SYNC

/* ---------- 主要结构体 ---------- */

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

//主结构体
typedef struct
{
    //平台指针
    void *objAo, *objAi;

    uint8_t *buff;         //缓冲区
    WMix_Point start, end; //缓冲区头尾指针
    WMix_Point head, tail; //当前缓冲区读写指针

    bool run;               //全局正常运行标志
    uint8_t loopWord;       //全局播放循环标志(每个播放线程的循环标志都要与该值一致,否则循环结束,用于打断全局播放)
    uint8_t loopWordRecord; //全局录音循环标志
    uint8_t loopWordFifo;   //全局fifo循环标志
    uint8_t loopWordRtp;    //全局rtp循环标志
    uint32_t tick;          //播放指针启动至今走过的字节数

    uint32_t thread_sys;    //线程计数 增加线程时+1 减少时-1 等于0时全部退出
    uint32_t thread_record; //线程计数 增加线程时+1 减少时-1 等于0时全部退出
    uint32_t thread_play;   //线程计数 增加线程时+1 减少时-1 等于0时全部退出

    bool playRun;   //指导 play_thread() 运行, thread_play=0 时暂停播放
    bool recordRun; //指导 wmix_shmem_write_circle() 运行, thread_record=0 时暂停播放
    int shmemRun;   //共享内存录音服务标志

    key_t msg_key; //接收来自客户端的消息
    int msg_fd;    //客户端消息句柄

    uint8_t reduceMode;   //背景消减倍数,平时为1(即播放音频数据/1,混音时大小不变)
    bool debug;           //打印log?
    WMix_Queue queue;     //排队头尾标记
    uint32_t onPlayCount; //当前排队总数

    //webrtc modules
    int webrtcEnable[WR_TOTAL];  //webrtc各模块启用标志
    void *webrtcPoint[WR_TOTAL]; //webrtc各模块初始化后的指针管理

    //自收发测试标志
    bool rwTest;

    //终端类型,用于确认是否需要fsync: 0/dev终端 1/文件
    char consoleType;

    //音量: 播放0~10, 录音0~10, agc增益0~100
    int volume, volumeMic, volumeAgc;

#if(WMIX_MATH_FFT)
    //FFT
    char fftPath[WMIX_MSG_BUFF_SIZE];
    float *fftStream;//数据池
    float *fftOutAF;//输出幅-频曲线
    float *fftOutPF;//输出相-频曲线
#endif

    //保存混音数据池的数据流到wav文件
    int noteFd;//写wav文件的描述符
    char notePath[WMIX_MSG_BUFF_SIZE];//首字符是否为0来判断是否在note模式
} WMix_Struct;

//抛线程参数结构
typedef struct
{
    WMix_Struct *wmix;
    long flag;
    uint8_t *param;
} WMixThread_Param;

#endif
