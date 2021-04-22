/*
 *  共享内存使用说明
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

static WMix_MemCircle *mem1x8000 = NULL, *memOrigin = NULL;

int wmix_mem_create(const char *path, int flag, int size, void **mem)
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

//单通道8000Hz共享内存数据,len和返回长度都按int16计算长度
int16_t wmix_mem_read_1x8000(int16_t *dat, int16_t len, int16_t *addr, bool wait)
{
    int16_t i = 0;
    int16_t w = *addr;
    if (!mem1x8000)
    {
        wmix_mem_create(WMIX_MSG_PATH, WMIX_MEM_AI_1X8000_CHAR, sizeof(WMix_MemCircle), (void **)&mem1x8000);
        if (!mem1x8000)
        {
            WMIX_ERR("wmix_mem_read_1x8000: shm_create err !!\r\n");
            return 0;
        }
        w = mem1x8000->w;
    }
    if (w < 0 || w >= WMIX_MEM_CIRCLE_BUFF_LEN)
        w = mem1x8000->w;
    for (i = 0; i < len;)
    {
        if (w == mem1x8000->w)
        {
            if (wait && mem1x8000)
            {
                usleep(5000);
                continue;
            }
            break;
        }
        *dat++ = mem1x8000->buff[w++];
        if (w >= WMIX_MEM_CIRCLE_BUFF_LEN)
            w = 0;
        i += 1;
    }
    *addr = w;
    return i;
}

//原始录音共享内存数据,len和返回长度都按int16计算长度
int16_t wmix_mem_read_origin(int16_t *dat, int16_t len, int16_t *addr, bool wait)
{
    int16_t i = 0;
    int16_t w = *addr;
    if (!memOrigin)
    {
        wmix_mem_create(WMIX_MSG_PATH, WMIX_MEM_AI_ORIGIN_CHAR, sizeof(WMix_MemCircle), (void **)&memOrigin);
        if (!memOrigin)
        {
            WMIX_ERR("wmix_mem_read_origin: shm_create err !!\r\n");
            return 0;
        }
        w = memOrigin->w;
    }
    if (w < 0 || w >= WMIX_MEM_CIRCLE_BUFF_LEN)
        w = memOrigin->w;
    for (i = 0; i < len;)
    {
        if (w == memOrigin->w)
        {
            if (wait && memOrigin)
            {
                usleep(5000);
                continue;
            }
            break;
        }
        *dat++ = memOrigin->buff[w++];
        if (w >= WMIX_MEM_CIRCLE_BUFF_LEN)
            w = 0;
        i += 1;
    }
    *addr = w;
    return i;
}

//单通道8000Hz共享内存数据,len和返回长度都按int16计算长度
int16_t wmix_mem_write_1x8000(int16_t *dat, int16_t len)
{
    int16_t i = 0;
    if (!mem1x8000)
    {
        wmix_mem_create(WMIX_MSG_PATH, WMIX_MEM_AI_1X8000_CHAR, sizeof(WMix_MemCircle), (void **)&mem1x8000);
        if (!mem1x8000)
        {
            WMIX_ERR("wmix_mem_write_1x8000: shm_create err !!\r\n");
            return 0;
        }
        mem1x8000->w = 0;
    }
    for (i = 0; i < len; i++)
    {
        mem1x8000->buff[mem1x8000->w] = *dat++;
        if (mem1x8000->w + 1 < WMIX_MEM_CIRCLE_BUFF_LEN)
            mem1x8000->w += 1;
        else
            mem1x8000->w = 0;
    }
    return i;
}

//原始录音共享内存数据,len和返回长度都按int16计算长度
int16_t wmix_mem_write_origin(int16_t *dat, int16_t len)
{
    int16_t i = 0;
    if (!memOrigin)
    {
        wmix_mem_create(WMIX_MSG_PATH, WMIX_MEM_AI_ORIGIN_CHAR, sizeof(WMix_MemCircle), (void **)&memOrigin);
        if (!memOrigin)
        {
            WMIX_ERR("wmix_mem_write_origin: shm_create err !!\r\n");
            return 0;
        }
        memOrigin->w = 0;
    }
    for (i = 0; i < len; i++)
    {
        memOrigin->buff[memOrigin->w] = *dat++;
        if (memOrigin->w + 1 < WMIX_MEM_CIRCLE_BUFF_LEN)
            memOrigin->w += 1;
        else
            memOrigin->w = 0;
    }
    return i;
}
