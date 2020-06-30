
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

    pthread_mutex_lock(&ss->lock);

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

    pthread_mutex_unlock(&ss->lock);

    return ret;
}

int rtp_recv(SocketStruct *ss, RtpPacket *rtpPacket, uint32_t *dataSize)
{
    int ret;

    pthread_mutex_lock(&ss->lock);

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

    pthread_mutex_unlock(&ss->lock);

    return ret;
}

SocketStruct *rtp_socket(char *ip, uint16_t port, uint8_t isServer)
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

    //非阻塞设置
    ret = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, ret | O_NONBLOCK);

    ss = (SocketStruct *)calloc(1, sizeof(SocketStruct));
    ss->fd = fd;
    ss->addr.sin_family = AF_INET;
    ss->addr.sin_port = htons(port);
    ss->addr.sin_addr.s_addr = inet_addr((const char *)ip);
    ss->addrSize = sizeof(ss->addr);

    if (!isServer)
    {
        ret = bind(fd, (const struct sockaddr *)&ss->addr, ss->addrSize);
        if (fd < 0)
        {
            fprintf(stderr, "bind err\n");
            free(ss);
            return NULL;
        }
    }

    pthread_mutex_init(&ss->lock, NULL);

    return ss;
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
