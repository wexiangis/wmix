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

//wav编码
#include "wav.h"
//g711和pcm互转编码
#include "g711codec.h"
//rtp协议和数据发收
#include "rtp.h"
//webrtc接口二次封装
#include "webrtc.h"
//speex接口二次封装
#include "speexlib.h"
//mp3编码
#if (WMIX_MP3)
#include "mad.h"
#include "id3.h"
#endif
//aac编码
#if (WMIX_AAC)
#include "aac.h"
#endif

void wmix_thread_play_wav_fifo(WMixThread_Param *wmtp)
{
    char *path = (char *)&wmtp->param[4];
    //
    uint8_t chn = wmtp->param[0];
    uint8_t sample = wmtp->param[1];
    uint16_t freq = (wmtp->param[2] << 8) | wmtp->param[3];
    //
    int fd_read;
    uint8_t *buff;
    uint32_t buffSize;
    //
    WMix_Point head, src;
    ssize_t ret, total = 0;
    // ssize_t totalWait;
    uint32_t tick, second = 0, bytes_p_second, bpsCount = 0;
    uint8_t rdce = ((wmtp->flag >> 8) & 0xFF) + 1, rdceIsMe = 0;
    //
    // int timeout;
    uint8_t loopWord;
    loopWord = wmtp->wmix->loopWordFifo;
    //
    if (mkfifo(path, 0666) < 0 && errno != EEXIST)
    {
        WMIX_ERR("mkfifo err\r\n");
        return;
    }
    //
    fd_read = open(path, O_RDONLY);
    //独占 reduceMode
    if (rdce > 1 && wmtp->wmix->reduceMode == 1)
    {
        wmtp->wmix->reduceMode = rdce;
        rdceIsMe = 1;
    }
    else
        rdce = 1;
    //
    bytes_p_second = chn * sample / 8 * freq;
    buffSize = bytes_p_second;
    // totalWait = bytes_p_second / 2;
    buff = (uint8_t *)calloc(buffSize, sizeof(uint8_t));
    //
    if (wmtp->wmix->debug)
        printf("<< FIFO-W: %s start >>\n   通道数: %d\n   采样位数: %d bit\n   采样率: %d Hz\n   每秒字节: %d Bytes\n\r\n",
               path, chn, sample, freq, bytes_p_second);
    //
    src.U8 = buff;
    head.U8 = 0;
    tick = 0;
    //线程计数
    wmtp->wmix->thread_play += 1;
    //
    while (wmtp->wmix->run && loopWord == wmtp->wmix->loopWordFifo)
    {
        ret = read(fd_read, buff, buffSize);
        if (ret > 0)
        {
            //等播放指针赶上写入进度
            // timeout = 0;
            // while(wmtp->wmix->run && timeout++ < 200 &&
            //     loopWord == wmtp->wmix->loopWordFifo &&
            //     tick > wmtp->wmix->tick &&
            //     tick - wmtp->wmix->tick > totalWait)
            //     delayus(5000);
            //
            head = wmix_load_data(
                wmtp->wmix,
                src, ret, freq, chn, sample, head, rdce, &tick);
            if (head.U8 == 0)
                break;
            //
            bpsCount += ret;
            total += ret;
            //播放时间
            if (bpsCount > bytes_p_second)
            {
                bpsCount -= bytes_p_second;
                second = total / bytes_p_second;
                if (wmtp->wmix->debug)
                    printf("  FIFO-W: %s %02d:%02d\r\n", path, second / 60, second % 60);
            }
            continue;
        }
        else if (errno != EAGAIN)
            break;
        //
        delayus(5000);
    }
    //
    if (wmtp->wmix->debug)
        printf(">> FIFO-W: %s end <<\r\n", path);
    //
    close(fd_read);
    //删除文件
    remove(path);
    //
    free(buff);
    //线程计数
    wmtp->wmix->thread_play -= 1;
    //
    if (wmtp->param)
        free(wmtp->param);
    //关闭 reduceMode
    if (rdceIsMe)
        wmtp->wmix->reduceMode = 1;
    free(wmtp);
}

void wmix_thread_record_wav_fifo(WMixThread_Param *wmtp)
{
    char *path = (char *)&wmtp->param[4];
    //
    uint8_t chn = wmtp->param[0];
    uint8_t sample = wmtp->param[1];
    uint16_t freq = (wmtp->param[2] << 8) | wmtp->param[3];
    //
    size_t buffSize, buffSize2, frame_size, count;
    WMix_Point src, dist;
    ssize_t ret, total = 0;
    uint32_t second = 0, bytes_p_second, bytes_p_second2, bpsCount = 0;
    float divCount, divPow;
    //
    int fd_write;
    int16_t record_addr = -1;
#if (WMIX_CHANNELS == 1)
    uint8_t buff[1024];
#else
    uint8_t buff[512];
#endif
    //
    uint8_t loopWord;
    loopWord = wmtp->wmix->loopWordFifo;
    //
    if (freq > WMIX_FREQ)
    {
        WMIX_ERR2("freq err, %dHz > %dHz(machine)\r\n", freq, WMIX_FREQ);
        return;
    }
    if (sample != WMIX_SAMPLE)
    {
        WMIX_ERR2("sample err, must be %dbit(machine)\r\n", WMIX_SAMPLE);
        return;
    }
    if (chn != 1 && chn != 2)
    {
        WMIX_ERR("channels err, must be 1 or 2\r\n");
        return;
    }
    //
    if (mkfifo(path, 0666) < 0 && errno != EEXIST)
    {
        WMIX_ERR("mkfifo err\r\n");
        return;
    }
    //
    fd_write = open(path, O_WRONLY);
    //
    bytes_p_second = WMIX_CHANNELS * WMIX_SAMPLE / 8 * WMIX_FREQ;
    bytes_p_second2 = chn * sample / 8 * freq;
    frame_size = WMIX_FRAME_SIZE;
#if (WMIX_CHANNELS == 1)
    buffSize = sizeof(buff) / 2;
#else
    buffSize = sizeof(buff);
#endif
    //
    divPow = (float)(WMIX_FREQ - freq) / freq;
    divCount = 0;
    //
    if (wmtp->wmix->debug)
        printf("<< FIFO-R: %s record >>\n   通道数: %d\n   采样位数: %d bit\n   采样率: %d Hz\n   每秒字节: %d Bytes\n   时间长度: -- sec\n\r\n",
               path, chn, sample, freq, bytes_p_second2);
    //线程计数
    wmtp->wmix->thread_record += 1;
    //
    while (wmtp->wmix->run && loopWord == wmtp->wmix->loopWordFifo)
    {
        ret = wmix_mem_read2((int16_t *)buff, buffSize / 2, &record_addr, false) * 2;
        if (ret > 0)
        {
            bpsCount += ret;
            total += ret;
            //录制时间
            if (bpsCount > bytes_p_second)
            {
                bpsCount -= bytes_p_second;
                second = total / bytes_p_second;
                if (wmtp->wmix->debug)
                    printf("  FIFO-R: %s %02d:%02d\r\n", path, second / 60, second % 60);
            }
            //
            if (!wmtp->wmix->run)
                break;
            //不同频率和通道数的数据处理
            RECORD_DATA_TRANSFER();
            //
            ret = write(fd_write, buff, buffSize2);
            if (ret < 0 && errno != EAGAIN)
                break;
        }
        else if (ret < 0)
        {
            WMIX_ERR2("read err %d\r\n", (int)ret);
            break;
        }
        else
        {
            delayus(500);
            fsync(fd_write);
        }
    }
    close(fd_write);
    //
    if (wmtp->wmix->debug)
        printf(">> FIFO-R: %s end <<\r\n", path);
    //删除文件
    remove(path);
    //线程计数
    wmtp->wmix->thread_record -= 1;
    //
    if (wmtp->param)
        free(wmtp->param);
    free(wmtp);
}

