
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

#include "rtp.h"
#include "aac.h"

#define RTP_IP "127.0.0.1"
#define RTP_PORT 9832

int main(int argc, char *argv[])
{
    int fd;
    int ret;
    SocketStruct *ss;
    int dataSize;
    uint8_t aacBuff[2048];
    AacHeader AacHeader;
    RtpPacket rtpPacket;

    if (argc != 2)
    {
        printf("Usage: %s <save file>\n", argv[0]);
        return -1;
    }

    remove(argv[1]);
    fd = open(argv[1], O_WRONLY | O_CREAT, 0666);
    if (fd < 0)
    {
        printf("failed to open %s\n", argv[1]);
        return -1;
    }

    ss = rtp_socket(RTP_IP, RTP_PORT, 0);
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
            aac_createHeader(aacBuff, 2, 44100, 0x7FF, dataSize);
            memcpy(&aacBuff[7], &rtpPacket.payload[4], dataSize);
            write(fd, aacBuff, dataSize + 7);
            continue;
        }

        usleep(10000);
    }

    close(fd);
    close(ss->fd);
    free(ss);

    return 0;
}
