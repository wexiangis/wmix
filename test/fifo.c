#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "wmix_user.h"
#include "wav.h"

#include <sys/time.h>
__time_t getTickUs(void)
{
    struct timespec tp = {0};
    struct timeval tv = {0};
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000000u + tv.tv_usec;
}

//============= shm =============

#include <sys/shm.h>

#define AI_CIRCLE_BUFF_LEN 10240
typedef struct
{
    int16_t w;
    int16_t buff[AI_CIRCLE_BUFF_LEN + 4];
} ShmemAi_Circle;

static ShmemAi_Circle *ai_circle = NULL, *ao_circle = NULL;

int fifo_mem_create(char *path, int flag, int size, void **mem)
{
    key_t key = ftok(path, flag);
    if (key < 0)
    {
        fprintf(stderr, "get key error\n");
        return -1;
    }
    int id;
    id = shmget(key, size, 0666);
    if (id < 0)
        id = shmget(key, size, IPC_CREAT | 0666);
    if (id < 0)
    {
        fprintf(stderr, "get id error\n");
        return -1;
    }
    if (mem)
        *mem = shmat(id, NULL, 0);

    return id;
}

int fifo_mem_destroy(int id)
{
    return shmctl(id, IPC_RMID, NULL);
}

int16_t fifo_mem_read(int16_t *dat, int16_t len, int16_t *addr, bool wait)
{
    int16_t i = 0;
    int16_t w = *addr;
    //
    if (!ai_circle)
    {
        fifo_mem_create("/tmp/wmix", 'I', sizeof(ShmemAi_Circle), &ai_circle);
        if (!ai_circle)
        {
            fprintf(stderr, "fifo_mem_read: shm_create err !!\n");
            return 0;
        }
        //
        w = ai_circle->w;
    }
    //
    if (w < 0 || w >= AI_CIRCLE_BUFF_LEN)
        w = ai_circle->w;
    for (i = 0; i < len; i++)
    {
        if (w == ai_circle->w)
        {
            if (wait && ai_circle)
            {
                usleep(1000);
                continue;
            }
            break;
        }
        //
        *dat++ = ai_circle->buff[w++];
        if (w >= AI_CIRCLE_BUFF_LEN)
            w = 0;
    }
    *addr = w;
    //
    return i;
}

int16_t fifo_mem_write(int16_t *dat, int16_t len)
{
    int16_t i = 0;
    //
    if (!ao_circle)
    {
        fifo_mem_create("/tmp/wmix", 'O', sizeof(ShmemAi_Circle), &ao_circle);
        if (!ao_circle)
        {
            fprintf(stderr, "fifo_mem_write: shm_create err !!\n");
            return 0;
        }
        //
        ao_circle->w = 0;
    }
    //
    for (i = 0; i < len; i++)
    {
        ao_circle->buff[ao_circle->w++] = *dat++;
        if (ao_circle->w >= AI_CIRCLE_BUFF_LEN)
            ao_circle->w = 0;
    }
    //
    return i;
}

//============= shm =============

void fun_write_fifo(void)
{
    int fd;
    ssize_t ret, total = 0;
    uint8_t buff[4096];
    int stream = wmix_stream_open(1, 16, 22050, 0, NULL);
    if (stream > 0)
    {
        // fd = open("./music.wav", O_RDONLY);
        fd = open("./music2.wav", O_RDONLY);
        if (fd > 0)
        {
            //跳过文件头
            read(fd, buff, 44);
            //
            while (1)
            {
                ret = read(fd, buff, 4096);
                if (ret > 0)
                    write(stream, buff, ret);
                else
                    break;
                total += ret;
            }
            //
            close(fd);
        }
        //
        close(stream);
        //
        printf("wav write end: %ld\n", (long)total);
    }
}

void fun_read_fifo(void)
{
    uint8_t chn = 2;
    uint8_t sample = 16;
    uint16_t freq = 16000;
    uint16_t second = 5;
    //
    WAVContainer_t wav;
    char buff[1024];
    size_t ret, total = 0;
    int fd;
    //
    int fd_record = wmix_record_stream_open(chn, sample, freq, NULL);
    size_t sum = chn * sample / 8 * freq * second;

    if (fd_record > 0)
    {
        fd = open("./capture.wav", O_WRONLY | O_CREAT | O_TRUNC, 0666);

        if (fd <= 0)
        {
            close(fd_record);
            return;
        }
        //
        WAV_Params(&wav, second, chn, sample, freq);
        WAV_WriteHeader(fd, &wav);
        //
        while (1)
        {
            ret = read(fd_record, buff, sizeof(buff));
            if (ret > 0)
            {
                if (write(fd, buff, ret) < 1)
                    break;
                total += ret;
                if (total >= sum)
                    break;
            }
            else
                break;
        }
        //
        close(fd_record);
        close(fd);
        printf("wav write end: %ld\n", total);
    }
}

void fun_wr_fifo(void)
{
    uint8_t chn = 2;
    uint8_t sample = 16;
    uint16_t freq = 16000;
    uint16_t second = 10;
    //
    char buff[1024];
    size_t ret, total = 0;
    //
    int fd = wmix_stream_open(chn, sample, freq, 3, NULL);
    int fd_record = wmix_record_stream_open(chn, sample, freq, NULL);
    size_t sum = chn * sample / 8 * freq * second;

    if (fd_record > 0 && fd > 0)
    {
        while (1)
        {
            ret = read(fd_record, buff, sizeof(buff));
            if (ret > 0)
            {
                if (write(fd, buff, ret) < 1)
                    break;
                total += ret;
                if (total >= sum)
                    break;
            }
            else
                break;
        }
        //
        close(fd_record);
        close(fd);
        printf("wav write end: %ld\n", total);
    }
}

