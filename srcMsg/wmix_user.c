/**************************************************
 * 
 *  wmix客户端开发API文件
 * 
 **************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>

#include "wmix_user.h"

/* ---------- 需和服务端(程序)同步的信息 ---------- */

//消息地址
#define WMIX_MSG_PATH "/tmp/wmix"
//消息地址标志
#define WMIX_MSG_ID 'w'
//消息体长度
#define WMIX_MSG_BUFF_SIZE 128

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
    WMT_FIFO_AAC = 28,        //fifo录音aac流 (value格式见wmix_user.c)
    WMT_FIFO_G711A = 29,      //fifo录音g711a流 (value格式见wmix_user.c)

    WMT_LOG_SW = 100,  //开关log
    WMT_INFO = 101,    //打印信息
    WMT_CONSOLE = 102, //重定向打印输出路径
    WMT_TOTAL,
} WMIX_MSG_TYPE;

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

#include <sys/shm.h>
//共享内存循环缓冲区长度
#define WMIX_MEM_CIRCLE_BUFF_LEN 10240
//共享内存1x8000录音数据地址标志
#define WMIX_MEM_AI_1X8000_CHAR 'I'
//共享内存原始录音数据地址标志
#define WMIX_MEM_AI_ORIGIN_CHAR 'L'

typedef struct
{
    int16_t w;
    int16_t buff[WMIX_MEM_CIRCLE_BUFF_LEN + 4];
} WMix_MemCircle;

/* ---------- 需和服务端(程序)同步的信息 ---------- */

//时间工具
#include <sys/time.h>
void wmix_delayus(uint32_t us)
{
    struct timeval delay;
    delay.tv_sec = us / 1000000;
    delay.tv_usec = us % 1000000;
    select(0, NULL, NULL, NULL, &delay);
}
uint32_t wmix_tickUs(void)
{
    struct timeval tv = {0};
    gettimeofday(&tv, NULL);
    return (uint32_t)(tv.tv_sec * 1000000u + tv.tv_usec);
}

#define MSG_INIT()                                          \
    key_t msg_key;                                          \
    int msg_fd;                                             \
    if ((msg_key = ftok(WMIX_MSG_PATH, WMIX_MSG_ID)) == -1) \
    {                                                       \
        fprintf(stderr, "wmix: ftok err\n");                \
        return -1;                                          \
    }                                                       \
    if ((msg_fd = msgget(msg_key, 0666)) == -1)             \
    {                                                       \
        fprintf(stderr, "wmix: msgget err\n");              \
        return -1;                                          \
    }

#define MSG_INIT_VOID()                                     \
    key_t msg_key;                                          \
    int msg_fd;                                             \
    if ((msg_key = ftok(WMIX_MSG_PATH, WMIX_MSG_ID)) == -1) \
    {                                                       \
        fprintf(stderr, "wmix: ftok err\n");                \
        return;                                             \
    }                                                       \
    if ((msg_fd = msgget(msg_key, 0666)) == -1)             \
    {                                                       \
        fprintf(stderr, "wmix: msgget err\n");              \
        return;                                             \
    }

void _wmix_set_value(int type, uint8_t value)
{
    WMix_Msg msg;
    //msg初始化
    MSG_INIT_VOID();
    //装填 message
    msg.type = type;
    msg.value[0] = value;
    //发出
    msgsnd(msg_fd, &msg, WMIX_MSG_BUFF_SIZE, IPC_NOWAIT);
}

void wmix_set_volume(uint8_t value)
{
    _wmix_set_value(WMT_VOLUME, value);
}

void wmix_set_volumeMic(uint8_t value)
{
    _wmix_set_value(WMT_VOLUME_MIC, value);
}

void wmix_set_volumeAgc(uint8_t value)
{
    _wmix_set_value(WMT_VOLUME_AGC, value);
}

//自动命名: 主路径WMIX_MSG_PATH + wav + (pid%1000000)*1000+(0~255)
//id<=0时生成id, id>0时按指定id
int wmix_auto_path(char *buff, int id)
{
    static uint8_t _id = 0;
    int ret;
    if (id > 0)
    {
        ret = id;
        sprintf(buff, "%s/%d", WMIX_MSG_PATH, ret);
    }
    else
    {
        ret = (getpid() % 1000000) * 1000 + (_id++);
        sprintf(buff, "%s/%d", WMIX_MSG_PATH, ret);
    }
    buff[strlen(buff)] = 0;
    return ret;
}