void wmix_thread_record_wav(WMixThread_Param *wmtp)
{
    char *path = (char *)&wmtp->param[6];
    //
    uint8_t chn = wmtp->param[0];
    uint8_t sample = wmtp->param[1];
    uint16_t freq = (wmtp->param[2] << 8) | wmtp->param[3];
    uint16_t duration_time = (wmtp->param[4] << 8) | wmtp->param[5];
    //
    size_t buffSize, buffSize2, frame_size, count;
    WMix_Point src, dist;
    ssize_t ret, total = 0;
    uint32_t second = 0, bytes_p_second, bytes_p_second2, bpsCount = 0, TOTAL;
    float divCount, divPow;
    //
    int fd;
    WAVContainer_t wav;
    int16_t record_addr = -1;
#if (WMIX_CHANNELS == 1)
    uint8_t buff[1024];
#else
    uint8_t buff[512];
#endif
    //
    uint8_t loopWord;
    loopWord = wmtp->wmix->loopWordRecord;
    //
    if (freq > WMIX_FREQ)
    {
        WMIX_ERR2("freq err, %dHz > %dHz(machine)\r\n", freq, WMIX_FREQ);
        return;
    }
    if (sample != WMIX_SAMPLE)
    {
        WMIX_ERR2("sample err, must be %dbit(machine)\r\n", WMIX_SAMPLE);
        return;
    }
    if (chn != 1 && chn != 2)
    {
        WMIX_ERR("channels err, must be 1 or 2\r\n");
        return;
    }
    //
    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd <= 0)
    {
        WMIX_ERR2("open %s err\r\n", path);
        return;
    }
    //
    WAV_Params(&wav, duration_time, chn, sample, freq);
    if (WAV_WriteHeader(fd, &wav) < 0)
    {
        close(fd);
        WMIX_ERR("WAV_WriteHeader err\r\n");
        return;
    }
    //
    bytes_p_second = WMIX_CHANNELS * WMIX_SAMPLE / 8 * WMIX_FREQ;
    bytes_p_second2 = chn * sample / 8 * freq;
    frame_size = WMIX_FRAME_SIZE;
#if (WMIX_CHANNELS == 1)
    buffSize = sizeof(buff) / 2;
#else
    buffSize = sizeof(buff);
#endif
    TOTAL = bytes_p_second * duration_time;
    //
    divPow = (float)(WMIX_FREQ - freq) / freq;
    divCount = 0;
    //
    if (wmtp->wmix->debug)
        printf("<< RECORD-WAV: %s record >>\n   通道数: %d\n   采样位数: %d bit\n   采样率: %d Hz\n   每秒字节: %d Bytes\n   时间长度: %d sec\n\r\n",
               path, chn, sample, freq, bytes_p_second2, duration_time);
    //线程计数
    wmtp->wmix->thread_record += 1;
    //
    while (wmtp->wmix->run && loopWord == wmtp->wmix->loopWordRecord)
    {
        //最后一帧
        if (total + buffSize >= TOTAL)
        {
            buffSize = TOTAL - total;
        }
        //
        ret = wmix_mem_read2((int16_t *)buff, buffSize / 2, &record_addr, false) * 2;
        if (ret > 0)
        {
            bpsCount += ret;
            total += ret;
            //录制时间
            if (bpsCount > bytes_p_second)
            {
                bpsCount -= bytes_p_second;
                second = total / bytes_p_second;
                if (wmtp->wmix->debug)
                    printf("  RECORD-WAV: %s %02d:%02d\r\n", path, second / 60, second % 60);
            }
            //
            if (!wmtp->wmix->run)
                break;
            //不同频率和通道数的数据处理
            RECORD_DATA_TRANSFER();
            //
            ret = write(fd, buff, buffSize2);
            if (ret < 0 && errno != EAGAIN)
            {
                WMIX_ERR2("write err %d\r\n", errno);
                break;
            }
            //
            if (total >= TOTAL)
                break;
        }
        else if (ret < 0)
        {
            WMIX_ERR2("read err %d\r\n", (int)ret);
            break;
        }
        else
            delayus(5000);
    }
    //
    close(fd);
    //
    if (wmtp->wmix->debug)
        printf(">> RECORD-WAV: %s end <<\r\n", path);
    //线程计数
    wmtp->wmix->thread_record -= 1;
    //
    if (wmtp->param)
        free(wmtp->param);
    free(wmtp);
}

#define WMIX_RTP_CTRL_MSG_RECV(thread_name)                  \
    if (msg_fd)                                              \
    {                                                        \
        if (msgrcv(msg_fd, &msg,                             \
                   WMIX_MSG_BUFF_SIZE, 0, IPC_NOWAIT) < 1)   \
        {                                                    \
            if (errno != ENOMSG)                             \
            {                                                \
                if (wmtp->wmix->debug)                       \
                    printf("%s exit: %d msgrecv err/%d\r\n", \
                           thread_name, msg_fd, errno);      \
                break;                                       \
            }                                                \
        }                                                    \
        else                                                 \
        {                                                    \
            if (wmtp->wmix->debug)                           \
                printf("%s: msg recv %ld\r\n",               \
                       thread_name, msg.type);               \
            ctrlType = msg.type & 0xFF;                      \
            if (ctrlType == WCT_RESET)                       \
            {                                                \
                rtpChain_reconnect(rcs);                     \
                ctrlType = WCT_CLEAR;                        \
            }                                                \
            else if (ctrlType == WCT_STOP)                   \
                break;                                       \
        }                                                    \
    }