#if (0)
int main(int argc, char **argv)
{
    uint8_t chn = 1;
    uint8_t sample = 16;
    uint16_t freq = 8000;
    uint16_t second = 20;
    //
    WAVContainer_t wav;
    char buff[1024];
    size_t ret, total = 0;
    int fd;
    //
    int fd_record = wmix_record_stream_open(chn, sample, freq);
    size_t sum = chn * sample / 8 * freq * second;

    if (fd_record > 0)
    {
        fd = open(argv[1], O_WRONLY | O_CREAT | O_TRUNC, 0666);

        if (fd <= 0)
        {
            close(fd_record);
            return;
        }
        //
        WAV_Params(&wav, second, chn, sample, freq);
        WAV_WriteHeader(fd, &wav);
        //
        while (1)
        {
            ret = read(fd_record, buff, sizeof(buff));
            if (ret > 0)
            {
                printf("read: %d\n", ret);
                if (write(fd, buff, ret) < 1)
                    break;
                total += ret;
                if (total >= sum)
                    break;
            }
            else
            {
                printf("read err !\n");
                break;
            }
        }
        //
        close(fd_record);
        close(fd);
        printf("wav write end: %ld\n", total);
    }
    else
        printf("wmix_record_stream_open err !\n");
}
#elif (1)
int main(int argc, char **argv)
{
    int fd, stream;
    ssize_t ret, total = 0, readSize = 320;
    uint8_t buff[8192], path[64];
    WAVContainer_t container;
    __time_t t1, t2;

    fd = open(argv[1], O_RDONLY);
    if (fd > 0)
    {
        if (WAV_ReadHeader(fd, &container) == 0)
        {
            stream = wmix_stream_open(
                container.format.channels,
                container.format.sample_length,
                container.format.sample_rate,
                0,
                (char *)path);
            if (stream > 0)
            {
                readSize = (ssize_t)(320 * container.format.channels * ((float)container.format.sample_rate / 8000));
                //跳过文件头
                lseek(fd, 44, SEEK_SET);
                //
                t1 = t2 = getTickUs();
                while (1)
                {
                    if (!wmix_check_path((char *)path))
                    {
                        fprintf(stderr, "check path failed !!\n");
                        break;
                    }
                    //
                    ret = read(fd, buff, readSize);
                    if (ret > 0)
                    {
                        write(stream, buff, ret);
                        total += ret;
                    }
                    else
                        lseek(fd, 44, SEEK_SET);
                    //
                    t2 = getTickUs();
                    //
                    if (t2 - t1 < 19000)
                        usleep(19000 - (t2 - t1));
                    //
                    t1 = getTickUs();
                }
                //
                close(stream);
                //
                printf("wav write end: %ld\n", (long)total);
            }
        }
        close(fd);
    }
    return 0;
}
#elif (0)
int main(int argc, char **argv)
{
    int fd;
    ssize_t ret, total = 0;
    uint8_t buff[320];
    fd = open(argv[1], O_RDONLY);
    if (fd > 0)
    {
        //跳过文件头
        lseek(fd, 44, SEEK_SET);
        //
        while (1)
        {
            ret = read(fd, buff, sizeof(buff));
            if (ret == sizeof(buff))
            {
                fifo_mem_write((int16_t *)buff, ret / 2);
                total += ret;
            }
            else
                lseek(fd, 44, SEEK_SET);
            usleep(19000);
        }
        //
        close(fd);
    }
    //
    printf("wav write end: %ld\n", (long)total);
    //
    return 0;
}
#else
int main(void)
{
    char input[16];
    pthread_t th;

    // pthread_create(&th, NULL, (void*)&fun, NULL);

    while (1)
    {
        memset(input, 0, sizeof(input));
        if (scanf("%s", input) > 0)
        {
            //数据流 播放
            if (input[0] == 'b' && input[1] == 0)
                pthread_create(&th, NULL, (void *)&fun_read_fifo, NULL);
            else if (input[0] == 'n' && input[1] == 0)
                pthread_create(&th, NULL, (void *)&fun_write_fifo, NULL);
            else if (input[0] == 'm' && input[1] == 0)
                pthread_create(&th, NULL, (void *)&fun_wr_fifo, NULL);

            //数据流 播放
            if (input[0] == 'i' && input[1] == 0)
                wmix_play("./capture.wav", 0, 0, 0);
            else if (input[0] == 'o' && input[1] == 0)
                wmix_play("./capture.wav", 0, 0, 0);
            else if (input[0] == 'p' && input[1] == 0)
                wmix_play("./capture.aac", 0, 0, 0);

            //录音
            else if (input[0] == 'j')
                pthread_create(&th, NULL, (void *)&fun_read_fifo, NULL);
            else if (input[0] == 'k')
                wmix_record("./capture.wav", 1, 16, 8000, 5, false); //录至文件
            else if (input[0] == 'l')
                wmix_record("./capture.aac", 1, 16, 8000, 5, false); //录至文件

            //设置音量
            else if (input[0] == 'v' && input[1] == '1' && input[2] == '0')
                wmix_set_volume(10, 10);
            else if (input[0] == 'v')
                wmix_set_volume(input[1] - '0', 10);

            //复位
            else if (input[0] == 'r')
                wmix_reset();

            //退出
            else if (input[0] == 'q')
                break;
        }
        usleep(10000);
    }
    //
    return 0;
}
#endif