int wmix_play(
    char *audioPath,
    uint8_t backgroundReduce,
    uint8_t repeatInterval,
    int order)
{
    WMix_Msg msg;
    char msgPath[128] = {0};
    int redId;

    if (!audioPath)
    {
        if (order < 0)
            wmix_play_kill(0);
        return 0;
    }

    redId = wmix_auto_path(msgPath, 0);

    if (strlen(msgPath) + strlen(audioPath) + 2 > WMIX_MSG_BUFF_SIZE)
    {
        fprintf(stderr, "wmix_play: %s > max len (%ld)\n",
                audioPath, (long)(WMIX_MSG_BUFF_SIZE - strlen(msgPath) - 2));
        return 0;
    }
    //msg初始化
    MSG_INIT();
    //装填 message
    memset(&msg, 0, sizeof(WMix_Msg));
    msg.type = backgroundReduce * 0x100 + repeatInterval * 0x10000;
    if (order < 0)
        msg.type += (int)WMT_PLYAY_MUTEX;
    else if (order == 0)
        msg.type += (int)WMT_PLAY_LAST;
    else if (order == 1)
        msg.type += (int)WMT_PLAY_FIRST;
    else
        msg.type += (int)WMT_PLAY_MIX;
    strcpy((char *)msg.value, audioPath);
    strcpy((char *)&msg.value[strlen(audioPath) + 1], msgPath);
    //发出
    msgsnd(msg_fd, &msg, WMIX_MSG_BUFF_SIZE, IPC_NOWAIT);

    return redId;
}

int wmix_play_kill(int id)
{
    if (id == 0)
    {
        WMix_Msg msg;
        //msg初始化
        MSG_INIT();
        //装填 message
        msg.type = (int)WMT_CLEAN_LIST;
        //发出
        msgsnd(msg_fd, &msg, WMIX_MSG_BUFF_SIZE, IPC_NOWAIT);
    }
    else
    {
        key_t msg_key;
        int msg_fd;
        int timeout;
        char msgPath[128] = {0};
        //关闭旧的播放线程
        wmix_auto_path(msgPath, id);
        if (access((const char *)msgPath, F_OK) == 0)
        {
            if ((msg_key = ftok(msgPath, WMIX_MSG_ID)) == -1)
            {
                fprintf(stderr, "wmix_stream_init: ftok err\n");
                return -1;
            }
            if ((msg_fd = msgget(msg_key, 0666)) == -1)
            {
                // fprintf(stderr, "wmix_stream_init: msgget err\n");
                remove(msgPath);
                return -1;
            }
            //通知关闭
            msgctl(msg_fd, IPC_RMID, NULL);
            //等待关闭
            timeout = 20; //200ms超时
            do
            {
                if (timeout-- == 0)
                    break;
                usleep(10000);
            } while (access((const char *)msgPath, F_OK) == 0);
            //
            remove(msgPath);
        }
    }
    return 0;
}

void wmix_kill_all()
{
    WMix_Msg msg;
    //msg初始化
    MSG_INIT_VOID();
    //装填 message
    msg.type = (int)WMT_CLEAN_ALL;
    //发出
    msgsnd(msg_fd, &msg, WMIX_MSG_BUFF_SIZE, IPC_NOWAIT);
}

void signal_get_SIGPIPE(int id) {}

void *_tmp_callback(void *path)
{
    char buff[64];
    int fd = open((char *)path, O_RDONLY | O_NONBLOCK); //防止下面的写open阻塞
    if (fd > 0)
    {
        while (1)
        {
            if (read(fd, buff, 0) < 0)
            {
                fprintf(stderr, "wmix_user: _tmp_callback read err\n");
                break;
            }
            if (access((const char *)path, F_OK) != 0)
            {
                fprintf(stderr, "wmix_user: _tmp_callback path err\n");
                while (read(fd, buff, 64) >= 0)
                    usleep(1000);
                break;
            }
            sleep(1);
        }
    }
    return NULL;
}