#if (WMIX_AAC)
void wmix_thread_record_aac(WMixThread_Param *wmtp)
{
    char *path = (char *)&wmtp->param[6];
    //
    uint8_t chn = wmtp->param[0];
    uint8_t sample = wmtp->param[1];
    uint16_t freq = (wmtp->param[2] << 8) | wmtp->param[3];
    uint16_t duration_time = (wmtp->param[4] << 8) | wmtp->param[5];
    //
    size_t buffSize, buffSize2, frame_size, count, buffSizeR;
    WMix_Point src, dist;
    ssize_t ret, total = 0;
    uint32_t second = 0, bytes_p_second, bytes_p_second2, bpsCount = 0, TOTAL;
    float divCount, divPow;
    //
    int fd;
    //
    void *aacEnc = NULL;
    uint8_t *buff, *buff2, *pBuff2_S, *pBuff2_E;
    int16_t record_addr = -1;
    //
    uint8_t aacBuff[4096];
    //
    uint8_t loopWord;
    loopWord = wmtp->wmix->loopWordRecord;
    //
    if (freq > WMIX_FREQ)
    {
        WMIX_ERR2("freq err, %dHz > %dHz(machine)\r\n", freq, WMIX_FREQ);
        return;
    }
    if (sample != WMIX_SAMPLE)
    {
        WMIX_ERR2("sample err, must be %dbit(machine)\r\n", WMIX_SAMPLE);
        return;
    }
    if (chn != 1 && chn != 2)
    {
        WMIX_ERR("channels err, must be 1 or 2\r\n");
        return;
    }
    //
    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd <= 0)
    {
        WMIX_ERR2("open %s err\r\n", path);
        return;
    }
    //
    bytes_p_second = WMIX_CHANNELS * WMIX_SAMPLE / 8 * WMIX_FREQ;
    bytes_p_second2 = chn * sample / 8 * freq;
    frame_size = WMIX_FRAME_SIZE;
    //
    buffSize = WMIX_CHANNELS * WMIX_SAMPLE / 8 * 1024;
    buffSizeR = chn * sample / 8 * 1024;
    //
    buff = malloc(2 * WMIX_SAMPLE / 8 * 1024);
    buff2 = malloc(buffSizeR * 2);
    pBuff2_S = pBuff2_E = buff2;
    //
    TOTAL = bytes_p_second * duration_time;
    //
    divPow = (float)(WMIX_FREQ - freq) / freq;
    divCount = 0;
    //
    if (wmtp->wmix->debug)
        printf("<< RECORD-AAC: %s record >>\n   通道数: %d\n   采样位数: %d bit\n   采样率: %d Hz\n   每秒字节: %d Bytes\n   时间长度: %d sec\n\r\n",
               path, chn, sample, freq, bytes_p_second2, duration_time);
    //线程计数
    wmtp->wmix->thread_record += 1;
    //
    while (wmtp->wmix->run && loopWord == wmtp->wmix->loopWordRecord)
    {
        //最后一帧
        if (total + buffSize >= TOTAL)
            buffSize = TOTAL - total;
        //
        ret = wmix_mem_read2((int16_t *)buff, buffSize / 2, &record_addr, true) * 2;
        if (ret > 0)
        {
            bpsCount += ret;
            total += ret;
            //录制时间
            if (bpsCount > bytes_p_second)
            {
                bpsCount -= bytes_p_second;
                second = total / bytes_p_second;
                if (wmtp->wmix->debug)
                    printf("  RECORD-AAC: %s %02d:%02d\r\n", path, second / 60, second % 60);
            }
            //
            if (!wmtp->wmix->run)
                break;
            //不同频率和通道数的数据处理
            RECORD_DATA_TRANSFER();
            //
            memcpy(pBuff2_E, buff, buffSize2);
            pBuff2_E += buffSize2;
            //
            while (pBuff2_E - pBuff2_S >= buffSizeR)
            {
                ret = aac_encode(&aacEnc, pBuff2_S, buffSizeR, aacBuff, 4096, chn, freq);
                if (ret > 0)
                {
                    ret = write(fd, aacBuff, ret);
                    if (ret < 0 && errno != EAGAIN)
                    {
                        WMIX_ERR2("write err %d\r\n", errno);
                        break;
                    }
                }
                pBuff2_S += buffSizeR;
            }
            if (ret < 0 && errno != EAGAIN)
                break;
            //
            memcpy(buff2, pBuff2_S, (size_t)(pBuff2_E - pBuff2_S));
            pBuff2_E = &buff2[pBuff2_E - pBuff2_S];
            pBuff2_S = buff2;
            //
            if (total >= TOTAL)
                break;
        }
        else if (ret < 0)
        {
            WMIX_ERR2("read err %d\r\n", (int)ret);
            break;
        }
        else
            delayus(10000);
    }
    //
    close(fd);
    free(buff);
    free(buff2);
    //
    if (wmtp->wmix->debug)
        printf(">> RECORD-AAC: %s end <<\r\n", path);
    //线程计数
    wmtp->wmix->thread_record -= 1;
    //
    if (aacEnc)
        aac_encodeRelease(&aacEnc);
    if (wmtp->param)
        free(wmtp->param);
    free(wmtp);
}

