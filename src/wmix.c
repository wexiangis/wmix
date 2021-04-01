
/**************************************************
 * 
 *  服务端+客户端(或用wmix_user.h自己开发) 组建的混音器、音频托管工具
 * 
 **************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <arpa/inet.h>

#include "wmix.h"
#include "delay.h"

/*******************************************************************************
 * 名称: wmix_console
 * 功能: 重定向输出
 * 参数: path 终端或文件路径
 * 返回: 无
 * 说明: 无
 ******************************************************************************/
void wmix_console(WMix_Struct *wmix, char *path)
{
    FILE *fp;
    //空路径
    if (!path || path[0] == 0)
        return;
    //这是/dev下的终端?
    if (strncmp(path, "/dev/", 5) == 0)
    {
        //路径不存在?
        if (access(path, F_OK) != 0)
        {
            printf("%s: %s not exist !!\r\n", __func__, path);
            return;
        }
        wmix->consoleType = 0;
    }
    //这是文件
    else
    {
        //尝试追加打开
        fp = fopen(path, "a+");
        if (!fp)
        {
            printf("%s: file %s open faile !!\r\n", __func__, path);
            return;
        }
        else
            fclose(fp);
        wmix->consoleType = 1;
    }
    printf("%s: point to %s \r\n", __func__, path);
    //重定向
    if (!freopen(path, wmix->consoleType == 0 ? "w" : "a+", stdout))
        WMIX_ERR2("freopen %s error !!\r\n", path);
}

void wmix_load_thread(
    WMix_Struct *wmix,
    long flag,
    uint8_t *param,
    size_t paramLen,
    void *callback)
{
    WMixThread_Param *wmtp;
    pthread_t th;
    pthread_attr_t attr;
    wmtp = (WMixThread_Param *)calloc(1, sizeof(WMixThread_Param));
    wmtp->wmix = wmix;
    wmtp->flag = flag;
    if (paramLen > 0 && param)
    {
        wmtp->param = (uint8_t *)calloc(paramLen, sizeof(uint8_t));
        memcpy(wmtp->param, param, paramLen);
    }
    else
        wmtp->param = NULL;
    //attr init
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED); //禁用线程同步, 线程运行结束后自动释放
    //抛出线程
    pthread_create(&th, &attr, callback, (void *)wmtp);
    //attr destroy
    pthread_attr_destroy(&attr);
}

void wmix_load_task(WMixThread_Param *wmtp)
{
    char *name = (char *)wmtp->param;
    uint16_t len = strlen((char *)wmtp->param);

    char *msgPath;
    key_t msg_key;
    int msg_fd = 0;
    FILE *fp;

    bool run = true, joinQueue = false;

    int queue = -1;

    uint8_t loopWord;
    loopWord = wmtp->wmix->loopWord;
    //线程计数
    wmtp->wmix->thread_play += 1;

    msgPath = (char *)&wmtp->param[len + 1];
    if (msgPath && msgPath[0])
    {
        //创建消息挂靠路径
        if (access(msgPath, F_OK) != 0)
            creat(msgPath, 0666);
        //写节点描述
        if ((fp = fopen(msgPath, "w")))
        {
            fprintf(fp, "play %s", name);
            fclose(fp);
        }
        //创建消息
        if ((msg_key = ftok(msgPath, WMIX_MSG_ID)) > 0)
            msg_fd = msgget(msg_key, IPC_CREAT | 0666);
    }
    else
        msgPath = NULL;
    //排队(循环播放和背景消减除时除外)
    if (((wmtp->flag & 0xFF) == 9 || (wmtp->flag & 0xFF) == 10) &&
        ((wmtp->flag >> 8) & 0xFF) == 0 && ((wmtp->flag >> 16) & 0xFF) == 0)
    {
        run = false;
        joinQueue = true;

        if ((wmtp->flag & 0xFF) == 9 &&
            wmtp->wmix->queue.head != wmtp->wmix->queue.tail) //排头
            queue = wmtp->wmix->queue.head--;
        else
            queue = wmtp->wmix->queue.tail++;

        while (wmtp->wmix->run && loopWord == wmtp->wmix->loopWord)
        {
            if (queue == wmtp->wmix->queue.head && wmtp->wmix->onPlayCount == 0)
            {
                run = true;
                break;
            }
            delayus(100000);
        }
    }

    if (run)
    {
        if (joinQueue)
            wmtp->wmix->onPlayCount += 1;

        if (len > 3 &&
            (name[len - 3] == 'a' || name[len - 3] == 'A') &&
            (name[len - 2] == 'a' || name[len - 2] == 'A') &&
            (name[len - 1] == 'c' || name[len - 1] == 'C'))
#if (MAKE_AAC)
            wmix_task_play_aac(
                wmtp->wmix, name, msg_fd, (wmtp->flag >> 8) & 0xFF, (wmtp->flag >> 16) & 0xFF);
#else
            ;
#endif
        else if (len > 3 &&
                 (name[len - 3] == 'm' || name[len - 3] == 'M') &&
                 (name[len - 2] == 'p' || name[len - 2] == 'P') &&
                 name[len - 1] == '3')
#if (MAKE_MP3)
            wmix_task_play_mp3(
                wmtp->wmix, name, msg_fd, (wmtp->flag >> 8) & 0xFF, (wmtp->flag >> 16) & 0xFF);
#else
            ;
#endif
        else
            wmix_task_play_wav(
                wmtp->wmix, name, msg_fd, (wmtp->flag >> 8) & 0xFF, (wmtp->flag >> 16) & 0xFF);

        if (joinQueue)
            wmtp->wmix->onPlayCount -= 1;
    }

    if (queue >= 0)
        wmtp->wmix->queue.head += 1;

    if (msg_fd)
        msgctl(msg_fd, IPC_RMID, NULL);
    if (msgPath)
        remove(msgPath);
    //线程计数
    wmtp->wmix->thread_play -= 1;

    if (wmtp->param)
        free(wmtp->param);
    free(wmtp);
}

//============= 辅助aec使用的数据队列 =============

// recordPkgBuff FIFO
static uint8_t recordPkgBuff[WMIX_PKG_SIZE];
static uint8_t _recordPkgBuff[AEC_FIFO_PKG_NUM][WMIX_PKG_SIZE];
static int _recordPkgBuff_count = 0;
// 入栈
void recordPkgBuff_add(uint8_t *pkgBuff)
{
    memcpy(_recordPkgBuff[_recordPkgBuff_count++], pkgBuff, WMIX_PKG_SIZE);
    if (_recordPkgBuff_count >= AEC_FIFO_PKG_NUM)
        _recordPkgBuff_count = 0;
}
// 出栈, delayms: 延后时长
uint8_t *recordPkgBuff_get(uint8_t *buff, int delayms)
{
    uint8_t *ret = buff;
    //包偏移
    int pkgCount = _recordPkgBuff_count - (delayms / WMIX_INTERVAL_MS);
    //字节偏移
    int byteCount = (int)((float)((delayms % WMIX_INTERVAL_MS) * WMIX_FRAME_NUM) / WMIX_INTERVAL_MS) * WMIX_FRAME_SIZE;
    //范围限制
    if (pkgCount >= AEC_FIFO_PKG_NUM)
        pkgCount = AEC_FIFO_PKG_NUM;
    else if (pkgCount < 0)
        pkgCount = 0;
    //与当前的相对位置
    pkgCount = _recordPkgBuff_count - pkgCount;
    //循环
    if (pkgCount >= AEC_FIFO_PKG_NUM)
        pkgCount -= AEC_FIFO_PKG_NUM;
    else if (pkgCount < 0)
        pkgCount += AEC_FIFO_PKG_NUM;
    //包偏移 + 帧偏移
    if (byteCount > 0)
    {
        if (pkgCount == 0)
            memcpy(buff, _recordPkgBuff[AEC_FIFO_PKG_NUM - 1] - byteCount, byteCount);
        else
            memcpy(buff, _recordPkgBuff[pkgCount - 1] - byteCount, byteCount);
        buff += byteCount;
    }
    //剩余偏移量
    memcpy(buff, _recordPkgBuff[pkgCount], WMIX_PKG_SIZE - byteCount);

    return ret;
}

