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

#define WMIX_MSG_PATH "/tmp/wmix"
#define WMIX_MSG_ID   'w'
#define WMIX_MSG_BUFF_SIZE 128

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
    //      11/rtp send
    //      12/rtp recv
    //      13/录音aac文件
    //      14/fifo录音aac流
    //      15/fifo播放aac流
    //type[8,15]: reduce
    //type[16,23]: repeatInterval
    long type;
    //value: filePath + '\0' + msgPath
    //value(rtp): chn(1) + bitWidth(1) + freq(2) + port(2) + ip + '\0' + msgPath
    uint8_t value[WMIX_MSG_BUFF_SIZE];
}WMix_Msg;

#define MSG_INIT() \
key_t msg_key;\
int msg_fd;\
if((msg_key = ftok(WMIX_MSG_PATH, WMIX_MSG_ID)) == -1){\
    fprintf(stderr, "wmix: ftok err\n");\
    return -1;\
}if((msg_fd = msgget(msg_key, 0666)) == -1){\
    fprintf(stderr, "wmix: msgget err\n");\
    return -1;\
}

int wmix_set_volume(uint8_t count, uint8_t div)
{
    WMix_Msg msg;
    //msg初始化
    MSG_INIT();
    //装填 message
    memset(&msg, 0, sizeof(WMix_Msg));
    msg.type = 1;
    if(count > div)
        msg.value[0] = div;
    else
        msg.value[0] = count;
    msg.value[1] = div;
    //发出
    msgsnd(msg_fd, &msg, WMIX_MSG_BUFF_SIZE, IPC_NOWAIT);
    return 0;
}

//自动命名: 主路径WMIX_MSG_PATH + wav + (pid%1000000)*1000+(0~255)
//id<=0时生成id, id>0时按指定id
int wmix_auto_path(char *buff, int id)
{
    static uint8_t _id = 0;
    int ret;
    if(id > 0)
    {
        ret = id;
        sprintf(buff, "%s/%d", WMIX_MSG_PATH, ret);
    }
    else
    {
        ret = (getpid()%1000000)*1000+(_id++);
        sprintf(buff, "%s/%d", WMIX_MSG_PATH, ret);
    }
    buff[strlen(buff)] = 0;
    return ret;
}

int wmix_play(char *wavOrMp3, uint8_t backgroundReduce, uint8_t repeatInterval, int order)
{
    WMix_Msg msg;
    char msgPath[128] = {0};
    int redId;
    //
    if(!wavOrMp3)
    {
        if(order < 0)
            wmix_play_kill(0);
        return 0;
    }
    //
    redId = wmix_auto_path(msgPath, 0);
    //
    if(strlen(msgPath) + strlen(wavOrMp3) + 2 > WMIX_MSG_BUFF_SIZE){
        fprintf(stderr, "wmix_play_wav: %s > max len (%ld)\n", 
            wavOrMp3, (long)(WMIX_MSG_BUFF_SIZE-strlen(msgPath)-2));
        return 0;
    }
    //msg初始化
    MSG_INIT();
    //装填 message
    memset(&msg, 0, sizeof(WMix_Msg));
    msg.type = backgroundReduce*0x100 + repeatInterval*0x10000;
    if(order < 0)
        msg.type += 2;
    else if(order == 0)
        msg.type += 3;
    else if(order == 1)
        msg.type += 9;
    else
        msg.type += 10;
    strcpy((char*)msg.value, wavOrMp3);
    strcpy((char*)&msg.value[strlen(wavOrMp3)+1], msgPath);
    //发出
    msgsnd(msg_fd, &msg, WMIX_MSG_BUFF_SIZE, IPC_NOWAIT);
    //
    return redId;
}

int wmix_play_kill(int id)
{
    if(id == 0)
    {
        WMix_Msg msg;
        //msg初始化
        MSG_INIT();
        //装填 message
        msg.type = 8;
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
        if(access(msgPath, F_OK) == 0)
        {
            if((msg_key = ftok(msgPath, WMIX_MSG_ID)) == -1){
                fprintf(stderr, "wmix_stream_init: ftok err\n");
                return -1;
            }
            if((msg_fd = msgget(msg_key, 0666)) == -1){
                // fprintf(stderr, "wmix_stream_init: msgget err\n");
                remove(msgPath);
                return -1;
            }
            //通知关闭
            msgctl(msg_fd, IPC_RMID, NULL);
            //等待关闭
            timeout = 20;//200ms超时
            do{
                if(timeout-- == 0)
                    break;
                usleep(10000);
            }while(access(msgPath, F_OK) == 0);
            //
            remove(msgPath);
        }
    }
    return 0;
}

void signal_get_SIGPIPE(int id){}

void *_tmp_callback(void *path)
{
    open((char*)path, O_RDONLY | O_NONBLOCK);//防止下面的写open阻塞
    return NULL;
}

