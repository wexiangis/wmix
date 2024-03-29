/**************************************************
 * 
 *  wmix客户端开发API文件
 * 
 **************************************************/
#ifndef _WMIX_USER_H_
#define _WMIX_USER_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

/* ---------- 需和服务端(程序)保持同步的信息 ---------- */

#define WMIX_VERSION "V6.0 - 20210427"

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

/* ---------- 需和服务端(程序)保持同步的信息 ---------- */

//时间工具
void wmix_delayus(uint32_t us);
uint32_t wmix_tickUs(void);

/* 
 *  设置音量
 *  value: 音量 0~10
 *  正常返回0
 */
void wmix_set_volume(uint8_t value);

/*
 *  设置录音音量
 *  value: 音量 0~10
 */
void wmix_set_volumeMic(uint8_t value);

/*
 *  设置录音音量增益
 *  value: 音量 0~100
 */
void wmix_set_volumeAgc(uint8_t value);

/*
 *  播放 wav、mp3(需启用MAKE_MP3)、aac(需启用MAKE_AAC) 文件
 *  参数:
 *    audioFile: 音频文件,支持格式: wav, aac, mp3
 *    reduce:
 *          播放当前音频时,降低背景音量,取值[1~15]
 *          0: 不启用  >0: 背景音量降低倍数 backgroundVolume/(reduce+1)
 *          (注意: 当有进程正在使用reduce功能时,当前启用无效(先占先得))
 *    interval:
 *          音频重复播放间隔,单位 sec,取值[1~255]
 *          0: 不启用  >0: 播放结束后间隔 interval sec 后重播
 *    repeat:
 *          播放次数,取值[1~127]
 *          0： 不起用  >0: 重复播放次数(interval没有设置时默认1秒)
 *    order:
 *          播放顺序(reduce>0或interval>0时不参与排队)
 *          -1: 打断所有 0:排尾 1:排头 2:混音
 * 
 *  返回: <=0错误, >0 正常返回特定id,可用于"wmix_play_kill(id)"
 */
int wmix_play(
    char *audioFile,
    uint8_t reduce,
    uint8_t interval,
    uint8_t repeat,
    int order);

// 根据 wmix_play() 返回的id关闭启动的音频,id=0时关闭所有, 正常返回0
int wmix_play_kill(int id);

// 关闭所有播放、录音、fifo、rtp
void wmix_kill_all();

/*
 *  音频流播放
 *  参数:
 *      reduce: 同上面
 *      chn: 声道数(取值1,2)
 *      freq: 频率(取值44100,32000,22050,16000,11025,8000)
 *  返回: 成功返回>0的fd(fifo的写入端)  失败返回-1
 */
int wmix_fifo_play(
    uint8_t chn,
    uint16_t freq,
    uint8_t reduce);

/*
 *  音频流录音
 *  参数:
 *      chn: 声道数(取值1,2)
 *      freq: 频率不得大于wmix原声频率
 *      type: 录音格式: 0/pcm 1/aac(需启用MAKE_AAC) 2/g711a
 *  返回: 成功返回>0的fd(fifo的写入端)  失败返回-1
 *  说明: 对于aac格式,仅支持双声道和16000、22050、32000、44100Hz配置
 */
int wmix_fifo_record(
    uint8_t chn,
    uint16_t freq,
    int type);

/*
 *  录音到文件
 *  参数:
 *      chn: 声道数(取值1,2)
 *      freq: 频率不得大于wmix原声频率
 *      second: 录音时长秒
 *      type: 录音格式: 0/wav 1/aac(需启用MAKE_AAC)
 *  返回: 正常0
 *  说明: 对于aac格式,仅支持双声道和16000、22050、32000、44100Hz配置
 */
int wmix_record(
    char *wavPath,
    uint8_t chn,
    uint16_t freq,
    uint16_t second,
    int type);

/*
 *  创建rtp接收任务
 *  参数:
 *      type: 0/pcma 1/aac
 *      chn: pcma只支持1通道
 *      freq: pcma只支持8000Hz
 *      bindMode: 以服务器形式连接(bind),这个设置很重要
 *      reduce: 同上面
 *  返回: 正常返回>0特定id,可用于"wmix_play_kill(id)"  失败返回-1
 */
int wmix_rtp_recv(
    char *ip,
    int port,
    int chn,
    int freq,
    int type,
    bool bindMode,
    uint8_t reduce);

/*
 *  创建rtp发送任务
 *  参数:
 *      chn: pcma只支持1通道
 *      freq: pcma只支持8000Hz
 *      type: 0/pcma 1/aac
 *      bindMode: 以服务器形式连接(bind),这个设置很重要
 *  返回: 正常返回>0特定id,可用于"wmix_play_kill(id)"  失败返回-1
 *  说明: 对于aac格式,仅支持双声道和16000、22050、32000、44100Hz配置
 */
int wmix_rtp_send(
    char *ip,
    int port,
    int chn,
    int freq,
    int type,
    bool bindMode);

// 重置wmix, 正常返回0
int wmix_reset(void);

// 检查指定id的音频是否存在
bool wmix_check_id(int id);

// 开关log
void wmix_log(bool on);

/*
 *  从共享内存读取录音数据
 *  参数:
 *      dat: 传入数据保存地址
 *      len: 按int16_t计算的数据长度
 *      addr: 返回当前在循环缓冲区中读数据的位置
 *      wait: 是否阻塞
 *  返回: 按int16_t计算的数据长度
 *  说明:
 *      wmix_mem_1x8000 读取8000Hz单声道数据
 *      wmix_mem_origin 读取原始数据
 */
int16_t wmix_mem_1x8000(int16_t *dat, int16_t len, int16_t *addr, bool wait);
int16_t wmix_mem_origin(int16_t *dat, int16_t len, int16_t *addr, bool wait);
void wmix_mem_open(void);
void wmix_mem_close(void);

// 检查wmix路径和获取id
void wmix_get_path(int id, char *path);
bool wmix_check_path(char *path);

// webrtc 模块开关
void wmix_webrtc_vad(bool on);    // 人声是别(录音辅助,在没说话时主动静音)
void wmix_webrtc_aec(bool on);    // 回声消除
void wmix_webrtc_ns(bool on);     // 噪音抑制(录音)
void wmix_webrtc_ns_pa(bool on);  // 噪音抑制(播音)
void wmix_webrtc_agc(bool on);    // 自动增益

// 自收发测试
void wmix_rw_test(bool on);

// 打印信息
void wmix_info(void);

// 重定向打印信息输出路径,path可以为终端路径,也可以为文件路径
void wmix_console(char *path);

/*
 *  给目标id的线程发控制状态
 *  参数:
 *      id: 上面产生的id
 *      ctrl_type: 控制类型,参考 WMIX_CTRL_TYPE
 *  返回: 0/正常 -1/发送失败,id线程不存在
 */
int wmix_ctrl(int id, WMIX_CTRL_TYPE ctrl_type);

//打印所有任务信息
void wmix_list(void);

//保存混音数据池的数据流到wav文件,置NULL关闭
void wmix_note(char *wavPath);

//输出fft图像,置NULL关闭(暂未启用)
void wmix_fft(char *path);

#ifdef __cplusplus
};
#endif

#endif
