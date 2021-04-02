/*
 *  共享内存使用说明：
 *  "/tmp/wmix", 'O'： 单通道8000Hz客户端写入音频数据，这里读并播放
 *  "/tmp/wmix", 'I'： 单通道8000Hz这里录音并写入，客户端读取录音数据
 *  "/tmp/wmix", 'L'： 原始录音数据写入，客户端读取或混音器自己的录音线程取用
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/shm.h>

#include "wmix.h"

typedef struct
{
    int16_t w;
    int16_t buff[AI_CIRCLE_BUFF_LEN + 4];
} ShmemAi_Circle;

static ShmemAi_Circle *ai_circle = NULL, *ao_circle = NULL, *ao_circleLocal = NULL;

int wmix_mem_create(char *path, int flag, int size, void **mem)
{
    int id;
    key_t key = ftok(path, flag);
    if (key < 0)
    {
        WMIX_ERR("get key error\r\n");
        return -1;
    }
    id = shmget(key, size, 0666);
    if (id < 0)
        id = shmget(key, size, IPC_CREAT | 0666);
    if (id < 0)
    {
        WMIX_ERR("get id error\r\n");
        return -1;
    }
    if (mem)
        *mem = shmat(id, NULL, 0);
    return id;
}

int wmix_mem_destroy(int id)
{
    return shmctl(id, IPC_RMID, NULL);
}

//单通道8000Hz共享内存数据, len和返回长度都按int16计算长度
int16_t wmix_mem_read(int16_t *dat, int16_t len, int16_t *addr, bool wait)
{
    int16_t i = 0;
    int16_t w = *addr;
    if (!ai_circle)
    {
        wmix_mem_create("/tmp/wmix", 'I', sizeof(ShmemAi_Circle), (void **)&ai_circle);
        if (!ai_circle)
        {
            WMIX_ERR("wmix_mem_read: shm_create err !!\r\n");
            return 0;
        }
        w = ai_circle->w;
    }
    if (w < 0 || w >= AI_CIRCLE_BUFF_LEN)
        w = ai_circle->w;
    for (i = 0; i < len;)
    {
        if (w == ai_circle->w)
        {
            if (wait && ai_circle)
            {
                usleep(5000);
                continue;
            }
            break;
        }
        *dat++ = ai_circle->buff[w++];
        if (w >= AI_CIRCLE_BUFF_LEN)
            w = 0;
        i += 1;
    }
    *addr = w;
    return i;
}

//原始录音共享内存数据, len和返回长度都按int16计算长度
int16_t wmix_mem_read2(int16_t *dat, int16_t len, int16_t *addr, bool wait)
{
    int16_t i = 0;
    int16_t w = *addr;
    if (!ao_circleLocal)
    {
        wmix_mem_create("/tmp/wmix", 'L', sizeof(ShmemAi_Circle), (void **)&ao_circleLocal);
        if (!ao_circleLocal)
        {
            WMIX_ERR("wmix_mem_read2: shm_create err !!\r\n");
            return 0;
        }
        w = ao_circleLocal->w;
    }
    if (w < 0 || w >= AI_CIRCLE_BUFF_LEN)
        w = ao_circleLocal->w;
    for (i = 0; i < len;)
    {
        if (w == ao_circleLocal->w)
        {
            if (wait && ao_circleLocal)
            {
                usleep(5000);
                continue;
            }
            break;
        }
        *dat++ = ao_circleLocal->buff[w++];
        if (w >= AI_CIRCLE_BUFF_LEN)
            w = 0;
        i += 1;
    }
    *addr = w;
    return i;
}

//单通道8000Hz共享内存数据, len和返回长度都按int16计算长度
int16_t wmix_mem_write(int16_t *dat, int16_t len)
{
    int16_t i = 0;
    if (!ao_circle)
    {
        wmix_mem_create("/tmp/wmix", 'I', sizeof(ShmemAi_Circle), (void **)&ao_circle);
        if (!ao_circle)
        {
            WMIX_ERR("wmix_mem_write: shm_create err !!\r\n");
            return 0;
        }
        ao_circle->w = 0;
    }
    for (i = 0; i < len; i++)
    {
        ao_circle->buff[ao_circle->w] = *dat++;
        if (ao_circle->w + 1 < AI_CIRCLE_BUFF_LEN)
            ao_circle->w += 1;
        else
            ao_circle->w = 0;
    }
    return i;
}

//原始录音共享内存数据, len和返回长度都按int16计算长度
int16_t wmix_mem_write2(int16_t *dat, int16_t len)
{
    int16_t i = 0;
    if (!ao_circleLocal)
    {
        wmix_mem_create("/tmp/wmix", 'L', sizeof(ShmemAi_Circle), (void **)&ao_circleLocal);
        if (!ao_circleLocal)
        {
            WMIX_ERR("wmix_mem_write2: shm_create err !!\r\n");
            return 0;
        }
        ao_circleLocal->w = 0;
    }
    for (i = 0; i < len; i++)
    {
        ao_circleLocal->buff[ao_circleLocal->w] = *dat++;
        if (ao_circleLocal->w + 1 < AI_CIRCLE_BUFF_LEN)
            ao_circleLocal->w += 1;
        else
            ao_circleLocal->w = 0;
    }
    return i;
}