int wmix_fifo_play(
    uint8_t chn,
    uint16_t freq,
    uint8_t backgroundReduce)
{
    if (!freq || !chn)
        return -1;

    int fd = 0;
    int timeout;
    char *path;
    WMix_Msg msg;

    //msg初始化
    MSG_INIT();
    //路径创建
    memset(&msg, 0, sizeof(WMix_Msg));
    path = (char *)&msg.value[4];
    wmix_auto_path(path, 0);
    // remove(path);
    //装填 message
    msg.type = (int)WMT_FIFO_PLAY + backgroundReduce * 0x100;
    msg.value[0] = chn > 1 ? 2 : 1;
    msg.value[1] = 16;
    msg.value[2] = (freq >> 8) & 0xFF;
    msg.value[3] = freq & 0xFF;
    //发出
    msgsnd(msg_fd, &msg, WMIX_MSG_BUFF_SIZE, IPC_NOWAIT);
    //等待路径创建
    timeout = 10; //100ms超时
    do
    {
        if (timeout-- == 0)
            break;
        usleep(10000);
    } while (access((const char *)path, F_OK) != 0);

    if (access((const char *)path, F_OK) != 0)
    {
        fprintf(stderr, "wmix_stream_init: %s timeout\n", path);
        return -1;
    }

#if 1 //用线程代替fork
    pthread_t th;
    pthread_attr_t attr;
    //attr init
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED); //禁用线程同步, 线程运行结束后自动释放
    //抛出线程
    pthread_create(&th, &attr, _tmp_callback, (void *)path);
    //attr destroy
    pthread_attr_destroy(&attr);
#else
    if (fork() == 0)
        open(path, O_RDONLY | O_NONBLOCK); //防止下面的写阻塞打不开
    else
#endif
    fd = open(path, O_WRONLY | O_NONBLOCK);

    signal(SIGPIPE, signal_get_SIGPIPE);
    return fd;
}

int wmix_fifo_record(
    uint8_t chn,
    uint16_t freq,
    int type)
{
    if (!freq || !chn)
        return -1;

    int fd = 0;
    int timeout;
    char *path;
    WMix_Msg msg;

    //msg初始化
    MSG_INIT();
    //路径创建
    memset(&msg, 0, sizeof(WMix_Msg));
    path = (char *)&msg.value[4];
    wmix_auto_path(path, 0);
    // remove(path);
    //装填 message
    if (type == 2)
        msg.type = (int)WMT_FIFO_G711A;
    else if (type == 1)
        msg.type = (int)WMT_FIFO_AAC;
    else
        msg.type = (int)WMT_FIFO_RECORD;
    msg.value[0] = chn > 1 ? 2 : 1;
    msg.value[1] = 16;
    msg.value[2] = (freq >> 8) & 0xFF;
    msg.value[3] = freq & 0xFF;
    //发出
    msgsnd(msg_fd, &msg, WMIX_MSG_BUFF_SIZE, IPC_NOWAIT);
    //等待路径创建
    timeout = 100; //1000ms超时
    do
    {
        if (timeout-- == 0)
            break;
        usleep(10000);
    } while (access((const char *)path, F_OK) != 0);

    if (access((const char *)path, F_OK) != 0)
    {
        fprintf(stderr, "wmix_stream_init: %s timeout\n", path);
        return -1;
    }
    fd = open(path, O_RDONLY);
    return fd;
}

int wmix_record(
    char *wavPath,
    uint8_t chn,
    uint16_t freq,
    uint16_t second,
    int type)
{
    if (!wavPath)
        return -1;
    WMix_Msg msg;
    //msg初始化
    MSG_INIT();
    //装填 message
    memset(&msg, 0, sizeof(WMix_Msg));
    msg.type = type == 1 ? WMT_RECORD_AAC : WMT_RECORD_WAV;
    msg.value[0] = chn > 1 ? 2 : 1;
    msg.value[1] = 16;
    msg.value[2] = (freq >> 8) & 0xFF;
    msg.value[3] = freq & 0xFF;
    msg.value[4] = (second >> 8) & 0xFF;
    msg.value[5] = second & 0xFF;

    if (strlen(wavPath) > WMIX_MSG_BUFF_SIZE - 7)
    {
        fprintf(stderr, "wmix_record: %s > max len (%d)\n", wavPath, WMIX_MSG_BUFF_SIZE - 7);
        return -1;
    }
    strcpy((char *)&msg.value[6], wavPath);
    //发出
    msgsnd(msg_fd, &msg, WMIX_MSG_BUFF_SIZE, IPC_NOWAIT);
    return 0;
}