// playPkgBuff FIFO
#if (MAKE_WEBRTC_AEC || MAKE_SPEEX_BETA3)
static uint8_t playPkgBuff[WMIX_PKG_SIZE];
#endif
static uint8_t _playPkgBuff[AEC_FIFO_PKG_NUM][WMIX_PKG_SIZE];
static int _playPkgBuff_count = 0;
// 入栈
void playPkgBuff_add(uint8_t *pkgBuff)
{
    memcpy(_playPkgBuff[_playPkgBuff_count++], pkgBuff, WMIX_PKG_SIZE);
    if (_playPkgBuff_count >= AEC_FIFO_PKG_NUM)
        _playPkgBuff_count = 0;
}
// 出栈, delayms: 延后时长
uint8_t *playPkgBuff_get(uint8_t *buff, int delayms)
{
    uint8_t *ret = buff;
    //包偏移
    int pkgCount = _playPkgBuff_count - (delayms / WMIX_INTERVAL_MS);
    //字节偏移
    int byteCount = (int)((float)((delayms % WMIX_INTERVAL_MS) * WMIX_FRAME_NUM) / WMIX_INTERVAL_MS) * WMIX_FRAME_SIZE;
    //范围限制
    if (pkgCount >= AEC_FIFO_PKG_NUM)
        pkgCount = AEC_FIFO_PKG_NUM;
    else if (pkgCount < 0)
        pkgCount = 0;
    //与当前的相对位置
    pkgCount = _playPkgBuff_count - pkgCount;
    //循环
    if (pkgCount >= AEC_FIFO_PKG_NUM)
        pkgCount -= AEC_FIFO_PKG_NUM;
    else if (pkgCount < 0)
        pkgCount += AEC_FIFO_PKG_NUM;
    //包偏移 + 帧偏移
    if (byteCount > 0)
    {
        if (pkgCount == 0)
            memcpy(buff, _playPkgBuff[AEC_FIFO_PKG_NUM - 1] - byteCount, byteCount);
        else
            memcpy(buff, _playPkgBuff[pkgCount - 1] - byteCount, byteCount);
        buff += byteCount;
    }
    //剩余偏移量
    memcpy(buff, _playPkgBuff[pkgCount], WMIX_PKG_SIZE - byteCount);

    return ret;
}

