
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
