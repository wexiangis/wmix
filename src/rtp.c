
/**************************************************
 * 
 *  rtp协议的数据打包、解包、udp发、收接口的封装
 * 
 **************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <arpa/inet.h>

#include "rtp.h"

void rtp_header(RtpPacket *rtpPacket, uint8_t cc, uint8_t x,
                uint8_t p, uint8_t v, uint8_t pt, uint8_t m,
                uint16_t seq, uint32_t timestamp, uint32_t ssrc)
{
    rtpPacket->rtpHeader.cc = cc;
    rtpPacket->rtpHeader.x = x;
    rtpPacket->rtpHeader.p = p;
    rtpPacket->rtpHeader.v = v;
    rtpPacket->rtpHeader.pt = pt;
    rtpPacket->rtpHeader.m = m;
    rtpPacket->rtpHeader.seq = seq;
    rtpPacket->rtpHeader.timestamp = timestamp;
    rtpPacket->rtpHeader.ssrc = ssrc;
}

int rtp_send(SocketStruct *ss, RtpPacket *rtpPacket, uint32_t dataSize)
{
    int ret;

    if (!ss || !rtpPacket)
        return -1;

    rtpPacket->rtpHeader.seq = htons(rtpPacket->rtpHeader.seq);
    rtpPacket->rtpHeader.timestamp = htonl(rtpPacket->rtpHeader.timestamp);
    rtpPacket->rtpHeader.ssrc = htonl(rtpPacket->rtpHeader.ssrc);

    if (rtpPacket->rtpHeader.pt == RTP_PAYLOAD_TYPE_AAC)
    {
        rtpPacket->payload[0] = 0x00;
        rtpPacket->payload[1] = 0x10;
        rtpPacket->payload[2] = (dataSize >> 5) & 0xFF;
        rtpPacket->payload[3] = (dataSize & 0x1F) << 3;
        dataSize += 4;
    }

    ret = sendto(
        ss->fd,
        (void *)rtpPacket,
        dataSize + RTP_HEADER_SIZE,
        MSG_DONTWAIT,
        (struct sockaddr *)&ss->addr,
        ss->addrSize);

    rtpPacket->rtpHeader.seq = ntohs(rtpPacket->rtpHeader.seq);
    rtpPacket->rtpHeader.timestamp = ntohl(rtpPacket->rtpHeader.timestamp);
    rtpPacket->rtpHeader.ssrc = ntohl(rtpPacket->rtpHeader.ssrc);

    rtpPacket->rtpHeader.seq++;

    return ret;
}

int rtp_recv(SocketStruct *ss, RtpPacket *rtpPacket, uint32_t *dataSize)
{
    int ret;

    if (!ss || !rtpPacket)
        return -1;

    ret = recvfrom(
        ss->fd,
        (void *)rtpPacket,
        sizeof(RtpPacket),
        0,
        (struct sockaddr *)&ss->addr,
        (socklen_t *)(&ss->addrSize));

    if (ret > 0 && dataSize)
    {
        if (rtpPacket->rtpHeader.pt == RTP_PAYLOAD_TYPE_AAC)
            *dataSize = (rtpPacket->payload[2] << 5) | (rtpPacket->payload[3] >> 3);
        else if (rtpPacket->rtpHeader.pt == RTP_PAYLOAD_TYPE_PCMA ||
                 rtpPacket->rtpHeader.pt == RTP_PAYLOAD_TYPE_PCMU)
            *dataSize = RTP_PCMA_PKT_SIZE;
        else
            *dataSize = 0;
    }

    return ret;
}

SocketStruct *rtp_socket(char *ip, uint16_t port, bool bindMode)
{
    int fd;
    int ret;
    SocketStruct *ss;

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0)
    {
        fprintf(stderr, "socket err\n");
        return NULL;
    }

    ss = (SocketStruct *)calloc(1, sizeof(SocketStruct));
    ss->fd = fd;
    ss->bindMode = bindMode;
    ss->addr.sin_family = AF_INET;
    ss->addr.sin_port = htons(port);
    ss->addr.sin_addr.s_addr = inet_addr((const char *)ip);
    bzero(&(ss->addr.sin_zero), 8);
    ss->addrSize = sizeof(ss->addr);

    //非阻塞设置
    ret = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, ret | O_NONBLOCK);

    if (bindMode)
    {
        ret = bind(fd, (const struct sockaddr *)&ss->addr, ss->addrSize);
        if (ret < 0)
        {
            fprintf(stderr, "bind err\n");
            free(ss);
            return NULL;
        }
    }

    return ss;
}

void rtp_socket_close(SocketStruct *ss)
{
    if (!ss)
        return;
    close(ss->fd);
}

void rtp_create_sdp(char *file, char *ip, uint16_t port, uint16_t chn, uint16_t freq, RTP_AUDIO_TYPE type)
{
    char buff[1024] = {0};
    char typeName[64] = {0};
    // char demo[] =
    //     "m=audio 9832 RTP/AVP 97\n"
    //     "a=rtpmap:97 mpeg4-generic/44100/2\n"
    //     "a=fmtp:97 sizeLength=13;mode=AAC-hbr;config=1210;\n"
    //     "c=IN IP4 127.0.0.1";
    char demo[] =
        "m=audio %d RTP/AVP %d\n"
        "a=rtpmap:%d %s/%d/%d\n"
        "a=fmtp:%d sizeLength=13;config=%d;\n"
        "c=IN IP4 %s";
    uint16_t config = 1410, _freq = 8;
    int fd;

    if (type == RTP_PAYLOAD_TYPE_AAC)
        strcpy(typeName, "mpeg4-generic");
    else if (type == RTP_PAYLOAD_TYPE_PCMA)
        strcpy(typeName, "pcma");
    else if (type == RTP_PAYLOAD_TYPE_PCMU)
        strcpy(typeName, "pcmu");
    else if (type == RTP_PAYLOAD_TYPE_GSM)
        strcpy(typeName, "gsm");
    else if (type == RTP_PAYLOAD_TYPE_G723)
        strcpy(typeName, "g723");
    else if (type == RTP_PAYLOAD_TYPE_G722)
        strcpy(typeName, "g722");
    else if (type == RTP_PAYLOAD_TYPE_G728)
        strcpy(typeName, "g728");
    else if (type == RTP_PAYLOAD_TYPE_G729)
        strcpy(typeName, "g729");
    else
        strcpy(typeName, "mpeg4-generic");

    if (freq == 96000)
        _freq = 0;
    else if (freq == 88200)
        _freq = 1;
    else if (freq == 64000)
        _freq = 2;
    else if (freq == 48000)
        _freq = 3;
    else if (freq == 44100)
        _freq = 4;
    else if (freq == 32000)
        _freq = 5;
    else if (freq == 24000)
        _freq = 6;
    else if (freq == 22050)
        _freq = 7;
    else if (freq == 16000)
        _freq = 8;
    else if (freq == 12000)
        _freq = 9;
    else if (freq == 11025)
        _freq = 10;
    else if (freq == 8000)
        _freq = 11;
    else if (freq == 7350)
        _freq = 12;
    config = 0x1;
    config <<= 5;
    config |= _freq;
    config <<= 4;
    config |= chn;
    config <<= 3;
    config = ((config >> 12) & 0xF) * 1000 + ((config >> 8) & 0xF) * 100 + ((config >> 4) & 0xF) * 10 + ((config >> 0) & 0xF);
    snprintf(buff, sizeof(buff), demo, port, type, type, typeName, freq, chn, type, config, ip);
    remove((const char *)file);
    if ((fd = open((const char *)file, O_WRONLY | O_CREAT, 0666)) > 0)
    {
        write(fd, buff, strlen(buff));
        close(fd);
    }
}

//
__time_t getTickUs(void)
{
    // struct timespec tp={0};
    struct timeval tv = {0};
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000000u + tv.tv_usec;
}

/* ----- 辅助 wmix 添加的链表管理结构 ----- */
/* ----- 检索到新增同ip和端口连接时使用现存,关闭时由最后使用者回收内存 ----- */

