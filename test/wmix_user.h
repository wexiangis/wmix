#ifndef _WMIX_USER_H_
#define _WMIX_USER_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WMIX_VERSION "V3.3 - 20200107"

//----- 设置音量 count/div 例如: 30% -> 30/100 -----
//count: 音量  div: 分度
//正常返回0
int wmix_set_volume(uint8_t count, uint8_t div);

//----- 播放 wav 和 mp3 文件 (互斥播放, wavOrMp3=NULL 时强制关闭播放) -----
//backgroundReduce: 播放当前音频时,降低背景音量
//  0: 不启用
//  >0: 背景音量降低倍数 backgroundVolume/(backgroundReduce+1)
//  (注意: 当有进程正在使用backgroundReduce功能时,当前启用无效(先占先得))
//repeatInterval: 音频重复播放间隔,单位 sec
//  0: 不启用
//  >0: 播放结束后间隔 repeatInterval sec 后重播
//order: 播放顺序(backgroundReduce>0或repeatInterval>0时不参与排队)
//  -1: 打断所有
//  0:混音
//  1:排头
//  2:排尾
//返回: <=0错误, >0 正常返回特定id,可用于"wmix_play_kill(id)"
int wmix_play(char *wavOrMp3, uint8_t backgroundReduce, uint8_t repeatInterval, int order);

//根据 wmix_play() 返回的id关闭启动的音频,id=0时关闭所有
//正常返回0
int wmix_play_kill(int id);

//----- 播放音频流,用于播放录音 -----
//成功返回fd(fifo的写入端)  失败返回0
//backgroundReduce: 播放当前音频时,降低背景音量
//  0: 不启用
//  >0: 背景音量降低倍数 backgroundVolume/(backgroundReduce+1)
//  (注意: 当有进程正在使用backgroundReduce功能时,当前启用无效(先占先得))
//channels: 声道数(取值1,2)
//sample: 采样位数bit(取值16)
//freq: 频率(取值44100,32000,22050,16000,11025,8000)
//正常返回>0的fd
int wmix_stream_open(
    uint8_t channels,
    uint8_t sample,
    uint16_t freq,
    uint8_t backgroundReduce);

//----- 录音 -----
//成功返回fd(fifo的读取端)  失败返回0
//backgroundReduce: 播放当前音频时,降低背景音量
//  0: 不启用
//  >0: 背景音量降低倍数 backgroundVolume/(backgroundReduce+1)
//注意: 当有进程正在使用backgroundReduce功能时,当前启用无效(先占先得)
//channels: 声道数(取值1,2)
//sample: 采样位数bit(取值16)
//freq: 频率(取值44100,32000,22050,16000,11025,8000)
//正常返回>0的fd
int wmix_record_stream_open(
    uint8_t channels,
    uint8_t sample,
    uint16_t freq);

//----- 录音到文件 -----
//channels: 声道数(取值1,2)
//sample: 采样位数bit(取值16)
//freq: 频率(取值44100,32000,22050,16000,11025,8000)
//正常返回0
int wmix_record(
    char *wavPath,
    uint8_t channels,
    uint8_t sample,
    uint16_t freq,
    uint16_t second,
    bool useAAC);

//----- rtp -----

//type: 0/pcma 1/aac(暂不支持)
//chn: pcma只支持1通道
//freq: pcma只支持8000Hz
//返回: >0 正常返回特定id,可用于"wmix_play_kill(id)"
int wmix_rtp_recv(char *ip, int port, int chn, int freq, int type);

//type: 0/pcma 1/aac(暂不支持)
//chn: pcma只支持1通道
//freq: pcma只支持8000Hz
//返回: >0 正常返回特定id,可用于"wmix_play_kill(id)"
int wmix_rtp_send(char *ip, int port, int chn, int freq, int type);

//rtp流控制
//id: 从上面两个函数返回的id值
//ctrl: 0/运行 1/停止 2/重连(启用ip,port参数)
void wmix_rtp_ctrl(int id, int ctrl, char *ip, int port);

//----- 其它 -----
//正常返回0
int wmix_reset(void);

//检查指定id的音频是否存在
bool wmix_check_id(int id);

//从共享内存读取录音数据
//dat: 传入数据保存地址
//len: 按int16_t计算的数据长度
//addr: 返回当前在循环缓冲区中读数据的位置
//wait: 阻塞
//返回: 按int16_t计算的数据长度
int16_t wmix_mem_read(int16_t *dat, int16_t len, int16_t *addr, bool wait);

#ifdef __cplusplus
};
#endif

#endif