int wmix_reset(void)
{
    WMix_Msg msg;
    //msg初始化
    MSG_INIT();
    //装填 message
    msg.type = (int)WMT_RESET;
    //发出
    msgsnd(msg_fd, &msg, WMIX_MSG_BUFF_SIZE, IPC_NOWAIT);
    return 0;
}

int _wmix_rtp(char *ip, int port, int chn, int freq, bool isSend, int type, bool bindMode, uint8_t backgroundReduce)
{
    WMix_Msg msg;
    char msgPath[128] = {0};
    int redId;

    if (!ip)
        return -1;

    redId = wmix_auto_path(msgPath, 0);

    if (strlen(msgPath) + strlen(ip) + 6 + 3 > WMIX_MSG_BUFF_SIZE)
    {
        fprintf(stderr, "_wmix_rtp: %s > max len (%ld)\n",
                ip, (long)(WMIX_MSG_BUFF_SIZE - strlen(msgPath) - 6 - 3));
        return -1;
    }
    //msg初始化
    MSG_INIT();
    //装填 message
    memset(&msg, 0, sizeof(WMix_Msg));
    if (type == 1)
        msg.type = isSend ? WMT_RTP_SEND_AAC : WMT_RTP_RECV_AAC;
    else
        msg.type = isSend ? WMT_RTP_SEND_PCMA : WMT_RTP_RECV_PCMA;
    msg.type += backgroundReduce * 0x100;
    msg.value[0] = chn > 1 ? 2 : 1;
    msg.value[1] = 16;
    msg.value[2] = (freq >> 8) & 0xff;
    msg.value[3] = freq & 0xff;
    msg.value[4] = (port >> 8) & 0xff;
    msg.value[5] = port & 0xff;
    msg.value[6] = bindMode ? 1 : 0;
    msg.value[7] = 0; //保留4字节
    msg.value[8] = 0;
    msg.value[9] = 0;
    msg.value[10] = 0;
    strcpy((char *)&msg.value[11], ip);
    strcpy((char *)&msg.value[strlen(ip) + 11 + 1], msgPath);
    //发出
    msgsnd(msg_fd, &msg, WMIX_MSG_BUFF_SIZE, IPC_NOWAIT);
    //等待路径创建
    int timeout = 100; //1000ms超时
    do
    {
        if (timeout-- == 0)
            break;
        usleep(10000);
    } while (access((const char *)msgPath, F_OK) != 0);

    if (access((const char *)msgPath, F_OK) != 0)
    {
        fprintf(stderr, "_wmix_rtp: create %s timeout\n", msgPath);
        return -1;
    }

    return redId;
}

int wmix_rtp_recv(char *ip, int port, int chn, int freq, int type, bool bindMode, uint8_t backgroundReduce)
{
    return _wmix_rtp(ip, port, chn, freq, false, type, bindMode, backgroundReduce);
}

int wmix_rtp_send(char *ip, int port, int chn, int freq, int type, bool bindMode)
{
    return _wmix_rtp(ip, port, chn, freq, true, type, bindMode, 1);
}

bool wmix_check_id(int id)
{
    char msgPath[128] = {0};
    //关闭旧的播放线程
    wmix_auto_path(msgPath, id);
    if (access((const char *)msgPath, F_OK) == 0)
        return true;
    return false;
}

//============= shm =============

static WMix_MemCircle *mem1x8000 = NULL, *memOrigin = NULL;
static int memIsOpen = 0;

int wmix_mem_create(const char *path, int flag, int size, void **mem)
{
    key_t key = ftok(path, flag);
    if (key < 0)
    {
        fprintf(stderr, "get key error\n");
        return -1;
    }
    int id;
    id = shmget(key, size, 0666);
    if (id < 0)
        id = shmget(key, size, IPC_CREAT | 0666);
    if (id < 0)
    {
        fprintf(stderr, "get id error\n");
        return -1;
    }
    if (mem)
        *mem = shmat(id, NULL, 0);

    return id;
}

int wmix_mem_destroy(int id)
{
    return shmctl(id, IPC_RMID, NULL);
}

void wmix_mem_open(void)
{
    if (memIsOpen)
        return;
    _wmix_set_value(WMT_MEM_SW, 1);
    memIsOpen = 1;
}