int wmix_stream_open(
    uint8_t channels,
    uint8_t sample,
    uint16_t freq,
    uint8_t backgroundReduce)
{
    if(!freq || !channels || !sample)
        return 0;
    //
    int fd = 0;
    int timeout;
    char *path;
    WMix_Msg msg;
    //msg初始化
    MSG_INIT();
    //路径创建
    memset(&msg, 0, sizeof(WMix_Msg));
    path = (char*)&msg.value[4];
    wmix_auto_path(path, 0);
    // remove(path);
    //装填 message
    msg.type = 4 + backgroundReduce*0x100;
    msg.value[0] = channels;
    msg.value[1] = sample;
    msg.value[2] = (freq>>8)&0xFF;
    msg.value[3] = freq&0xFF;
    //发出
    msgsnd(msg_fd, &msg, WMIX_MSG_BUFF_SIZE, IPC_NOWAIT);
    //等待路径创建
    timeout = 10;//100ms超时
    do{
        if(timeout-- == 0)
            break;
        usleep(10000);
    }while(access(path, F_OK) != 0);
    //
    if(access(path, F_OK) != 0){
        fprintf(stderr, "wmix_stream_init: %s timeout\n", path);
        return 0;
    }
    //
#if 1//用线程代替fork
    pthread_t th;
    pthread_attr_t attr;
    //attr init
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);//禁用线程同步, 线程运行结束后自动释放
    //抛出线程
    pthread_create(&th, &attr, _tmp_callback, (void*)path);
    //attr destroy
    pthread_attr_destroy(&attr);
#else
    if(fork() == 0)
        open(path, O_RDONLY | O_NONBLOCK);//防止下面的写阻塞打不开
    else
#endif
    fd = open(path, O_WRONLY);
    //
    signal(SIGPIPE, signal_get_SIGPIPE);
    //
    return fd;
}

int wmix_record_stream_open(
    uint8_t channels,
    uint8_t sample,
    uint16_t freq)
{
    if(!freq || !channels || !sample)
        return 0;
    //
    int fd = 0;
    int timeout;
    char *path;
    WMix_Msg msg;
    //msg初始化
    MSG_INIT();
    //路径创建
    memset(&msg, 0, sizeof(WMix_Msg));
    path = (char*)&msg.value[4];
    wmix_auto_path(path, 0);
    // remove(path);
    //装填 message
    msg.type = 6;
    msg.value[0] = channels;
    msg.value[1] = sample;
    msg.value[2] = (freq>>8)&0xFF;
    msg.value[3] = freq&0xFF;
    //发出
    msgsnd(msg_fd, &msg, WMIX_MSG_BUFF_SIZE, IPC_NOWAIT);
    //等待路径创建
    timeout = 100;//1000ms超时
    do{
        if(timeout-- == 0)
            break;
        usleep(10000);
    }while(access(path, F_OK) != 0);
    //
    if(access(path, F_OK) != 0){
        fprintf(stderr, "wmix_stream_init: %s timeout\n", path);
        return 0;
    }
    //
    fd = open(path, O_RDONLY);
    //
    return fd;
}

int wmix_record(
    char *wavPath,
    uint8_t channels,
    uint8_t sample,
    uint16_t freq,
    uint16_t second,
    bool useAAC)
{
    if(!wavPath)
        return;
    WMix_Msg msg;
    //msg初始化
    MSG_INIT();
    //装填 message
    memset(&msg, 0, sizeof(WMix_Msg));
    msg.type = useAAC?13:7;
    msg.value[0] = channels;
    msg.value[1] = sample;
    msg.value[2] = (freq>>8)&0xFF;
    msg.value[3] = freq&0xFF;
    msg.value[4] = (second>>8)&0xFF;
    msg.value[5] = second&0xFF;
    //
    if(strlen(wavPath) > WMIX_MSG_BUFF_SIZE - 7){
        fprintf(stderr, "wmix_play_wav: %s > max len (%d)\n", wavPath, WMIX_MSG_BUFF_SIZE - 7);
        return ;
    }
    strcpy((char*)&msg.value[6], wavPath);
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
    msg.type = 5;
    //发出
    msgsnd(msg_fd, &msg, WMIX_MSG_BUFF_SIZE, IPC_NOWAIT);
    return 0;
}