void wmix_shmem_write_circle(WMixThread_Param *wmtp)
{
    WMix_Struct *wmix = wmtp->wmix;
    WMix_Point src, dist;
    static WMix_Point rwTestSrc = {.U8 = 0}, rwTestHead = {.U8 = 0};
    static uint32_t tick = 0;
    size_t count, ret;
    //音频转换中间参数
    float divCount, divPow;
    //转换目标格式, "/tmp/wmix", 'I' 共享内存写入音频参数
    int chn = 1, freq = 8000;
    //转换后的包大小
    size_t buffSize2;
#ifndef WMIX_RECORD_PLAY_SYNC
    //严格录音间隔
    DELAY_INIT;
    //采样时间间隔us
    int intervalUs = WMIX_INTERVAL_MS * 1000 - 2000;
#endif
    int frame_size = WMIX_FRAME_SIZE;
    //每包字节数
    uint8_t buff[WMIX_PKG_SIZE];

#ifdef AEC_SYNC_SAVE_FILE
    static int fd = 0;
    int16_t *pL, *pR;
    int aec_sync_c;
    if (fd == 0)
        fd = open(AEC_SYNC_SAVE_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0666);
#endif

    //录音频率和目标频率的比值
    divPow = (float)(WMIX_FREQ - freq) / freq;
    divCount = 0;

#ifdef WMIX_RECORD_PLAY_SYNC
    if (wmix->run)
#else
    //线程数+1
    wmix->thread_sys += 1;
    DELAY_RESET();
    while (wmix->run)
#endif
    {
        //失能释放
#if (MAKE_WEBRTC_VAD)
        if (!wmix->webrtcEnable[WR_VAD] && wmix->webrtcPoint[WR_VAD])
        {
            vad_release(wmix->webrtcPoint[WR_VAD]);
            wmix->webrtcPoint[WR_VAD] = NULL;
        }
#endif
        //失能释放
#if (MAKE_WEBRTC_NS)
        if (!wmix->webrtcEnable[WR_NS] && wmix->webrtcPoint[WR_NS])
        {
            ns_release(wmix->webrtcPoint[WR_NS]);
            wmix->webrtcPoint[WR_NS] = NULL;
        }
#endif
        //失能释放
#if (MAKE_WEBRTC_AEC)
        if (!wmix->webrtcEnable[WR_AEC] && wmix->webrtcPoint[WR_AEC])
        {
            aec_release(wmix->webrtcPoint[WR_AEC]);
            wmix->webrtcPoint[WR_AEC] = NULL;
        }
#elif (MAKE_SPEEX_BETA3)
        if (!wmix->webrtcEnable[WR_AEC] && wmix->webrtcPoint[WR_AEC])
        {
            spx_aec_release(wmix->webrtcPoint[WR_AEC]);
            wmix->webrtcPoint[WR_AEC] = NULL;
        }
#endif
        //失能释放
#if (MAKE_WEBRTC_AGC)
        if (!wmix->webrtcEnable[WR_AGC] && wmix->webrtcPoint[WR_AGC])
        {
            agc_release(wmix->webrtcPoint[WR_AGC]);
            wmix->webrtcPoint[WR_AGC] = NULL;
        }
#endif
        //有录音线程或有客户端在用mem(内存共享)数据录音
        if (wmix->recordRun || wmix->shmemRun > 0 || wmix->rwTest)
        {
            if (wmix->objAi)
            {
                //读取一包数据
                memset(buff, 0, WMIX_PKG_SIZE);
                ret = wmix_ai_read(wmix->objAi, buff, WMIX_PKG_SIZE);
                if (ret > 0)
                {
                    //一包数据进入队列(要求ret == WMIX_PKG_SIZE)
                    recordPkgBuff_add(buff);
#if (MAKE_WEBRTC_NS)
                    //噪音抑制
                    if (wmix->webrtcEnable[WR_NS] && WMIX_FREQ <= 32000 && WMIX_FREQ % 8000 == 0)
                    {
                        if (wmix->webrtcPoint[WR_NS] == NULL)
                            wmix->webrtcPoint[WR_NS] = ns_init(WMIX_CHN, WMIX_FREQ, &wmix->debug);
                        if (wmix->webrtcPoint[WR_NS])
                        {
                            //开始转换
                            ns_process(
                                wmix->webrtcPoint[WR_NS],
                                (int16_t *)buff,
                                (int16_t *)buff,
                                WMIX_FRAME_NUM);
                        }
                    }
#endif

#if (MAKE_WEBRTC_AEC)
                    //回声消除 (16000Hz时要求CPU算力较高)
                    if (wmix->webrtcEnable[WR_AEC] && WMIX_FREQ <= 16000 && WMIX_FREQ % 8000 == 0)
                    {
                        if (wmix->webrtcPoint[WR_AEC] == NULL)
                            wmix->webrtcPoint[WR_AEC] = aec_init(WMIX_CHN, WMIX_FREQ, WMIX_INTERVAL_MS, &wmix->debug);
                        if (wmix->webrtcPoint[WR_AEC])
                        {

#ifdef AEC_SYNC_SAVE_FILE
                            playPkgBuff_get(playPkgBuff, AEC_INTERVALMS);
                            pL = (int16_t *)buff;
                            pR = (int16_t *)playPkgBuff;
                            for (aec_sync_c = 0; aec_sync_c < WMIX_FRAME_NUM; aec_sync_c++)
                            {
                                write(fd, pL++, 2);
                                write(fd, pR++, 2);
                            }
#endif
                            //开始转换
                            aec_process2(
                                wmix->webrtcPoint[WR_AEC],
                                (int16_t *)playPkgBuff_get(playPkgBuff, AEC_INTERVALMS), //要消除的数据,即 播音数据
                                (int16_t *)buff,                                          //混杂的数据,即 播音数据 + 人说话声音
                                (int16_t *)buff,                                          //输出的数据,得 人说话声音
                                WMIX_FRAME_NUM,
                                0); //评估回声时延
                        }
                    }
#elif (MAKE_SPEEX_BETA3)
                    //回声消除 (16000Hz时要求CPU算力较高)
                    if (wmix->webrtcEnable[WR_AEC] && WMIX_FREQ <= 16000 && WMIX_FREQ % 8000 == 0)
                    {
                        if (wmix->webrtcPoint[WR_AEC] == NULL)
                            wmix->webrtcPoint[WR_AEC] = spx_aec_init(WMIX_CHN, WMIX_FREQ, WMIX_INTERVAL_MS, 0, &wmix->debug);
                        if (wmix->webrtcPoint[WR_AEC])
                        {
                            //开始转换
                            spx_aec_process(
                                wmix->webrtcPoint[WR_AEC],
                                (int16_t *)playPkgBuff_get(playPkgBuff, AEC_INTERVALMS), //要消除的数据,即 播音数据
                                (int16_t *)buff,                                          //混杂的数据,即 播音数据 + 人说话声音
                                (int16_t *)buff,                                          //输出的数据,得 人说话声音
                                WMIX_FRAME_NUM);
                        }
                    }
#endif

#if (MAKE_WEBRTC_AGC)
                    //录音增益
                    if (wmix->webrtcEnable[WR_AGC] && WMIX_FREQ <= 32000 && WMIX_FREQ % 8000 == 0)
                    {
                        if (wmix->webrtcPoint[WR_AGC] == NULL)
                            wmix->webrtcPoint[WR_AGC] = agc_init(WMIX_CHN, WMIX_FREQ, WMIX_INTERVAL_MS, wmix->volumeAgc, &wmix->debug);
                        if (wmix->webrtcPoint[WR_AGC])
                        {
                            //开始转换
                            agc_process(
                                wmix->webrtcPoint[WR_AGC],
                                (int16_t *)buff,
                                (int16_t *)buff,
                                WMIX_FRAME_NUM);
                        }
                    }
#endif

#if (MAKE_WEBRTC_VAD)
                    //人声识别
                    if (wmix->webrtcEnable[WR_VAD] && WMIX_FREQ <= 32000 && WMIX_FREQ % 8000 == 0)
                    {
                        // 人声识别,初始化
                        if (wmix->webrtcPoint[WR_VAD] == NULL)
                            wmix->webrtcPoint[WR_VAD] = vad_init(WMIX_CHN, WMIX_FREQ, WMIX_INTERVAL_MS, &wmix->debug);
                        if (wmix->webrtcPoint[WR_VAD])
                            vad_process(
                                wmix->webrtcPoint[WR_VAD],
                                (int16_t *)buff,
                                WMIX_FRAME_NUM);
                    }
#endif
                    //原始数据写共享内存
                    wmix_mem_write2((int16_t *)buff, ret / 2);

                    //自收发测试
                    if (wmix->rwTest)
                    {
                        rwTestSrc.U8 = buff;
                        rwTestHead = wmix_load_data(
                            wmix,
                            rwTestSrc,
                            ret,
                            WMIX_FREQ,
                            WMIX_CHN,
                            WMIX_SAMPLE,
                            rwTestHead,
                            1,
                            &tick);
                    }
                    else
                    {
                        rwTestHead.U8 = 0;
                        tick = 0;
                    }

                    //转换格式为单声道8000Hz并存到共享内存
                    RECORD_DATA_TRANSFER();
                    wmix_mem_write((int16_t *)buff, buffSize2 / 2);
                }
                else
                {
                    //没录到声音
                    memset(recordPkgBuff, 0, WMIX_PKG_SIZE);
                    recordPkgBuff_add(recordPkgBuff);
                }
            }
            else
            {
                //没录到声音
                memset(recordPkgBuff, 0, WMIX_PKG_SIZE);
                recordPkgBuff_add(recordPkgBuff);

                if ((wmix->objAi = wmix_ai_init(WMIX_CHN, WMIX_FREQ)))
                {
                    if (wmix->debug)
                        printf("wmix record: start\r\n");
                    //录音量更新
                    if (wmix->volumeMic != wmix_ai_vol_get(wmix->objAi))
                        wmix_ai_vol_set(wmix->objAi, wmix->volumeMic);
                    //丢弃一包数据
                    wmix_ai_read(wmix->objAi, buff, WMIX_PKG_SIZE);
#ifdef WMIX_RECORD_PLAY_SYNC
                    return;
#else
                    continue;
#endif
                }
            }
        }
        else
        {
            //没录到声音
            memset(recordPkgBuff, 0, WMIX_PKG_SIZE);
            recordPkgBuff_add(recordPkgBuff);
            //无录音任务释放录音句柄
            if (wmix->objAi)
            {
                if (wmix->debug)
                    printf("wmix record: clear\r\n");
                wmix_ai_exit(wmix->objAi);
                wmix->objAi = NULL;
            }
            //失能释放
#if (MAKE_WEBRTC_VAD)
            if (wmix->webrtcPoint[WR_VAD])
            {
                vad_release(wmix->webrtcPoint[WR_VAD]);
                wmix->webrtcPoint[WR_VAD] = NULL;
            }
#endif
            //失能释放
#if (MAKE_WEBRTC_NS)
            if (wmix->webrtcPoint[WR_NS])
            {
                ns_release(wmix->webrtcPoint[WR_NS]);
                wmix->webrtcPoint[WR_NS] = NULL;
            }
#endif
            //失能释放
#if (MAKE_WEBRTC_AEC)
            if (wmix->webrtcPoint[WR_AEC])
            {
                aec_release(wmix->webrtcPoint[WR_AEC]);
                wmix->webrtcPoint[WR_AEC] = NULL;
            }
#endif
            //失能释放
#if (MAKE_WEBRTC_AGC)
            if (wmix->webrtcPoint[WR_AGC])
            {
                agc_release(wmix->webrtcPoint[WR_AGC]);
                wmix->webrtcPoint[WR_AGC] = NULL;
            }
#endif
        }

#ifdef WMIX_RECORD_PLAY_SYNC
        return;
#else
        //矫正到20ms
        DELAY_US(intervalUs);
#endif
    }
#ifdef AEC_SYNC_SAVE_FILE
    close(fd);
#endif
    //失能释放
#if (MAKE_WEBRTC_VAD)
    if (!wmix->webrtcEnable[WR_VAD] && wmix->webrtcPoint[WR_VAD])
    {
        vad_release(wmix->webrtcPoint[WR_VAD]);
        wmix->webrtcPoint[WR_VAD] = NULL;
    }
#endif
    //失能释放
#if (MAKE_WEBRTC_NS)
    if (!wmix->webrtcEnable[WR_NS] && wmix->webrtcPoint[WR_NS])
    {
        ns_release(wmix->webrtcPoint[WR_NS]);
        wmix->webrtcPoint[WR_NS] = NULL;
    }
#endif
    //失能释放
#if (MAKE_WEBRTC_AEC)
    if (!wmix->webrtcEnable[WR_AEC] && wmix->webrtcPoint[WR_AEC])
    {
        aec_release(wmix->webrtcPoint[WR_AEC]);
        wmix->webrtcPoint[WR_AEC] = NULL;
    }
#elif (MAKE_SPEEX_BETA3)
    if (!wmix->webrtcEnable[WR_AEC] && wmix->webrtcPoint[WR_AEC])
    {
        spx_aec_release(wmix->webrtcPoint[WR_AEC]);
        wmix->webrtcPoint[WR_AEC] = NULL;
    }
#endif
    //失能释放
#if (MAKE_WEBRTC_AGC)
    if (!wmix->webrtcEnable[WR_AGC] && wmix->webrtcPoint[WR_AGC])
    {
        agc_release(wmix->webrtcPoint[WR_AGC]);
        wmix->webrtcPoint[WR_AGC] = NULL;
    }
#endif
    if (wmix->objAi)
    {
        wmix_ai_exit(wmix->objAi);
        wmix->objAi = NULL;
    }
#ifndef WMIX_RECORD_PLAY_SYNC
    wmix->thread_sys -= 1;
#endif
}