void wmix_mem_close(void)
{
    if (!memIsOpen)
        return;
    _wmix_set_value(WMT_MEM_SW, 0);
    memIsOpen = 0;
}

//len和返回长度都按int16计算长度
int16_t wmix_mem_1x8000(int16_t *dat, int16_t len, int16_t *addr, bool wait)
{
    int16_t i = 0;
    int16_t w = *addr;
    int timeout = 0;

    if (!mem1x8000)
    {
        wmix_mem_open();
        wmix_mem_create(WMIX_MSG_PATH, WMIX_MEM_AI_1X8000_CHAR, sizeof(WMix_MemCircle), (void **)&mem1x8000);
        if (!mem1x8000)
        {
            fprintf(stderr, "wmix_mem_1x8000: shm_create err !!\n");
            return 0;
        }
        w = mem1x8000->w;
    }

    if (w < 0 || w >= WMIX_MEM_CIRCLE_BUFF_LEN)
        w = mem1x8000->w;
    for (i = 0; i < len;)
    {
        if (w == mem1x8000->w)
        {
            if (wait && mem1x8000)
            {
                timeout += 5;
                if (timeout > 2000) //2秒超时
                {
                    wmix_mem_open();
                    break;
                }
                usleep(5000);
                continue;
            }
            break;
        }
        *dat++ = mem1x8000->buff[w++];
        if (w >= WMIX_MEM_CIRCLE_BUFF_LEN)
            w = 0;
        i += 1;
    }
    *addr = w;
    return i;
}

//原始录音共享内存数据, len和返回长度都按int16计算长度
int16_t wmix_mem_origin(int16_t *dat, int16_t len, int16_t *addr, bool wait)
{
    int16_t i = 0;
    int16_t w = *addr;
    int timeout = 0;

    if (!memOrigin)
    {
        wmix_mem_create(WMIX_MSG_PATH, WMIX_MEM_AI_ORIGIN_CHAR, sizeof(WMix_MemCircle), (void **)&memOrigin);
        if (!memOrigin)
        {
            fprintf(stderr, "wmix_mem_origin: shm_create err !!\n");
            return 0;
        }
        w = memOrigin->w;
    }
    if (w < 0 || w >= WMIX_MEM_CIRCLE_BUFF_LEN)
        w = memOrigin->w;
    for (i = 0; i < len;)
    {
        if (w == memOrigin->w)
        {
            if (wait && memOrigin)
            {
                timeout += 5;
                if (timeout > 2000) //2秒超时
                {
                    wmix_mem_open();
                    break;
                }
                usleep(5000);
                continue;
            }
            break;
        }
        *dat++ = memOrigin->buff[w++];
        if (w >= WMIX_MEM_CIRCLE_BUFF_LEN)
            w = 0;
        i += 1;
    }
    *addr = w;
    return i;
}

//============= shm =============

void wmix_log(bool on)
{
    _wmix_set_value(WMT_LOG_SW, on ? 1 : 0);
}

//============= id path =============

void wmix_get_path(int id, char *path)
{
    wmix_auto_path(path, id);
}

bool wmix_check_path(char *path)
{
    if (access((const char *)path, F_OK) == 0)
        return true;
    else
        return false;
}

//============= webrtc modules =============

void wmix_webrtc_vad(bool on)
{
    _wmix_set_value(WMT_WEBRTC_VAD_SW, on ? 1 : 0);
}
void wmix_webrtc_aec(bool on)
{
    _wmix_set_value(WMT_WEBRTC_AEC_SW, on ? 1 : 0);
}
void wmix_webrtc_ns(bool on)
{
    _wmix_set_value(WMT_WEBRTC_NS_SW, on ? 1 : 0);
}
void wmix_webrtc_ns_pa(bool on)
{
    _wmix_set_value(WMT_WEBRTC_NS_PA_SW, on ? 1 : 0);
}
void wmix_webrtc_agc(bool on)
{
    _wmix_set_value(WMT_WEBRTC_AGC_SW, on ? 1 : 0);
}

void wmix_rw_test(bool on)
{
    _wmix_set_value(WMT_RW_TEST, on ? 1 : 0);
}

