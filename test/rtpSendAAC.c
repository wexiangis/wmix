
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

#include "rtp.h"
#include "aac.h"

//网络参数,有些端口vlc是不支持的
#define RTP_IP "127.0.0.1"
#define RTP_PORT 9832 //用vlc播放的时候只识别9832...

int main(int argc, char *argv[])
{
    int ret;
    uint8_t aacBuff[2048];
    char wsdp = 0;
    //各种句柄
    AacHeader AacHeader;
    RtpPacket rtpPacket;
    SocketStruct *ss;
    int fd;
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

    ss = rtp_socket(ip, port, bindMode);
    if (!ss)
    {
        printf("failed to create udp socket\n");
        close(fd);
        return -1;
    }

    rtp_header(&rtpPacket, 0, 0, 0, RTP_VESION, RTP_PAYLOAD_TYPE_AAC, 1, 0, 0, 0x32411);

    while (1)
    {
        printf("--------------------------------\n");

        ret = read(fd, aacBuff, 7);
        if (ret <= 0)
        {
            lseek(fd, 0, SEEK_SET);
            continue;
        }

        if (aac_parseHeader(aacBuff, &AacHeader, 1) < 0)
        {
            printf("parse err\n");
            lseek(fd, 0, SEEK_SET);
            continue;
        }

        if (!wsdp)
        {
            wsdp = 1;
            rtp_create_sdp("/tmp/record-aac.sdp",
                           RTP_IP, RTP_PORT,
                           AacHeader.channelCfg,
                           aac_freqList[AacHeader.samplingFreqIndex],
                           RTP_PAYLOAD_TYPE_AAC);
        }

        ret = read(fd, &rtpPacket.payload[4], AacHeader.aacFrameLength - 7);
        if (ret <= 0)
        {
            lseek(fd, 0, SEEK_SET);
            continue;
        }

        rtp_send(ss, &rtpPacket, ret);

        rtpPacket.rtpHeader.timestamp += (AacHeader.adtsBufferFullness + 1) / 2;

        usleep(
            (AacHeader.adtsBufferFullness + 1) / 2 * 1000 * 1000 / aac_freqList[AacHeader.samplingFreqIndex] - 1000);
    }

    close(fd);
    close(ss->fd);
    free(ss);

    return 0;
}
