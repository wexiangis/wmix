/*
 *  需要抛线程来实现的各种音频功能模块
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <arpa/inet.h>

#include "wmix.h"
#include "delay.h"

void wmix_thread_fifo_pcm_play(WMixThread_Param *wmtp)
{
    //传入参数(目标播放参数)
    char *path = (char *)&wmtp->param[4];
    uint8_t chn = wmtp->param[0];
    uint8_t sample = wmtp->param[1];
    uint16_t freq = (wmtp->param[2] << 8) | wmtp->param[3];
    //读流
    ssize_t ret;
    int fd_read;
    uint8_t *buff;
    uint32_t buffSize;
    //载入混音池标记
    uint32_t tick;
    WMix_Point head, src;
    //计时打印:
    uint32_t second = 0, secBytes, secBytesCount = 0;
    //背景削减: reduce>1时表示别人的削减倍数,reduceSkip标记自己不参与削减
    uint8_t reduce = ((wmtp->flag >> 8) & 0xFF) + 1, reduceSkip = 0;
    //线程同步
    uint8_t loopWord;
    loopWord = wmtp->wmix->loopWordFifo;

    //确认路径和打开流
    if (mkfifo(path, 0666) < 0 && errno != EEXIST)
    {
        WMIX_ERR("mkfifo err\r\n");
        return;
    }
    fd_read = open(path, O_RDONLY);

    //独占 reduceMode
    if (reduce > 1 && wmtp->wmix->reduceMode == 1)
    {
        wmtp->wmix->reduceMode = reduce;
        reduceSkip = 1;
    }
    else
        reduce = 1;

    //一秒数据量和缓存分配
    secBytes = chn * sample / 8 * freq;
    buffSize = secBytes;
    buff = (uint8_t *)calloc(buffSize, sizeof(uint8_t));

    if (wmtp->wmix->debug)
        printf("<< FIFO-W: %s start >>\n"
               "   通道数: %d\n"
               "   采样位数: %d bit\n"
               "   采样率: %d Hz\n",
               path, chn, sample, freq);

    //指针准备
    src.U8 = buff;
    head.U8 = 0;
    tick = 0;

    //线程计数
    wmtp->wmix->thread_play += 1;
    while (wmtp->wmix->run && loopWord == wmtp->wmix->loopWordFifo)
    {
        ret = read(fd_read, buff, buffSize);
        if (ret > 0)
        {
            //载入混音池
            head = wmix_load_data(
                wmtp->wmix, src, ret, freq, chn, sample, head, reduce, &tick);
            if (head.U8 == 0)
                break;
            //播放时间
            if (wmtp->wmix->debug)
            {
                secBytesCount += ret;
                if (secBytesCount >= secBytes)
                {
                    secBytesCount -= secBytes;
                    second += 1;
                    printf("  FIFO-W: %s %02d:%02d\r\n", path, second / 60, second % 60);
                }
            }
            continue;
        }
        else if (errno != EAGAIN)
            break;
        delayus(5000);
    }
    if (wmtp->wmix->debug)
        printf(">> FIFO-W: %s end <<\r\n", path);
    //关闭流和删除fifo文件
    close(fd_read);
    remove(path);
    //线程计数
    wmtp->wmix->thread_play -= 1;
    //关闭 reduceMode
    if (reduceSkip)
        wmtp->wmix->reduceMode = 1;
    //内存回收
    free(buff);
    free(wmtp->param);
    free(wmtp);
}

void wmix_thread_fifo_pcm_record(WMixThread_Param *wmtp)
{
    //传入参数(目标录制参数)
    char *path = (char *)&wmtp->param[4];
    uint8_t chn = wmtp->param[0];
    uint8_t sample = wmtp->param[1];
    uint16_t freq = (wmtp->param[2] << 8) | wmtp->param[3];
    //写流
    ssize_t ret;
    int fd_write;
    int16_t addr = -1; //读mem用
    uint8_t *buffSrc;  //源录音数据
    uint8_t *buffDist; //目标格式的录音数据
    uint32_t buffSizeSrc;
    uint32_t buffSizeDist;
    //计时打印:
    uint32_t second = 0, secBytes, secBytesCount = 0;
    //线程同步
    uint8_t loopWord;
    loopWord = wmtp->wmix->loopWordFifo;

    //确认路径和打开流
    if (mkfifo(path, 0666) < 0 && errno != EEXIST)
    {
        WMIX_ERR("mkfifo err\r\n");
        return;
    }
    fd_write = open(path, O_WRONLY);

    //一秒数据量
    secBytes = WMIX_CHN * WMIX_SAMPLE / 8 * WMIX_FREQ;
    //缓存分配,10包作为最小单位
    buffSizeSrc = WMIX_PKG_SIZE * 10;
    buffSrc = (uint8_t *)calloc(buffSizeSrc, sizeof(uint8_t));
    buffSizeDist = wmix_len_of_out(WMIX_CHN, WMIX_FREQ, buffSizeSrc, chn, freq);
    buffDist = (uint8_t *)calloc(buffSizeDist, sizeof(uint8_t));

    if (wmtp->wmix->debug)
        printf("<< FIFO-R: %s record >>\n"
               "   通道数: %d\n"
               "   采样位数: %d bit\n"
               "   采样率: %d Hz\n"
               "   时间长度: -- sec\n\r\n",
               path, chn, sample, freq);

    //线程计数
    wmtp->wmix->thread_record += 1;
    while (wmtp->wmix->run && loopWord == wmtp->wmix->loopWordFifo)
    {
        ret = wmix_mem_read_origin((int16_t *)buffSrc, buffSizeSrc / 2, &addr, false) * 2;
        if (ret > 0)
        {
            //录制时间
            if (wmtp->wmix->debug)
            {
                secBytesCount += ret;
                if (secBytesCount >= secBytes)
                {
                    secBytesCount -= secBytes;
                    second += 1;
                    printf("  FIFO-R: %s %02d:%02d\r\n", path, second / 60, second % 60);
                }
            }
            //缩放
            ret = wmix_pcm_zoom(WMIX_CHN, WMIX_FREQ, buffSrc, ret, chn, freq, buffDist);
            //输出
            ret = write(fd_write, buffDist, ret);
            if (ret < 0 && errno != EAGAIN)
                break;
        }
        else if (ret < 0)
        {
            WMIX_ERR2("read mem err %d\r\n", (int)ret);
            break;
        }
        else
        {
            delayus(5000);
            fsync(fd_write);
        }
    }
    if (wmtp->wmix->debug)
        printf(">> FIFO-R: %s end <<\r\n", path);
    //关闭流和删除fifo文件
    close(fd_write);
    remove(path);
    //线程计数
    wmtp->wmix->thread_record -= 1;
    //内存回收
    free(buffSrc);
    free(buffDist);
    free(wmtp->param);
    free(wmtp);
}

void wmix_thread_fifo_g711a_record(WMixThread_Param *wmtp)
{
    //传入参数(目标录制参数)
    char *path = (char *)&wmtp->param[4];
    uint8_t chn = wmtp->param[0];
    uint8_t sample = wmtp->param[1];
    uint16_t freq = (wmtp->param[2] << 8) | wmtp->param[3];
    //写流
    ssize_t ret;
    int fd_write;
    int16_t addr = -1; //读mem用
    uint8_t *buffSrc;  //源录音数据
    uint8_t *buffDist; //目标格式的录音数据
    uint32_t buffSizeSrc;
    uint32_t buffSizeDist;
    //g711
    uint8_t g711aBuff[640];
    //计时打印:
    uint32_t second = 0, secBytes, secBytesCount = 0;
    //线程同步
    uint8_t loopWord;
    loopWord = wmtp->wmix->loopWordFifo;

    //确认路径和打开流
    if (mkfifo(path, 0666) < 0 && errno != EEXIST)
    {
        WMIX_ERR("mkfifo err\r\n");
        return;
    }
    fd_write = open(path, O_WRONLY);

    //一秒数据量
    secBytes = WMIX_CHN * WMIX_SAMPLE / 8 * WMIX_FREQ;
    //缓存分配,根据输出指定输入量
    buffSizeDist = 640; //目标输出320字节的g711a数据,所以这里为320*2长度
    buffDist = (uint8_t *)calloc(buffSizeDist, sizeof(uint8_t));
    buffSizeSrc = wmix_len_of_in(WMIX_CHN, WMIX_FREQ, chn, freq, buffSizeDist);
    buffSrc = (uint8_t *)calloc(buffSizeSrc, sizeof(uint8_t));

    if (wmtp->wmix->debug)
        printf("<< FIFO-g711a: %s record >>\n"
               "   通道数: %d\n"
               "   采样位数: %d bit\n"
               "   采样率: %d Hz\n"
               "   时间长度: -- sec\n\r\n",
               path, chn, sample, freq);

    //线程计数
    wmtp->wmix->thread_record += 1;
    while (wmtp->wmix->run && loopWord == wmtp->wmix->loopWordFifo)
    {
        ret = wmix_mem_read_origin((int16_t *)buffSrc, buffSizeSrc / 2, &addr, true) * 2;
        if (ret > 0)
        {
            //录制时间
            if (wmtp->wmix->debug)
            {
                secBytesCount += ret;
                if (secBytesCount >= secBytes)
                {
                    secBytesCount -= secBytes;
                    second += 1;
                    printf("  FIFO-g711a: %s %02d:%02d\r\n", path, second / 60, second % 60);
                }
            }
            //缩放
            ret = wmix_pcm_zoom(WMIX_CHN, WMIX_FREQ, buffSrc, ret, chn, freq, buffDist);
            //转g711a
            ret = PCM2G711a((char *)buffDist, (char *)g711aBuff, ret, 0);
            //输出
            ret = write(fd_write, g711aBuff, ret);
            if (ret < 0 && errno != EAGAIN)
                break;
        }
        else if (ret < 0)
        {
            WMIX_ERR2("read mem err %d\r\n", (int)ret);
            break;
        }
    }
    if (wmtp->wmix->debug)
        printf(">> FIFO-g711a: %s end <<\r\n", path);
    //关闭流和删除fifo文件
    close(fd_write);
    remove(path);
    //线程计数
    wmtp->wmix->thread_record -= 1;
    //内存回收
    free(buffSrc);
    free(buffDist);
    free(wmtp->param);
    free(wmtp);
}

#if (MAKE_AAC)
void wmix_thread_fifo_aac_record(WMixThread_Param *wmtp)
{
    //传入参数(目标录制参数)
    char *path = (char *)&wmtp->param[4];
    uint8_t chn = wmtp->param[0];
    uint8_t sample = wmtp->param[1];
    uint16_t freq = (wmtp->param[2] << 8) | wmtp->param[3];
    //写流
    ssize_t ret;
    int fd_write;
    int16_t addr = -1; //读mem用
    uint8_t *buffSrc;  //源录音数据
    uint8_t *buffDist; //目标格式的录音数据
    uint32_t buffSizeSrc;
    uint32_t buffSizeDist;
    //aac编码用
    void *aacEnc = NULL;
    uint8_t aacBuff[4096];
    //计时打印:
    uint32_t second = 0, secBytes, secBytesCount = 0;
    //线程同步
    uint8_t loopWord;
    loopWord = wmtp->wmix->loopWordFifo;

    //确认路径和打开流
    if (mkfifo(path, 0666) < 0 && errno != EEXIST)
    {
        WMIX_ERR("mkfifo err\r\n");
        return;
    }
    fd_write = open(path, O_WRONLY);

    //一秒数据量
    secBytes = WMIX_CHN * WMIX_SAMPLE / 8 * WMIX_FREQ;
    //缓存分配,配合aac编码需要,设置输出格式
    buffSizeDist = 1024 * chn * sample / 8;
    buffDist = (uint8_t *)calloc(buffSizeDist, sizeof(uint8_t));
    buffSizeSrc = wmix_len_of_in(WMIX_CHN, WMIX_FREQ, chn, freq, buffSizeDist);
    buffSrc = (uint8_t *)calloc(buffSizeSrc, sizeof(uint8_t));

    if (wmtp->wmix->debug)
        printf("<< FIFO-AAC: %s aac >>\n"
               "   通道数: %d\n"
               "   采样位数: %d bit\n"
               "   采样率: %d Hz\n",
               path, chn, sample, freq);

    //线程计数
    wmtp->wmix->thread_record += 1;
    while (wmtp->wmix->run && loopWord == wmtp->wmix->loopWordFifo)
    {
        ret = wmix_mem_read_origin((int16_t *)buffSrc, buffSizeSrc / 2, &addr, true) * 2;
        if (ret > 0)
        {
            //录制时间
            if (wmtp->wmix->debug)
            {
                secBytesCount += ret;
                if (secBytesCount >= secBytes)
                {
                    secBytesCount -= secBytes;
                    second += 1;
                    printf("  FIFO-AAC: %s %02d:%02d\r\n", path, second / 60, second % 60);
                }
            }
            //缩放
            ret = wmix_pcm_zoom(WMIX_CHN, WMIX_FREQ, buffSrc, ret, chn, freq, buffDist);
            //编码
            ret = aac_encode(&aacEnc, buffDist, ret, aacBuff, sizeof(aacBuff), chn, freq);
            //输出
            ret = write(fd_write, aacBuff, ret);
            if (ret < 0 && errno != EAGAIN)
                break;
        }
        else if (ret < 0)
        {
            WMIX_ERR2("read mem err %d\r\n", (int)ret);
            break;
        }
    }
    if (wmtp->wmix->debug)
        printf(">> FIFO-AAC: %s end <<\r\n", path);
    //关闭aac编码器
    if (aacEnc)
        aac_encodeRelease(&aacEnc);
    //关闭流和删除fifo文件
    close(fd_write);
    remove(path);
    //线程计数
    wmtp->wmix->thread_record -= 1;
    //内存回收
    free(buffSrc);
    free(buffDist);
    free(wmtp->param);
    free(wmtp);
}
#endif

void wmix_thread_record_wav(WMixThread_Param *wmtp)
{
    //传入参数(目标录音参数)
    char *path = (char *)&wmtp->param[6];
    uint8_t chn = wmtp->param[0];
    uint8_t sample = wmtp->param[1];
    uint16_t freq = (wmtp->param[2] << 8) | wmtp->param[3];
    uint16_t time = (wmtp->param[4] << 8) | wmtp->param[5];
    //读流
    ssize_t ret;
    int fd_write;
    int16_t addr = -1; //读mem用
    uint8_t *buffSrc;  //源录音数据
    uint8_t *buffDist; //目标格式的录音数据
    uint32_t buffSizeSrc;
    uint32_t buffSizeDist;
    WAVContainer_t wav;
    //计时打印:
    uint32_t second = 0, secBytes, secBytesCount = 0;
    //线程同步
    uint8_t loopWord;
    loopWord = wmtp->wmix->loopWordRecord;

    fd_write = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd_write < 1)
    {
        WMIX_ERR2("open %s err\r\n", path);
        return;
    }
    //wav头
    WAV_Params(&wav, time, chn, sample, freq);
    if (WAV_WriteHeader(fd_write, &wav) < 0)
    {
        close(fd_write);
        WMIX_ERR("WAV_WriteHeader err\r\n");
        return;
    }

    //一秒数据量
    secBytes = WMIX_CHN * WMIX_SAMPLE / 8 * WMIX_FREQ;
    //缓存分配,5包作为最小单位
    buffSizeSrc = WMIX_PKG_SIZE * 5;
    buffSrc = (uint8_t *)calloc(buffSizeSrc, sizeof(uint8_t));
    buffSizeDist = wmix_len_of_out(WMIX_CHN, WMIX_FREQ, buffSizeSrc, chn, freq);
    buffDist = (uint8_t *)calloc(buffSizeDist, sizeof(uint8_t));

    if (wmtp->wmix->debug)
        printf("<< RECORD-WAV: %s record >>\n"
               "   通道数: %d\n"
               "   采样位数: %d bit\n"
               "   采样率: %d Hz\n"
               "   时间长度: %d sec\n\r\n",
               path, chn, sample, freq, time);

    //线程计数
    wmtp->wmix->thread_record += 1;
    while (wmtp->wmix->run && loopWord == wmtp->wmix->loopWordRecord)
    {
        ret = wmix_mem_read_origin((int16_t *)buffSrc, buffSizeSrc / 2, &addr, false) * 2;
        if (ret > 0)
        {
            secBytesCount += ret;
            if (secBytesCount >= secBytes)
            {
                secBytesCount -= secBytes;
                second += 1;
                if (wmtp->wmix->debug)
                    printf("  RECORD-WAV: %s %02d:%02d\r\n", path, second / 60, second % 60);
            }
            //缩放
            ret = wmix_pcm_zoom(WMIX_CHN, WMIX_FREQ, buffSrc, ret, chn, freq, buffDist);
            //输出
            ret = write(fd_write, buffDist, ret);
            if (ret < 0 || second >= time)
                break;
        }
        else if (ret < 0)
        {
            WMIX_ERR2("read mem err %d\r\n", (int)ret);
            break;
        }
        else
            delayus(5000);
    }
    if (wmtp->wmix->debug)
        printf(">> RECORD-WAV: %s end <<\r\n", path);
    //关闭流
    close(fd_write);
    //线程计数
    wmtp->wmix->thread_record -= 1;
    //内存回收
    free(buffSrc);
    free(buffDist);
    free(wmtp->param);
    free(wmtp);
}

#define WMIX_RTP_CTRL_MSG_RECV(thread_name)                     \
    if (msg_fd)                                                 \
    {                                                           \
        if (msgrcv(msg_fd, &msg,                                \
                   WMIX_MSG_BUFF_SIZE, 0, IPC_NOWAIT) < 1)      \
        {                                                       \
            if (errno != ENOMSG)                                \
            {                                                   \
                if (wmtp->wmix->debug)                          \
                    printf("%s exit: %d msgrecv err/%d\r\n",    \
                           thread_name, msg_fd, errno);         \
                break;                                          \
            }                                                   \
        }                                                       \
        else                                                    \
        {                                                       \
            if (wmtp->wmix->debug)                              \
                printf("%s: msg recv %ld\r\n",                  \
                       thread_name, msg.type);                  \
            ctrlType = msg.type & 0xFF;                         \
            if (ctrlType == WCT_RESET)                          \
            {                                                   \
                rtp_socket_reconnect(&ss, url, port, bindMode); \
                ctrlType = WCT_CLEAR;                           \
            }                                                   \
            else if (ctrlType == WCT_STOP)                      \
                break;                                          \
        }                                                       \
    }

#if (MAKE_AAC)
void wmix_thread_record_aac(WMixThread_Param *wmtp)
{
    //传入参数(目标录制参数)
    char *path = (char *)&wmtp->param[6];
    uint8_t chn = wmtp->param[0];
    uint8_t sample = wmtp->param[1];
    uint16_t freq = (wmtp->param[2] << 8) | wmtp->param[3];
    uint16_t time = (wmtp->param[4] << 8) | wmtp->param[5];
    //写流
    ssize_t ret;
    int fd_write;
    int16_t addr = -1; //读mem用
    uint8_t *buffSrc;  //源录音数据
    uint8_t *buffDist; //目标格式的录音数据
    uint32_t buffSizeSrc;
    uint32_t buffSizeDist;
    //aac编码用
    void *aacEnc = NULL;
    uint8_t aacBuff[4096];
    //计时打印:
    uint32_t second = 0, secBytes, secBytesCount = 0;
    //线程同步
    uint8_t loopWord;
    loopWord = wmtp->wmix->loopWordRecord;

    fd_write = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd_write < 1)
    {
        WMIX_ERR2("open %s err\r\n", path);
        return;
    }

    //一秒数据量
    secBytes = WMIX_CHN * WMIX_SAMPLE / 8 * WMIX_FREQ;
    //缓存分配,配合aac编码需要,设置输出格式
    buffSizeDist = 1024 * chn * sample / 8;
    buffDist = (uint8_t *)calloc(buffSizeDist, sizeof(uint8_t));
    buffSizeSrc = wmix_len_of_in(WMIX_CHN, WMIX_FREQ, chn, freq, buffSizeDist);
    buffSrc = (uint8_t *)calloc(buffSizeSrc, sizeof(uint8_t));

    if (wmtp->wmix->debug)
        printf("<< RECORD-AAC: %s record >>\n"
               "   通道数: %d\n"
               "   采样位数: %d bit\n"
               "   采样率: %d Hz\n"
               "   时间长度: %d sec %d - %d\n\r\n",
               path, chn, sample, freq, time, buffSizeSrc, buffSizeDist);

    //线程计数
    wmtp->wmix->thread_record += 1;
    while (wmtp->wmix->run && loopWord == wmtp->wmix->loopWordRecord)
    {
        ret = wmix_mem_read_origin((int16_t *)buffSrc, buffSizeSrc / 2, &addr, true) * 2;
        if (ret > 0)
        {
            //录制时间
            secBytesCount += ret;
            if (secBytesCount >= secBytes)
            {
                secBytesCount -= secBytes;
                second += 1;
                if (wmtp->wmix->debug)
                    printf("  RECORD-AAC: %s %02d:%02d\r\n", path, second / 60, second % 60);
            }
            //缩放
            ret = wmix_pcm_zoom(WMIX_CHN, WMIX_FREQ, buffSrc, ret, chn, freq, buffDist);
            //编码
            ret = aac_encode(&aacEnc, buffDist, ret, aacBuff, sizeof(aacBuff), chn, freq);
            //输出
            ret = write(fd_write, aacBuff, ret);
            if (ret < 0 || second >= time)
                break;
        }
        else if (ret < 0)
        {
            WMIX_ERR2("read mem err %d\r\n", (int)ret);
            break;
        }
    }
    if (wmtp->wmix->debug)
        printf(">> RECORD-AAC: %s end <<\r\n", path);
    //关闭aac编码器
    if (aacEnc)
        aac_encodeRelease(&aacEnc);
    //关闭流和删除fifo文件
    close(fd_write);
    //线程计数
    wmtp->wmix->thread_record -= 1;
    //内存回收
    free(buffSrc);
    free(buffDist);
    free(wmtp->param);
    free(wmtp);
}

void wmix_thread_rtp_send_aac(WMixThread_Param *wmtp)
{
    char *msgPath;
    key_t msg_key;
    int msg_fd = 0;
    FILE *fp;
    WMix_Msg msg;
    //传入参数(目标录制参数)
    char *url = (char *)&wmtp->param[11];
    uint8_t chn = wmtp->param[0];
    uint8_t sample = wmtp->param[1];
    uint16_t freq = (wmtp->param[2] << 8) | wmtp->param[3];
    uint16_t port = (wmtp->param[4] << 8) | wmtp->param[5];
    bool bindMode = wmtp->param[6] ? true : false;
    //写流
    ssize_t ret;
    int16_t addr = -1; //读mem用
    uint8_t *buffSrc;  //源录音数据
    uint8_t *buffDist; //目标格式的录音数据
    uint32_t buffSizeSrc;
    uint32_t buffSizeDist;
    //aac编码用
    void *aacEnc = NULL;
    uint8_t aacBuff[4096];
    AacHeader *aacHead = (AacHeader *)aacBuff;
    //计时打印:
    uint32_t second = 0, secBytes, secBytesCount = 0;
    //rtp
    RtpPacket rtpPacket;
    SocketStruct *ss;
    long ctrlType = 0;
    //线程同步
    uint8_t loopWord;
    loopWord = wmtp->wmix->loopWordRtp;

    //初始化rtp,建立socket
    ss = rtp_socket(url, port, bindMode);
    if (!ss)
    {
        WMIX_ERR2("rtp_socket: %s:%d <%d> err\r\n", url, port, bindMode ? 1 : 0);
        return;
    }
    //rtp帧头准备
    rtp_header(&rtpPacket, 0, 0, 0, RTP_VESION, RTP_PAYLOAD_TYPE_AAC, 1, 0, 0, 0x32411);

    //初始化消息
    msgPath = (char *)&wmtp->param[strlen(url) + 11 + 1];
    if (msgPath && msgPath[0])
    {
        //创建消息挂靠路径
        if (access(msgPath, F_OK) != 0)
            creat(msgPath, 0666);
        //写节点描述
        if ((fp = fopen(msgPath, "w")))
        {
            fprintf(fp, "rtp send aac %s:%d", url, port);
            fclose(fp);
        }
        //创建消息
        if ((msg_key = ftok(msgPath, WMIX_MSG_ID)) > 0)
            msg_fd = msgget(msg_key, IPC_CREAT | 0666);
    }
    else
        msgPath = NULL;

    //一秒数据量
    secBytes = WMIX_CHN * WMIX_SAMPLE / 8 * WMIX_FREQ;
    //缓存分配,配合aac编码需要,设置输出格式
    buffSizeDist = 1024 * chn * sample / 8;
    buffDist = (uint8_t *)calloc(buffSizeDist, sizeof(uint8_t));
    buffSizeSrc = wmix_len_of_in(WMIX_CHN, WMIX_FREQ, chn, freq, buffSizeDist);
    buffSrc = (uint8_t *)calloc(buffSizeSrc, sizeof(uint8_t));

    //生成sdp文件,可用于vlc播放
    rtp_create_sdp(
        "/tmp/record-aac.sdp",
        url, port, chn, freq,
        RTP_PAYLOAD_TYPE_AAC);

    if (wmtp->wmix->debug)
        printf(
            "<< RTP-SEND-AAC: %s:%d start >>\r\n"
            "   通道数: %d\r\n"
            "   采样位数: %d bit\r\n"
            "   采样率: %d Hz\r\n",
            url, port, chn, sample, freq);

    //往文件写入当前任务的具体信息(方便查询)
    wmix_write_file(msgPath, "rtp send aac, chn %d, freq %d, url %s:%d", chn, freq, url, port);

    //线程计数
    wmtp->wmix->thread_record += 1;
    while (wmtp->wmix->run && loopWord == wmtp->wmix->loopWordRtp)
    {
        //msg 检查
        WMIX_RTP_CTRL_MSG_RECV("RTP-SEND-AAC");
        //读原始录音数据
        ret = wmix_mem_read_origin((int16_t *)buffSrc, buffSizeSrc / 2, &addr, true) * 2;
        if (ret > 0)
        {
            //使用0数据
            if (ctrlType == WCT_SILENCE)
                memset(buffSrc, 0, ret);
            //录制时间
            if (wmtp->wmix->debug)
            {
                secBytesCount += ret;
                if (secBytesCount >= secBytes)
                {
                    secBytesCount -= secBytes;
                    second += 1;
                    printf("  RTP-SEND-AAC: %s %02d:%02d\r\n", url, second / 60, second % 60);
                }
            }
            //缩放
            ret = wmix_pcm_zoom(WMIX_CHN, WMIX_FREQ, buffSrc, ret, chn, freq, buffDist);
            //编码
            ret = aac_encode(&aacEnc, buffDist, ret, aacBuff, sizeof(aacBuff), chn, freq);
            //输出
            if (ret > 7)
            {
                // aac_parseHeader(aacHead, NULL, NULL, NULL, 1);
                ret -= 7;
                memcpy(&rtpPacket.payload[4], &aacBuff[7], ret);
                //时间戳更新
                rtpPacket.rtpHeader.timestamp +=
                    (((aacHead->adtsBufferFullnessH << 6) | aacHead->adtsBufferFullnessL) + 1) / 2;
                //发出
                ret = rtp_send(ss, &rtpPacket, ret);
                if (ret < 0)
                {
                    // WMIX_ERR("rtp_send err !!\r\n");
                    delayus(1000000);
                    //重连
                    rtp_socket_reconnect(&ss, url, port, bindMode);
                    break;
                }
            }
        }
        else if (ret < 0)
        {
            WMIX_ERR2("read mem err %d\r\n", (int)ret);
            break;
        }
    }
    if (wmtp->wmix->debug)
        printf(">> RTP-SEND-AAC: %s:%d end <<\r\n", url, port);
    //关闭aac编码器
    if (aacEnc)
        aac_encodeRelease(&aacEnc);
    //释放rtp
    rtp_socket_close(&ss);
    //关闭流和删除fifo文件
    if (msg_fd)
        msgctl(msg_fd, IPC_RMID, NULL);
    if (msgPath)
        remove(msgPath);
    //线程计数
    wmtp->wmix->thread_record -= 1;
    //内存回收
    free(buffSrc);
    free(buffDist);
    free(wmtp->param);
    free(wmtp);
}

void wmix_thread_rtp_recv_aac(WMixThread_Param *wmtp)
{
    char *msgPath;
    key_t msg_key;
    int msg_fd = 0;
    FILE *fp;
    WMix_Msg msg;
    //传入参数(目标播放参数)
    char *url = (char *)&wmtp->param[11];
    uint8_t chn = wmtp->param[0];
    uint8_t sample = wmtp->param[1];
    uint16_t freq = (wmtp->param[2] << 8) | wmtp->param[3];
    uint16_t port = (wmtp->param[4] << 8) | wmtp->param[5];
    bool bindMode = wmtp->param[6] ? true : false;
    //读流
    ssize_t ret;
    uint8_t *buff;
    uint32_t buffSize;
    //载入混音池标记
    uint32_t tick;
    WMix_Point head, src;
    //计时打印:
    uint32_t second = 0, secBytes, secBytesCount = 0;
    //背景削减: reduce>1时表示别人的削减倍数,reduceSkip标记自己不参与削减
    uint8_t reduce = ((wmtp->flag >> 8) & 0xFF) + 1, reduceSkip = 0;
    //aac解码句柄
    void *aacDec = NULL;
    int datUse = 0;
    uint8_t chnInt; //用于decode参数
    uint16_t freqInt;
    uint8_t aacBuff[4096];
    //rtp
    RtpPacket rtpPacket;
    SocketStruct *ss;
    long ctrlType = 0;
    int retSize;
    int recv_timeout = 0;
    int intervalUs = 10000;
    //线程同步
    uint8_t loopWord;
    loopWord = wmtp->wmix->loopWordRtp;

    //初始化rtp,建立socket
    ss = rtp_socket(url, port, bindMode);
    if (!ss)
    {
        WMIX_ERR2("rtp_socket: %s:%d <%d> err\r\n", url, port, bindMode ? 1 : 0);
        return;
    }

    //初始化消息
    msgPath = (char *)&wmtp->param[strlen(url) + 11 + 1];
    if (msgPath && msgPath[0])
    {
        //创建消息挂靠路径
        if (access(msgPath, F_OK) != 0)
            creat(msgPath, 0666);
        //写节点描述
        if ((fp = fopen(msgPath, "w")))
        {
            fprintf(fp, "rtp recv aac %s:%d", url, port);
            fclose(fp);
        }
        //创建消息
        if ((msg_key = ftok(msgPath, WMIX_MSG_ID)) > 0)
            msg_fd = msgget(msg_key, IPC_CREAT | 0666);
    }
    else
        msgPath = NULL;

    //独占 reduceMode
    if (reduce > 1 && wmtp->wmix->reduceMode == 1)
    {
        wmtp->wmix->reduceMode = reduce;
        reduceSkip = 1;
    }
    else
        reduce = 1;

    //一秒数据量和缓存分配
    secBytes = chn * sample / 8 * freq;
    buffSize = chn * sample / 8 * 1024;
    buff = (uint8_t *)calloc(buffSize, sizeof(uint8_t));

    if (wmtp->wmix->debug)
        printf(
            "<< RTP-RECV-AAC: %s:%d start >>\r\n"
            "   通道数: %d\r\n"
            "   采样位数: %d bit\r\n"
            "   采样率: %d Hz\r\n",
            url, port, chn, sample, freq);

    //往文件写入当前任务的具体信息(方便查询)
    wmix_write_file(msgPath, "rtp recv aac, chn %d, freq %d, url %s:%d", chn, freq, url, port);

    src.U8 = buff;
    head.U8 = 0;
    tick = 0;
    chnInt = chn;
    freqInt = freq;

    //线程计数
    wmtp->wmix->thread_play += 1;
    while (wmtp->wmix->run && loopWord == wmtp->wmix->loopWordRtp)
    {
        //msg 检查
        WMIX_RTP_CTRL_MSG_RECV("RTP-RECV-AAC");
        //往aacBuff读入数据
        ret = rtp_recv(ss, &rtpPacket, (uint32_t *)&retSize);
        if (ret > 0 && retSize > 0)
        {
            //自创建aac头(rtp不传输aac头)并组装数据
            aac_createHeader((AacHeader *)aacBuff, chnInt, freqInt, 0x7FF, retSize);
            memcpy(&aacBuff[7], &rtpPacket.payload[4], retSize);
            //开始解码
            ret = aac_decode(
                &aacDec,
                aacBuff, retSize + 7,
                buff, &datUse,
                &chnInt, &freqInt);
            if (ret < 0)
                WMIX_ERR2("aac_decode err %d !!\r\n", (int)ret);
            //自动纠正参数(功能有限)
            if (chnInt != chn || freqInt != freq)
            {
                chn = chnInt;
                freq = freqInt;
                //一秒数据量和缓存分配
                secBytes = chn * sample / 8 * freq;
                buffSize = chn * sample / 8 * 1024;
                free(buff);
                buff = (uint8_t *)calloc(buffSize, sizeof(uint8_t));
                src.U8 = buff;
                datUse = 0;

                if (wmtp->wmix->debug)
                    printf(
                        "<< RTP-RECV-AAC: %s:%d start >>\r\n"
                        "   通道数: %d\r\n"
                        "   采样位数: %d bit\r\n"
                        "   采样率: %d Hz\r\n",
                        url, port, chn, sample, freq);

                //往文件写入当前任务的具体信息(方便查询)
                wmix_write_file(msgPath, "rtp recv aac, chn %d, freq %d, url %s:%d", chn, freq, url, port);
            }
            recv_timeout = 0;
        }
        else
        {
            if (errno == EAGAIN && recv_timeout < 3000) //当前缓冲区已无数据可读
                recv_timeout += intervalUs / 1000;
            // else if(errno == ECONNRESET)//对方发送了RST
            //     ;
            // else if(errno == EINTR)//被信号中断
            //     ;
            else
            {
                // WMIX_ERR("rtp_recv err !!\r\n");
                delayus(1000000);
                //重连
                rtp_socket_reconnect(&ss, url, port, bindMode);
                recv_timeout = 0;
                continue;
            }
            ret = -1;
        }
        //播放文件
        if (ret > 0)
        {
            //使用0数据
            if (ctrlType == WCT_SILENCE)
                memset(buff, 0, ret);
            //写入循环缓冲区
            head = wmix_load_data(
                wmtp->wmix,
                src, ret,
                freq,
                chn,
                sample, head, reduce, &tick);
            //播放时间
            if (wmtp->wmix->debug)
            {
                secBytesCount += ret;
                if (secBytesCount >= secBytes)
                {
                    secBytesCount -= secBytes;
                    second += 1;
                    printf("  RTP-RECV-AAC: %s %02d:%02d\r\n", url, second / 60, second % 60);
                }
            }
            continue;
        }
        else
            delayus(intervalUs);
    }
    if (wmtp->wmix->debug)
        printf(">> RTP-RECV-AAC: %s:%d end <<\r\n", url, port);
    //删除文件
    if (msg_fd)
        msgctl(msg_fd, IPC_RMID, NULL);
    if (msgPath)
        remove(msgPath);
    //关闭解码器
    if (aacDec)
        aac_decodeRelease(&aacDec);
    //释放rtp
    rtp_socket_close(&ss);
    //线程计数
    wmtp->wmix->thread_play -= 1;
    //关闭 reduceMode
    if (reduceSkip)
        wmtp->wmix->reduceMode = 1;
    //内存回收
    free(buff);
    free(wmtp->param);
    free(wmtp);
}
#endif //if(MAKE_AAC)

void wmix_thread_rtp_send_pcma(WMixThread_Param *wmtp)
{
    char *msgPath;
    key_t msg_key;
    int msg_fd = 0;
    FILE *fp;
    WMix_Msg msg;
    //传入参数(目标录制参数)
    char *url = (char *)&wmtp->param[11];
    uint8_t chn = wmtp->param[0];
    uint8_t sample = wmtp->param[1];
    uint16_t freq = (wmtp->param[2] << 8) | wmtp->param[3];
    uint16_t port = (wmtp->param[4] << 8) | wmtp->param[5];
    bool bindMode = wmtp->param[6] ? true : false;
    //写流
    ssize_t ret = 0;
    int16_t addr = -1; //读mem用
    uint8_t *buffSrc;  //源录音数据
    uint8_t *buffDist; //目标格式的录音数据
    uint32_t buffSizeSrc;
    uint32_t buffSizeDist;
    //计时打印:
    uint32_t second = 0, secBytes, secBytesCount = 0;
    //rtp
    RtpPacket rtpPacket;
    SocketStruct *ss;
    long ctrlType = 0;
    //线程同步
    uint8_t loopWord;
    loopWord = wmtp->wmix->loopWordRecord;

    //初始化rtp,建立socket
    ss = rtp_socket(url, port, bindMode);
    if (!ss)
    {
        WMIX_ERR2("rtp_socket: %s:%d <%d> err\r\n", url, port, bindMode ? 1 : 0);
        return;
    }
    //rtp帧头准备
    rtp_header(&rtpPacket, 0, 0, 0, RTP_VESION, RTP_PAYLOAD_TYPE_PCMA, 1, 0, 0, 0);
    //初始化消息
    msgPath = (char *)&wmtp->param[strlen(url) + 11 + 1];
    if (msgPath && msgPath[0])
    {
        //创建消息挂靠路径
        if (access(msgPath, F_OK) != 0)
            creat(msgPath, 0666);
        //写节点描述
        if ((fp = fopen(msgPath, "w")))
        {
            fprintf(fp, "rtp send pcma %s:%d", url, port);
            fclose(fp);
        }
        //创建消息
        if ((msg_key = ftok(msgPath, WMIX_MSG_ID)) > 0)
            msg_fd = msgget(msg_key, IPC_CREAT | 0666);
    }
    else
        msgPath = NULL;

    //一秒数据量
    secBytes = WMIX_CHN * WMIX_SAMPLE / 8 * WMIX_FREQ;

#if 0
    //该模式固定1x8000Hz配置
    chn = 1;
    freq = 8000;
    // 按每包rtp装载160字节g711a的量
    buffSizeDist = 160 * 2;
#else
    // 按20ms采样间隔的量
    buffSizeDist = WMIX_INTERVAL_MS * freq / 1000 * chn * sample / 8;
#endif

    //缓存分配
    buffDist = (uint8_t *)calloc(buffSizeDist, sizeof(uint8_t));
    buffSizeSrc = wmix_len_of_in(WMIX_CHN, WMIX_FREQ, chn, freq, buffSizeDist);
    buffSrc = (uint8_t *)calloc(buffSizeSrc, sizeof(uint8_t));

    //生成sdp文件,可用于vlc播放
    rtp_create_sdp(
        "/tmp/record.sdp",
        url, port, chn, freq,
        RTP_PAYLOAD_TYPE_PCMA);

    if (wmtp->wmix->debug)
        printf(
            "<< RTP-SEND-PCM: %s:%d start >>\r\n"
            "   通道数: %d\r\n"
            "   采样位数: %d bit\r\n"
            "   采样率: %d Hz\r\n",
            url, port, chn, sample, freq);

    //往文件写入当前任务的具体信息(方便查询)
    wmix_write_file(msgPath, "rtp send pcma, chn %d, freq %d, url %s:%d", chn, freq, url, port);

    //线程计数
    wmtp->wmix->thread_record += 1;
    while (wmtp->wmix->run && loopWord == wmtp->wmix->loopWordRecord)
    {
        //发数据
        if (ret > 0)
        {
            //使用0数据
            if (ctrlType == WCT_SILENCE)
                memset(buffSrc, 0, ret);
            //录制时间
            if (wmtp->wmix->debug)
            {
                secBytesCount += ret;
                if (secBytesCount >= secBytes)
                {
                    secBytesCount -= secBytes;
                    second += 1;
                    printf("  RTP-SEND-PCM: %s %02d:%02d\r\n", url, second / 60, second % 60);
                }
            }
            //缩放
            ret = wmix_pcm_zoom(WMIX_CHN, WMIX_FREQ, buffSrc, ret, chn, freq, buffDist);
            //g711a编码,同时塞入rtp包中
            ret = PCM2G711a((char *)buffDist, (char *)rtpPacket.payload, ret, 0);
            //时间戳更新
            rtpPacket.rtpHeader.timestamp += ret / chn;
            //发
            ret = rtp_send(ss, &rtpPacket, ret);
            if (ret < 0)
            {
                // WMIX_ERR("rtp_send err !!\r\n");
                delayus(1000000);
                //重连
                rtp_socket_reconnect(&ss, url, port, bindMode);
                continue;
            }
        }
        //发完数据,趁空闲立即取数据
        ret = wmix_mem_read_origin((int16_t *)buffSrc, buffSizeSrc / 2, &addr, true) * 2;
        //msg 检查
        WMIX_RTP_CTRL_MSG_RECV("RTP-SEND-PCM");
    }
    if (wmtp->wmix->debug)
        printf(">> RTP-SEND-PCM: %s:%d end <<\r\n", url, port);
    //释放rtp
    rtp_socket_close(&ss);
    //关闭流和删除fifo文件
    if (msg_fd)
        msgctl(msg_fd, IPC_RMID, NULL);
    if (msgPath)
        remove(msgPath);
    //线程计数
    wmtp->wmix->thread_record -= 1;
    //内存回收
    free(buffSrc);
    free(buffDist);
    free(wmtp->param);
    free(wmtp);
}

void wmix_thread_rtp_recv_pcma(WMixThread_Param *wmtp)
{
    char *msgPath;
    key_t msg_key;
    int msg_fd = 0;
    FILE *fp;
    WMix_Msg msg;
    //传入参数(目标播放参数)
    char *url = (char *)&wmtp->param[11];
    uint8_t chn = wmtp->param[0];
    uint8_t sample = wmtp->param[1];
    uint16_t freq = (wmtp->param[2] << 8) | wmtp->param[3];
    uint16_t port = (wmtp->param[4] << 8) | wmtp->param[5];
    bool bindMode = wmtp->param[6] ? true : false;
    //读流
    ssize_t ret;
    uint8_t *buff;
    uint32_t buffSize;
    //载入混音池标记
    uint32_t tick;
    WMix_Point head, src;
    //计时打印:
    uint32_t second = 0, secBytes, secBytesCount = 0;
    //背景削减: reduce>1时表示别人的削减倍数,reduceSkip标记自己不参与削减
    uint8_t reduce = ((wmtp->flag >> 8) & 0xFF) + 1, reduceSkip = 0;
    //rtp
    RtpPacket rtpPacket;
    SocketStruct *ss;
    long ctrlType = 0;
    int retSize;
    int recv_timeout = 0;
    int intervalUs = 5000;
    //线程同步
    uint8_t loopWord;
    loopWord = wmtp->wmix->loopWordRtp;

    //初始化rtp,建立socket
    ss = rtp_socket(url, port, bindMode);
    if (!ss)
    {
        WMIX_ERR2("rtp_socket: %s:%d <%d> err\r\n", url, port, bindMode ? 1 : 0);
        return;
    }

    //初始化消息
    msgPath = (char *)&wmtp->param[strlen(url) + 11 + 1];
    if (msgPath && msgPath[0])
    {
        //创建消息挂靠路径
        if (access(msgPath, F_OK) != 0)
            creat(msgPath, 0666);
        //写节点描述
        if ((fp = fopen(msgPath, "w")))
        {
            fprintf(fp, "rtp recv pcma %s:%d", url, port);
            fclose(fp);
        }
        //创建消息
        if ((msg_key = ftok(msgPath, WMIX_MSG_ID)) > 0)
            msg_fd = msgget(msg_key, IPC_CREAT | 0666);
    }
    else
        msgPath = NULL;

    //独占 reduceMode
    if (reduce > 1 && wmtp->wmix->reduceMode == 1)
    {
        wmtp->wmix->reduceMode = reduce;
        reduceSkip = 1;
    }
    else
        reduce = 1;

    //一秒数据量
    secBytes = chn * sample / 8 * freq;
    //最多每包200ms数据量
    buffSize = 200 * freq / 1000 * chn * sample / 8;
    buff = (uint8_t *)calloc(buffSize, sizeof(uint8_t));

    if (wmtp->wmix->debug)
        printf(
            "<< RTP-RECV-PCM: %s:%d start >>\r\n"
            "   通道数: %d\r\n"
            "   采样位数: %d bit\r\n"
            "   采样率: %d Hz\r\n",
            url, port, chn, sample, freq);

    //往文件写入当前任务的具体信息(方便查询)
    wmix_write_file(msgPath, "rtp recv pcma, chn %d, freq %d, url %s:%d", chn, freq, url, port);

    src.U8 = buff;
    head.U8 = 0;
    tick = 0;
    //线程计数
    wmtp->wmix->thread_play += 1;
    while (wmtp->wmix->run && loopWord == wmtp->wmix->loopWordRtp)
    {
        //msg 检查
        WMIX_RTP_CTRL_MSG_RECV("RTP-RECV-PCM");
        if (!wmtp->wmix->run)
            break;
        //读rtp数据
        ret = rtp_recv(ss, &rtpPacket, (uint32_t *)&retSize);
        //g711a解码
        if (ret > 0 && retSize > 0)
        {
            ret = G711a2PCM((char *)rtpPacket.payload, (char *)buff, retSize, 0);
            recv_timeout = 0;
        }
        else
        {
            if (errno == EAGAIN && recv_timeout < 3000) //当前缓冲区已无数据可读
                recv_timeout += intervalUs / 1000;
            // else if(errno == ECONNRESET)//对方发送了RST
            //     ;
            // else if(errno == EINTR)//被信号中断
            //     ;
            else
            {
                // WMIX_ERR("rtp_recv err !!\r\n");
                delayus(1000000);
                //重连
                rtp_socket_reconnect(&ss, url, port, bindMode);
                recv_timeout = 0;
                continue;
            }
            ret = -1;
        }
        //播放文件
        if (ret > 0)
        {
            //使用0数据
            if (ctrlType == WCT_SILENCE)
                memset(buff, 0, ret);
            //写入循环缓冲区
            head = wmix_load_data(
                wmtp->wmix,
                src, ret,
                freq,
                chn,
                sample, head, reduce, &tick);
            //播放时间
            if (wmtp->wmix->debug)
            {
                secBytesCount += ret;
                if (secBytesCount >= secBytes)
                {
                    secBytesCount -= secBytes;
                    second += 1;
                    printf("  RTP-RECV-PCM: %s %02d:%02d\r\n", url, second / 60, second % 60);
                }
            }
            continue;
        }
        else
            delayus(intervalUs);
    }
    if (wmtp->wmix->debug)
        printf(">> RTP-RECV-PCM: %s:%d end <<\r\n", url, port);
    //删除文件
    if (msg_fd)
        msgctl(msg_fd, IPC_RMID, NULL);
    if (msgPath)
        remove(msgPath);
    //释放rtp
    rtp_socket_close(&ss);
    //线程计数
    wmtp->wmix->thread_play -= 1;
    //关闭 reduceMode
    if (reduceSkip)
        wmtp->wmix->reduceMode = 1;
    //内存回收
    free(buff);
    free(wmtp->param);
    free(wmtp);
}

void wmix_task_play_wav(
    WMix_Struct *wmix,
    char *wavPath,
    int msg_fd,
    uint8_t reduce,
    uint8_t interval,
    uint8_t repeat)
{
    int fd = 0;
    ssize_t ret = 0;
    uint8_t *buff = NULL;
    //每次读
    uint32_t buffSize, buffSize2;
    //wav文件头信息
    WAVContainer_t wav;
    //写循环缓冲区
    WMix_Point src, head;
    //播放计时
    uint32_t tick, total = 0, totalWait;
    uint32_t second = 0, secBytesCount = 0;
    //背景消减和重复播放延时
    uint8_t reduceSkip = 0; //自己不被背景削减
    int intervalMs = interval * 1000;
    //消息通信
    WMix_Msg msg;
    //系统更新loopWord时,会关闭该条播放
    int timeout;
    uint8_t loopWord;
    loopWord = wmix->loopWord;

    if (!wmix || !wmix->run || !wavPath)
        return;

    if ((fd = open(wavPath, O_RDONLY)) <= 0)
    {
        WMIX_ERR2("%s open err\r\n", wavPath);
        return;
    }
    if (WAV_ReadHeader(fd, &wav) < 0)
    {
        WMIX_ERR2("Error WAV_Parse [%s]\r\n", wavPath);
        close(fd);
        return;
    }

    if (wmix->debug)
        printf(
            "<< PLAY-WAV: %s start >>\r\n"
            "   通道数: %d\r\n"
            "   采样位数: %d bit\r\n"
            "   采样率: %d Hz\r\n"
            "   每秒字节: %d Bytes\r\n"
            "   重播间隔/次数: %d/%d\r\n"
            "   tick: %d\r\n"
            "   msgid: %d\r\n",
            wavPath,
            wav.format.channels,
            wav.format.sample_length,
            wav.format.sample_rate,
            wav.format.bytes_p_second,
            interval, repeat,
            wmix->tick,
            msg_fd);
    //独占 reduceMode
    reduce += 1;
    if (reduce > 1 && wmix->reduceMode == 1)
    {
        //系统背景削减量以我为准
        wmix->reduceMode = reduce;
        reduceSkip = 1;
    }
    else
        reduce = 1;
    //默认缓冲区大小设为1秒字播放字节数
    buffSize = wav.format.bytes_p_second;
    buffSize2 = WMIX_CHN * WMIX_SAMPLE / 8 * WMIX_FREQ;
    totalWait = buffSize2 / 2;

    //把每秒数据包拆得越细, 打断速度越快
    //以下拆包的倍数必须能同时被 wav.format.sample_rate 和 WMIX_FREQ 整除 !!
    if (wav.format.sample_rate % 4 == 0)
    {
        buffSize /= 4;
        buffSize2 /= 4;
        totalWait = buffSize2;
    }
    else if (wav.format.sample_rate % 3 == 0)
    {
        buffSize /= 3;
        buffSize2 /= 3;
        totalWait = buffSize2;
    }
    else
    {
        buffSize /= 2;
        buffSize2 /= 2;
        totalWait = buffSize2 / 2;
    }
    //内存
    buff = (uint8_t *)calloc(buffSize, sizeof(uint8_t));

    src.U8 = buff;
    head.U8 = 0;
    tick = 0;

    while (wmix->run && loopWord == wmix->loopWord)
    {
        //检查消息是否已被销毁
        if (msg_fd)
        {
            if (msgrcv(msg_fd, &msg, WMIX_MSG_BUFF_SIZE, 0, IPC_NOWAIT) < 1 && errno != ENOMSG)
            {
                if (wmix->debug)
                    printf("PLAY-WAV exit: %d msgrecv err/%d\r\n", msg_fd, errno);
                break;
            }
        }
        //播放文件
        ret = read(fd, buff, buffSize);
        if (ret > 0)
        {
            //等播放指针赶上写入进度
            timeout = 0;
            while (wmix->run && timeout++ < 200 &&
                   loopWord == wmix->loopWord &&
                   tick > wmix->tick &&
                   tick - wmix->tick > totalWait)
                delayus(5000);
            if (!wmix->run || loopWord != wmix->loopWord)
                break;
            //写入循环缓冲区
            head = wmix_load_data(
                wmix,
                src, ret,
                wav.format.sample_rate,
                wav.format.channels,
                wav.format.sample_length, head, reduce, &tick);
            //写入的总字节数统计
            secBytesCount += ret;
            total += ret;
            //播放时间
            if (secBytesCount > wav.format.bytes_p_second)
            {
                secBytesCount -= wav.format.bytes_p_second;
                second = total / wav.format.bytes_p_second;
                if (wmix->debug)
                    printf("  PLAY-WAV: %s %02d:%02d\r\n", wavPath, second / 60, second % 60);
            }
            if (head.U8 == 0)
            {
                if (wmix->debug)
                    printf("PLAY-WAV exit: head.U8 = 0\r\n");
                break;
            }
        }
        else if (interval > 0 || repeat > 0)
        {
            //配置了播放次数
            if (repeat > 0)
            {
                //已经是最后一次
                if (repeat == 1)
                    break;
                else
                    repeat -= 1;
                //没有配置播放间隔时,默认1秒
                if (interval < 1)
                {
                    interval = 1;
                    intervalMs = 1000;
                }
            }
            //关闭 reduceMode
            if (reduceSkip && wmix->reduceMode == reduce)
                wmix->reduceMode = 1;
            //播放位置重置(跳过wav文件头)
            lseek(fd, 44, SEEK_SET);
            //播放间隔延时,期间不忘检查msg消息和退出请求
            for (ret = 0; ret < intervalMs; ret += 100)
            {
                delayus(100000);
                //检查退出请求
                if (!wmix->run || loopWord != wmix->loopWord)
                    break;
                //检查消息状态
                if (msg_fd)
                {
                    //检查消息是否已被销毁
                    if (msgrcv(msg_fd, &msg, WMIX_MSG_BUFF_SIZE, 0, IPC_NOWAIT) < 1 && errno != ENOMSG)
                    {
                        if (wmix->debug)
                            printf("PLAY-WAV exit: %d msgrecv err/%d\r\n", msg_fd, errno);
                        break;
                    }
                }
            }
            //收到了退出请求
            if (ret < intervalMs)
            {
                ret = -1;
                break;
            }
            //重启 reduceMode
            if (reduceSkip && wmix->reduceMode == 1)
                wmix->reduceMode = reduce;

            if (wmix->debug)
                printf(
                    "<< PLAY-WAV: %s start >>\r\n"
                    "   通道数: %d\r\n"
                    "   采样位数: %d bit\r\n"
                    "   采样率: %d Hz\r\n"
                    "   每秒字节: %d Bytes\r\n"
                    "   重播间隔/次数: %d/%d\r\n"
                    "   tick: %d\r\n"
                    "   msgid: %d\r\n",
                    wavPath,
                    wav.format.channels,
                    wav.format.sample_length,
                    wav.format.sample_rate,
                    wav.format.bytes_p_second,
                    interval, repeat,
                    wmix->tick,
                    msg_fd);

            total = secBytesCount = 0;
            src.U8 = buff;
            head.U8 = wmix->head.U8;
            tick = 0;
        }
        else
            break;
    }

    close(fd);
    free(buff);

    if (wmix->debug)
        printf(">> PLAY-WAV: %s end <<\r\n", wavPath);
    //关闭 reduceMode
    if (reduceSkip && wmix->reduceMode == reduce)
        wmix->reduceMode = 1;
}

#if (MAKE_AAC)
void wmix_task_play_aac(
    WMix_Struct *wmix,
    char *aacPath,
    int msg_fd,
    uint8_t reduce,
    uint8_t interval,
    uint8_t repeat)
{
    int fd = 0;
    ssize_t ret = 0;
    //写循环缓冲区
    WMix_Point src, head;
    //播放计时
    uint32_t tick, total = 0, totalWait;
    uint32_t second = 0, secBytesCount = 0;
    //背景消减和重复播放延时
    uint8_t reduceSkip = 0; //自己不被背景削减
    int intervalMs = interval * 1000;
    //消息通信
    WMix_Msg msg;
    //aac解码句柄
    void *aacDec = NULL;
    uint8_t buff[8192];
    //音频基本参数
    uint8_t chn;
    uint16_t freq;
    int sample = 16;
    uint32_t secBytes;
    //系统更新loopWord时,会关闭该条播放
    int timeout;
    uint8_t loopWord;
    loopWord = wmix->loopWord;

    if (!wmix || !wmix->run || !aacPath)
        return;

    if ((fd = open(aacPath, O_RDONLY)) <= 0)
    {
        WMIX_ERR2("%s open err\r\n", aacPath);
        return;
    }
    //初始化解码器
    ret = aac_decode2(&aacDec, fd, buff, &chn, &freq);
    if (ret < 0)
    {
        WMIX_ERR("aac_decode2 err\r\n");
        close(fd);
        return;
    }

    //一秒字节数
    secBytes = chn * sample / 8 * freq;

    if (wmix->debug)
        printf(
            "<< PLAY-AAC: %s start >>\r\n"
            "   通道数: %d\r\n"
            "   采样位数: %d bit\r\n"
            "   采样率: %d Hz\r\n"
            "   每秒字节: %d Bytes\r\n"
            "   重播间隔/次数: %d/%d\r\n",
            aacPath, chn, sample, freq, secBytes, interval, repeat);

    //独占 reduceMode
    reduce += 1;
    if (reduce > 1 && wmix->reduceMode == 1)
    {
        //系统背景削减量以我为准
        wmix->reduceMode = reduce;
        reduceSkip = 1;
    }
    else
        reduce = 1;

    totalWait = WMIX_CHN * WMIX_SAMPLE / 8 * WMIX_FREQ / 2;

    src.U8 = buff;
    head.U8 = 0;
    tick = 0;

    while (wmix->run && loopWord == wmix->loopWord)
    {
        //检查消息队列是否被关闭
        if (msg_fd)
        {
            if (msgrcv(msg_fd, &msg, WMIX_MSG_BUFF_SIZE, 0, IPC_NOWAIT) < 1 && errno != ENOMSG)
            {
                if (wmix->debug)
                    printf("PLAY-AAC exit: %d msgrecv err/%d\r\n", msg_fd, errno);
                break;
            }
        }
        //播放文件
        if (ret > 0)
        {
            //等播放指针赶上写入进度
            timeout = 0;
            while (wmix->run && timeout++ < 200 &&
                   loopWord == wmix->loopWord &&
                   tick > wmix->tick &&
                   tick - wmix->tick > totalWait)
                delayus(5000);
            //是否有退出请求
            if (!wmix->run || loopWord != wmix->loopWord)
                break;
            //写入循环缓冲区
            head = wmix_load_data(
                wmix,
                src, ret,
                freq,
                chn,
                sample, head, reduce, &tick);
            //写入的总字节数统计
            secBytesCount += ret;
            total += ret;
            //播放时间
            if (secBytesCount > secBytes)
            {
                secBytesCount -= secBytes;
                second = total / secBytes;
                if (wmix->debug)
                    printf("  PLAY-AAC: %s %02d:%02d\r\n", aacPath, second / 60, second % 60);
            }
            if (head.U8 == 0)
                break;
        }
        else if (ret == 0)
            ;
        else if (interval > 0 || repeat > 0)
        {
            //配置了播放次数
            if (repeat > 0)
            {
                //已经是最后一次
                if (repeat == 1)
                    break;
                else
                    repeat -= 1;
                //没有配置播放间隔时,默认1秒
                if (interval < 1)
                {
                    interval = 1;
                    intervalMs = 1000;
                }
            }
            //关闭 reduceMode
            if (reduceSkip && wmix->reduceMode == reduce)
                wmix->reduceMode = 1;
            //播放位置重置
            lseek(fd, 0, SEEK_SET);
            //播放间隔延时,期间不忘检查msg消息和退出请求
            for (ret = 0; ret < intervalMs; ret += 100)
            {
                delayus(100000);
                //检查退出请求
                if (!wmix->run || loopWord != wmix->loopWord)
                    break;
                //检查消息队列是否被关闭
                if (msg_fd)
                {
                    if (msgrcv(msg_fd, &msg, WMIX_MSG_BUFF_SIZE, 0, IPC_NOWAIT) < 1 && errno != ENOMSG)
                    {
                        if (wmix->debug)
                            printf("PLAY-AAC exit: %d msgrecv err/%d\r\n", msg_fd, errno);
                        break;
                    }
                }
            }
            //是退出请求
            if (ret < intervalMs)
            {
                ret = -1;
                break;
            }
            //重启 reduceMode
            if (reduceSkip && wmix->reduceMode == 1)
                wmix->reduceMode = reduce;

            if (wmix->debug)
                printf("<< PLAY-AAC: %s start >>\n"
                       "   通道数: %d\n"
                       "   采样位数: %d bit\n"
                       "   采样率: %d Hz\n"
                       "   每秒字节: %d Bytes\n"
                       "   重播间隔/次数: %d/%d\r\n",
                       aacPath, chn, sample, freq, secBytes, interval, repeat);

            total = secBytesCount = 0;
            src.U8 = buff;
            head.U8 = wmix->head.U8;
            tick = 0;
        }
        else
            break;
        //解码
        ret = aac_decode2(&aacDec, fd, buff, &chn, &freq);
        if (ret < 0)
            break;
    }
    close(fd);
    if (aacDec)
        aac_decodeRelease(&aacDec);
    if (wmix->debug)
        printf(">> PLAY-AAC: %s end <<\r\n", aacPath);
    //关闭 reduceMode
    if (reduceSkip && wmix->reduceMode == reduce)
        wmix->reduceMode = 1;
}
#endif

#if (MAKE_MP3)
typedef struct
{
    // char *msgPath;//消息队列挂靠路径
    char *mp3Path;
    WMix_Msg msg;
    // key_t msg_key;
    int msg_fd;

    void *fdm;     //mmap首地址
    uint32_t seek; //fdm跳过多少才到mp3数据段
    uint32_t size; //实际mp3数据段长度

    WMix_Point head, src; //同步循环缓冲区的head指针 和 指向数据的src指针
    WMix_Struct *wmix;

    uint32_t tick;             //循环缓冲区head指针走过了多少字节,用于防止数据写如缓冲区太快超过head指针
    uint8_t loopWord;          //每个播放线程的循环标志都要与该值一致,否则循环结束,用于打断全局播放
    uint32_t total, totalWait; //total:文件读取字节数

    uint32_t bps, secBytesCount; //bps:每秒字节数 secBytesCount计数用于计算当前播放时间

    uint8_t reduce;
    uint8_t interval;
    int intervalMs;
    uint8_t repeat;
    uint8_t reduceSkip; //自己不被背景削减
} WMix_Mp3;

static int16_t mad_scale(mad_fixed_t sample)
{
    sample += (1L << (MAD_F_FRACBITS - 16));
    if (sample >= MAD_F_ONE)
        sample = MAD_F_ONE - 1;
    else if (sample < -MAD_F_ONE)
        sample = -MAD_F_ONE;
    return sample >> (MAD_F_FRACBITS + 1 - 16);
}

enum mad_flow mad_output(void *data, struct mad_header const *header, struct mad_pcm *pcm)
{
    WMix_Mp3 *wmm = (WMix_Mp3 *)data;
    uint32_t count = 0, second;
    int16_t *val = (int16_t *)&pcm->samples[0][count];
    int timeout;

    if (!wmm->wmix->run || wmm->loopWord != wmm->wmix->loopWord)
        return MAD_FLOW_STOP;
    //参数初始化
    if (wmm->head.U8 == 0)
    {
        wmm->bps = pcm->channels * 16 / 8 * header->samplerate;
        wmm->totalWait = WMIX_CHN * WMIX_SAMPLE / 8 * WMIX_FREQ / 2; //等半秒

        if (wmm->wmix->debug)
            printf(
                "<< PLAY-MP3: %s start >>\r\n"
                "   通道数: %d\r\n"
                "   采样位数: %d bit\r\n"
                "   采样率: %d Hz\r\n"
                "   每秒字节: %d Bytes\r\n"
                "   重播间隔/次数: %d/%d\r\n",
                wmm->mp3Path,
                pcm->channels, 16,
                header->samplerate,
                wmm->bps,
                wmm->interval, wmm->repeat);

        wmm->total = wmm->secBytesCount = 0;
        wmm->head.U8 = 0;
        wmm->tick = 0;
    }
    //检查消息队列是否被关闭
    if (wmm->msg_fd)
    {
        if (msgrcv(wmm->msg_fd,
                   &wmm->msg,
                   WMIX_MSG_BUFF_SIZE,
                   0, IPC_NOWAIT) < 1 &&
            errno != ENOMSG) //消息队列被关闭
        {
            if (wmm->wmix->debug)
                printf("PLAY-MP3 exit: %d msgrecv err/%d\r\n", wmm->msg_fd, errno);
            // break;
            return MAD_FLOW_STOP;
        }
    }

    if (pcm->channels == 2)
    {
        for (count = 0; count < pcm->length; count++)
        {
            *val++ = mad_scale(pcm->samples[0][count]);
            *val++ = mad_scale(pcm->samples[1][count]);
        }
        count = pcm->length * 4;
    }
    else if (pcm->channels == 1)
    {
        for (count = 0; count < pcm->length; count++)
        {
            *val++ = mad_scale(pcm->samples[0][count]);
            // *val++ = mad_scale(pcm->samples[1][count]);
        }
        count = pcm->length * 2;
    }
    else
        return MAD_FLOW_STOP;
    //等待消化
    timeout = 0;
    while (wmm->wmix->run && timeout++ < 200 &&
           wmm->loopWord == wmm->wmix->loopWord &&
           wmm->tick > wmm->wmix->tick &&
           wmm->tick - wmm->wmix->tick > wmm->totalWait)
        delayus(5000);
    if (!wmm->wmix->run || wmm->loopWord != wmm->wmix->loopWord)
        return MAD_FLOW_STOP;
    //写入到循环缓冲区
    wmm->src.U8 = (uint8_t *)&pcm->samples[0][0];
    wmm->head = wmix_load_data(
        wmm->wmix,
        wmm->src,
        count,
        header->samplerate,
        pcm->channels,
        16,
        wmm->head, wmm->reduce, &wmm->tick);
    //总字节数计数
    wmm->secBytesCount += count;
    wmm->total += count;
    //播放时间
    if (wmm->secBytesCount > wmm->bps)
    {
        wmm->secBytesCount -= wmm->bps;
        second = wmm->total / wmm->bps;
        if (wmm->wmix->debug)
            printf("  PLAY-MP3: %s %02d:%02d\r\n", wmm->mp3Path, second / 60, second % 60);
    }

    if (wmm->head.U8 == 0)
        return MAD_FLOW_STOP;
    return MAD_FLOW_CONTINUE;
}

enum mad_flow mad_input(void *data, struct mad_stream *stream)
{
    WMix_Mp3 *wmm = (WMix_Mp3 *)data;
    uint8_t count;
    if (wmm->size > 0)
    {
        mad_stream_buffer(stream, (const unsigned char *)wmm->fdm + wmm->seek, wmm->size);

        if (wmm->interval > 0 || wmm->repeat > 0)
        {
            //配置了播放次数
            if (wmm->repeat > 0)
            {
                //已经是最后一次
                if (wmm->repeat == 1)
                    return MAD_FLOW_STOP;
                else
                    wmm->repeat -= 1;
                //没有配置播放间隔时,默认1秒
                if (wmm->interval < 1)
                {
                    wmm->interval = 1;
                    wmm->intervalMs = 1000;
                }
            }
            //已经播放完一遍了
            if (wmm->head.U8)
            {
                //关闭 reduceMode
                if (wmm->reduceSkip && wmm->wmix->reduceMode == wmm->reduce)
                    wmm->wmix->reduceMode = 1;
                //播放间隔延时
                for (count = 0; count < wmm->intervalMs; count += 100)
                {
                    delayus(100000);
                    //检查退出请求
                    if (!wmm->wmix->run || wmm->loopWord != wmm->wmix->loopWord)
                        return MAD_FLOW_STOP;
                    //检查消息队列是否被关闭
                    if (wmm->msg_fd)
                    {
                        if (msgrcv(wmm->msg_fd, &wmm->msg, WMIX_MSG_BUFF_SIZE, 0, IPC_NOWAIT) < 1 && errno != ENOMSG)
                        {
                            if (wmm->wmix->debug)
                                printf("PLAY-MP3 exit: %d msgrecv err/%d\r\n", wmm->msg_fd, errno);
                            return MAD_FLOW_STOP;
                        }
                    }
                }
                //重启 reduceMode
                if (wmm->reduceSkip && wmm->wmix->reduceMode == 1)
                    wmm->wmix->reduceMode = wmm->reduce;
            }
        }
        else
            wmm->size = 0;

        wmm->head.U8 = 0;
        return MAD_FLOW_CONTINUE;
    }
    return MAD_FLOW_STOP;
}

enum mad_flow mad_error(void *data, struct mad_stream *stream, struct mad_frame *frame)
{
    WMIX_ERR2("decoding error 0x%04x (%s)\r\n",
              stream->error, mad_stream_errorstr(stream));
    return MAD_FLOW_CONTINUE;
}

void wmix_task_play_mp3(
    WMix_Struct *wmix,
    char *mp3Path,
    int msg_fd,
    uint8_t reduce,
    uint8_t interval,
    uint8_t repeat)
{
    WMix_Mp3 wmm;
    struct mad_decoder decoder;

    struct stat sta;
    int fd;

    //参数准备
    memset(&wmm, 0, sizeof(WMix_Mp3));
    wmm.wmix = wmix;
    wmm.mp3Path = mp3Path;
    wmm.interval = interval;
    wmm.intervalMs = interval * 1000;
    wmm.repeat = repeat;
    wmm.loopWord = wmix->loopWord;
    wmm.reduceSkip = 0;
    wmm.reduce = reduce + 1;
    wmm.msg_fd = msg_fd;

    if ((fd = open(mp3Path, O_RDONLY)) <= 0)
    {
        WMIX_ERR2("open %s err\r\n", mp3Path);
        return;
    }
    if (fstat(fd, &sta) == -1 || sta.st_size == 0)
    {
        WMIX_ERR2("stat %s err\r\n", mp3Path);
        close(fd);
        return;
    }

    //跳过id3标签
    wmm.seek = id3_len(mp3Path);
    wmm.size = sta.st_size - wmm.seek;

    wmm.fdm = mmap(0, sta.st_size, PROT_READ, MAP_SHARED, fd, 0);
    if (wmm.fdm == MAP_FAILED || !wmm.fdm)
    {
        WMIX_ERR("mmap err\r\n");
        close(fd);
        return;
    }

    //独占 reduceMode
    if (wmm.reduce > 1 && wmix->reduceMode == 1)
    {
        //系统背景削减量以我为准
        wmix->reduceMode = wmm.reduce;
        wmm.reduceSkip = 1;
    }
    else
        wmm.reduce = 1;

    //configure input, output, and error functions
    mad_decoder_init(&decoder, &wmm,
                     mad_input, 0 /* header */, 0 /* filter */, mad_output,
                     mad_error, 0 /* message */);

    //start decoding
    mad_decoder_run(&decoder, MAD_DECODER_MODE_SYNC);

    //release the decoder
    mad_decoder_finish(&decoder);

    //关闭 reduceMode
    if (wmm.reduceSkip && wmix->reduceMode == wmm.reduce)
        wmix->reduceMode = 1;

    close(fd);
    munmap(wmm.fdm, sta.st_size);

    if (wmix->debug)
        printf(">> PLAY-MP3: %s end <<\r\n", mp3Path);
}
#endif