//本地保留一个链表头,不参与遍历
static RtpChain_Struct rtpChain_struct = {
    .next = NULL,
};

//链表操作禁止异步,简单的互斥保护
static bool rtpChain_busy = false;

//申请节点(已自动连上socket),NULL为失败
RtpChain_Struct *rtpChain_get(char *ip, int port, bool send, bool bindMode)
{
    SocketStruct *ss = NULL;
    RtpChain_Struct *rcs = &rtpChain_struct;
    while (rtpChain_busy)
        usleep(10000);
    rtpChain_busy = true;
    //遍历链表
    while (rcs->next)
    {
        //下一个
        rcs = rcs->next;
        //已存在ip和端口相同 且bind模式相同
        if (port == rcs->port &&
            strlen(ip) == strlen(rcs->ip) &&
            strcmp(ip, rcs->ip) == 0 &&
            rcs->bindMode == bindMode)
        {
            //保证只有一个s或r
            if (rcs->send_run && send)
            {
                rtpChain_busy = false;
                return NULL;
            }
            else if (rcs->recv_run && !send)
            {
                rtpChain_busy = false;
                return NULL;
            }
            //占坑
            else if (send)
                rcs->send_run = true;
            else
                rcs->recv_run = true;
            //有效返回
            rtpChain_busy = false;
            printf("rtpChain_get the same node send/%d bindMode/%d %s:%d\r\n",
                   send ? 1 : 0, bindMode ? 1 : 0, ip, port);
            return rcs;
        }
    }
    //新建连接
    ss = rtp_socket(ip, port, bindMode);
    if (!ss)
    {
        rtpChain_busy = false;
        return NULL;
    }
    //添加节点
    rcs->next = (RtpChain_Struct *)calloc(1, sizeof(RtpChain_Struct));
    rcs->next->last = rcs;
    rcs = rcs->next;
    //参数备份
    strcpy(rcs->ip, ip);
    rcs->port = port;
    rcs->ss = ss;
    if (send)
        rcs->send_run = true;
    else
        rcs->recv_run = true;
    rcs->bindMode = bindMode;
    pthread_mutex_init(&rcs->lock, NULL);
    //有效返回
    rtpChain_busy = false;
    printf("rtpChain_get a new node send/%d bindMode/%d %s:%d\r\n",
           send ? 1 : 0, bindMode ? 1 : 0, ip, port);
    return rcs;
}