void wmix_msg_thread(WMixThread_Param *wmtp)
{
    WMix_Struct *wmix = wmtp->wmix;
    WMix_Msg msg;
    ssize_t ret;
    bool err_exit = false;
    //刚启动,playRun和recordRun都为false,这里置9999,不再清理
    int playTickTimeout = 9999, recordTickTimeout = 9999;
    WAVContainer_t wav;

    //路径检查 //F_OK 是否存在 R_OK 是否有读权限 W_OK 是否有写权限 X_OK 是否有执行权限
    if (access(WMIX_MSG_PATH, F_OK) != 0)
        mkdir(WMIX_MSG_PATH, 0777);
    //再次检查
    if (access(WMIX_MSG_PATH, F_OK) != 0)
    {
        WMIX_ERR("msg path not found\r\n");
        return;
    }
    //清空文件夹
    system(WMIX_MSG_PATH_CLEAR);
    //权限处理
    system(WMIX_MSG_PATH_AUTHORITY);
    //获得管道
    if ((wmix->msg_key = ftok(WMIX_MSG_PATH, WMIX_MSG_ID)) == -1)
    {
        WMIX_ERR("ftok err\r\n");
        return;
    }
    //清空队列
    if ((wmix->msg_fd = msgget(wmix->msg_key, 0666)) != -1)
        msgctl(wmix->msg_fd, IPC_RMID, NULL);
    //重新创建队列
    if ((wmix->msg_fd = msgget(wmix->msg_key, IPC_CREAT | 0666)) == -1)
    {
        WMIX_ERR("msgget err\r\n");
        return;
    }
    //线程计数
    wmix->thread_sys += 1;
    //接收来信
    while (wmix->run)
    {
        memset(&msg, 0, sizeof(WMix_Msg));
        ret = msgrcv(wmix->msg_fd, &msg, WMIX_MSG_BUFF_SIZE, 0, IPC_NOWAIT); //返回队列中的第一个消息 非阻塞方式
        if (ret > 0)
        {
            if (wmix->debug)
                printf("\r\nwmix_msg_thread: msg %ld -- val[0] %d\r\n", msg.type & 0xFF, msg.value[0]);

            switch (msg.type & 0xFF)
            {
            //音量设置
            case WMT_VOLUME:
                wmix_ao_vol_set(wmix->objAo, msg.value[0]);
                break;
            //互斥播放音频
            case WMT_PLYAY_MUTEX:
                wmix->loopWord += 1;
            //混音播放音频
            case WMT_PLAY_MIX:
            //排头播放
            case WMT_PLAY_FIRST:
            //排尾播放
            case WMT_PLAY_LAST:
                wmix_load_thread(wmix,
                                 msg.type,
                                 msg.value,
                                 WMIX_MSG_BUFF_SIZE,
                                 &wmix_load_task);
                break;
            //fifo播放wav流
            case WMT_FIFO_PLAY:
                wmix_load_thread(wmix,
                                 msg.type,
                                 msg.value,
                                 WMIX_MSG_BUFF_SIZE,
                                 &wmix_thread_play_wav_fifo);
                break;
            //复位
            case WMT_RESET:
                wmix->loopWord += 1;
                wmix->run = false;
                break;
            //fifo录音wav流
            case WMT_FIFO_RECORD:
                wmix_load_thread(wmix,
                                 msg.type,
                                 msg.value,
                                 WMIX_MSG_BUFF_SIZE,
                                 &wmix_thread_record_wav_fifo);
                break;
            //录音wav文件
            case WMT_RECORD_WAV:
                wmix_load_thread(wmix,
                                 msg.type,
                                 msg.value,
                                 WMIX_MSG_BUFF_SIZE,
                                 &wmix_thread_record_wav);
                break;
            //清空播放列表
            case WMT_CLEAN_LIST:
                wmix->loopWord += 1;
                break;
            //rtp send pcma
            case WMT_RTP_SEND_PCMA:
                wmix_load_thread(wmix,
                                 msg.type,
                                 msg.value,
                                 WMIX_MSG_BUFF_SIZE,
                                 &wmix_thread_rtp_send_pcma);
                break;
            //rtp recv pcma
            case WMT_RTP_RECV_PCMA:
                wmix_load_thread(wmix,
                                 msg.type,
                                 msg.value,
                                 WMIX_MSG_BUFF_SIZE,
                                 &wmix_thread_rtp_recv_pcma);
                break;
#if (MAKE_AAC)
            //录音aac文件
            case WMT_RECORD_AAC:
                wmix_load_thread(wmix,
                                 msg.type,
                                 msg.value,
                                 WMIX_MSG_BUFF_SIZE,
                                 &wmix_thread_record_aac);
                break;
#endif
            //开/关 shmem
            case WMT_MEM_SW:
                if (msg.value[0])
                    wmix->shmemRun += 1;
                else
                {
                    wmix->shmemRun -= 1;
                    if (wmix->shmemRun < 0)
                        wmix->shmemRun = 0;
                }
                break;
            //开/关 webrtc.vad
            case WMT_WEBRTC_VAD_SW:
                if (msg.value[0])
                    wmix->webrtcEnable[WR_VAD] = 1;
                else
                    wmix->webrtcEnable[WR_VAD] = 0;
                break;
            //开/关 webrtc.aec
            case WMT_WEBRTC_AEC_SW:
                if (msg.value[0])
                    wmix->webrtcEnable[WR_AEC] = 1;
                else
                    wmix->webrtcEnable[WR_AEC] = 0;
                break;
            //开/关 webrtc.ns
            case WMT_WEBRTC_NS_SW:
                if (msg.value[0])
                    wmix->webrtcEnable[WR_NS] = 1;
                else
                    wmix->webrtcEnable[WR_NS] = 0;
                break;
            //开/关 webrtc.ns_pa
            case WMT_WEBRTC_NS_PA_SW:
                if (msg.value[0])
                    wmix->webrtcEnable[WR_NS_PA] = 1;
                else
                    wmix->webrtcEnable[WR_NS_PA] = 0;
                break;
            //开/关 webrtc.agc
            case WMT_WEBRTC_AGC_SW:
                if (msg.value[0])
                    wmix->webrtcEnable[WR_AGC] = 1;
                else
                    wmix->webrtcEnable[WR_AGC] = 0;
                break;
            //自发收测试
            case WMT_RW_TEST:
                if (msg.value[0])
                    wmix->rwTest = true;
                else
                    wmix->rwTest = false;
                break;
            //录音音量设置
            case WMT_VOLUME_MIC:
                if (wmix->objAi)
                    wmix_ai_vol_set(wmix->objAi, msg.value[0]);
                wmix->volumeMic = msg.value[0] < 10 ? msg.value[0] : 10;
                break;
            //录音音量增益设置
            case WMT_VOLUME_AGC:
#if (MAKE_WEBRTC_AGC)
                if (wmix->webrtcEnable[WR_AGC])
                {
                    wmix->volumeAgc = msg.value[0];
                    if (wmix->webrtcPoint[WR_AGC])
                        agc_addition(wmix->webrtcPoint[WR_AGC], msg.value[0]);
                }
#endif
                break;
#if (MAKE_AAC)
            //rtp send aac
            case WMT_RTP_SEND_AAC:
                wmix_load_thread(wmix,
                                 msg.type,
                                 msg.value,
                                 WMIX_MSG_BUFF_SIZE,
                                 &wmix_thread_rtp_send_aac);
                break;
            //rtp recv aac
            case WMT_RTP_RECV_AAC:
                wmix_load_thread(wmix,
                                 msg.type,
                                 msg.value,
                                 WMIX_MSG_BUFF_SIZE,
                                 &wmix_thread_rtp_recv_aac);
                break;
#endif
            //关闭所有播放和录音
            case WMT_CLEAN_ALL:
                wmix->loopWord += 1;
                wmix->loopWordRecord += 1;
                wmix->loopWordFifo += 1;
                wmix->loopWordRtp += 1;
                break;
            //保存混音数据池的数据流到wav文件,写0关闭
            case WMT_NOTE:
                //关闭正在进行的note
                wmix->notePath[0] = 0;
                //这是一次关闭指令
                if (!msg.value[0])
                    break;
                //等待关闭
                while (wmix->noteFd > 0)
                    delayus(5000);
                //开始新文件
                wmix->noteFd = open((const char *)msg.value, O_WRONLY | O_CREAT | O_TRUNC, 0666);
                if (wmix->noteFd < 1)
                {
                    printf("%s: create note %s failed !!\r\n", __func__, msg.value);
                    wmix->noteFd = 0;
                    break;
                }
                //创建wav头
                WAV_Params(&wav, 10, WMIX_CHN, WMIX_SAMPLE, WMIX_FREQ);
                WAV_WriteHeader(wmix->noteFd, &wav);
                fsync(wmix->noteFd);
                //通知 wmix_play_thread 开始写数据
                strcpy(wmix->notePath, (char *)msg.value);
                break;
#if (MAKE_MATH_FFT)
            //输出幅频/相频图像到fb设备或bmp文件,写0关闭
            case WMT_FFT:
                //这是一次关闭指令
                if (!msg.value[0])
                {
                    if (!wmix->fftPath[0])
                    {
                        wmix->fftPath[0] = 0;
                    }
                    break;
                }
                break;
#endif
#if (MAKE_AAC)
            //fifo录音aac流
            case WMT_FIFO_AAC:
                wmix_load_thread(wmix,
                                 msg.type,
                                 msg.value,
                                 WMIX_MSG_BUFF_SIZE,
                                 &wmix_thread_record_aac_fifo);
                break;
#endif
            //开关log
            case WMT_LOG_SW:
                if (msg.value[0])
                    wmix->debug = true;
                else
                    wmix->debug = false;
                break;
            //打印信息
            case WMT_INFO:
                printf(
                    "\r\n"
                    "\r\n---- WMix info -----\r\n"
                    "\r\n"
                    "   chn: %d\r\n"
                    "   freq: %d Hz\r\n"
                    "   sample: %d bit\r\n"
                    "   volume: play/%d, mic/%d, agc/%d\r\n"
                    "   webrtc: vad/%d, aec/%d, ns/%d, ns_pa/%d agc/%d\r\n"
                    "   playRun: %d\r\n"
                    "   recordRun: %d\r\n"
                    "\r\n"
                    "   circleBuff: tick/%d, buff/%p, head/%p, tail/%p\r\n"
                    "   loopWord: all/%d, record/%d, fifo/%d, rtp/%d\r\n"
                    "   thread: sys/%d, record/%d, play/%d\r\n"
                    "   queue: total/%d, head/%d, tail/%d\r\n"
                    "   shmemRun: %d\r\n"
                    "   reduceMode: %d\r\n"
                    "   note: %s\r\n"
#if (MAKE_MATH_FFT)
                    "   fft: %s\r\n"
#endif
                    "   debug: %d\r\n"
                    "\r\n"
                    "   version: %s\r\n"
                    "\r\n"
                    "\r\n",
                    WMIX_CHN, WMIX_FREQ, WMIX_SAMPLE,
                    wmix->volume, wmix->volumeMic, wmix->volumeAgc,
                    wmix->webrtcEnable[WR_VAD],
                    wmix->webrtcEnable[WR_AEC],
                    wmix->webrtcEnable[WR_NS],
                    wmix->webrtcEnable[WR_NS_PA],
                    wmix->webrtcEnable[WR_AGC],
                    wmix->playRun ? 1 : 0, wmix->recordRun ? 1 : 0,
                    wmix->tick, wmix->buff, wmix->head.U8, wmix->tail.U8,
                    wmix->loopWord, wmix->loopWordRecord, wmix->loopWordFifo, wmix->loopWordRtp,
                    wmix->thread_sys, wmix->thread_record, wmix->thread_play,
                    wmix->onPlayCount, wmix->queue.head, wmix->queue.tail,
                    wmix->shmemRun,
                    wmix->reduceMode,
                    wmix->notePath,
#if (MAKE_MATH_FFT)
                    wmix->fftPath,
#endif
                    wmix->debug ? 1 : 0,
                    WMIX_VERSION);
                break;
            //重定向打印信息输出路径
            case WMT_CONSOLE:
                wmix_console(wmix, (char *)msg.value);
                break;
            }
            continue;
        }
        //在别的地方重开了该程序的副本
        else if (ret < 1 && errno != ENOMSG)
        {
            if (wmix->debug)
                printf("%s: %d msgrecv err/%d\r\n", __func__, wmix->msg_fd, errno);
            err_exit = true;
            break;
        }
        //长时间没有播放任务,关闭播放器
        if (wmix->thread_play == 0 && wmix->shmemRun == 0 && !wmix->rwTest)
        {
            //连续5秒没有播放线程,清tick
            if (playTickTimeout < 5000)
                playTickTimeout += 10;
            else
            {
                //先关闭标志
                if (playTickTimeout < 6000)
                {
                    playTickTimeout += 10;
                    wmix->playRun = false;
                }
                //再清理tick
                else if (playTickTimeout != 9999)
                {
                    if (wmix->debug)
                        printf("wmix play: clear\r\n");
                    playTickTimeout = 9999;
                    wmix->playRun = false;
                    wmix->head.U8 = wmix->tail.U8 = wmix->start.U8;
                    wmix->tick = 0;
                }
            }
        }
        else
        {
            playTickTimeout = 0;
            if (!wmix->playRun)
                printf("wmix play: start\r\n");
            wmix->playRun = true;
        }
        //长时间没有录音任务,关闭录音
        if (wmix->thread_record == 0 && wmix->shmemRun == 0 && !wmix->rwTest)
        {
            //连续5秒没有录音线程,清tick
            if (recordTickTimeout < 5000)
                recordTickTimeout += 10;
            else
            {
                if (recordTickTimeout != 9999)
                {
                    recordTickTimeout = 9999;
                    wmix->recordRun = false;
                }
            }
        }
        else
        {
            recordTickTimeout = 0;
            wmix->recordRun = true;
        }
        delayus(10000);
    }
    //删除队列
    msgctl(wmix->msg_fd, IPC_RMID, NULL);

    if (wmix->debug)
        printf("wmix_msg_thread exit\r\n");

    if (wmtp->param)
        free(wmtp->param);
    free(wmtp);

    if (err_exit)
    {
        wmix_signal(SIGINT);
        exit(0);
    }
    //线程计数
    wmix->thread_sys -= 1;
}

