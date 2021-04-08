
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

#include "rtp.h"
#include "aacType.h"

#define RTP_IP "127.0.0.1"
#define RTP_PORT 9832

int main(int argc, char *argv[])
{
    int fd;
    int ret;
    SocketStruct *ss;
    uint32_t dataSize;
    uint8_t aacBuff[2048];
    RtpPacket rtpPacket;
    bool bindMode = false;
    char *ip = RTP_IP;
    int port = RTP_PORT;

    int chn = 2, freq = 44100;

    if (argc < 2 || strstr(argv[1], "?") || strstr(argv[1], "help"))
    {
        printf("Usage: %s <save file> <bind 0/1> <chn %d> <freq %d> <ip %s> <port %d>\n",
            argv[0], chn, freq, ip, port);
        return -1;
    }
    if (argc > 2)
        if (argv[2][0] != '0')
            bindMode = true;
    if (argc > 3)
        chn = atoi(argv[3]);
    if (argc > 4)
        freq = atoi(argv[4]);
    if (argc > 5)
        ip = argv[5];
    if (argc > 6)
        port = atoi(argv[6]);

    remove(argv[1]);
    fd = open(argv[1], O_WRONLY | O_CREAT, 0666);
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

    while (1)
    {
        ret = rtp_recv(ss, &rtpPacket, &dataSize);
        if (ret > 0)
        {
            printf("rtp_recv: %d / %d + %d\n", ret, ret - dataSize, dataSize);
            aac_createHeader((AacHeader *)aacBuff, chn, freq, 0x7FF, dataSize);
            memcpy(&aacBuff[7], &rtpPacket.payload[4], dataSize);
            write(fd, aacBuff, dataSize + 7);
            continue;
        }

        usleep(10000);
    }

    close(fd);
    rtp_socket_close(&ss);

    return 0;
}
