
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

#include "rtp.h"
#include "g711codec.h"
#include "delay.h"

//网络参数,有些端口vlc是不支持的
#define RTP_IP "127.0.0.1"
#define RTP_PORT 9832 //用vlc播放的时候只识别9832...

//发送文件参数
#define SEND_CHN 1
#define SEND_FREQ 8000

//每包数据发出后的延时
#define SEND_DELAYUS 20000

//rtp包时间戳增量
//不同于AAC格式,PCMA/PCMU格式RTP包的时间戳用于记录数据量即160
#define SEND_TIMESTAMP 160 //320 bytes PCM --> 160 bytes PCMA/PCMU

int main(int argc, char *argv[])
{
    int ret;
    char wsdp = 0;
    __time_t tick1, tick2;
    int pkgType = RTP_PAYLOAD_TYPE_PCMA; //RTP_PAYLOAD_TYPE_PCMU
    //各种句柄
    SocketStruct *ss;
    RtpPacket rtpPacket;
    int fd;
    int seekStart = 0;
    unsigned char pcm[SEND_TIMESTAMP * 2];
    bool bindMode = false;
    char *ip = RTP_IP;
    int port = RTP_PORT;

    if (argc < 2 || strstr(argv[1], "?") || strstr(argv[1], "help"))
    {
        printf("Usage: %s <read file> <bind 0/1> <ip %s> <port %d>\n", argv[0], ip, port);
        return -1;
    }
    if (argc > 2)
        if (argv[2][0] != '0')
            bindMode = true;
    if (argc > 3)
        ip = argv[3];
    if (argc > 4)
        port = atoi(argv[4]);

    fd = open(argv[1], O_RDONLY);
    if (fd < 0)
    {
        printf("failed to open %s\n", argv[1]);
        return -1;
    }

    //wav格式跳过文件头
    if (strstr(argv[1], ".wav"))
    {
        seekStart = 44;
        read(fd, rtpPacket.payload, seekStart);
    }

    //udp准备
    ss = rtp_socket(ip, port, bindMode);
    if (!ss)
    {
        printf("failed to create udp socket\n");
        close(fd);
        return -1;
    }

    //rtp头生成
    rtp_header(&rtpPacket, 0, 0, 0, RTP_VESION, pkgType, 0, 0, 0, 0);

    while (1)
    {
        tick1 = getTickUs();

        printf("--------------------------------\n");

        //生成vlc播放用的.sdp文件
        if (!wsdp)
        {
            wsdp = 1;
            rtp_create_sdp("/tmp/record.sdp", RTP_IP, RTP_PORT, SEND_CHN, SEND_FREQ, pkgType);
        }

        //读文件
        ret = read(fd, pcm, sizeof(pcm));
        if (ret < sizeof(pcm))
        {
            lseek(fd, seekStart, SEEK_SET); //循环播放,wav格式的跳过文件头
            continue;
        }

        //格式转换
        if (pkgType == 1)
            ret = PCM2G711u((char *)pcm, (char *)rtpPacket.payload, ret, 0); //PCM -> PCMU
        else
            ret = PCM2G711a((char *)pcm, (char *)rtpPacket.payload, ret, 0); //PCM -> PCMA

        //发包
        ret = rtp_send(ss, &rtpPacket, ret);
        if (ret > 0)
            printf("send: %s:%d bytes %d, seq %d\n", RTP_IP, RTP_PORT, ret, rtpPacket.rtpHeader.seq);

        //时间戳增量
        rtpPacket.rtpHeader.timestamp += SEND_TIMESTAMP;

        //保证每次发包延时一致
        tick2 = getTickUs();
        if (tick2 > tick1 && tick2 - tick1 < SEND_DELAYUS)
            usleep(SEND_DELAYUS - (tick2 - tick1));
        else
            usleep(1000);
        // printf("tick1: %d, tick2: %d, delay: %d\n", tick1, tick2, SEND_DELAYUS-(tick2-tick1));
    }

    close(fd);
    close(ss->fd);
    free(ss);

    return 0;
}