void wmix_thread_rtp_send_aac(WMixThread_Param *wmtp)
{
    char *path = (char *)&wmtp->param[11];
    char *msgPath;
    key_t msg_key;
    int msg_fd = 0;
    FILE *fp;
    WMix_Msg msg;
    //
    uint8_t chn = wmtp->param[0];
    uint8_t sample = wmtp->param[1];
    uint16_t freq = (wmtp->param[2] << 8) | wmtp->param[3];
    uint16_t port = (wmtp->param[4] << 8) | wmtp->param[5];
    bool bindMode = wmtp->param[6] ? true : false;
    // int reserve = (wmtp->param[7] << 24) | (wmtp->param[8] << 16) | (wmtp->param[9] << 8) | wmtp->param[10];
    //
    size_t buffSize, buffSizeR, buffSize2, frame_size, count;
    WMix_Point src, dist;
    ssize_t ret, total = 0;
    uint32_t second = 0, bytes_p_second, bytes_p_second2, bpsCount = 0;
    float divCount, divPow;
    //
    int16_t record_addr = -1;
    uint8_t *buff, *buff2, *pBuff2_S, *pBuff2_E;
    void *aacEnc = NULL;
    uint8_t aacbuff[4096];
    //
    DELAY_INIT;
    //
    RtpChain_Struct *rcs;
    RtpPacket rtpPacket;
    long ctrlType = 0;
    //
    uint8_t loopWord;
    loopWord = wmtp->wmix->loopWordRtp;
    //参数检查,是否在允许的变参范围内
    // if (freq > WMIX_FREQ)
    // {
    //     WMIX_ERR2("freq err, %dHz > %dHz(machine)\r\n", freq, WMIX_FREQ);
    //     return;
    // }
    if (sample != WMIX_SAMPLE)
    {
        WMIX_ERR2("sample err, must be %dbit(machine)\r\n", WMIX_SAMPLE);
        return;
    }
    if (chn != 1 && chn != 2)
    {
        WMIX_ERR("channels err, must be 1 or 2\r\n");
        return;
    }
    //初始化rtp
    rcs = rtpChain_get(path, port, true, bindMode);
    if (!rcs)
    {
        WMIX_ERR("rtpChain_get: err\r\n");
        return;
    }
    rtp_header(&rtpPacket, 0, 0, 0, RTP_VESION, RTP_PAYLOAD_TYPE_AAC, 1, 0, 0, 0x32411);
    //初始化消息
    msgPath = (char *)&wmtp->param[strlen(path) + 11 + 1];
    if (msgPath && msgPath[0])
    {
        //创建消息挂靠路径
        if (access(msgPath, F_OK) != 0)
            creat(msgPath, 0666);
        //写节点描述
        if ((fp = fopen(msgPath, "w")))
        {
            fprintf(fp, "rtp send aac %s:%d", path, port);
            fclose(fp);
        }
        //创建消息
        if ((msg_key = ftok(msgPath, WMIX_MSG_ID)) > 0)
            msg_fd = msgget(msg_key, IPC_CREAT | 0666);
    }
    else
        msgPath = NULL;
    //生成sdp文件
    rtp_create_sdp(
        "/tmp/record-aac.sdp",
        path, port, chn, freq,
        RTP_PAYLOAD_TYPE_AAC);
    //
    bytes_p_second = WMIX_CHANNELS * WMIX_SAMPLE / 8 * WMIX_FREQ;
    bytes_p_second2 = chn * sample / 8 * freq;
    frame_size = WMIX_FRAME_SIZE;
    //每次从ai读取字节数
    buffSize = WMIX_CHANNELS * WMIX_SAMPLE / 8 * 1024;
    buffSizeR = chn * sample / 8 * 1024;
    //
    buff = malloc(2 * WMIX_SAMPLE / 8 * 1024);
    buff2 = malloc(buffSizeR * 2);
    pBuff2_S = pBuff2_E = buff2;
    //
    divPow = (float)(WMIX_FREQ - freq) / freq;
    divCount = 0;
    //
    if (wmtp->wmix->debug)
        printf(
            "<< RTP-SEND-AAC: %s:%d start >>\r\n"
            "   通道数: %d\r\n"
            "   采样位数: %d bit\r\n"
            "   采样率: %d Hz\r\n"
            "   每秒字节: %d Bytes\r\n",
            path, port, chn, sample, freq, bytes_p_second2);
    //线程计数
    wmtp->wmix->thread_record += 1;
    //
    while (wmtp->wmix->run && loopWord == wmtp->wmix->loopWordRtp)
    {
        //msg 检查
        WMIX_RTP_CTRL_MSG_RECV("RTP-SEND-AAC");
        //
        DELAY_RESET();
        //
        ret = wmix_mem_read2((int16_t *)buff, buffSize / 2, &record_addr, true) * 2;
        if (ret > 0)
        {
            //使用0数据
            if (ctrlType == WCT_SILENCE)
                memset(buff, 0, ret);
            //录制时间
            bpsCount += ret;
            total += ret;
            if (bpsCount > bytes_p_second)
            {
                bpsCount -= bytes_p_second;
                second = total / bytes_p_second;
                if (wmtp->wmix->debug)
                    printf("  RTP-SEND-AAC: %s:%d %02d:%02d\r\n", path, port, second / 60, second % 60);
            }
            //
            if (!wmtp->wmix->run)
                break;
            //不同频率和通道数的数据处理
            RECORD_DATA_TRANSFER();
            //
            memcpy(pBuff2_E, buff, buffSize2);
            pBuff2_E += buffSize2;
            //
            while (pBuff2_E - pBuff2_S >= buffSizeR)
            {
                ret = aac_encode(&aacEnc, pBuff2_S, buffSizeR, aacbuff, sizeof(aacbuff), chn, freq);
                if (ret > 0)
                {
                    ret -= 7;
                    memcpy(&rtpPacket.payload[4], &aacbuff[7], ret);
                    pthread_mutex_lock(&rcs->lock);
                    //bindMode时,作为主机的一端必须先收到数据才开始发送数据
                    // if(rcs->bindMode && rcs->recv_run && rcs->flagRecv == 0)
                    //     ;
                    // else
                    ret = rtp_send(rcs->ss, &rtpPacket, ret);
                    pthread_mutex_unlock(&rcs->lock);
                    if (ret < 0)
                    {
                        // WMIX_ERR("rtp_send err !!\r\n");
                        delayus(1000000);
                        //重连
                        rtpChain_reconnect(rcs);
                        break;
                    }
                    //理论上无须延时,阻塞取数据足矣
                    DELAY_US(18000);
                }
                pBuff2_S += buffSizeR;
            }
            if (ret < 0)
                break;
            //
            memcpy(buff2, pBuff2_S, (size_t)(pBuff2_E - pBuff2_S));
            pBuff2_E = &buff2[pBuff2_E - pBuff2_S];
            pBuff2_S = buff2;
        }
        else if (ret < 0)
        {
            WMIX_ERR2("read mem err %d\r\n", (int)ret);
            break;
        }
    }
    //
    if (wmtp->wmix->debug)
        printf(">> RTP-SEND-AAC: %s:%d end <<\r\n", path, port);
    //线程计数
    wmtp->wmix->thread_record -= 1;
    //
    if (msg_fd)
        msgctl(msg_fd, IPC_RMID, NULL);
    if (msgPath)
        remove(msgPath);
    free(buff);
    free(buff2);
    rtpChain_release(rcs, true);
    //
    if (aacEnc)
        aac_encodeRelease(&aacEnc);
    if (wmtp->param)
        free(wmtp->param);
    free(wmtp);
}