// #define AEC_FILE_STREAM_TEST //回声消除,文件流干扰测试

void wmix_play_thread(WMixThread_Param *wmtp)
{
    WMix_Struct *wmix = wmtp->wmix;
    //for循环时指向目标字符串
    WMix_Point dist;
    //for循环计数
    uint32_t count = 0, countTotal = 0;
    //tick严格延时间隔
    __time_t tick1 = 0, tick2 = 0, tickT = 0;
    //数据量转换为用时us
    double dataToTime = 1000000 / (WMIX_CHN * WMIX_SAMPLE / 8 * WMIX_FREQ);
    //一包数据量
    uint8_t playBuff[WMIX_PKG_SIZE];
#ifdef AEC_FILE_STREAM_TEST
    int i, ret;
    int16_t *p1, *p2;
    uint8_t fileBuff[WMIX_PKG_SIZE];
    int fd = open("./audio/1x8000.wav", O_RDONLY);
    lseek(fd, 44, SEEK_SET);
#endif
    //线程计数
    wmix->thread_sys += 1;
    //wmix 运行标志
    while (wmix->run)
    {
        //播放标志,在 wmix_msg_thread 中判断没有播放任务时置 false
        if (wmix->playRun || wmix->rwTest)
        {
            //循环缓冲区
            if (wmix->head.U8 >= wmix->end.U8)
                wmix->head.U8 = wmix->start.U8;
            //理论延时还没用完
            if (tickT > 0)
            {
                //扣除掉上一次循环到现在的时间
                tick2 = getTickUs() - tick1;
                if (tickT > tick2)
                    tickT -= tick2;
                //当实际运行环境比较忙,过大的延时可能导致播放卡顿
                //通过调小 *0.8 值修复卡顿
                delayus((unsigned int)(tickT * 0.8));
            }
            tick1 = getTickUs();
            for (count = countTotal = 0, dist.U8 = playBuff; countTotal < WMIX_PKG_SIZE * 4;) //每次最多传x4包
            {
#if (WMIX_CHN == 1)
                //每次拷贝 2字节
                *dist.U16++ = *wmix->head.U16; //从循环缓冲区取数据
                *wmix->head.U16++ = 0;         //缓冲区数据清0
                wmix->tick += 2;
                count += 2;
#else
                //每次拷贝 4字节
                *dist.U32++ = *wmix->head.U32; //从循环缓冲区取数据
                *wmix->head.U32++ = 0;         //缓冲区数据清0
                wmix->tick += 4;
                count += 4;
#endif
                //循环缓冲区
                if (wmix->head.U8 >= wmix->end.U8)
                    wmix->head.U8 = wmix->start.U8;
                //一包数装填完毕
                if (count == WMIX_PKG_SIZE)
                {
                    //发包计数
                    countTotal += WMIX_PKG_SIZE;
#if (MAKE_WEBRTC_NS)
                    //噪音抑制
                    if (wmix->webrtcEnable[WR_NS_PA] && WMIX_FREQ <= 32000 && WMIX_FREQ % 8000 == 0)
                    {
                        if (wmix->webrtcPoint[WR_NS_PA] == NULL)
                            wmix->webrtcPoint[WR_NS_PA] = ns_init(WMIX_CHN, WMIX_FREQ, &wmix->debug);
                        if (wmix->webrtcPoint[WR_NS_PA])
                        {
                            //开始转换
                            ns_process(
                                wmix->webrtcPoint[WR_NS_PA],
                                (int16_t *)playBuff,
                                (int16_t *)playBuff,
                                WMIX_FRAME_NUM);
                        }
                    }
#endif

#ifdef AEC_FILE_STREAM_TEST
#if (MAKE_WEBRTC_AEC)
                    //回声消除,文件流干扰测试
                    if (wmix->webrtcEnable[WR_AEC] && WMIX_FREQ <= 32000 && WMIX_FREQ % 8000 == 0)
                    {
                        if (wmix->webrtcPoint[WR_AEC] == NULL)
                            wmix->webrtcPoint[WR_AEC] = aec_init(WMIX_CHN, WMIX_FREQ, WMIX_INTERVAL_MS, &wmix->debug);
                        if (wmix->webrtcPoint[WR_AEC])
                        {
                            memset(fileBuff, 0, sizeof(fileBuff));
                            ret = read(fd, fileBuff, WMIX_PKG_SIZE);
                            if (ret < WMIX_PKG_SIZE)
                                lseek(fd, 44, SEEK_SET);
                            p1 = (int16_t *)fileBuff;
                            p2 = (int16_t *)playBuff;
                            for (i = 0; i < WMIX_FRAME_NUM; i++)
                            {
                                // p1[i] >>= 1;
                                p2[i] += p1[i];
                            }
                            aec_process2(
                                wmix->webrtcPoint[WR_AEC],
                                (int16_t *)fileBuff,
                                (int16_t *)playBuff,
                                (int16_t *)playBuff,
                                WMIX_FRAME_NUM,
                                0); //WMIX_INTERVAL_MS/2);
                        }
                    }
#endif
#endif
                    playPkgBuff_add(playBuff);
                    //开始播放
                    wmix_ao_write(wmix->objAo, playBuff, WMIX_PKG_SIZE);
                    //note模式,写文件
                    if (wmix->noteFd > 0 && wmix->notePath[0])
                    {
                        write(wmix->noteFd, playBuff, WMIX_PKG_SIZE);
                        fsync(wmix->noteFd);
                    }
                    //非note模式,关闭描述符
                    else if (wmix->noteFd > 0 && !wmix->notePath[0])
                    {
                        WAV_WriteLen(wmix->noteFd);
                        close(wmix->noteFd);
                        wmix->noteFd = 0;
                    }

#ifndef AEC_FILE_STREAM_TEST
#ifdef WMIX_RECORD_PLAY_SYNC
                    wmix_shmem_write_circle(wmtp);
#endif
#endif
                    //重置数据和指针
                    memset(playBuff, 0, WMIX_PKG_SIZE);
                    dist.U8 = playBuff;
                    //清拷贝计数
                    count = 0;
                }
            }
            //当前发包后理论应用掉多少时间us
            tickT = countTotal * dataToTime;
            //先从总延时里花掉5000us
            delayus(5000);
            if (tickT > 5000)
                tickT -= 5000;
            else
                tickT = 0;
        }
        //没有音频需要播放,写数据0,使播放器不处于"饥饿"状态
        else
        {
            memset(playBuff, 0, WMIX_PKG_SIZE);
            playPkgBuff_add(playBuff);
            //开始播放
            wmix_ao_write(wmix->objAo, playBuff, WMIX_PKG_SIZE);

#ifdef WMIX_RECORD_PLAY_SYNC
            wmix_shmem_write_circle(wmtp);
#endif

            //没在播放状态的延时,矫正到20ms
            tickT = WMIX_INTERVAL_MS * 1000 - 2000;
            tick2 = getTickUs();
            if (tick2 > tick1 && tick2 - tick1 < tickT)
                delayus(tickT - (tick2 - tick1));
            tick1 = getTickUs();
            tickT = 0;
        }
        //失能释放
#if (MAKE_WEBRTC_NS)
        if (!wmix->webrtcEnable[WR_NS_PA] && wmix->webrtcPoint[WR_NS_PA])
        {
            ns_release(wmix->webrtcPoint[WR_NS_PA]);
            wmix->webrtcPoint[WR_NS_PA] = NULL;
        }
#endif
        //非note模式,关闭描述符
        if (wmix->noteFd > 0 && !wmix->notePath[0])
        {
            WAV_WriteLen(wmix->noteFd);
            close(wmix->noteFd);
            wmix->noteFd = 0;
        }
    }
#ifdef AEC_FILE_STREAM_TEST
    close(fd);
#endif
    //失能释放
#if (MAKE_WEBRTC_NS)
    if (wmix->webrtcPoint[WR_NS_PA])
    {
        ns_release(wmix->webrtcPoint[WR_NS_PA]);
        wmix->webrtcPoint[WR_NS_PA] = NULL;
    }
#endif

#ifdef WMIX_RECORD_PLAY_SYNC
    wmix_shmem_write_circle(wmtp);
#endif

    if (wmix->debug)
        printf("%s: exit\r\n", __func__);
    if (wmtp->param)
        free(wmtp->param);
    free(wmtp);
    //线程计数
    wmix->thread_sys -= 1;
}