void wmix_info(void)
{
    WMix_Msg msg;
    //msg初始化
    MSG_INIT_VOID();
    //装填 message
    msg.type = (int)WMT_INFO;
    //发出
    msgsnd(msg_fd, &msg, WMIX_MSG_BUFF_SIZE, IPC_NOWAIT);
}

void wmix_console(char *path)
{
    WMix_Msg msg;
    //msg初始化
    MSG_INIT_VOID();
    //装填 message
    msg.type = (int)WMT_CONSOLE;
    if (path)
        strcpy((char *)msg.value, path);
    else
        memset(msg.value, 0, sizeof(msg.value));
    //发出
    msgsnd(msg_fd, &msg, WMIX_MSG_BUFF_SIZE, IPC_NOWAIT);
}

int wmix_ctrl(int id, WMIX_CTRL_TYPE ctrl_type)
{
    key_t msg_key;
    int msg_fd;
    char msgPath[128] = {0};
    WMix_Msg msg;
    //检查id线程存在
    wmix_auto_path(msgPath, id);
    if (access((const char *)msgPath, F_OK) != 0)
    {
        fprintf(stderr, "wmix: ctrl thread id %d not exist\n", id);
        return -1;
    }
    //连接msg
    if ((msg_key = ftok(msgPath, WMIX_MSG_ID)) == -1)
    {
        fprintf(stderr, "wmix: ftok err\n");
        return -1;
    }
    if ((msg_fd = msgget(msg_key, 0666)) == -1)
    {
        fprintf(stderr, "wmix: msgget err\n");
        return -1;
    }
    //组装消息
    msg.type = (int)ctrl_type;
    //发出
    msgsnd(msg_fd, &msg, WMIX_MSG_BUFF_SIZE, IPC_NOWAIT);
    return 0;
}

#include <dirent.h>
void wmix_list(void)
{
    FILE *fp;
    char buff[512];
    DIR *dir;
    struct dirent *ptr;
    //打开路径
    if ((dir = opendir(WMIX_MSG_PATH)) == NULL)
        return;
    printf("wmix_list:\r\n");
    //只遍历一级目录
    while ((ptr = readdir(dir)) != NULL)
    {
        //这是文件
        if (ptr->d_type == 8)
        {
            //拼接绝对路径
            memset(buff, 0, sizeof(buff));
            sprintf(buff, "%s/%s", WMIX_MSG_PATH, ptr->d_name);
            //读取文件描述
            fp = fopen(buff, "r");
            if (fp)
            {
                memset(buff, 0, sizeof(buff));
                if (fread(buff, 1, sizeof(buff), fp) > 0)
                    printf("  ID %s : %s \r\n", ptr->d_name, buff);
                fclose(fp);
            }
        }
        else
            continue;
        /*if(strcmp(ptr->d_name,".")==0 || strcmp(ptr->d_name,"..")==0)    ///current dir OR parrent dir
            continue;
        else if(ptr->d_type == 8)    ///file
            printf("d_name:%s/%s\n",basePath,ptr->d_name);
        else if(ptr->d_type == 10)    ///link file
            printf("d_name:%s/%s\n",basePath,ptr->d_name);
        else if(ptr->d_type == 4) { ///dir
            memset(base,'\0',sizeof(base));
            strcpy(base,basePath);
            strcat(base,"/");
            strcat(base,ptr->d_name);
            readFileList(base);
        }*/
    }
    closedir(dir);
}

//保存混音数据池的数据流到wav文件,置NULL关闭
void wmix_note(char *wavPath)
{
    WMix_Msg msg;
    //msg初始化
    MSG_INIT_VOID();
    //装填 message
    msg.type = (int)WMT_NOTE;
    if (wavPath)
        strcpy((char *)msg.value, wavPath);
    else
        memset(msg.value, 0, sizeof(msg.value));
    //发出
    msgsnd(msg_fd, &msg, WMIX_MSG_BUFF_SIZE, IPC_NOWAIT);
}

//输出fft图像
void wmix_fft(char *path)
{
    WMix_Msg msg;
    //msg初始化
    MSG_INIT_VOID();
    //装填 message
    msg.type = (int)WMT_FFT;
    if (path)
        strcpy((char *)msg.value, path);
    else
        memset(msg.value, 0, sizeof(msg.value));
    //发出
    msgsnd(msg_fd, &msg, WMIX_MSG_BUFF_SIZE, IPC_NOWAIT);
}