int _wmix_rtp(char *ip, int port, int chn, int freq, bool isSend, int type)
{
    WMix_Msg msg;
    char msgPath[128] = {0};
    int redId;
    //
    if(!ip)
        return 0;
    //
    redId = wmix_auto_path(msgPath, 0);
    //
    if(strlen(msgPath) + strlen(ip) + 6 + 3 > WMIX_MSG_BUFF_SIZE){
        fprintf(stderr, "wmix_play_wav: %s > max len (%ld)\n", 
            ip, (long)(WMIX_MSG_BUFF_SIZE-strlen(msgPath)-6-3));
        return 0;
    }
    //msg初始化
    MSG_INIT();
    //装填 message
    memset(&msg, 0, sizeof(WMix_Msg));
    msg.type = isSend?11:12;
    msg.value[0] = chn;
    msg.value[1] = 16;
    msg.value[2] = (freq>>8)&0xff;
    msg.value[3] = freq&0xff;
    msg.value[4] = (port>>8)&0xff;
    msg.value[5] = port&0xff;
    strcpy((char*)&msg.value[6], ip);
    strcpy((char*)&msg.value[strlen(ip)+6+1], msgPath);
    //发出
    msgsnd(msg_fd, &msg, WMIX_MSG_BUFF_SIZE, IPC_NOWAIT);
    //等待路径创建
    int timeout = 100;//1000ms超时
    do{
        if(timeout-- == 0)
            break;
        usleep(10000);
    }while(access(msgPath, F_OK) != 0);
    //
    if(access(msgPath, F_OK) != 0){
        fprintf(stderr, "_wmix_rtp: create %s timeout\n", msgPath);
        return 0;
    }
    //
    return redId;
}

int wmix_rtp_recv(char *ip, int port, int chn, int freq, int type)
{
    return _wmix_rtp(ip, port, chn, freq, false, type);
}

int wmix_rtp_send(char *ip, int port, int chn, int freq, int type)
{
    return _wmix_rtp(ip, port, chn, freq, true, type);
}

//rtp流控制
//id: 从上面两个函数返回的id值
//ctrl: 0/运行 1/停止 2/重连(启用ip,port参数)
void wmix_rtp_ctrl(int id, int ctrl, char *ip, int port)
{
    char msgPath[128] = {0};
    WMix_Msg msg;
    //
    if(id == 0)
        return;
    //
    wmix_auto_path(msgPath, id);
    //
    key_t msg_key;
    int msg_fd;
    if((msg_key = ftok(msgPath, WMIX_MSG_ID)) == -1){
        fprintf(stderr, "wmix_rtp_ctrl: ftok err\n");
        return ;
    }if((msg_fd = msgget(msg_key, 0666)) == -1){
        fprintf(stderr, "wmix_rtp_ctrl: msgget err\n");
        return ;
    }
    //装填 message
    memset(&msg, 0, sizeof(WMix_Msg));
    msg.type = ctrl;
    msg.value[0] = (port>>8)&0xff;
    msg.value[1] = port&0xff;
    strcpy((char*)&msg.value[2], ip);
    //发出
    msgsnd(msg_fd, &msg, WMIX_MSG_BUFF_SIZE, IPC_NOWAIT);
}

bool wmix_check_id(int id)
{
    char msgPath[128] = {0};
    //关闭旧的播放线程
    wmix_auto_path(msgPath, id);
    if(access(msgPath, F_OK) == 0)
        return true;
    return false;
}

//============= shm
#include <sys/shm.h>
int shm_create(char *path, int flag, int size, void **mem)
{
    key_t key = ftok(path, flag);
    if(key < 0)
    {
        fprintf(stderr, "get key error\n");
        return -1;
    }
    int id;
    id = shmget(key, size, 0666);
    if(id < 0)
        id = shmget(key, size, IPC_CREAT|0666);
    if(id < 0)
    {
        fprintf(stderr, "get id error\n");
        return -1;
    }
    if(mem)
        *mem = shmat(id, NULL, 0);

    return id;
}
int shm_destroy(int id)
{
	return shmctl(id,IPC_RMID,NULL);
}
#define AI_CIRCLE_BUFF_LEN 10240

typedef struct{
    int16_t w;
    int16_t buff[AI_CIRCLE_BUFF_LEN+4];
}HiaudioAi_Circle;

static HiaudioAi_Circle *ai_circle = NULL;

int16_t wmix_mem_read(int16_t *dat, int16_t len, int16_t *addr, bool wait)
{
    int16_t i = 0;
    int16_t w = *addr;
    //
    if(!ai_circle)
    {
        shm_create("/tmp/wmix", 'h', sizeof(HiaudioAi_Circle), &ai_circle);
        if(!ai_circle)
        {
            fprintf(stderr, "wmix_mem_read: shm_create err !!\n");
            return 0;
        }
        //
        w = ai_circle->w;
    }
    //
    if(w < 0 || w >= AI_CIRCLE_BUFF_LEN)
        w = ai_circle->w;
    for(i = 0; i < len; i++)
    {
        if(w == ai_circle->w)
        {
            if(wait && ai_circle)
            {
                usleep(1000);
                continue;
            }
            break;
        }
        //
        *dat++ = ai_circle->buff[w++];
        if(w >= AI_CIRCLE_BUFF_LEN)
            w = 0;
    }
    *addr = w;
    //
    return i;
}