//释放节点(不要调用free(rcs)!! 链表的内存由系统决定何时回收)
void rtpChain_release(RtpChain_Struct *rcs, bool send)
{
    if (!rcs)
        return;
    while (rtpChain_busy)
        usleep(10000);
    rtpChain_busy = true;
    //清标志
    if (send)
        rcs->send_run = false;
    else
        rcs->recv_run = false;
    //发收线程都退出了,允许回收内存
    if (!rcs->send_run && !rcs->recv_run)
    {
        //关闭连接
        if (rcs->ss)
        {
            rtp_socket_close(rcs->ss);
            free(rcs->ss);
        }
        //移除节点
        if (rcs->next)
            rcs->next->last = rcs->last;
        rcs->last->next = rcs->next; //上一个节点必然存在
        //释放内存
        pthread_mutex_destroy(&rcs->lock);
        free(rcs);
    }
    //
    rtpChain_busy = false;
}

void rtpChain_reconnect(RtpChain_Struct *rcs)
{
    if (!rcs)
        return;
    // printf("rtpChain_reconnect bindMode/%d %s:%d\r\n",
    //        rcs->bindMode ? 1 : 0, rcs->ip, rcs->port);
    pthread_mutex_lock(&rcs->lock);
    if (rcs->ss)
    {
        rtp_socket_close(rcs->ss);
        free(rcs->ss);
    }
    rcs->ss = rtp_socket(rcs->ip, rcs->port, rcs->bindMode);
    pthread_mutex_unlock(&rcs->lock);
}