void wmix_thread_rtp_recv_aac(WMixThread_Param *wmtp)
{
    char *path = (char *)&wmtp->param[11];
    char *msgPath;
    key_t msg_key;
    int msg_fd = 0;
    FILE *fp;
    WMix_Msg msg;
    //
    uint8_t chn = wmtp->param[0];
    uint8_t sample = wmtp->param[1];
    uint16_t freq = (wmtp->param[2] << 8) | wmtp->param[3];
    uint16_t port = (wmtp->param[4] << 8) | wmtp->param[5];
    bool bindMode = wmtp->param[6] ? true : false;
    // int reserve = (wmtp->param[7] << 24) | (wmtp->param[8] << 16) | (wmtp->param[9] << 8) | wmtp->param[10];
    uint32_t bytes_p_second;
    int chnInt, freqInt; //用于decode参数
    //
    int ret = 0;
    uint8_t buff[8192];
    WMix_Point head, src;
    uint32_t tick, total = 0;
    uint32_t second = 0, bpsCount = 0;
    uint8_t rdce = ((wmtp->flag >> 8) & 0xFF) + 1, rdceIsMe = 0;
    //aac解码句柄
    void *aacDec = NULL;
    int datUse = 0;
    uint8_t aacBuff[4096];
    //
    RtpChain_Struct *rcs;
    RtpPacket rtpPacket;
    long ctrlType = 0;
    int retSize;
    int recv_timeout = 0;
    int intervalUs = 10000;
    //
    uint8_t loopWord;
    loopWord = wmtp->wmix->loopWordRtp;
    //初始化rtp
    rcs = rtpChain_get(path, port, false, bindMode);
    if (!rcs)
    {
        WMIX_ERR("rtpChain_get: err\r\n");
        return;
    }
    //初始化消息
    msgPath = (char *)&wmtp->param[strlen(path) + 11 + 1];
    if (msgPath && msgPath[0])
    {
        //创建消息挂靠路径
        if (access(msgPath, F_OK) != 0)
            creat(msgPath, 0666);
        //写节点描述
        if ((fp = fopen(msgPath, "w")))
        {
            fprintf(fp, "rtp recv aac %s:%d", path, port);
            fclose(fp);
        }
        //创建消息
        if ((msg_key = ftok(msgPath, WMIX_MSG_ID)) > 0)
            msg_fd = msgget(msg_key, IPC_CREAT | 0666);
    }
    else
        msgPath = NULL;
    //独占 reduceMode
    if (rdce > 1 && wmtp->wmix->reduceMode == 1)
    {
        wmtp->wmix->reduceMode = rdce;
        rdceIsMe = 1;
    }
    else
        rdce = 1;
    //默认缓冲区大小设为1秒字播放字节数
    bytes_p_second = chn * sample / 8 * freq;
    //
    if (wmtp->wmix->debug)
        printf(
            "<< RTP-RECV-AAC: %s:%d start >>\r\n"
            "   通道数: %d\r\n"
            "   采样位数: %d bit\r\n"
            "   采样率: %d Hz\r\n",
            path, port, chn, sample, freq);
    //
    src.U8 = buff;
    head.U8 = 0;
    tick = 0;
    chnInt = chn;
    freqInt = freq;
    //线程计数
    wmtp->wmix->thread_play += 1;
    //
    while (wmtp->wmix->run && loopWord == wmtp->wmix->loopWordRtp)
    {
        //msg 检查
        WMIX_RTP_CTRL_MSG_RECV("RTP-RECV-AAC");
        //往aacBuff读入数据
        pthread_mutex_lock(&rcs->lock);
        ret = rtp_recv(rcs->ss, &rtpPacket, (uint32_t *)&retSize);
        pthread_mutex_unlock(&rcs->lock);
        if (ret > 0 && retSize > 0)
        {
            aac_createHeader(aacBuff, chnInt, freqInt, 0x7FF, retSize);
            memcpy(&aacBuff[7], &rtpPacket.payload[4], retSize);
            ret = aac_decode(
                &aacDec,
                aacBuff, retSize + 7,
                buff, &datUse,
                &chnInt, &freqInt);
            if (ret < 0)
                WMIX_ERR2("aac_decode err %d !!\r\n", ret);
            //自动纠正参数(功能有限)
            if (chnInt != chn || freqInt != freq)
            {
                chn = chnInt;
                freq = freqInt;
                //默认缓冲区大小设为1秒字播放字节数
                bytes_p_second = chn * sample / 8 * freq;
                //
                if (wmtp->wmix->debug)
                    printf(
                        "<< RTP-RECV-AAC: %s:%d start >>\r\n"
                        "   通道数: %d\r\n"
                        "   采样位数: %d bit\r\n"
                        "   采样率: %d Hz\r\n",
                        path, port, chn, sample, freq);
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
                rtpChain_reconnect(rcs);
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
            //
            if (!wmtp->wmix->run)
                break;
            //写入循环缓冲区
            head = wmix_load_data(
                wmtp->wmix,
                src, ret,
                freq,
                chn,
                sample, head, rdce, &tick);
            //写入的总字节数统计
            bpsCount += ret;
            total += ret;
            //播放时间
            if (bpsCount > bytes_p_second)
            {
                bpsCount -= bytes_p_second;
                second = total / bytes_p_second;
                if (wmtp->wmix->debug)
                    printf("  RTP-RECV-AAC: %s:%d %02d:%02d\r\n", path, port, second / 60, second % 60);
            }
            continue;
        }
        else
            delayus(intervalUs);
    }
    //
    if (wmtp->wmix->debug)
        printf(">> RTP-RECV-AAC: %s:%d end <<\r\n", path, port);
    //删除文件
    if (msg_fd)
        msgctl(msg_fd, IPC_RMID, NULL);
    if (msgPath)
        remove(msgPath);
    rtpChain_release(rcs, false);
    if (aacDec)
        aac_decodeRelease(&aacDec);
    //线程计数
    wmtp->wmix->thread_play -= 1;
    //
    if (wmtp->param)
        free(wmtp->param);
    //关闭 reduceMode
    if (rdceIsMe)
        wmtp->wmix->reduceMode = 1;
    free(wmtp);
}
#endif //if(WMIX_AAC)

void wmix_thread_rtp_send_pcma(WMixThread_Param *wmtp)
{
    char *path = (char *)&wmtp->param[11];
    char *msgPath;
    key_t msg_key;
    int msg_fd = 0;
    FILE *fp;
    WMix_Msg msg;
    //
    uint8_t chn = wmtp->param[0];
    uint8_t sample = wmtp->param[1];
    uint16_t freq = (wmtp->param[2] << 8) | wmtp->param[3];
    uint16_t port = (wmtp->param[4] << 8) | wmtp->param[5];
    bool bindMode = wmtp->param[6] ? true : false;
    // int reserve = (wmtp->param[7] << 24) | (wmtp->param[8] << 16) | (wmtp->param[9] << 8) | wmtp->param[10];
    //
    size_t buffSize;
    // WMix_Point src, dist;
    ssize_t ret = 0, total = 0;
    uint32_t second = 0, bytes_p_second, bpsCount = 0;
    //
    RtpChain_Struct *rcs;
    RtpPacket rtpPacket;
    long ctrlType = 0;
    //
    DELAY_INIT2;
    //
    int16_t record_addr = -1;
    uint8_t *buff;
    //
    uint8_t loopWord;
    loopWord = wmtp->wmix->loopWordRecord;
    //参数检查,是否在允许的变参范围内
    if (freq != 8000)
    {
        WMIX_ERR("freq warnning, must be 8000Hz \r\n");
        freq = 8000;
    }
    if (sample != WMIX_SAMPLE)
    {
        WMIX_ERR2("sample err, must be %dbit(machine)\r\n", WMIX_SAMPLE);
        return;
    }
    if (chn != 1 && chn != 2)
    {
        WMIX_ERR("channels err, must be 1 or 2\r\n");
        return;
    }
    //初始化rtp
    rcs = rtpChain_get(path, port, true, bindMode);
    if (!rcs)
    {
        WMIX_ERR("rtpChain_get: err\r\n");
        return;
    }
    rtp_header(&rtpPacket, 0, 0, 0, RTP_VESION, RTP_PAYLOAD_TYPE_PCMA, 1, 0, 0, 0);
    //初始化消息
    msgPath = (char *)&wmtp->param[strlen(path) + 11 + 1];
    if (msgPath && msgPath[0])
    {
        //创建消息挂靠路径
        if (access(msgPath, F_OK) != 0)
            creat(msgPath, 0666);
        //写节点描述
        if ((fp = fopen(msgPath, "w")))
        {
            fprintf(fp, "rtp send pcma %s:%d", path, port);
            fclose(fp);
        }
        //创建消息
        if ((msg_key = ftok(msgPath, WMIX_MSG_ID)) > 0)
            msg_fd = msgget(msg_key, IPC_CREAT | 0666);
    }
    else
        msgPath = NULL;
    //生成sdp文件
    rtp_create_sdp(
        "/tmp/record.sdp",
        path, port, chn, freq,
        RTP_PAYLOAD_TYPE_PCMA);
    //
    bytes_p_second = chn * sample / 8 * freq;
    //每次从ai读取字节数
    buffSize = 320;
    buff = malloc(2 * WMIX_SAMPLE / 8 * 1024);
    //
    if (wmtp->wmix->debug)
        printf(
            "<< RTP-SEND-PCM: %s:%d start >>\r\n"
            "   通道数: %d\r\n"
            "   采样位数: %d bit\r\n"
            "   采样率: %d Hz\r\n",
            path, port, chn, sample, freq);
    //线程计数
    wmtp->wmix->thread_record += 1;
    //
    while (wmtp->wmix->run && loopWord == wmtp->wmix->loopWordRecord)
    {
        //发数据
        if (ret > 0)
        {
            //逐级逼近20ms延时
            DELAY_US2(19500, 50);
            //使用0数据
            if (ctrlType == WCT_SILENCE)
                memset(buff, 0, ret);
            //录制时间
            bpsCount += ret;
            total += ret;
            if (bpsCount > bytes_p_second)
            {
                bpsCount -= bytes_p_second;
                second = total / bytes_p_second;
                if (wmtp->wmix->debug)
                    printf("  RTP-SEND-PCM: %s:%d %02d:%02d\r\n", path, port, second / 60, second % 60);
            }
            //把buff数据转换后存到rtpPacket.payload
            //注意参数ret是按char计算长度,返回ret都是按int16计算的长度
            ret = PCM2G711a((char *)buff, (char *)rtpPacket.payload, ret, 0);
            rtpPacket.rtpHeader.timestamp += ret;
            pthread_mutex_lock(&rcs->lock);
            //bindMode时,作为主机的一端必须先收到数据才开始发送数据
            // if(rcs->bindMode && rcs->recv_run && rcs->flagRecv == 0)
            //     ;
            // else
            ret = rtp_send(rcs->ss, &rtpPacket, ret);
            pthread_mutex_unlock(&rcs->lock);
            if (ret < 0)
            {
                // WMIX_ERR("rtp_send err !!\r\n");
                delayus(1000000);
                //重连
                rtpChain_reconnect(rcs);
                continue;
            }
        }
        //发完数据,趁空闲立即取数据
        ret = wmix_mem_read((int16_t *)buff, buffSize / 2, &record_addr, true) * 2;
        //msg 检查
        WMIX_RTP_CTRL_MSG_RECV("RTP-SEND-PCM");
    }

    if (wmtp->wmix->debug)
        printf(">> RTP-SEND-PCM: %s:%d end <<\r\n", path, port);
    //线程计数
    wmtp->wmix->thread_record -= 1;

    if (msg_fd)
        msgctl(msg_fd, IPC_RMID, NULL);
    if (msgPath)
        remove(msgPath);
    rtpChain_release(rcs, true);

    if (wmtp->param)
        free(wmtp->param);
    free(wmtp);
}

void wmix_thread_rtp_recv_pcma(WMixThread_Param *wmtp)
{
    char *path = (char *)&wmtp->param[11];
    char *msgPath;
    key_t msg_key;
    int msg_fd = 0;
    FILE *fp;
    WMix_Msg msg;
    //
    uint8_t chn = wmtp->param[0];
    uint8_t sample = wmtp->param[1];
    uint16_t freq = (wmtp->param[2] << 8) | wmtp->param[3];
    uint16_t port = (wmtp->param[4] << 8) | wmtp->param[5];
    bool bindMode = wmtp->param[6] ? true : false;
    // int reserve = (wmtp->param[7] << 24) | (wmtp->param[8] << 16) | (wmtp->param[9] << 8) | wmtp->param[10];
    uint32_t bytes_p_second;
    //
    int ret = 0;
    uint8_t buff[1024];
    WMix_Point head, src;
    uint32_t tick, total = 0;
    uint32_t second = 0, bpsCount = 0;
    uint8_t rdce = ((wmtp->flag >> 8) & 0xFF) + 1, rdceIsMe = 0;
    //
    RtpChain_Struct *rcs;
    RtpPacket rtpPacket;
    long ctrlType = 0;
    int retSize;
    int recv_timeout = 0;
    int intervalUs = 8000;
    //
    uint8_t loopWord;
    loopWord = wmtp->wmix->loopWordRtp;
    //初始化rtp
    rcs = rtpChain_get(path, port, false, bindMode);
    if (!rcs)
    {
        WMIX_ERR("rtpChain_get: err\r\n");
        return;
    }
    //初始化消息
    msgPath = (char *)&wmtp->param[strlen(path) + 11 + 1];
    if (msgPath && msgPath[0])
    {
        //创建消息挂靠路径
        if (access(msgPath, F_OK) != 0)
            creat(msgPath, 0666);
        //写节点描述
        if ((fp = fopen(msgPath, "w")))
        {
            fprintf(fp, "rtp recv pcma %s:%d", path, port);
            fclose(fp);
        }
        //创建消息
        if ((msg_key = ftok(msgPath, WMIX_MSG_ID)) > 0)
            msg_fd = msgget(msg_key, IPC_CREAT | 0666);
    }
    else
        msgPath = NULL;
    //独占 reduceMode
    if (rdce > 1 && wmtp->wmix->reduceMode == 1)
    {
        wmtp->wmix->reduceMode = rdce;
        rdceIsMe = 1;
    }
    else
        rdce = 1;
    //默认缓冲区大小设为1秒字播放字节数
    bytes_p_second = chn * sample / 8 * freq;
    //
    if (wmtp->wmix->debug)
        printf(
            "<< RTP-RECV-PCM: %s:%d start >>\r\n"
            "   通道数: %d\r\n"
            "   采样位数: %d bit\r\n"
            "   采样率: %d Hz\r\n",
            path, port, chn, sample, freq);
    //
    src.U8 = buff;
    head.U8 = 0;
    tick = 0;
    //线程计数
    wmtp->wmix->thread_play += 1;
    //
    while (wmtp->wmix->run && loopWord == wmtp->wmix->loopWordRtp)
    {
        //msg 检查
        WMIX_RTP_CTRL_MSG_RECV("RTP-RECV-PCM");
        //
        if (!wmtp->wmix->run)
            break;
        //读rtp数据
        pthread_mutex_lock(&rcs->lock);
        ret = rtp_recv(rcs->ss, &rtpPacket, (uint32_t *)&retSize);
        pthread_mutex_unlock(&rcs->lock);
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
                rtpChain_reconnect(rcs);
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
                sample, head, rdce, &tick);
            //写入的总字节数统计
            bpsCount += ret;
            total += ret;
            //播放时间
            if (bpsCount > bytes_p_second)
            {
                bpsCount -= bytes_p_second;
                second = total / bytes_p_second;
                if (wmtp->wmix->debug)
                    printf("  RTP-RECV-PCM: %s:%d %02d:%02d\r\n", path, port, second / 60, second % 60);
            }
            continue;
        }
        else
            delayus(intervalUs);
    }
    //
    if (wmtp->wmix->debug)
        printf(">> RTP-RECV-PCM: %s:%d end <<\r\n", path, port);
    //删除文件
    if (msg_fd)
        msgctl(msg_fd, IPC_RMID, NULL);
    if (msgPath)
        remove(msgPath);
    rtpChain_release(rcs, false);
    //线程计数
    wmtp->wmix->thread_play -= 1;
    //
    if (wmtp->param)
        free(wmtp->param);
    //关闭 reduceMode
    if (rdceIsMe)
        wmtp->wmix->reduceMode = 1;
    free(wmtp);
}