void wmix_exit(WMix_Struct *wmix)
{
    int timeout;
    if (wmix)
    {
        wmix->run = false;
        //等待线程关闭,等待各指针不再有人使用
        timeout = 200; //2秒超时
        do
        {
            if (timeout-- < 1)
                break;
            delayus(10000);
        } while (wmix->thread_sys > 0 ||
                 wmix->thread_play > 0 ||
                 wmix->thread_record > 0);
        if (wmix->objAo)
            wmix_ao_exit(wmix->objAo);
        if (wmix->objAi)
            wmix_ai_exit(wmix->objAi);
#if (MAKE_MATH_FFT)
        free(wmix->fftStream);
        free(wmix->fftOutAF);
        free(wmix->fftOutPF);
#endif
        free(wmix);
    }
}

WMix_Struct *wmix_init(void)
{
    WMix_Struct *wmix = NULL;
    void *objAo = NULL, *objAi = NULL;

    //路径检查 //F_OK 是否存在 R_OK 是否有读权限 W_OK 是否有写权限 X_OK 是否有执行权限
    if (access(WMIX_MSG_PATH, F_OK) != 0)
        mkdir(WMIX_MSG_PATH, 0777);

    //录播音指针始化
    objAo = wmix_ao_init(WMIX_CHN, WMIX_FREQ);
    if (!objAo)
    {
        WMIX_ERR("wmix_ao_init failed \r\n");
        return NULL;
    }
    //可以在需要时再初始化
    // objAi = wmix_ai_init(WMIX_CHN, WMIX_FREQ);

    //混音器内部数据初始化
    wmix = (WMix_Struct *)calloc(1, sizeof(WMix_Struct));
    wmix->buff = (uint8_t *)calloc(WMIX_BUFF_SIZE + 4, sizeof(uint8_t));

    wmix->objAo = objAo;
    wmix->objAi = objAi;

    wmix->start.U8 = wmix->head.U8 = wmix->tail.U8 = wmix->buff;
    wmix->end.U8 = wmix->buff + WMIX_BUFF_SIZE;

    wmix->run = true;
    wmix->reduceMode = 1;

    //webrtc功能默认启动状态
    wmix->webrtcEnable[WR_VAD] = 0;
    wmix->webrtcEnable[WR_AEC] = 0;
    wmix->webrtcEnable[WR_NS] = 1;
    wmix->webrtcEnable[WR_NS_PA] = 0; //正常播放用不到
    wmix->webrtcEnable[WR_AGC] = 1;

    //混音器主要线程初始化
#ifndef WMIX_RECORD_PLAY_SYNC
    wmix_load_thread(wmix, 0, NULL, 0, &wmix_shmem_write_circle); //录音及数据写共享内存线程
#endif
    wmix_load_thread(wmix, 0, NULL, 0, &wmix_msg_thread);  //接收客户端消息的线程
    wmix_load_thread(wmix, 0, NULL, 0, &wmix_play_thread); //从播音数据迟取数据并播放的线程

    //默认音量
    wmix->volume = 10;
    wmix->volumeMic = 10;
    wmix->volumeAgc = 5;
    //设置音量
    wmix_ao_vol_set(wmix->objAo, wmix->volume);
    if (wmix->objAi)
        wmix_ai_vol_set(wmix->objAi, wmix->volumeMic);

    //接收 ctrl+c 信号,在进程关闭时做出内存释放处理
    signal(SIGINT, wmix_signal);
    signal(SIGTERM, wmix_signal);
    signal(SIGPIPE, wmix_signal);

#if (MAKE_MATH_FFT)
    wmix->fftStream = (float *)calloc(MAKE_MATH_FFT, sizeof(float));
    wmix->fftOutAF = (float *)calloc(MAKE_MATH_FFT, sizeof(float));
    wmix->fftOutPF = (float *)calloc(MAKE_MATH_FFT, sizeof(float));
#endif

    return wmix;
}

//两声音相加
static int16_t volumeAdd(int16_t L1, int16_t L2)
{
    int32_t sum;
    //
    if (L1 == 0)
        return L2;
    else if (L2 == 0)
        return L1;
    else
    {
        sum = (int32_t)L1 + L2;
        //防止爆表
        if (sum < -32768)
            return -32768;
        else if (sum > 32767)
            return 32767;
        else
            return sum;
    }
}

