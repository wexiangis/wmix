
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

#include "rtp.h"
#include "wav.h"
#include "g711codec.h"

#define RTP_IP "127.0.0.1"
#define RTP_PORT 9832

int main(int argc, char *argv[])
{
    int fd;
    int ret;
    SocketStruct *ss;
    RtpPacket rtpPacket;
    unsigned char buff[1024];

    int readSize = RTP_PCMA_PKT_SIZE;

    WAVContainer_t container;

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

    WAV_Params(&container, 5, 1, 16, 8000);
    WAV_WriteHeader(fd, &container);

    ss = rtp_socket(RTP_IP, RTP_PORT, false);
    if (!ss)
    {
        printf("failed to create udp socket\n");
        close(fd);
        return -1;
    }

    while (1)
    {
        ret = rtp_recv(ss, &rtpPacket, &readSize);
        if (ret > 0)
        {
            printf("rtp_recv: %d / %d + %d\n", ret, ret - readSize, readSize);
            ret = G711a2PCM(rtpPacket.payload, buff, readSize, 0);
            write(fd, buff, ret);
            continue;
        }

        usleep(10000);
    }

    close(fd);
    close(ss->fd);
    free(ss);

    return 0;
}