void wmix_task_play_wav(
    WMix_Struct *wmix,
    char *wavPath,
    int msg_fd,
    uint8_t reduce,
    uint8_t repeatInterval)
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
    uint32_t second = 0, bpsCount = 0;
    //背景消减和重复播放延时
    uint8_t rdce = reduce + 1, rdceIsMe = 0;
    uint16_t repeat = (uint16_t)repeatInterval * 10;
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
            "   重播间隔: %d sec\r\n"
            "   tick: %d\r\n"
            "   msgid: %d\r\n",
            wavPath,
            wav.format.channels,
            wav.format.sample_length,
            wav.format.sample_rate,
            wav.format.bytes_p_second,
            repeat / 10,
            wmix->tick,
            msg_fd);
    //独占 reduceMode
    if (rdce > 1 && wmix->reduceMode == 1)
    {
        wmix->reduceMode = rdce;
        rdceIsMe = 1;
    }
    else
        rdce = 1;
    //默认缓冲区大小设为1秒字播放字节数
    buffSize = wav.format.bytes_p_second;
    buffSize2 = WMIX_CHANNELS * WMIX_SAMPLE / 8 * WMIX_FREQ;
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

    buff = (uint8_t *)calloc(buffSize, sizeof(uint8_t));

    src.U8 = buff;
    head.U8 = 0;
    tick = 0;

    while (wmix->run && loopWord == wmix->loopWord)
    {
        //msg 检查
        if (msg_fd)
        {
            if (msgrcv(msg_fd, &msg,
                       WMIX_MSG_BUFF_SIZE,
                       0, IPC_NOWAIT) < 1 &&
                errno != ENOMSG) //消息队列被关闭
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
                wav.format.sample_length, head, rdce, &tick);
            //写入的总字节数统计
            bpsCount += ret;
            total += ret;
            //播放时间
            if (bpsCount > wav.format.bytes_p_second)
            {
                bpsCount -= wav.format.bytes_p_second;
                second = total / wav.format.bytes_p_second;
                if (wmix->debug)
                    printf("  PLAY-WAV: %s %02d:%02d\r\n", wavPath, second / 60, second % 60);
            }
            //
            if (head.U8 == 0)
            {
                if (wmix->debug)
                    printf("PLAY-WAV exit: head.U8 = 0\r\n");
                break;
            }
        }
        else if (repeat)
        {
            //关闭 reduceMode
            if (rdceIsMe && wmix->reduceMode == rdce)
                wmix->reduceMode = 1;
            //
            lseek(fd, 44, SEEK_SET);
            //
            for (ret = 0; ret < repeat; ret++)
            {
                delayus(100000);
                //
                if (!wmix->run || loopWord != wmix->loopWord)
                    break;
                //
                if (msg_fd)
                {
                    if (msgrcv(msg_fd, &msg,
                               WMIX_MSG_BUFF_SIZE,
                               0, IPC_NOWAIT) < 1 &&
                        errno != ENOMSG) //消息队列被关闭
                    {
                        if (wmix->debug)
                            printf("PLAY-WAV exit: %d msgrecv err/%d\r\n", msg_fd, errno);
                        break;
                    }
                }
            }
            //
            if (ret != repeat)
            {
                ret = -1;
                break;
            }
            //重启 reduceMode
            if (rdceIsMe && wmix->reduceMode == 1)
                wmix->reduceMode = rdce;
            //
            if (wmix->debug)
                printf("<< PLAY-WAV: %s start >>\n   通道数: %d\n   采样位数: %d bit\n   采样率: %d Hz\n   每秒字节: %d Bytes\n   重播间隔: %d sec\r\n",
                       wavPath,
                       wav.format.channels,
                       wav.format.sample_length,
                       wav.format.sample_rate,
                       wav.format.bytes_p_second,
                       repeat / 10);
            //
            total = bpsCount = 0;
            src.U8 = buff;
            head.U8 = wmix->head.U8;
            tick = 0;
        }
        else
            break;
    }
    //
    close(fd);
    free(buff);
    //
    if (wmix->debug)
        printf(">> PLAY-WAV: %s end <<\r\n", wavPath);
    //关闭 reduceMode
    if (rdceIsMe && wmix->reduceMode == rdce)
        wmix->reduceMode = 1;
}