//要播放的音频数据变更频率后,叠加到播放循环缓冲区数据中(即混音)
WMix_Point wmix_load_data(
    WMix_Struct *wmix,
    WMix_Point src,
    uint32_t srcU8Len,
    uint16_t freq,
    uint8_t channels,
    uint8_t sample,
    WMix_Point head,
    uint8_t reduce,
    uint32_t *tick)
{
    WMix_Point pHead = head, pSrc = src;
    //srcU8Len 计数
    uint32_t count, tickAdd = 0;
    uint8_t *rdce = &wmix->reduceMode, rdce1 = 1;
    //频率差
    int32_t freqErr = WMIX_FREQ - freq;
    //步差计数 和 步差分量
    float divCount, divPow;
    int divCount2;
    //陪衬的数据也要作均值滤波
    int16_t repairBuff[64], repairBuffCount, repairTemp;
    float repairStep, repairStepSum;

    if (!wmix || !wmix->run || !pSrc.U8 || srcU8Len < 1)
        return pHead;

    if (!pHead.U8 || (*tick) < wmix->tick)
    {
        pHead.U8 = wmix->head.U8 + VIEW_PLAY_CORRECT;
        (*tick) = wmix->tick + VIEW_PLAY_CORRECT;
        //循环处理
        if (pHead.U8 >= wmix->end.U8)
            pHead.U8 = wmix->start.U8;
    }

    if (reduce == wmix->reduceMode)
        rdce = &rdce1;
    //---------- 参数一致 直接拷贝 ----------
    if (freq == WMIX_FREQ &&
        channels == WMIX_CHN &&
        sample == WMIX_SAMPLE)
    {
        for (count = 0; count < srcU8Len;)
        {
            //拷贝一帧数据
            *pHead.S16 = volumeAdd(*pHead.S16, *pSrc.S16 / (*rdce));
            pHead.S16++;
            pSrc.S16++;
            count += 2;
            tickAdd += 2;

#if (WMIX_CHN != 1)
            *pHead.S16 = volumeAdd(*pHead.S16, *pSrc.S16 / (*rdce));
            pHead.S16++;
            pSrc.S16++;
            count += 2;
            tickAdd += 2;
#endif
            //循环处理
            if (pHead.U8 >= wmix->end.U8)
                pHead.U8 = wmix->start.U8;
        }
    }
    //---------- 参数不一致 插值拷贝 ----------
    else
    {
        //音频频率大于默认频率 //--- 重复代码比较多且使用可能极小,为减小函数入栈容量,不写了 ---
        if (freqErr < 0)
        {
            divPow = (float)(-freqErr) / WMIX_FREQ;
            //
            switch (sample)
            {
            case 8:
                if (channels == 2)
                    ;
                else if (channels == 1)
                    ;
                break;
            case 16:
                if (channels == 2)
                {
                    for (count = 0, divCount = 0; count < srcU8Len;)
                    {
                        //步差计数已满 跳过帧
                        if (divCount >= 1.0)
                        {
                            pSrc.S16++;
                            pSrc.S16++;

                            divCount -= 1.0;
                            count += 4;
                        }
                        else
                        {
                            //拷贝一帧数据
                            *pHead.S16 = volumeAdd(*pHead.S16, *pSrc.S16 / (*rdce));
                            pHead.S16++;
                            pSrc.S16++;
                            tickAdd += 2;
#if (WMIX_CHN != 1)
                            *pHead.S16 = volumeAdd(*pHead.S16, *pSrc.S16 / (*rdce));
                            pHead.S16++;
                            tickAdd += 2;
#endif
                            pSrc.S16++;

                            divCount += divPow;
                            count += 4;
                        }
                        //循环处理
                        if (pHead.U8 >= wmix->end.U8)
                            pHead.U8 = wmix->start.U8;
                    }
                }
                else if (channels == 1)
                {
                    for (count = 0, divCount = 0; count < srcU8Len;)
                    {
                        //步差计数已满 跳过帧
                        if (divCount >= 1.0)
                        {
                            pSrc.S16++;

                            divCount -= 1.0;
                            count += 2;
                        }
                        else
                        {
                            //拷贝一帧数据
                            *pHead.S16 = volumeAdd(*pHead.S16, *pSrc.S16 / (*rdce));
                            pHead.S16++;
                            // pSrc.S16++;
                            tickAdd += 2;
#if (WMIX_CHN != 1)
                            *pHead.S16 = volumeAdd(*pHead.S16, *pSrc.S16 / (*rdce));
                            pHead.S16++;
                            tickAdd += 2;
#endif
                            pSrc.S16++;

                            divCount += divPow;
                            count += 2;
                        }
                        //循环处理
                        if (pHead.U8 >= wmix->end.U8)
                            pHead.U8 = wmix->start.U8;
                    }
                }
                break;
            case 32:
                if (channels == 2)
                    ;
                else if (channels == 1)
                    ;
                break;
            }
        }
        //音频频率小于等于默认频率
        else
        {
            divPow = (float)freqErr / freq;
            //
            switch (sample)
            {
            //8bit采样 //--- 重复代码比较多且使用可能极小,为减小函数入栈容量,不写了 ---
            case 8:
                if (channels == 2)
                    ;
                else if (channels == 1)
                    ;
                break;
            //16bit采样 //主流的采样方式
            case 16:
                if (channels == 2)
                {
                    for (count = 0, divCount = 0; count < srcU8Len;)
                    {
                        //步差计数已满 跳过帧
                        if (divCount >= 1.0)
                        {
                            //循环缓冲区指针继续移动,pSrc指针不动
                            // *pHead.S16 = volumeAdd(*pHead.S16, *pSrc.S16/(*rdce));
                            *pHead.S16 = volumeAdd(*pHead.S16, repairBuff[repairBuffCount] / (*rdce));
                            pHead.S16++;
                            tickAdd += 2;
#if (WMIX_CHN != 1)
                            // *pHead.S16 = volumeAdd(*pHead.S16, *pSrc.S16/(*rdce));
                            *pHead.S16 = volumeAdd(*pHead.S16, repairBuff[repairBuffCount] / (*rdce));
                            pHead.S16++;
                            tickAdd += 2;
#endif
                            divCount -= 1.0;

                            repairBuffCount += 1;
                        }
                        else
                        {
                            //拷贝一帧数据
                            *pHead.S16 = volumeAdd(*pHead.S16, *pSrc.S16 / (*rdce));
                            pHead.S16++;
                            pSrc.S16++;
                            tickAdd += 2;
#if (WMIX_CHN != 1)
                            *pHead.S16 = volumeAdd(*pHead.S16, *pSrc.S16 / (*rdce));
                            pHead.S16++;
                            tickAdd += 2;
#endif
                            pSrc.S16++;

                            divCount += divPow;
                            count += 4;

                            //填充数据均值滤波
                            if (divCount >= 1.0)
                            {
                                divCount2 = (int)divCount + 1;
                                repairTemp = *(pSrc.S16 - 2);
                                repairStep = (float)((*pSrc.S16) - repairTemp) / divCount2;
                                for (repairBuffCount = 0, repairStepSum = repairStep; repairBuffCount < divCount2;)
                                {
                                    repairBuff[repairBuffCount] = repairTemp + repairStepSum;
                                    repairBuffCount += 1;
                                    repairStepSum += repairStep;
                                }
                                repairBuffCount = 0;
                            }
                        }
                        //循环处理
                        if (pHead.U8 >= wmix->end.U8)
                            pHead.U8 = wmix->start.U8;
                    }
                }
                else if (channels == 1)
                {
                    for (count = 0, divCount = 0; count < srcU8Len;)
                    {
                        if (divCount >= 1.0)
                        {
                            //拷贝一帧数据 pSrc指针不动
                            // *pHead.S16 = volumeAdd(*pHead.S16, *pSrc.S16/(*rdce));
                            *pHead.S16 = volumeAdd(*pHead.S16, repairBuff[repairBuffCount] / (*rdce));
                            pHead.S16++;
                            tickAdd += 2;
#if (WMIX_CHN != 1)
                            // *pHead.S16 = volumeAdd(*pHead.S16, *pSrc.S16/(*rdce));
                            *pHead.S16 = volumeAdd(*pHead.S16, repairBuff[repairBuffCount] / (*rdce));
                            pHead.S16++;
                            tickAdd += 2;
#endif
                            divCount -= 1.0;

                            repairBuffCount += 1;
                        }
                        else
                        {
                            //拷贝一帧数据
                            *pHead.S16 = volumeAdd(*pHead.S16, *pSrc.S16 / (*rdce));
                            pHead.S16++;
                            tickAdd += 2;
#if (WMIX_CHN != 1)
                            *pHead.S16 = volumeAdd(*pHead.S16, *pSrc.S16 / (*rdce));
                            pHead.S16++;
                            tickAdd += 2;
#endif
                            pSrc.S16++;

                            divCount += divPow;
                            count += 2;

                            //填充数据均值滤波
                            if (divCount >= 1.0)
                            {
                                divCount2 = (int)divCount + 1;
                                repairTemp = *(pSrc.S16 - 1);
                                repairStep = (float)((*pSrc.S16) - repairTemp) / divCount2;
                                for (repairBuffCount = 0, repairStepSum = repairStep; repairBuffCount < divCount2;)
                                {
                                    repairBuff[repairBuffCount] = repairTemp + repairStepSum;
                                    repairBuffCount += 1;
                                    repairStepSum += repairStep;
                                }
                                repairBuffCount = 0;
                            }
                        }
                        //循环处理
                        if (pHead.U8 >= wmix->end.U8)
                            pHead.U8 = wmix->start.U8;
                    }
                }
                break;
            //32bit采样 //--- 重复代码比较多且使用可能极小,为减小函数入栈容量,不写了 ---
            case 32:
                if (channels == 2)
                    ;
                else if (channels == 1)
                    ;
                break;
            }
        }
    }

    //当前播放指针已慢于播放指针,更新为播放指针
    if ((*tick) < wmix->tick)
    {
        pHead.U8 = wmix->head.U8 + tickAdd;
        tickAdd += wmix->tick;

        if (pHead.U8 >= wmix->end.U8)
            pHead.U8 -= WMIX_BUFF_SIZE;
    }
    else
        tickAdd += (*tick);

    *tick = tickAdd;

    return pHead;
}