#if (WMIX_AAC)
void wmix_task_play_aac(
    WMix_Struct *wmix,
    char *aacPath,
    int msg_fd,
    uint8_t reduce,
    uint8_t repeatInterval)
{
    int fd = 0;
    ssize_t ret = 0;
    //写循环缓冲区
    WMix_Point src, head;
    //播放计时
    uint32_t tick, total = 0, totalWait;
    uint32_t second = 0, bpsCount = 0;
    //背景消减和重复播放延时
    uint8_t rdce = reduce + 1, rdceIsMe = 0;
    uint16_t repeat = (uint16_t)repeatInterval * 10;
    //消息通信
    WMix_Msg msg;
    //aac解码句柄
    void *aacDec = NULL;
    uint8_t out[8192];
    //音频基本参数
    int chn;
    int sample = 16;
    int freq;
    uint32_t bytes_p_second;
    //系统更新loopWord时,会关闭该条播放
    int timeout;
    uint8_t loopWord;
    loopWord = wmix->loopWord;
    //
    if (!wmix || !wmix->run || !aacPath)
        return;
    //
    if ((fd = open(aacPath, O_RDONLY)) <= 0)
    {
        WMIX_ERR2("%s open err\r\n", aacPath);
        return;
    }
    //初始化解码器
    ret = aac_decode2(&aacDec, fd, out, (int *)&chn, (int *)&freq);
    if (ret < 0)
    {
        WMIX_ERR("aac_decode2 err\r\n");
        close(fd);
        return;
    }
    //
    bytes_p_second = chn * sample / 8 * freq;
    //
    if (wmix->debug)
        printf(
            "<< PLAY-AAC: %s start >>\r\n"
            "   通道数: %d\r\n"
            "   采样位数: %d bit\r\n"
            "   采样率: %d Hz\r\n"
            "   每秒字节: %d Bytes\r\n"
            "   重播间隔: %d sec\r\n",
            aacPath,
            chn,
            sample,
            freq,
            bytes_p_second,
            repeat / 10);
    //独占 reduceMode
    if (rdce > 1 && wmix->reduceMode == 1)
    {
        wmix->reduceMode = rdce;
        rdceIsMe = 1;
    }
    else
        rdce = 1;
    //
    totalWait = WMIX_CHANNELS * WMIX_SAMPLE / 8 * WMIX_FREQ / 2;
    //
    src.U8 = out;
    head.U8 = 0;
    tick = 0;
    //
    while (wmix->run &&
           loopWord == wmix->loopWord)
    {
        //msg 检查
        if (msg_fd)
        {
            if (msgrcv(msg_fd, &msg,
                       WMIX_MSG_BUFF_SIZE,
                       0, IPC_NOWAIT) < 1 &&
                errno != ENOMSG) //消息队列被关闭
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
            if (!wmix->run || loopWord != wmix->loopWord)
                break;
            //写入循环缓冲区
            head = wmix_load_data(
                wmix,
                src, ret,
                freq,
                chn,
                sample, head, rdce, &tick);
            //写入的总字节数统计
            bpsCount += ret;
            total += ret;
            //播放时间
            if (bpsCount > bytes_p_second)
            {
                bpsCount -= bytes_p_second;
                second = total / bytes_p_second;
                if (wmix->debug)
                    printf("  PLAY-AAC: %s %02d:%02d\r\n", aacPath, second / 60, second % 60);
            }
            //
            if (head.U8 == 0)
                break;
        }
        else if (ret == 0)
            ;
        else if (repeat)
        {
            //关闭 reduceMode
            if (rdceIsMe && wmix->reduceMode == rdce)
                wmix->reduceMode = 1;
            //
            lseek(fd, 0, SEEK_SET);
            //
            for (ret = 0; ret < repeat; ret++)
            {
                delayus(100000);
                //
                if (!wmix->run || loopWord != wmix->loopWord)
                    break;
                //
                if (msg_fd)
                {
                    if (msgrcv(msg_fd, &msg,
                               WMIX_MSG_BUFF_SIZE,
                               0, IPC_NOWAIT) < 1 &&
                        errno != ENOMSG) //消息队列被关闭
                    {
                        if (wmix->debug)
                            printf("PLAY-AAC exit: %d msgrecv err/%d\r\n", msg_fd, errno);
                        break;
                    }
                }
            }
            //
            if (ret != repeat)
            {
                ret = -1;
                break;
            }
            //重启 reduceMode
            if (rdceIsMe && wmix->reduceMode == 1)
                wmix->reduceMode = rdce;
            //
            if (wmix->debug)
                printf("<< PLAY-AAC: %s start >>\n   通道数: %d\n   采样位数: %d bit\n   采样率: %d Hz\n   每秒字节: %d Bytes\n   重播间隔: %d sec\r\n",
                       aacPath,
                       chn,
                       sample,
                       freq,
                       bytes_p_second,
                       repeat / 10);
            //
            total = bpsCount = 0;
            src.U8 = out;
            head.U8 = wmix->head.U8;
            tick = 0;
        }
        else
            break;
        //
        ret = aac_decode2(&aacDec, fd, out, (int *)&chn, (int *)&freq);
    }
    //
    close(fd);
    if (aacDec)
        aac_decodeRelease(&aacDec);
    //
    if (wmix->debug)
        printf(">> PLAY-AAC: %s end <<\r\n", aacPath);
    //关闭 reduceMode
    if (rdceIsMe && wmix->reduceMode == rdce)
        wmix->reduceMode = 1;
}
#endif