//--------------- wmix main ---------------

// 全局唯一指针
static WMix_Struct *main_wmix = NULL;

// 信号事件接收,主要是识别ctrl+c结束时完成收尾工作
void wmix_signal(int signo)
{
    if (SIGINT == signo || SIGTERM == signo)
    {
        wmix_exit(main_wmix);
        exit(0);
    }
}

void help(char *argv0)
{
    printf(
        "\r\n"
        "Usage: %s [option]\r\n"
        "\r\n"
        "Option:\r\n"
        "  -d : 显示debug信息\r\n"
        "  -v volume : 设置音量0~10\r\n"
        "  -vr volume : 设置录音音量0~10\r\n"
        "  -vad 0/1 : 关/开 webrtc.vad 人声识别,录音辅助,在没人说话时主动静音\n"
        "  -aec 0/1 : 关/开 webrtc.aec 回声消除\n"
        "  -ns 0/1 : 关/开 webrtc.ns 噪音抑制(录音)\n"
        "  -ns_pa 0/1 : 关/开 webrtc.ns 噪音抑制(播音)\n"
        "  -agc 0/1 : 关/开 webrtc.agc 自动增益\n"
        "  -? --help : 显示帮助\r\n"
        "  -console path : 重定向打印信息输出路径,path示例: /dev/console /dev/ttyAMA0 或者文件\n"
        "\r\n"
        "Version: %s\r\n"
        "\r\n"
        "Example:\r\n"
        "  %s &\r\n"
        "\r\n",
        argv0, WMIX_VERSION, argv0);
}

void show_setup(void)
{
    printf("\n---- WMix info -----\r\n"
           "   chn: %d\r\n"
           "   freq: %d Hz\r\n"
           "   sample: %d bit\r\n"
           "   webrtc: vad/%d, aec/%d, ns/%d, ns_pa/%d agc/%d\r\n",
           WMIX_CHN, WMIX_FREQ, WMIX_SAMPLE,
           main_wmix->webrtcEnable[WR_VAD],
           main_wmix->webrtcEnable[WR_AEC],
           main_wmix->webrtcEnable[WR_NS],
           main_wmix->webrtcEnable[WR_NS_PA],
           main_wmix->webrtcEnable[WR_AGC]);
}

void main_loop(WMixThread_Param *wmtp)
{
    int consoleSyncCount = 0;
    sleep(1);
    while (1)
    {
        //各线程全都关闭时,reset混音器
        if (wmtp->wmix->run == false &&
            wmtp->wmix->thread_sys == 0 &&
            wmtp->wmix->thread_record == 0 &&
            wmtp->wmix->thread_play == 0)
        {
            delayus(500000);
            wmtp->wmix->run = true;
#ifndef WMIX_RECORD_PLAY_SYNC
            wmix_load_thread(wmtp->wmix, 0, NULL, 0, &wmix_shmem_write_circle);
#endif
            wmix_load_thread(wmtp->wmix, 0, NULL, 0, &wmix_msg_thread);
            wmix_load_thread(wmtp->wmix, 0, NULL, 0, &wmix_play_thread);
        }
        delayus(500000);

        //当stdout指向文件时,定时刷新输出
        consoleSyncCount += 500;
        if (consoleSyncCount > 1999)
        {
            consoleSyncCount = 0;
            if (wmtp->wmix->consoleType == 1)
                fflush(stdout);
        }
    }
}

//第三方程序调用入口
void wmix_start(void)
{
    main_wmix = wmix_init();
    if (main_wmix)
    {
        //开始
        show_setup();
        wmix_load_thread(main_wmix, 0, NULL, 0, &main_loop);
    }
    else
        printf("audio init failed !!\r\n");
}

#if 1
//主程序入口
int main(int argc, char **argv)
{
    int i, volume = -1, volumeMic = -1, volumeAgc = -1;
    char *path = NULL; //启动音频路径
    char *consolePath = NULL;
    int argvLen;

    //传入参数处理
    if (argc > 1)
    {
        for (i = 1; i < argc; i++)
        {
            if (strstr(argv[i], "-?") || strstr(argv[i], "help"))
            {
                help(argv[0]);
                return 0;
            }
        }
    }

    main_wmix = wmix_init();

    if (main_wmix)
    {
        //传入参数处理
        if (argc > 1)
        {
            for (i = 1; i < argc; i++)
            {
                argvLen = strlen(argv[i]);

                if (argvLen == 2 && strstr(argv[i], "-d"))
                {
                    main_wmix->debug = true;
                }
                else if (argvLen == 2 && strstr(argv[i], "-v") && i + 1 < argc)
                {
                    sscanf(argv[++i], "%d", &volume);
                }
                else if (argvLen == 3 && strstr(argv[i], "-vr") && i + 1 < argc)
                {
                    sscanf(argv[++i], "%d", &volumeMic);
                }
                else if (argvLen == 3 && strstr(argv[i], "-va") && i + 1 < argc)
                {
                    sscanf(argv[++i], "%d", &volumeAgc);
                }
                else if (argvLen == 4 && strstr(argv[i], "-vad") && i + 1 < argc)
                {
                    if (argv[++i][0] == '1')
                        main_wmix->webrtcEnable[WR_VAD] = true;
                    else
                        main_wmix->webrtcEnable[WR_VAD] = false;
                }
                else if (argvLen == 4 && strstr(argv[i], "-aec") && i + 1 < argc)
                {
                    if (argv[++i][0] == '1')
                        main_wmix->webrtcEnable[WR_AEC] = 1;
                    else
                        main_wmix->webrtcEnable[WR_AEC] = 0;
                }
                else if (argvLen == 3 && strstr(argv[i], "-ns") && i + 1 < argc)
                {
                    if (argv[++i][0] == '1')
                        main_wmix->webrtcEnable[WR_NS] = 1;
                    else
                        main_wmix->webrtcEnable[WR_NS] = 0;
                }
                else if (argvLen == 6 && strstr(argv[i], "-ns_pa") && i + 1 < argc)
                {
                    if (argv[++i][0] == '1')
                        main_wmix->webrtcEnable[WR_NS_PA] = 1;
                    else
                        main_wmix->webrtcEnable[WR_NS_PA] = 0;
                }
                else if (argvLen == 4 && strstr(argv[i], "-agc") && i + 1 < argc)
                {
                    if (argv[++i][0] == '1')
                        main_wmix->webrtcEnable[WR_AGC] = 1;
                    else
                        main_wmix->webrtcEnable[WR_AGC] = 0;
                }
                else if (argvLen == 8 && strstr(argv[i], "-console") && i + 1 < argc)
                {
                    consolePath = argv[++i];
                }
                else if (strstr(argv[i], ".wav") || strstr(argv[i], ".mp3") || strstr(argv[i], ".aac"))
                    path = argv[i];
            }
            if (consolePath)
                wmix_console(main_wmix, consolePath);
            if (volume >= 0)
                wmix_ao_vol_set(main_wmix->objAo, volume);
            if (volumeMic >= 0)
            {
                if (main_wmix->objAi)
                    wmix_ai_vol_set(main_wmix->objAi, volumeMic);
                main_wmix->volumeMic = volumeMic < 10 ? volumeMic : 10;
            }
            if (volumeAgc >= 0)
                main_wmix->volumeAgc = volumeAgc;
            if (path)
            {
                wmix_load_thread(
                    main_wmix,
                    3,
                    (uint8_t *)path,
                    WMIX_MSG_BUFF_SIZE,
                    &wmix_load_task);
            }
        }
        //开始
        show_setup();
        wmix_load_thread(main_wmix, 0, NULL, 0, &main_loop);
        while (1)
            delayus(500000);
    }
    return 0;
}
#endif