#if (WMIX_MP3)
typedef struct
{
    // char *msgPath;//消息队列挂靠路径
    char *mp3Path;
    //
    WMix_Msg msg;
    // key_t msg_key;
    int msg_fd;
    //
    void *fdm;     //mmap首地址
    uint32_t seek; //fdm跳过多少才到mp3数据段
    uint32_t size; //实际mp3数据段长度
    //
    WMix_Point head, src; //同步循环缓冲区的head指针 和 指向数据的src指针
    WMix_Struct *wmix;
    //
    uint32_t tick;             //循环缓冲区head指针走过了多少字节,用于防止数据写如缓冲区太快超过head指针
    uint8_t loopWord;          //每个播放线程的循环标志都要与该值一致,否则循环结束,用于打断全局播放
    uint32_t total, totalWait; //total:文件读取字节数
    //
    uint32_t bps, bpsCount; //bps:每秒字节数 bpsCount计数用于计算当前播放时间
    //
    uint8_t rdce;    //reduce
    uint16_t repeat; //repeatInterval*10
    uint8_t rdceIsMe;
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
    WMix_Mp3 *wmm = data;
    uint32_t count = 0, second;
    int16_t *val = (int16_t *)&pcm->samples[0][count];
    int timeout;
    //
    if (!wmm->wmix->run || wmm->loopWord != wmm->wmix->loopWord)
        return MAD_FLOW_STOP;
    //参数初始化
    if (wmm->head.U8 == 0)
    {
        wmm->bps = pcm->channels * 16 / 8 * header->samplerate;
        wmm->totalWait = WMIX_CHANNELS * WMIX_SAMPLE / 8 * WMIX_FREQ / 2; //等半秒
        //
        if (wmm->wmix->debug)
            printf(
                "<< PLAY-MP3: %s start >>\r\n"
                "   通道数: %d\r\n"
                "   采样位数: %d bit\r\n"
                "   采样率: %d Hz\r\n"
                "   每秒字节: %d Bytes\r\n"
                "   重播间隔: %d sec\r\n",
                wmm->mp3Path,
                pcm->channels, 16,
                header->samplerate,
                wmm->bps,
                wmm->repeat / 10);
        //
        wmm->total = wmm->bpsCount = 0;
        wmm->head.U8 = 0;
        wmm->tick = 0;
    }
    //msg 检查
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
    //
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
        wmm->head, wmm->rdce, &wmm->tick);
    //总字节数计数
    wmm->bpsCount += count;
    wmm->total += count;
    //播放时间
    if (wmm->bpsCount > wmm->bps)
    {
        wmm->bpsCount -= wmm->bps;
        second = wmm->total / wmm->bps;
        if (wmm->wmix->debug)
            printf("  PLAY-MP3: %s %02d:%02d\r\n", wmm->mp3Path, second / 60, second % 60);
    }
    //
    if (wmm->head.U8 == 0)
        return MAD_FLOW_STOP;
    //
    return MAD_FLOW_CONTINUE;
}

enum mad_flow mad_input(void *data, struct mad_stream *stream)
{
    WMix_Mp3 *wmm = data;
    uint8_t count;
    if (wmm->size > 0)
    {
        mad_stream_buffer(stream, wmm->fdm + wmm->seek, wmm->size);
        //
        if (wmm->repeat)
        {
            if (wmm->head.U8) //已经播放完一遍了
            {
                //关闭 reduceMode
                if (wmm->rdceIsMe && wmm->wmix->reduceMode == wmm->rdce)
                    wmm->wmix->reduceMode = 1;
                //
                for (count = 0; count < wmm->repeat; count++)
                {
                    delayus(100000);
                    //
                    if (!wmm->wmix->run || wmm->loopWord != wmm->wmix->loopWord)
                        return MAD_FLOW_STOP;
                    //msg 检查
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
                            return MAD_FLOW_STOP;
                        }
                    }
                }
                //重启 reduceMode
                if (wmm->rdceIsMe && wmm->wmix->reduceMode == 1)
                    wmm->wmix->reduceMode = wmm->rdce;
            }
        }
        else
            wmm->size = 0;
        //
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
    uint8_t repeatInterval)
{
    struct stat sta;
    int fd;
    //
    WMix_Mp3 wmm;
    struct mad_decoder decoder;

    //参数准备
    memset(&wmm, 0, sizeof(WMix_Mp3));
    wmm.wmix = wmix;
    wmm.mp3Path = mp3Path;
    wmm.repeat = (uint16_t)repeatInterval * 10;
    wmm.loopWord = wmix->loopWord;
    wmm.rdceIsMe = 0;
    wmm.rdce = reduce + 1;
    wmm.msg_fd = msg_fd;
    //
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

    //
    wmm.fdm = mmap(0, sta.st_size, PROT_READ, MAP_SHARED, fd, 0);
    if (wmm.fdm == MAP_FAILED || !wmm.fdm)
    {
        WMIX_ERR("mmap err\r\n");
        close(fd);
        return;
    }

    //独占 reduceMode
    if (wmm.rdce > 1 && wmix->reduceMode == 1)
    {
        wmix->reduceMode = wmm.rdce;
        wmm.rdceIsMe = 1;
    }
    else
        wmm.rdce = 1;

    //configure input, output, and error functions
    mad_decoder_init(&decoder, &wmm,
                     mad_input, 0 /* header */, 0 /* filter */, mad_output,
                     mad_error, 0 /* message */);

    //start decoding
    mad_decoder_run(&decoder, MAD_DECODER_MODE_SYNC);

    //release the decoder
    mad_decoder_finish(&decoder);

    //关闭 reduceMode
    if (wmm.rdceIsMe && wmix->reduceMode == wmm.rdce)
        wmix->reduceMode = 1;

    close(fd);
    munmap(wmm.fdm, sta.st_size);

    if (wmix->debug)
        printf(">> PLAY-MP3: %s end <<\r\n", mp3Path);
}
#endif
