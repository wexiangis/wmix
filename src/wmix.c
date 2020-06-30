
/**************************************************
 * 
 *  服务端+客户端(或用wmix_user.h自己开发) 组建的混音器、音频托管工具
 * 
 **************************************************/
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <arpa/inet.h>

#include "wmix.h"
#include "wav.h"

#include "g711codec.h"
#include "rtp.h"
#include "webrtc.h"

#if (WMIX_MP3)
#include "mad.h"
#include "id3.h"
#endif

#if (WMIX_AAC)
#include "aac.h"
#endif

//录、播音同步,将把"录音线程"改为"心跳函数"内嵌到"播音线程"中
#define WMIX_RECORD_PLAY_SYNC

static WMix_Struct *main_wmix = NULL;

static void signal_callback(int signo)
{
    if (SIGINT == signo || SIGTERM == signo)
    {
        wmix_exit(main_wmix);
#if (WMIX_MODE == 1)
        hiaudio_exit();
#endif
    }
    exit(0);
}

#include <sys/time.h>
void delayus(unsigned int us)
{
    struct timeval delay;
    delay.tv_sec = us / 1000000;
    delay.tv_usec = us % 1000000;
    select(0, NULL, NULL, NULL, &delay);
}

/*******************************************************************************
 * 名称: sys_volume_set
 * 功能: 扬声器音量设置
 * 参数: vol_value 设置的音量值 (范围：0-10之间)
 * 返回: 0：正常 -1:错误
 * 说明: 无
 ******************************************************************************/
int sys_volume_set(uint8_t count, uint8_t div)
{
#if (WMIX_MODE == 1)
    if (count > div)
        count = div;
    if (count == 0)
        return hiaudio_set_volume(-120);
    else
        return hiaudio_set_volume(5 - (div - count) * 5);
#else
    snd_mixer_t *mixer;
    snd_mixer_elem_t *pcm_element;
    long volume_value = (long)(count > div ? div : count);
    long volume_div = (long)div;
    //
    snd_mixer_open(&mixer, 0);
    snd_mixer_attach(mixer, "default");
    snd_mixer_selem_register(mixer, NULL, NULL);
    snd_mixer_load(mixer);
    //找到Pcm对应的element
    pcm_element = snd_mixer_first_elem(mixer);                             /* 取得第一个 element，也就是 Master */
    snd_mixer_selem_set_playback_volume_range(pcm_element, 0, volume_div); //设置音量范围：0-100之间
    //左音量
    if (snd_mixer_selem_set_playback_volume(pcm_element, SND_MIXER_SCHN_FRONT_LEFT, volume_value) < 0)
        return -1;
    //右音量
    if (snd_mixer_selem_set_playback_volume(pcm_element, SND_MIXER_SCHN_FRONT_RIGHT, volume_value) < 0)
        return -1;
    //
    snd_mixer_selem_get_playback_volume(pcm_element, SND_MIXER_SCHN_FRONT_LEFT, &volume_value); //获取音量
    printf("volume: %ld / %ld\r\n", volume_value, volume_div);
    //处理事件
    snd_mixer_handle_events(mixer);
    snd_mixer_close(mixer);
    //
    return volume_value;
#endif
}

#if (WMIX_MODE == 0)

/*****************************sndwav_common**************************************************************/

/*******************************************************************************
 * 名称: SNDWAV_P_GetFormat
 * 功能: wav文件格式获取
 * 参数: wav    ： WAVContainer_t结构体指针
 *      snd_format： snd_pcm_format_t结构体指针
 * 返回: 0：正常 -1:错误
 * 说明: 无
 ******************************************************************************/
int SNDWAV_P_GetFormat(WAVContainer_t *wav, snd_pcm_format_t *snd_format)
{
    if (LE_SHORT(wav->format.format) != WAV_FMT_PCM)
        return -1;

    switch (LE_SHORT(wav->format.sample_length))
    {
    case 16:
        *snd_format = SND_PCM_FORMAT_S16_LE;
        break;
    case 8:
        *snd_format = SND_PCM_FORMAT_U8;
        break;
    default:
        *snd_format = SND_PCM_FORMAT_UNKNOWN;
        break;
    }

    return 0;
}
/*******************************************************************************
 * 名称: SNDWAV_ReadPcm
 * 功能: pcm设备读取
 * 参数: sndpcm ：SNDPCMContainer_t结构体指针
 *      frame_num ： 读取的帧数(每帧chn*sample/8字节,sample一般为16)
 * 返回: >0:帧数 -1:错误
 * 说明: 无
 ******************************************************************************/
int SNDWAV_ReadPcm(SNDPCMContainer_t *sndpcm, size_t frame_num)
{
    int ret;
    size_t result = 0;
    size_t count = frame_num;
    uint8_t *data = sndpcm->data_buf;

    // if (count != sndpcm->chunk_size) {
    //     count = sndpcm->chunk_size;
    // }

    while (count > 0)
    {
        ret = snd_pcm_readi(sndpcm->handle, data, count);
        //返回异常,recover处理
        if (ret < 0)
            ret = snd_pcm_recover(sndpcm->handle, ret, 0);
        //其它问题处理
        if (ret == -EAGAIN || (ret >= 0 && (size_t)ret < count))
        {
            snd_pcm_wait(sndpcm->handle, 1000);
        }
        else if (ret == -EPIPE)
        {
            snd_pcm_prepare(sndpcm->handle);
            fprintf(stderr, "R-Error: Buffer Underrun\n");
        }
        else if (ret == -ESTRPIPE)
        {
            fprintf(stderr, "R-Error: Need suspend\n");
        }
        else if (ret < 0)
        {
            fprintf(stderr, "R-Error: SNDWAV_ReadPcm: [%s]\n", snd_strerror(ret));
            return -1;
        }
        //帧读够了
        if (count < ret)
            break;
        //帧计数
        if (ret > 0)
        {
            result += ret;
            count -= ret;
            //按实际读取的帧数移动 uint8 数据指针
            data += ret * sndpcm->bits_per_frame / 8;
        }
    }

    return result;
}
/*******************************************************************************
 * 名称: SNDWAV_WritePcm
 * 功能: wav文件数据写入pcm设备
 * 参数: sndpcm ：SNDPCMContainer_t结构体指针
 *       wcount ： 写入的大小
 * 返回: 0：正常 -1:错误
 * 说明: 无
 ******************************************************************************/
int SNDWAV_WritePcm(SNDPCMContainer_t *sndpcm, size_t wcount)
{
    int ret;
    int result = 0;
    uint8_t *data = sndpcm->data_buf;

    // if (wcount < sndpcm->chunk_size) {
    //     snd_pcm_format_set_silence(sndpcm->format,
    //         data + wcount * sndpcm->bits_per_frame / 8,
    //         (sndpcm->chunk_size - wcount) * sndpcm->channels);
    //     wcount = sndpcm->chunk_size;
    // }

    while (wcount > 0)
    {
        ret = snd_pcm_writei(sndpcm->handle, data, wcount);
        //
        if (ret < 0)
            ret = snd_pcm_recover(sndpcm->handle, ret, 0);
        //
        if (ret == -EAGAIN || (ret >= 0 && (size_t)ret < wcount))
        {
            snd_pcm_wait(sndpcm->handle, 1000);
        }
        else if (ret == -EPIPE)
        {
            snd_pcm_prepare(sndpcm->handle);
            fprintf(stderr, "W-Error: Buffer Underrun\n");
        }
        else if (ret == -ESTRPIPE)
        {
            fprintf(stderr, "W-Error: Need suspend\n");
        }
        else if (ret < 0)
        {
            fprintf(stderr, "W-Error snd_pcm_writei: [%s]", snd_strerror(ret));
            return -1;
        }
        //
        if (wcount < ret)
            break;
        //
        if (ret > 0)
        {
            result += ret;
            wcount -= ret;
            data += ret * sndpcm->bits_per_frame / 8;
        }
    }
    return result;
}
/*******************************************************************************
 * 名称: SNDWAV_SetParams
 * 功能: wav文件播报参数配置
 * 参数: sndpcm ：SNDPCMContainer_t结构体指针
 *       wav    ： WAVContainer_t 结构体指针
 * 返回: 0：正常 -1:错误
 * 说明: 无
 ******************************************************************************/
int SNDWAV_SetParams(SNDPCMContainer_t *sndpcm, WAVContainer_t *wav)
{
    snd_pcm_hw_params_t *hwparams;
    snd_pcm_format_t format;
    uint32_t exact_rate;
    uint32_t buffer_time, period_time;

    /* 分配snd_pcm_hw_params_t结构体  Allocate the snd_pcm_hw_params_t structure on the stack. */
    snd_pcm_hw_params_alloca(&hwparams);

    /* 初始化hwparams  Init hwparams with full configuration space */
    if (snd_pcm_hw_params_any(sndpcm->handle, hwparams) < 0)
    {
        fprintf(stderr, "Error snd_pcm_hw_params_any\n");
        return -1;
    }
    //初始化访问权限
    if (snd_pcm_hw_params_set_access(sndpcm->handle, hwparams, SND_PCM_ACCESS_RW_INTERLEAVED) < 0)
    {
        fprintf(stderr, "Error snd_pcm_hw_params_set_access\n");
        return -1;
    }

    /* 初始化采样格式,16位 Set sample format */
    if (SNDWAV_P_GetFormat(wav, &format) < 0)
    {
        fprintf(stderr, "Error get_snd_pcm_format\n");
        return -1;
    }
    if (snd_pcm_hw_params_set_format(sndpcm->handle, hwparams, format) < 0)
    {
        fprintf(stderr, "Error snd_pcm_hw_params_set_format\n");
        return -1;
    }
    sndpcm->format = format;

    /* 设置通道数量 Set number of channels */
    if (snd_pcm_hw_params_set_channels(sndpcm->handle, hwparams, LE_SHORT(wav->format.channels)) < 0)
    {
        fprintf(stderr, "Error snd_pcm_hw_params_set_channels\n");
        return -1;
    }
    sndpcm->channels = LE_SHORT(wav->format.channels);
    //设置采样率，如果硬件不支持我们设置的采样率，将使用最接近的
    /* Set sample rate. If the exact rate is not supported */
    /* by the hardware, use nearest possible rate.         */
    exact_rate = LE_INT(wav->format.sample_rate);
    if (snd_pcm_hw_params_set_rate_near(sndpcm->handle, hwparams, &exact_rate, 0) < 0)
    {
        fprintf(stderr, "Error snd_pcm_hw_params_set_rate_near\n");
        return -1;
    }
    if (LE_INT(wav->format.sample_rate) != exact_rate)
    {
        fprintf(stderr, "The rate %d Hz is not supported by your hardware.\n ==> Using %d Hz instead.\n",
                LE_INT(wav->format.sample_rate), exact_rate);
    }

    if (snd_pcm_hw_params_get_buffer_time_max(hwparams, &buffer_time, 0) < 0)
    {
        fprintf(stderr, "Error snd_pcm_hw_params_get_buffer_time_max\n");
        return -1;
    }
    if (buffer_time > 500000)
        buffer_time = 500000;
    period_time = buffer_time / 4;

    if (snd_pcm_hw_params_set_buffer_time_near(sndpcm->handle, hwparams, &buffer_time, 0) < 0)
    {
        fprintf(stderr, "Error snd_pcm_hw_params_set_buffer_time_near\n");
        return -1;
    }

    if (snd_pcm_hw_params_set_period_time_near(sndpcm->handle, hwparams, &period_time, 0) < 0)
    {
        fprintf(stderr, "Error snd_pcm_hw_params_set_period_time_near\n");
        return -1;
    }

    /* Set hw params */
    if (snd_pcm_hw_params(sndpcm->handle, hwparams) < 0)
    {
        fprintf(stderr, "Error snd_pcm_hw_params(handle, params)\n");
        return -1;
    }

    snd_pcm_hw_params_get_period_size(hwparams, &sndpcm->chunk_size, 0);
    snd_pcm_hw_params_get_buffer_size(hwparams, &sndpcm->buffer_size);
    if (sndpcm->chunk_size == sndpcm->buffer_size)
    {
        fprintf(stderr, "Can't use period equal to buffer size (%lu == %lu)\n", sndpcm->chunk_size, sndpcm->buffer_size);
        return -1;
    }

    sndpcm->bits_per_sample = snd_pcm_format_physical_width(format);
    sndpcm->bits_per_frame = sndpcm->bits_per_sample * LE_SHORT(wav->format.channels);

    sndpcm->chunk_bytes = sndpcm->chunk_size * sndpcm->bits_per_frame / 8;

    // printf("---- wav info -----\n   通道数: %d\n   采样率: %d Hz\n   采样位数: %d bit\n   总数据量: %d Bytes\n"
    //     "  每次写入帧数: %d\n   每帧字节数: %ld Bytes\n   每次读写字节数: %ld Bytes\n   缓冲区大小: %d Bytes\n",
    //     wav->format.channels,
    //     wav->format.sample_rate,
    //     wav->format.sample_length,
    //     wav->chunk.length,
    //     sndpcm->chunk_size,
    //     sndpcm->bits_per_frame/8,
    //     sndpcm->chunk_bytes,
    //     sndpcm->buffer_size);

    /* Allocate audio data buffer */
#if (WMIX_CHANNELS == 1)
    sndpcm->data_buf = (uint8_t *)malloc(sndpcm->chunk_bytes * 2 + 1);
#else
    sndpcm->data_buf = (uint8_t *)malloc(sndpcm->chunk_bytes + 1);
#endif
    if (!sndpcm->data_buf)
    {
        fprintf(stderr, "Error malloc: [data_buf]\n");
        return -1;
    }

    return 0;
}

/*****************************录音部分********************************************************************/

/*******************************************************************************
 * 名称: SNDWAV_Record
 * 功能: wav文件录音
 * 参数: sndpcm ：SNDPCMContainer_t结构体指针
 *      wav ： WAVContainer_t结构体指针
 *      fd  ： 文件句柄
 * 返回: 0：正常 -1:错误
 * 说明: 无
 ******************************************************************************/
void SNDWAV_Record(SNDPCMContainer_t *sndpcm, WAVContainer_t *wav, int fd)
{
    int64_t rest;
    size_t c, frame_size;

    if (WAV_WriteHeader(fd, wav) < 0)
    {
        return;
    }

    rest = wav->chunk.length;
    while (rest > 0)
    {
        c = (rest <= (int64_t)sndpcm->chunk_bytes) ? (size_t)rest : sndpcm->chunk_bytes;
        frame_size = c * 8 / sndpcm->bits_per_frame;
        if (SNDWAV_ReadPcm(sndpcm, frame_size) != frame_size)
            break;

        if (write(fd, sndpcm->data_buf, c) != c)
        {
            fprintf(stderr, "Error SNDWAV_Record[write]\n");
            return;
        }

        rest -= c;
    }
}
/*******************************************************************************
 * 名称: record_wav
 * 功能: 录音主函数
 * 参数: filename 文件路径 (如：/home/user/record.wav)
 *      duration_time 录音时间 单位：秒
 * 返回: 0：正常 -1:错误
 * 说明: 无
 ******************************************************************************/
int record_wav(char *filename, uint32_t duration_time, uint8_t chn, uint8_t sample, uint16_t freq)
{
    // char *filename;
    char *devicename = "default";
    int fd;
    WAVContainer_t wav;
    SNDPCMContainer_t record;

    memset(&record, 0x0, sizeof(record));

    // filename = argv[1];
    remove(filename);
    if ((fd = open(filename, O_WRONLY | O_CREAT, 0644)) == -1)
    {
        fprintf(stderr, "Error open: [%s]\n", filename);
        return -1;
    }

    if (snd_output_stdio_attach(&record.log, stderr, 0) < 0)
    {
        fprintf(stderr, "Error snd_output_stdio_attach\n");
        goto Err;
    }

    if (snd_pcm_open(&record.handle, devicename, SND_PCM_STREAM_CAPTURE, 0) < 0)
    {
        fprintf(stderr, "Error snd_pcm_open [ %s]\n", devicename);
        goto Err;
    }

    WAV_Params(&wav, duration_time, chn, sample, freq);

    if (SNDWAV_SetParams(&record, &wav) < 0)
    {
        fprintf(stderr, "Error set_snd_pcm_params\n");
        goto Err;
    }
    snd_pcm_dump(record.handle, record.log);

    SNDWAV_Record(&record, &wav, fd);

    snd_pcm_drain(record.handle);

    close(fd);
    free(record.data_buf);
    snd_output_close(record.log);
    snd_pcm_close(record.handle);
    return 0;

Err:
    close(fd);
    remove(filename);
    if (record.data_buf)
        free(record.data_buf);
    if (record.log)
        snd_output_close(record.log);
    if (record.handle)
        snd_pcm_close(record.handle);
    return -1;
}

/*****************************播放音频部分********************************/

/*******************************************************************************
 * 名称: SNDWAV_P_SaveRead
 * 功能: wav文件数据读取
 * 形参: count： 读取的大小
 *       buf ： 读取缓冲区
 *       fd  :  音频文件句柄
 * 返回: 无
 * 说明: 无
 ******************************************************************************/
int SNDWAV_P_SaveRead(int fd, void *buf, size_t count)
{
    int result = 0, res;

    while (count > 0)
    {
        if ((res = read(fd, buf, count)) == 0)
            break;
        if (res < 0)
            return result > 0 ? result : res;
        count -= res;
        result += res;
        buf = (char *)buf + res;
    }
    return result;
}
/*******************************************************************************
 * 名称: SNDWAV_Play
 * 功能: wav文件数据读取与写入
 * 形参: sndpcm：SNDPCMContainer_t结构体指针
 *       wav ： WAVContainer_t
 *       fd  :  音频文件句柄
 * 返回: 无
 * 说明: 无
 ******************************************************************************/
void SNDWAV_Play(SNDPCMContainer_t *sndpcm, WAVContainer_t *wav, int fd)
{
    int i = 0, bit_count = 0;
    int16_t *valueP;

    int load, ret;
    int64_t written = 0;
    int64_t c;
    int64_t count = LE_INT(wav->chunk.length);

    load = 0;
    while (written < count)
    {
        /* Must read [chunk_bytes] bytes data enough. */
        do
        {
            c = count - written;
            if (c > sndpcm->chunk_bytes)
                c = sndpcm->chunk_bytes;
            c -= load;

            if (c == 0)
                break;

            ret = SNDWAV_P_SaveRead(fd, sndpcm->data_buf + load, c);

            if (ret < 0)
            {
                fprintf(stderr, "Error safe_read\n");
                return;
            }
            if (ret == 0)
                break;
            load += ret;
        } while ((size_t)load < sndpcm->chunk_bytes);

        // printf("load = %ld, written = %ld\n", load, written+load);

        if (bit_count)
        {
            for (i = 0, valueP = (int16_t *)sndpcm->data_buf; i < load / 2; i++)
            {
                if (valueP[i] & 0x8000)
                    valueP[i] = (0xFFFF << (16 - bit_count)) | (valueP[i] >> bit_count);
                else
                    valueP[i] = valueP[i] >> bit_count;
                // valueP[i] *= 0.0625;
            }
        }

        /* Transfer to size frame */
        load = load * 8 / sndpcm->bits_per_frame;

        ret = SNDWAV_WritePcm(sndpcm, load);

        if (ret != load)
            break;

        ret = ret * sndpcm->bits_per_frame / 8;
        written += ret;
        load = 0;
    }
}

int play_wav(char *filename)
{
    char devicename[] = "default";
    WAVContainer_t wav; //wav文件头信息
    int fd;
    SNDPCMContainer_t playback;

    memset(&playback, 0x0, sizeof(playback));

    fd = open(filename, O_RDONLY);
    if (fd < 0)
    {
        fprintf(stderr, "Error open [%s]\n", filename);
        return -1;
    }
    //读取WAV文件头
    if (WAV_ReadHeader(fd, &wav) < 0)
    {
        fprintf(stderr, "Error WAV_Parse [%s]\n", filename);
        // goto Err;
    }
    //Creates a new output object using an existing stdio \c FILE pointer.
    if (snd_output_stdio_attach(&playback.log, stderr, 0) < 0)
    {
        fprintf(stderr, "Error snd_output_stdio_attach\n");
        goto Err;
    }
    // 打开PCM，最后一个参数为0意味着标准配置 SND_PCM_ASYNC
    if (snd_pcm_open(&playback.handle, devicename, SND_PCM_STREAM_PLAYBACK, 0) < 0)
    {
        fprintf(stderr, "Error snd_pcm_open [ %s]\n", devicename);
        goto Err;
    }
    //配置PCM参数
    if (SNDWAV_SetParams(&playback, &wav) < 0)
    {
        fprintf(stderr, "Error set_snd_pcm_params\n");
        goto Err;
    }
    snd_pcm_dump(playback.handle, playback.log);

    SNDWAV_Play(&playback, &wav, fd);

    snd_pcm_drain(playback.handle);

    close(fd);
    free(playback.data_buf);
    snd_output_close(playback.log);
    snd_pcm_close(playback.handle);

    return 0;

Err:
    close(fd);

    if (playback.data_buf)
        free(playback.data_buf);
    if (playback.log)
        snd_output_close(playback.log);
    if (playback.handle)
        snd_pcm_close(playback.handle);

    return -1;
}

int SNDWAV_SetParams2(SNDPCMContainer_t *sndpcm, uint16_t freq, uint8_t channels, uint8_t sample)
{
    snd_pcm_hw_params_t *hwparams;
    snd_pcm_format_t format;
    uint32_t exact_rate;
    uint32_t buffer_time, period_time;

    /* 分配snd_pcm_hw_params_t结构体  Allocate the snd_pcm_hw_params_t structure on the stack. */
    snd_pcm_hw_params_alloca(&hwparams);

    /* 初始化hwparams  Init hwparams with full configuration space */
    if (snd_pcm_hw_params_any(sndpcm->handle, hwparams) < 0)
    {
        fprintf(stderr, "Error snd_pcm_hw_params_any\n");
        return -1;
    }
    //初始化访问权限
    if (snd_pcm_hw_params_set_access(sndpcm->handle, hwparams, SND_PCM_ACCESS_RW_INTERLEAVED) < 0)
    {
        fprintf(stderr, "Error snd_pcm_hw_params_set_access\n");
        return -1;
    }

    /* 初始化采样格式,16位 Set sample format */
    if (sample == 8)
        format = SND_PCM_FORMAT_S8;
    else if (sample == 16)
        format = SND_PCM_FORMAT_S16_LE;
    else if (sample == 24)
        format = SND_PCM_FORMAT_S24_LE;
    else if (sample == 32)
        format = SND_PCM_FORMAT_S32_LE;
    else
        return -1;

    if (snd_pcm_hw_params_set_format(sndpcm->handle, hwparams, format) < 0)
    {
        fprintf(stderr, "Error snd_pcm_hw_params_set_format\n");
        return -1;
    }
    sndpcm->format = format;

    /* 设置通道数量 Set number of channels */
    if (snd_pcm_hw_params_set_channels(sndpcm->handle, hwparams, channels) < 0)
    {
        fprintf(stderr, "Error snd_pcm_hw_params_set_channels\n");
        return -1;
    }
    sndpcm->channels = channels;
    //设置采样率，如果硬件不支持我们设置的采样率，将使用最接近的
    /* Set sample rate. If the exact rate is not supported */
    /* by the hardware, use nearest possible rate.         */
    exact_rate = freq;
    if (snd_pcm_hw_params_set_rate_near(sndpcm->handle, hwparams, &exact_rate, 0) < 0)
    {
        fprintf(stderr, "Error snd_pcm_hw_params_set_rate_near\n");
        return -1;
    }
    if (freq != exact_rate)
    {
        fprintf(stderr, "The rate %d Hz is not supported by your hardware.\n ==> Using %d Hz instead.\n",
                freq, exact_rate);
    }

    if (snd_pcm_hw_params_get_buffer_time_max(hwparams, &buffer_time, 0) < 0)
    {
        fprintf(stderr, "Error snd_pcm_hw_params_get_buffer_time_max\n");
        return -1;
    }

    //ubuntu下该值会非常大,需限制
    if (buffer_time > 500000)
        buffer_time = 500000;
    period_time = buffer_time / 4;

    if (snd_pcm_hw_params_set_buffer_time_near(sndpcm->handle, hwparams, &buffer_time, 0) < 0)
    {
        fprintf(stderr, "Error snd_pcm_hw_params_set_buffer_time_near\n");
        return -1;
    }

    if (snd_pcm_hw_params_set_period_time_near(sndpcm->handle, hwparams, &period_time, 0) < 0)
    {
        fprintf(stderr, "Error snd_pcm_hw_params_set_period_time_near\n");
        return -1;
    }

    /* Set hw params */
    if (snd_pcm_hw_params(sndpcm->handle, hwparams) < 0)
    {
        fprintf(stderr, "Error snd_pcm_hw_params(handle, params)\n");
        return -1;
    }

    snd_pcm_hw_params_get_period_size(hwparams, &sndpcm->chunk_size, 0);
    snd_pcm_hw_params_get_buffer_size(hwparams, &sndpcm->buffer_size);
    if (sndpcm->chunk_size == sndpcm->buffer_size)
    {
        fprintf(stderr, "Can't use period equal to buffer size (%lu == %lu)\n", sndpcm->chunk_size, sndpcm->buffer_size);
        return -1;
    }

    sndpcm->bits_per_sample = snd_pcm_format_physical_width(format);
    sndpcm->bits_per_frame = sndpcm->bits_per_sample * channels;
    sndpcm->chunk_bytes = sndpcm->chunk_size * sndpcm->bits_per_frame / 8;

    printf("\n---- SNDWAV_SetParams2 -----\n"
           "  chunk_size: %d\n"             //每次写入帧数
           "  bits_per_frame/8: %d Bytes\n" //每帧字节数
           "  chunk_bytes: %d Bytes\n"      //每次读写字节数
           "  buffer_size: %d Bytes\n"      //缓冲区大小
           "  buffer_time: %d Bytes\n"      //缓冲区大小
           "  period_time: %d Bytes\n",     //缓冲区大小
           (int)sndpcm->chunk_size,
           (int)(sndpcm->bits_per_frame / 8),
           (int)sndpcm->chunk_bytes,
           (int)sndpcm->buffer_size,
           buffer_time,
           period_time);

    /* Allocate audio data buffer */
#if (WMIX_CHANNELS == 1)
    sndpcm->data_buf = (uint8_t *)malloc(sndpcm->chunk_bytes * 2 + 1);
#else
    sndpcm->data_buf = (uint8_t *)malloc(sndpcm->chunk_bytes + 1);
#endif
    if (!sndpcm->data_buf)
    {
        fprintf(stderr, "Error malloc: [data_buf]\n");
        return -1;
    }

    return 0;
}

SNDPCMContainer_t *wmix_alsa_init(uint8_t channels, uint8_t sample, uint16_t freq, char p_or_c)
{
    char devicename[] = "default";

    SNDPCMContainer_t *playback = (SNDPCMContainer_t *)calloc(1, sizeof(SNDPCMContainer_t));

    //Creates a new output object using an existing stdio \c FILE pointer.
    if (snd_output_stdio_attach(&playback->log, stderr, 0) < 0)
    {
        fprintf(stderr, "Error snd_output_stdio_attach\n");
        goto Err;
    }
    // 打开PCM，最后一个参数为0意味着标准配置 SND_PCM_ASYNC
    if (snd_pcm_open(
            &playback->handle,
            devicename,
            p_or_c == 'c' ? SND_PCM_STREAM_CAPTURE : SND_PCM_STREAM_PLAYBACK, 0) < 0)
    {
        fprintf(stderr, "Error snd_pcm_open [ %s]\n", devicename);
        goto Err;
    }
    //配置PCM参数
    if (SNDWAV_SetParams2(playback, freq, channels, sample) < 0)
    {
        fprintf(stderr, "Error set_snd_pcm_params\n");
        goto Err;
    }
    snd_pcm_dump(playback->handle, playback->log);

    return playback;

Err:

    if (playback->data_buf)
        free(playback->data_buf);
    if (playback->log)
        snd_output_close(playback->log);
    if (playback->handle)
        snd_pcm_close(playback->handle);
    free(playback);

    return NULL;
}

void wmix_alsa_release(SNDPCMContainer_t *playback)
{
    if (playback)
    {
        snd_pcm_drain(playback->handle);
        //
        if (playback->data_buf)
            free(playback->data_buf);
        if (playback->log)
            snd_output_close(playback->log);
        if (playback->handle)
            snd_pcm_close(playback->handle);
        free(playback);
    }
}

#endif

//--------------------------------------- 混音方式播放 ---------------------------------------

typedef struct
{
    WMix_Struct *wmix;
    long flag;
    uint8_t *param;
} WMixThread_Param;

void wmix_throwOut_thread(
    WMix_Struct *wmix,
    long flag,
    uint8_t *param,
    size_t paramLen,
    void *callback)
{
    WMixThread_Param *wmtp;
    pthread_t th;
    pthread_attr_t attr;
    //
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

#if (WMIX_CHANNELS == 1)
#define RECORD_DATA_TRANSFER()                                                                 \
    if (chn == 1)                                                                              \
    {                                                                                          \
        for (count = 0, src.U8 = dist.U8 = buff; count < ret; count += frame_size)             \
        {                                                                                      \
            if (divCount >= 1.0)                                                               \
            {                                                                                  \
                src.U16++;                                                                     \
                divCount -= 1.0;                                                               \
            }                                                                                  \
            else                                                                               \
            {                                                                                  \
                *dist.U16++ = *src.U16++;                                                      \
                divCount += divPow;                                                            \
            }                                                                                  \
        }                                                                                      \
        src.U8 = buff;                                                                         \
        buffSize2 = (size_t)(dist.U16 - src.U16) * 2;                                          \
    }                                                                                          \
    else                                                                                       \
    {                                                                                          \
        memcpy(&buff[ret], buff, ret);                                                         \
        for (count = 0, src.U8 = &buff[ret], dist.U8 = buff; count < ret; count += frame_size) \
        {                                                                                      \
            if (divCount >= 1.0)                                                               \
            {                                                                                  \
                src.U16++;                                                                     \
                divCount -= 1.0;                                                               \
            }                                                                                  \
            else                                                                               \
            {                                                                                  \
                *dist.U16++ = *src.U16;                                                        \
                *dist.U16++ = *src.U16++;                                                      \
                divCount += divPow;                                                            \
            }                                                                                  \
        }                                                                                      \
        src.U8 = buff;                                                                         \
        buffSize2 = (size_t)(dist.U16 - src.U16) * 2;                                          \
    }
#else
#define RECORD_DATA_TRANSFER()                                                     \
    if (chn == 1)                                                                  \
    {                                                                              \
        for (count = 0, src.U8 = dist.U8 = buff; count < ret; count += frame_size) \
        {                                                                          \
            if (divCount >= 1.0)                                                   \
            {                                                                      \
                src.U16++;                                                         \
                src.U16++;                                                         \
                divCount -= 1.0;                                                   \
            }                                                                      \
            else                                                                   \
            {                                                                      \
                *dist.U16++ = *src.U16++;                                          \
                src.U16++;                                                         \
                divCount += divPow;                                                \
            }                                                                      \
        }                                                                          \
        src.U8 = buff;                                                             \
        buffSize2 = (size_t)(dist.U16 - src.U16) * 2;                              \
    }                                                                              \
    else                                                                           \
    {                                                                              \
        for (count = 0, src.U8 = dist.U8 = buff; count < ret; count += frame_size) \
        {                                                                          \
            if (divCount >= 1.0)                                                   \
            {                                                                      \
                src.U32++;                                                         \
                divCount -= 1.0;                                                   \
            }                                                                      \
            else                                                                   \
            {                                                                      \
                *dist.U32++ = *src.U32++;                                          \
                divCount += divPow;                                                \
            }                                                                      \
        }                                                                          \
        src.U8 = buff;                                                             \
        buffSize2 = (size_t)(dist.U32 - src.U32) * 4;                              \
    }
#endif

//============= shm =============

#include <sys/shm.h>

#define AI_CIRCLE_BUFF_LEN 10240
typedef struct
{
    int16_t w;
    int16_t buff[AI_CIRCLE_BUFF_LEN + 4];
} ShmemAi_Circle;

/*
    共享内存使用说明：

    "/tmp/wmix", 'O'： 单通道8000Hz客户端写入音频数据，这里读并播放

    "/tmp/wmix", 'I'： 单通道8000Hz这里录音并写入，客户端读取录音数据

    "/tmp/wmix", 'L'： 双通道44100Hz这里录音并写入，客户端读取或混音器自己的录音线程取用
 */

static ShmemAi_Circle *ai_circle = NULL, *ao_circle = NULL, *ao_circleLocal = NULL;

int wmix_mem_create(char *path, int flag, int size, void **mem)
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

int wmix_mem_destroy(int id)
{
    return shmctl(id, IPC_RMID, NULL);
}

int16_t wmix_mem_read(int16_t *dat, int16_t len, int16_t *addr, bool wait)
{
    int16_t i = 0;
    int16_t w = *addr;
    //
    if (!ai_circle)
    {
        wmix_mem_create("/tmp/wmix", 'O', sizeof(ShmemAi_Circle), (void **)&ai_circle);
        if (!ai_circle)
        {
            fprintf(stderr, "wmix_mem_read: shm_create err !!\n");
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

int16_t wmix_mem_read2(int16_t *dat, int16_t len, int16_t *addr, bool wait)
{
    int16_t i = 0;
    int16_t w = *addr;
    //
    if (!ao_circleLocal)
    {
        wmix_mem_create("/tmp/wmix", 'L', sizeof(ShmemAi_Circle), (void **)&ao_circleLocal);
        if (!ao_circleLocal)
        {
            fprintf(stderr, "wmix_mem_read2: shm_create err !!\n");
            return 0;
        }
        //
        w = ao_circleLocal->w;
    }
    //
    if (w < 0 || w >= AI_CIRCLE_BUFF_LEN)
        w = ao_circleLocal->w;
    for (i = 0; i < len; i++)
    {
        if (w == ao_circleLocal->w)
        {
            if (wait && ao_circleLocal)
            {
                usleep(1000);
                continue;
            }
            break;
        }
        //
        *dat++ = ao_circleLocal->buff[w++];
        if (w >= AI_CIRCLE_BUFF_LEN)
            w = 0;
    }
    *addr = w;
    //
    return i;
}

int16_t wmix_mem_write(int16_t *dat, int16_t len)
{
    int16_t i = 0;
    //
    if (!ao_circle)
    {
        wmix_mem_create("/tmp/wmix", 'I', sizeof(ShmemAi_Circle), (void **)&ao_circle);
        if (!ao_circle)
        {
            fprintf(stderr, "wmix_mem_write: shm_create err !!\n");
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

int16_t wmix_mem_write2(int16_t *dat, int16_t len)
{
    int16_t i = 0;
    //
    if (!ao_circleLocal)
    {
        wmix_mem_create("/tmp/wmix", 'L', sizeof(ShmemAi_Circle), (void **)&ao_circleLocal);
        if (!ao_circleLocal)
        {
            fprintf(stderr, "wmix_mem_write2: shm_create err !!\n");
            return 0;
        }
        //
        ao_circleLocal->w = 0;
    }
    //
    for (i = 0; i < len; i++)
    {
        ao_circleLocal->buff[ao_circleLocal->w++] = *dat++;
        if (ao_circleLocal->w >= AI_CIRCLE_BUFF_LEN)
            ao_circleLocal->w = 0;
    }
    //
    return i;
}

//回声消除,时间间隔估算
#define AEC_INTERVAL_MS  (WMIX_INTERVAL_MS)

//FIFO 循环缓冲区包数量
#define AEC_FIFO_PKG_NUM (AEC_INTERVAL_MS/WMIX_INTERVAL_MS + 2)

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
uint8_t* recordPkgBuff_get(uint8_t *buff, int delayms)
{
    uint8_t *ret = buff;
    //包偏移
    int pkgCount = _recordPkgBuff_count - (delayms / WMIX_INTERVAL_MS);
    //字节偏移
    int byteCount = (int)((float)((delayms % WMIX_INTERVAL_MS) * WMIX_FRAME_NUM) / WMIX_INTERVAL_MS) * WMIX_FRAME_SIZE;
    //范围限制
    if(pkgCount >= AEC_FIFO_PKG_NUM)
        pkgCount = AEC_FIFO_PKG_NUM;
    else if(pkgCount < 0)
        pkgCount = 0;
    //与当前的相对位置
    pkgCount = _recordPkgBuff_count - pkgCount;
    //循环
    if(pkgCount >= AEC_FIFO_PKG_NUM)
        pkgCount -= AEC_FIFO_PKG_NUM;
    else if(pkgCount < 0)
        pkgCount += AEC_FIFO_PKG_NUM;
    //包偏移 + 帧偏移
    if(byteCount > 0)
    {
        if(pkgCount == 0)
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
static uint8_t playPkgBuff[WMIX_PKG_SIZE];
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
uint8_t* playPkgBuff_get(uint8_t *buff, int delayms)
{
    uint8_t *ret = buff;
    //包偏移
    int pkgCount = _playPkgBuff_count - (delayms / WMIX_INTERVAL_MS);
    //字节偏移
    int byteCount = (int)((float)((delayms % WMIX_INTERVAL_MS) * WMIX_FRAME_NUM) / WMIX_INTERVAL_MS) * WMIX_FRAME_SIZE;
    //范围限制
    if(pkgCount >= AEC_FIFO_PKG_NUM)
        pkgCount = AEC_FIFO_PKG_NUM;
    else if(pkgCount < 0)
        pkgCount = 0;
    //与当前的相对位置
    pkgCount = _playPkgBuff_count - pkgCount;
    //循环
    if(pkgCount >= AEC_FIFO_PKG_NUM)
        pkgCount -= AEC_FIFO_PKG_NUM;
    else if(pkgCount < 0)
        pkgCount += AEC_FIFO_PKG_NUM;
    //包偏移 + 帧偏移
    if(byteCount > 0)
    {
        if(pkgCount == 0)
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
    static WMix_Point rwTestSrc, rwTestHead;
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
    __time_t tick1, tick2;
    //采样时间间隔us
    int intervalUs = WMIX_INTERVAL_MS * 1000 - 2000;
#endif
    //按20ms的间隔采样,每次帧数
    size_t frame_num = WMIX_FRAME_NUM;
    //每帧字节数
    size_t frame_size = WMIX_FRAME_SIZE;
    //每包字节数
    size_t buffSize = WMIX_PKG_SIZE;
#if (WMIX_MODE == 0)
    uint8_t *buff;
    //录音句柄已初始化
    if (wmix->recordback)
        buff = wmix->recordback->data_buf;
#else
    uint8_t buff[WMIX_PKG_SIZE];
#endif
    //录音频率和目标频率的比值
    divPow = (float)(WMIX_FREQ - freq) / freq;
    divCount = 0;
    //线程数+1
    wmix->thread_sys += 1;

#ifdef WMIX_RECORD_PLAY_SYNC
    if (wmix->run)
#else
    tick1 = getTickUs();
    while (wmix->run)
#endif
    {
        //失能释放
#if (WMIX_WEBRTC_VAD)
        if (!wmix->webrtcEnable[WR_VAD] && wmix->webrtcPoint[WR_VAD])
        {
            vad_release(wmix->webrtcPoint[WR_VAD]);
            wmix->webrtcPoint[WR_VAD] = NULL;
        }
#endif
        //失能释放
#if (WMIX_WEBRTC_NS)
        if (!wmix->webrtcEnable[WR_NS] && wmix->webrtcPoint[WR_NS])
        {
            ns_release(wmix->webrtcPoint[WR_NS]);
            wmix->webrtcPoint[WR_NS] = NULL;
        }
#endif
        //失能释放
#if (WMIX_WEBRTC_AEC)
        if (!wmix->webrtcEnable[WR_AEC] && wmix->webrtcPoint[WR_AEC])
        {
            aec_release(wmix->webrtcPoint[WR_AEC]);
            wmix->webrtcPoint[WR_AEC] = NULL;
        }
#endif
        //有录音线程或有客户端在用mem(内存共享)数据录音
        if (wmix->recordRun || wmix->shmemRun > 0 || wmix->rwTest)
        {
#if (WMIX_MODE == 0)
            if (wmix->recordback)
#else
            if (hiaudio_state())
#endif
            {
                // frame_num*frame_size = buffSize 实际读取字节数
                memset(buff, 0, buffSize);
#if (WMIX_MODE == 0)
                ret = SNDWAV_ReadPcm(wmix->recordback, frame_num) * frame_size;
#else
                ret = hiaudio_ai_read((int16_t *)buff, 1000) * frame_size;
#endif
                if (ret > 0)
                {
                    recordPkgBuff_add(buff);

#if (WMIX_WEBRTC_AEC)
                    //回声消除
                    if (wmix->webrtcEnable[WR_AEC] && WMIX_FREQ <= 32000)
                    {
                        if (wmix->webrtcPoint[WR_AEC] == NULL)
                            wmix->webrtcPoint[WR_AEC] = aec_init(WMIX_CHANNELS, WMIX_FREQ, WMIX_INTERVAL_MS);
                        if (wmix->webrtcPoint[WR_AEC])
                        {
                            //开始转换
                            // aec_process2(
                            //     wmix->webrtcPoint[WR_AEC],
                            //     (int16_t *)playPkgBuff_get(playPkgBuff, AEC_INTERVAL_MS), //要消除的数据,即 播音数据
                            //     (int16_t *)buff,              //混杂的数据,即 播音数据 + 人说话声音
                            //     (int16_t *)buff,              //输出的数据,得 人说话声音
                            //     frame_num,
                            //     0); //评估回声时延

                            aec_process3(
                                wmix->webrtcPoint[WR_AEC],
                                (int16_t *)playPkgBuff_get(playPkgBuff, AEC_INTERVAL_MS), //要消除的数据,即 播音数据
                                (int16_t *)buff,              //混杂的数据,即 播音数据 + 人说话声音
                                (int16_t *)buff,              //输出的数据,得 人说话声音
                                frame_num,
                                0.5); //评估回声时延
                        }
                    }
#endif

#if (WMIX_WEBRTC_NS)
                    //噪音抑制
                    if (wmix->webrtcEnable[WR_NS] && WMIX_FREQ <= 32000)
                    {
                        if (wmix->webrtcPoint[WR_NS] == NULL)
                            wmix->webrtcPoint[WR_NS] = ns_init(WMIX_CHANNELS, WMIX_FREQ);
                        if (wmix->webrtcPoint[WR_NS])
                        {
                            //开始转换
                            ns_process(
                                wmix->webrtcPoint[WR_NS],
                                (int16_t *)buff,
                                (int16_t *)buff,
                                frame_num);
                        }
                    }
#endif

#if (WMIX_WEBRTC_VAD)
                    //人声识别
                    if (wmix->webrtcEnable[WR_VAD] && WMIX_FREQ <= 32000)
                    {
                        // 人声识别,初始化
                        if (wmix->webrtcPoint[WR_VAD] == NULL)
                            wmix->webrtcPoint[WR_VAD] = vad_init(WMIX_CHANNELS, WMIX_FREQ, WMIX_INTERVAL_MS);
                        if (wmix->webrtcPoint[WR_VAD])
                            vad_process(
                                wmix->webrtcPoint[WR_VAD],
                                (int16_t *)buff,
                                frame_num);
                    }
#endif
                    //原始数据写共享内存
                    wmix_mem_write2((int16_t *)buff, ret / 2);

                    //自收发测试
                    if (wmix->rwTest)
                    {
                        rwTestSrc.U8 = buff;
                        rwTestHead = wmix_load_wavStream(
                            wmix,
                            rwTestSrc,
                            ret,
                            WMIX_FREQ,
                            WMIX_CHANNELS,
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
                    memset(buff, 0, frame_num * frame_size);
                    recordPkgBuff_add(buff);
                }
            }
            else
            {
                //没录到声音
                memset(buff, 0, frame_num * frame_size);
                recordPkgBuff_add(buff);

#if (WMIX_MODE == 0)
                if ((wmix->recordback = wmix_alsa_init(WMIX_CHANNELS, WMIX_SAMPLE, WMIX_FREQ, 'c')))
                {
                    printf("wmix record: start\n");
                    buff = wmix->recordback->data_buf;
                    //丢弃一包数据
                    SNDWAV_ReadPcm(wmix->recordback, frame_num);

#ifdef WMIX_RECORD_PLAY_SYNC
                    return;
#else
                    continue;
#endif
                }
#else
                if (hiaudio_state() == 0)
                {
                    printf("wmix record: start\n");
                    hiaudio_ai_init(WMIX_CHANNELS, WMIX_SAMPLE, WMIX_FREQ, WMIX_FREQ / 1000 * WMIX_INTERVAL_MS);

#ifdef WMIX_RECORD_PLAY_SYNC
                    return;
#else
                    continue;
#endif
                }
#endif
            }
        }
        else
        {
            //没录到声音
            memset(buff, 0, frame_num * frame_size);
            recordPkgBuff_add(buff);

            //无录音任务释放录音句柄
#if (WMIX_MODE == 0)
            if (wmix->recordback)
            {
                printf("wmix record: clear\n");
                wmix_alsa_release(wmix->recordback);
                wmix->recordback = NULL;
            }
#else
            if (hiaudio_state())
            {
                printf("wmix record: clear\n");
                hiaudio_ai_exit();
            }
#endif
            //无录音任务释放vad
#if (WMIX_WEBRTC_VAD)
            if (wmix->webrtcEnable[WR_VAD] && wmix->webrtcPoint[WR_VAD])
            {
                vad_release(wmix->webrtcPoint[WR_VAD]);
                wmix->webrtcPoint[WR_VAD] = NULL;
            }
#endif
        }

#ifdef WMIX_RECORD_PLAY_SYNC
        return;
#else
        //矫正到20ms
        tick2 = getTickUs();
        if (tick2 > tick1 && tick2 - tick1 < intervalUs)
            delayus(intervalUs - (tick2 - tick1));
        tick1 = getTickUs();
#endif
    }
    //
#if (WMIX_MODE == 0)
    if (wmix->recordback)
    {
        wmix_alsa_release(wmix->recordback);
        wmix->recordback = NULL;
    }
#else
    if (hiaudio_state())
    {
        hiaudio_ai_exit();
    }
#endif
    //
    wmix->thread_sys -= 1;
}

void wmix_shmem_read_circle(WMixThread_Param *wmtp)
{
#if 0
    WMix_Struct *wmix = wmtp->wmix;
    //
    uint8_t buff[320];
    int16_t addr = -1, ret;
    WMix_Point head, src;
    uint32_t tick;
    uint8_t rdce = 2;
    //
    uint32_t bpsCount = 0, total = 0, totalWait;
    uint32_t second = 0, bytes_p_second = WMIX_FREQ*WMIX_CHANNELS*WMIX_SAMPLE/8;
    int timeout;
    //
    //独占 reduceMode
    if(rdce > 1)
        wmix->reduceMode = rdce;
    //
    src.U8 = buff;
    head.U8 = 0;
    tick = 0;
    //
    totalWait = bytes_p_second/2;
    //
    wmix->thread_sys += 1;
    //
    while(wmix->run)
    {
        ret = wmix_mem_read((int16_t*)buff, 160, &addr, false)*2;
        if(ret > 0)
        {
            //等播放指针赶上写入进度
            // timeout = 0;
            // while(wmix->run && timeout++ < 200 &&
            //     tick > wmix->tick && 
            //     tick - wmix->tick > totalWait)
            //     delayus(5000);
            // if(!wmix->run)
            //     break;
            //
            head = wmix_load_wavStream(
                wmix, 
                src, ret, WMIX_FREQ, WMIX_CHANNELS, WMIX_SAMPLE, head, rdce, &tick);
            //
            bpsCount += ret;
            total += ret;
            //播放时间
            if(bpsCount > bytes_p_second)
            {
                bpsCount -= bytes_p_second;
                second = total/bytes_p_second;
                if(wmix->debug) printf("  SHMEM-PLAY: %02d:%02d\n", second/60, second%60);
            }
            //
            delayus(1000);
            continue;
        }
        //
        delayus(10000);
    }
    //
    if(wmix->debug) printf(">> SHMEM-PLAY: end <<\n");
    //
    if(rdce > 1)
        wmix->reduceMode = 1;
    //
    wmix->thread_sys -= 1;
#endif
}

//============= shm =============

void wmix_load_wav_fifo_thread(WMixThread_Param *wmtp)
{
    char *path = (char *)&wmtp->param[4];
    //
    uint8_t chn = wmtp->param[0];
    uint8_t sample = wmtp->param[1];
    uint16_t freq = (wmtp->param[2] << 8) | wmtp->param[3];
    //
    int fd_read;
    uint8_t *buff;
    uint32_t buffSize;
    //
    WMix_Point head, src;
    ssize_t ret, total = 0, total2 = 0, totalWait;
    double buffSizePow;
    uint32_t tick, second = 0, bytes_p_second, bytes_p_second2, bpsCount = 0;
    uint8_t rdce = ((wmtp->flag >> 8) & 0xFF) + 1, rdceIsMe = 0;
    //
    // int timeout;
    uint8_t loopWord;
    loopWord = wmtp->wmix->loopWordFifo;
    //
    if (mkfifo(path, 0666) < 0 && errno != EEXIST)
    {
        fprintf(stderr, "wmix_load_wav_fifo_thread: mkfifo err\n");
        return;
    }
    //
    fd_read = open(path, O_RDONLY);
    //独占 reduceMode
    if (rdce > 1 && wmtp->wmix->reduceMode == 1)
    {
        wmtp->wmix->reduceMode = rdce;
        rdceIsMe = 1;
    }
    else
        rdce = 1;
    //
    bytes_p_second = chn * sample / 8 * freq;
    buffSize = bytes_p_second;
#if (WMIX_MODE == 1)
    totalWait = bytes_p_second / 2;
#else
    totalWait = wmtp->wmix->playback->chunk_bytes;
#endif
    buff = (uint8_t *)calloc(buffSize, sizeof(uint8_t));
    //
    bytes_p_second2 = WMIX_CHANNELS * WMIX_SAMPLE / 8 * WMIX_FREQ;
    buffSizePow = (double)bytes_p_second2 / bytes_p_second;
    //
    if (wmtp->wmix->debug)
        printf("<< FIFO-W: %s start >>\n   通道数: %d\n   采样位数: %d bit\n   采样率: %d Hz\n   每秒字节: %d Bytes\n\n",
               path, chn, sample, freq, bytes_p_second);
    //
    src.U8 = buff;
    head.U8 = 0;
    tick = 0;
    //线程计数
    wmtp->wmix->thread_play += 1;
    //
    while (wmtp->wmix->run &&
           loopWord == wmtp->wmix->loopWordFifo)
    {
        ret = read(fd_read, buff, buffSize);
        if (ret > 0)
        {
            //等播放指针赶上写入进度
            // timeout = 0;
            // while(wmtp->wmix->run && timeout++ < 200 &&
            //     loopWord == wmtp->wmix->loopWordFifo &&
            //     tick > wmtp->wmix->tick &&
            //     tick - wmtp->wmix->tick > totalWait)
            //     delayus(5000);
            //
            head = wmix_load_wavStream(
                wmtp->wmix,
                src, ret, freq, chn, sample, head, rdce, &tick);
            if (head.U8 == 0)
                break;
            //
            bpsCount += ret;
            total += ret;
            total2 = total * buffSizePow;
            //播放时间
            if (bpsCount > bytes_p_second)
            {
                bpsCount -= bytes_p_second;
                second = total / bytes_p_second;
                if (wmtp->wmix->debug)
                    printf("  FIFO-W: %s %02d:%02d\n", path, second / 60, second % 60);
            }
            continue;
        }
        else if (errno != EAGAIN)
            break;
        //
        delayus(5000);
    }
    //
    if (wmtp->wmix->debug)
        printf(">> FIFO-W: %s end <<\n", path);
    //
    close(fd_read);
    //删除文件
    remove(path);
    //
    free(buff);
    //线程计数
    wmtp->wmix->thread_play -= 1;
    //
    if (wmtp->param)
        free(wmtp->param);
    //关闭 reduceMode
    if (rdceIsMe)
        wmtp->wmix->reduceMode = 1;
    free(wmtp);
}

void wmix_record_wav_fifo_thread(WMixThread_Param *wmtp)
{
    char *path = (char *)&wmtp->param[4];
    //
    uint8_t chn = wmtp->param[0];
    uint8_t sample = wmtp->param[1];
    uint16_t freq = (wmtp->param[2] << 8) | wmtp->param[3];
    //
    size_t buffSize, buffSize2, frame_size, count;
    WMix_Point src, dist;
    ssize_t ret, total = 0;
    uint32_t second = 0, bytes_p_second, bytes_p_second2, bpsCount = 0;
    float divCount, divPow;
    //
    int fd_write;
    int16_t record_addr = -1;
#if (WMIX_CHANNELS == 1)
    unsigned char buff[1024];
#else
    unsigned char buff[512];
#endif
    //
    uint8_t loopWord;
    loopWord = wmtp->wmix->loopWordFifo;
    //
    if (freq > WMIX_FREQ)
    {
        fprintf(stderr, "wmix_record_wav_fifo_thread: freq err, %dHz > %dHz(machine)\n", freq, WMIX_FREQ);
        return;
    }
    if (sample != WMIX_SAMPLE)
    {
        fprintf(stderr, "wmix_record_wav_fifo_thread: sample err, must be %dbit(machine)\n", WMIX_SAMPLE);
        return;
    }
    if (chn != 1 && chn != 2)
    {
        fprintf(stderr, "wmix_record_wav_fifo_thread: channels err, must be 1 or 2\n");
        return;
    }
    //
    if (mkfifo(path, 0666) < 0 && errno != EEXIST)
    {
        fprintf(stderr, "wmix_record_wav_fifo_thread: mkfifo err\n");
        return;
    }
    //
    fd_write = open(path, O_WRONLY);
    //
    bytes_p_second = WMIX_CHANNELS * WMIX_SAMPLE / 8 * WMIX_FREQ;
    bytes_p_second2 = chn * sample / 8 * freq;
    frame_size = WMIX_FRAME_SIZE;
#if (WMIX_CHANNELS == 1)
    buffSize = sizeof(buff) / 2;
#else
    buffSize = sizeof(buff);
#endif
    //
    divPow = (float)(WMIX_FREQ - freq) / freq;
    divCount = 0;
    //
    if (wmtp->wmix->debug)
        printf("<< FIFO-R: %s record >>\n   通道数: %d\n   采样位数: %d bit\n   采样率: %d Hz\n   每秒字节: %d Bytes\n   时间长度: -- sec\n\n",
               path, chn, sample, freq, bytes_p_second2);
    //线程计数
    wmtp->wmix->thread_record += 1;
    //
    while (wmtp->wmix->run &&
           loopWord == wmtp->wmix->loopWordFifo)
    {
        ret = wmix_mem_read2((int16_t *)buff, buffSize / 2, &record_addr, false) * 2;
        if (ret > 0)
        {
            bpsCount += ret;
            total += ret;
            //录制时间
            if (bpsCount > bytes_p_second)
            {
                bpsCount -= bytes_p_second;
                second = total / bytes_p_second;
                if (wmtp->wmix->debug)
                    printf("  FIFO-R: %s %02d:%02d\n", path, second / 60, second % 60);
            }
            //
            if (!wmtp->wmix->run)
                break;
            //不同频率和通道数的数据处理
            RECORD_DATA_TRANSFER();
            //
            ret = write(fd_write, buff, buffSize2);
            if (ret < 0 && errno != EAGAIN)
                break;
        }
        else if (ret < 0)
        {
            fprintf(stderr, "wmix_record_wav_fifo_thread: wmix_record_wav_fifo_thread read err %d\n", (int)ret);
            break;
        }
        else
        {
            delayus(500);
            fsync(fd_write);
        }
    }
    close(fd_write);
    //
    if (wmtp->wmix->debug)
        printf(">> FIFO-R: %s end <<\n", path);
    //删除文件
    remove(path);
    //线程计数
    wmtp->wmix->thread_record -= 1;
    //
    if (wmtp->param)
        free(wmtp->param);
    free(wmtp);
}

void wmix_record_wav_thread(WMixThread_Param *wmtp)
{
    char *path = (char *)&wmtp->param[6];
    //
    uint8_t chn = wmtp->param[0];
    uint8_t sample = wmtp->param[1];
    uint16_t freq = (wmtp->param[2] << 8) | wmtp->param[3];
    uint16_t duration_time = (wmtp->param[4] << 8) | wmtp->param[5];
    //
    size_t buffSize, buffSize2, frame_size, count;
    WMix_Point src, dist;
    ssize_t ret, total = 0;
    uint32_t second = 0, bytes_p_second, bytes_p_second2, bpsCount = 0, TOTAL;
    float divCount, divPow;
    //
    int fd;
    WAVContainer_t wav;
    int16_t record_addr = -1;
#if (WMIX_CHANNELS == 1)
    unsigned char buff[1024];
#else
    unsigned char buff[512];
#endif
    //
    uint8_t loopWord;
    loopWord = wmtp->wmix->loopWordRecord;
    //
    if (freq > WMIX_FREQ)
    {
        fprintf(stderr, "wmix_record_wav_thread: freq err, %dHz > %dHz(machine)\n", freq, WMIX_FREQ);
        return;
    }
    if (sample != WMIX_SAMPLE)
    {
        fprintf(stderr, "wmix_record_wav_thread: sample err, must be %dbit(machine)\n", WMIX_SAMPLE);
        return;
    }
    if (chn != 1 && chn != 2)
    {
        fprintf(stderr, "wmix_record_wav_thread: channels err, must be 1 or 2\n");
        return;
    }
    //
    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd <= 0)
    {
        fprintf(stderr, "wmix_record_wav_thread: open %s err\n", path);
        return;
    }
    //
    WAV_Params(&wav, duration_time, chn, sample, freq);
    if (WAV_WriteHeader(fd, &wav) < 0)
    {
        close(fd);
        fprintf(stderr, "wmix_record_wav_thread: WAV_WriteHeader err\n");
        return;
    }
    //
    bytes_p_second = WMIX_CHANNELS * WMIX_SAMPLE / 8 * WMIX_FREQ;
    bytes_p_second2 = chn * sample / 8 * freq;
    frame_size = WMIX_FRAME_SIZE;
#if (WMIX_CHANNELS == 1)
    buffSize = sizeof(buff) / 2;
#else
    buffSize = sizeof(buff);
#endif
    TOTAL = bytes_p_second * duration_time;
    //
    divPow = (float)(WMIX_FREQ - freq) / freq;
    divCount = 0;
    //
    if (wmtp->wmix->debug)
        printf("<< RECORD-WAV: %s record >>\n   通道数: %d\n   采样位数: %d bit\n   采样率: %d Hz\n   每秒字节: %d Bytes\n   时间长度: %d sec\n\n",
               path, chn, sample, freq, bytes_p_second2, duration_time);
    //线程计数
    wmtp->wmix->thread_record += 1;
    //
    while (wmtp->wmix->run &&
           loopWord == wmtp->wmix->loopWordRecord)
    {
        //最后一帧
        if (total + buffSize >= TOTAL)
        {
            buffSize = TOTAL - total;
        }
        //
        ret = wmix_mem_read2((int16_t *)buff, buffSize / 2, &record_addr, false) * 2;
        if (ret > 0)
        {
            bpsCount += ret;
            total += ret;
            //录制时间
            if (bpsCount > bytes_p_second)
            {
                bpsCount -= bytes_p_second;
                second = total / bytes_p_second;
                if (wmtp->wmix->debug)
                    printf("  RECORD-WAV: %s %02d:%02d\n", path, second / 60, second % 60);
            }
            //
            if (!wmtp->wmix->run)
                break;
            //不同频率和通道数的数据处理
            RECORD_DATA_TRANSFER();
            //
            ret = write(fd, buff, buffSize2);
            if (ret < 0 && errno != EAGAIN)
            {
                fprintf(stderr, "wmix_record_wav_thread: write err %d\n", errno);
                break;
            }
            //
            if (total >= TOTAL)
                break;
        }
        else if (ret < 0)
        {
            fprintf(stderr, "wmix_record_wav_thread: read err %d\n", (int)ret);
            break;
        }
        else
            delayus(5000);
    }
    //
    close(fd);
    //
    if (wmtp->wmix->debug)
        printf(">> RECORD-WAV: %s end <<\n", path);
    //线程计数
    wmtp->wmix->thread_record -= 1;
    //
    if (wmtp->param)
        free(wmtp->param);
    free(wmtp);
}

#if (WMIX_AAC)
void wmix_record_aac_thread(WMixThread_Param *wmtp)
{
    char *path = (char *)&wmtp->param[6];
    //
    uint8_t chn = wmtp->param[0];
    uint8_t sample = wmtp->param[1];
    uint16_t freq = (wmtp->param[2] << 8) | wmtp->param[3];
    uint16_t duration_time = (wmtp->param[4] << 8) | wmtp->param[5];
    //
    size_t buffSize, buffSize2, frame_size, count, buffSizeR;
    WMix_Point src, dist;
    ssize_t ret, total = 0;
    uint32_t second = 0, bytes_p_second, bytes_p_second2, bpsCount = 0, TOTAL;
    float divCount, divPow;
    //
    int fd;
    //
    void *aacEnc = NULL;
    unsigned char *buff, *buff2, *pBuff2_S, *pBuff2_E;
    int16_t record_addr = -1;
    //
    unsigned char aacBuff[4096];
    //
    uint8_t loopWord;
    loopWord = wmtp->wmix->loopWordRecord;
    //
    if (freq > WMIX_FREQ)
    {
        fprintf(stderr, "wmix_record_aac_thread: freq err, %dHz > %dHz(machine)\n", freq, WMIX_FREQ);
        return;
    }
    if (sample != WMIX_SAMPLE)
    {
        fprintf(stderr, "wmix_record_aac_thread: sample err, must be %dbit(machine)\n", WMIX_SAMPLE);
        return;
    }
    if (chn != 1 && chn != 2)
    {
        fprintf(stderr, "wmix_record_aac_thread: channels err, must be 1 or 2\n");
        return;
    }
    //
    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd <= 0)
    {
        fprintf(stderr, "wmix_record_aac_thread: open %s err\n", path);
        return;
    }
    //
    bytes_p_second = WMIX_CHANNELS * WMIX_SAMPLE / 8 * WMIX_FREQ;
    bytes_p_second2 = chn * sample / 8 * freq;
    frame_size = WMIX_FRAME_SIZE;
    //
    buffSize = WMIX_CHANNELS * WMIX_SAMPLE / 8 * 1024;
    buffSizeR = chn * sample / 8 * 1024;
    //
    buff = malloc(2 * WMIX_SAMPLE / 8 * 1024);
    buff2 = malloc(buffSizeR * 2);
    pBuff2_S = pBuff2_E = buff2;
    //
    TOTAL = bytes_p_second * duration_time;
    //
    divPow = (float)(WMIX_FREQ - freq) / freq;
    divCount = 0;
    //
    if (wmtp->wmix->debug)
        printf("<< RECORD-AAC: %s record >>\n   通道数: %d\n   采样位数: %d bit\n   采样率: %d Hz\n   每秒字节: %d Bytes\n   时间长度: %d sec\n\n",
               path, chn, sample, freq, bytes_p_second2, duration_time);
    //线程计数
    wmtp->wmix->thread_record += 1;
    //
    while (wmtp->wmix->run &&
           loopWord == wmtp->wmix->loopWordRecord)
    {
        //最后一帧
        if (total + buffSize >= TOTAL)
            buffSize = TOTAL - total;
        //
        ret = wmix_mem_read2((int16_t *)buff, buffSize / 2, &record_addr, true) * 2;
        if (ret > 0)
        {
            bpsCount += ret;
            total += ret;
            //录制时间
            if (bpsCount > bytes_p_second)
            {
                bpsCount -= bytes_p_second;
                second = total / bytes_p_second;
                if (wmtp->wmix->debug)
                    printf("  RECORD-AAC: %s %02d:%02d\n", path, second / 60, second % 60);
            }
            //
            if (!wmtp->wmix->run)
                break;
            //不同频率和通道数的数据处理
            RECORD_DATA_TRANSFER();
            //
            memcpy(pBuff2_E, buff, buffSize2);
            pBuff2_E += buffSize2;
            //
            while (pBuff2_E - pBuff2_S >= buffSizeR)
            {
                ret = aac_encode(&aacEnc, pBuff2_S, buffSizeR, aacBuff, 4096, chn, freq);
                if (ret > 0)
                {
                    ret = write(fd, aacBuff, ret);
                    if (ret < 0 && errno != EAGAIN)
                    {
                        fprintf(stderr, "wmix_record_aac_thread: write err %d\n", errno);
                        break;
                    }
                }
                pBuff2_S += buffSizeR;
            }
            if (ret < 0 && errno != EAGAIN)
                break;
            //
            memcpy(buff2, pBuff2_S, (size_t)(pBuff2_E - pBuff2_S));
            pBuff2_E = &buff2[pBuff2_E - pBuff2_S];
            pBuff2_S = buff2;
            //
            if (total >= TOTAL)
                break;
        }
        else if (ret < 0)
        {
            fprintf(stderr, "wmix_record_aac_thread: read err %d\n", (int)ret);
            break;
        }
        else
            delayus(10000);
    }
    //
    close(fd);
    free(buff);
    free(buff2);
    //
    if (wmtp->wmix->debug)
        printf(">> RECORD-AAC: %s end <<\n", path);
    //线程计数
    wmtp->wmix->thread_record -= 1;
    //
    if (aacEnc)
        aac_encodeRelease(&aacEnc);
    if (wmtp->param)
        free(wmtp->param);
    free(wmtp);
}

void wmix_rtp_send_aac_thread(WMixThread_Param *wmtp)
{
    char *path = (char *)&wmtp->param[6];
    char *msgPath;
    key_t msg_key;
    int msg_fd = 0;
    WMix_Msg msg;
    //
    uint8_t chn = wmtp->param[0];
    uint8_t sample = wmtp->param[1];
    uint16_t freq = (wmtp->param[2] << 8) | wmtp->param[3];
    uint16_t port = (wmtp->param[4] << 8) | wmtp->param[5];
    //
    size_t buffSize, buffSizeR, buffSize2, frame_size, count;
    WMix_Point src, dist;
    ssize_t ret, total = 0;
    uint32_t second = 0, bytes_p_second, bytes_p_second2, bpsCount = 0;
    float divCount, divPow;
    //
    int16_t record_addr = -1;
    unsigned char *buff, *buff2, *pBuff2_S, *pBuff2_E;
    void *aacEnc = NULL;
    unsigned char aacbuff[2048];
    //
    SocketStruct *ss;
    RtpPacket rtpPacket;
    //
    uint8_t loopWord;
    loopWord = wmtp->wmix->loopWordRtp;
    //
    //参数检查,是否在允许的变参范围内
    if (freq > WMIX_FREQ)
    {
        fprintf(stderr, "wmix_rtp_send_aac_thread: freq err, %dHz > %dHz(machine)\n", freq, WMIX_FREQ);
        return;
    }
    if (sample != WMIX_SAMPLE)
    {
        fprintf(stderr, "wmix_rtp_send_aac_thread: sample err, must be %dbit(machine)\n", WMIX_SAMPLE);
        return;
    }
    if (chn != 1 && chn != 2)
    {
        fprintf(stderr, "wmix_rtp_send_aac_thread: channels err, must be 1 or 2\n");
        return;
    }
    //初始化rtp
    ss = rtp_socket(path, port, 1);
    if (!ss)
    {
        fprintf(stderr, "rtp_socket: err\n");
        return;
    }
    rtp_header(&rtpPacket, 0, 0, 0, RTP_VESION, RTP_PAYLOAD_TYPE_AAC, 1, 0, 0, 0x32411);
    //初始化消息
    msgPath = (char *)&wmtp->param[strlen(path) + 6 + 1];
    if (msgPath && msgPath[0])
    {
        //创建消息挂靠路径
        if (access(msgPath, F_OK) != 0)
            creat(msgPath, 0777);
        //创建消息
        if ((msg_key = ftok(msgPath, WMIX_MSG_ID)) > 0)
            msg_fd = msgget(msg_key, IPC_CREAT | 0666);
    }
    else
        msgPath = NULL;
    //生成sdp文件
    rtp_create_sdp(
        "/tmp/record.sdp",
        path, port, chn, freq,
        RTP_PAYLOAD_TYPE_AAC);
    //
    bytes_p_second = WMIX_CHANNELS * WMIX_SAMPLE / 8 * WMIX_FREQ;
    bytes_p_second2 = chn * sample / 8 * freq;
    frame_size = WMIX_FRAME_SIZE;
    //每次从ai读取字节数
    buffSize = WMIX_CHANNELS * WMIX_SAMPLE / 8 * 1024;
    buffSizeR = chn * sample / 8 * 1024;
    //
    buff = malloc(2 * WMIX_SAMPLE / 8 * 1024);
    buff2 = malloc(buffSizeR * 2);
    pBuff2_S = pBuff2_E = buff2;
    //
    divPow = (float)(WMIX_FREQ - freq) / freq;
    divCount = 0;
    //
    if (wmtp->wmix->debug)
        printf(
            "<< RTP-SEND-AAC: %s:%d start >>\n"
            "   通道数: %d\n"
            "   采样位数: %d bit\n"
            "   采样率: %d Hz\n"
            "   每秒字节: %d Bytes\n",
            path, port, chn, sample, freq, bytes_p_second2);
    //线程计数
    wmtp->wmix->thread_record += 1;
    //
    while (wmtp->wmix->run &&
           loopWord == wmtp->wmix->loopWordRtp)
    {
        //msg 检查
        if (msg_fd)
        {
            if (msgrcv(msg_fd, &msg,
                       WMIX_MSG_BUFF_SIZE,
                       0, IPC_NOWAIT) < 1 &&
                errno != ENOMSG) //消息队列被关闭
            {
                if (wmtp->wmix->debug)
                    printf("RTP-SEND-AAC exit: %d msgrecv err/%d\n", msg_fd, errno);
                break;
            }
        }
        //
        ret = wmix_mem_read2((int16_t *)buff, buffSize / 2, &record_addr, true) * 2;
        if (ret > 0)
        {
            bpsCount += ret;
            total += ret;
            //录制时间
            if (bpsCount > bytes_p_second)
            {
                bpsCount -= bytes_p_second;
                second = total / bytes_p_second;
                if (wmtp->wmix->debug)
                    printf("  RTP-SEND-AAC: %s:%d %02d:%02d\n", path, port, second / 60, second % 60);
            }
            //
            if (!wmtp->wmix->run)
                break;
            //不同频率和通道数的数据处理
            RECORD_DATA_TRANSFER();
            //
            memcpy(pBuff2_E, buff, buffSize2);
            pBuff2_E += buffSize2;
            //
            while (pBuff2_E - pBuff2_S >= buffSizeR)
            {
                ret = aac_encode(&aacEnc, pBuff2_S, buffSizeR, aacbuff, 4096, chn, freq);
                if (ret > 0)
                {
                    ret -= 7;
                    memcpy(&rtpPacket.payload[4], &aacbuff[7], ret);
                    ret = rtp_send(ss, &rtpPacket, ret);
                    if (ret < 1)
                    {
                        printf("rtp_send: err\n");
                        break;
                    }
                }
                pBuff2_S += buffSizeR;
                delayus(5000);
            }
            if (ret < 0)
                break;
            //
            memcpy(buff2, pBuff2_S, (size_t)(pBuff2_E - pBuff2_S));
            pBuff2_E = &buff2[pBuff2_E - pBuff2_S];
            pBuff2_S = buff2;
        }
        else if (ret < 0)
        {
            fprintf(stderr, "wmix_rtp_send_aac_thread: read err %d\n", (int)ret);
            break;
        }
    }
    //
    if (wmtp->wmix->debug)
        printf(">> RTP-SEND-AAC: %s:%d end <<\n", path, port);
    //线程计数
    wmtp->wmix->thread_record -= 1;
    //
    if (msg_fd)
        msgctl(msg_fd, IPC_RMID, NULL);
    if (msgPath)
        remove(msgPath);
    free(buff);
    free(buff2);
    close(ss->fd);
    free(ss);
    //
    if (aacEnc)
        aac_encodeRelease(&aacEnc);
    if (wmtp->param)
        free(wmtp->param);
    free(wmtp);
}

void wmix_rtp_recv_aac_thread(WMixThread_Param *wmtp)
{
    char *path = (char *)&wmtp->param[6];
    char *msgPath;
    key_t msg_key;
    int msg_fd = 0;
    WMix_Msg msg;
    //
    uint8_t chn = wmtp->param[0];
    uint8_t sample = wmtp->param[1];
    uint16_t freq = (wmtp->param[2] << 8) | wmtp->param[3];
    uint16_t port = (wmtp->param[4] << 8) | wmtp->param[5];
    uint32_t bytes_p_second;
    //
    int ret = 0;
    uint8_t buff[8192];
    uint32_t buffSize, buffSize2;
    WMix_Point head, src;
    uint32_t tick, total = 0, total2 = 0, totalWait;
    uint32_t second = 0, bpsCount = 0;
    double totalPow;
    uint8_t rdce = 2, rdceIsMe = 0;
    //aac解码句柄
    void *aacDec = NULL;
    int datUse = 0, retLen;
    unsigned char aacBuff[2048];
    //
    SocketStruct *ss;
    RtpPacket rtpPacket;
    int retSize;
    //
    // int timeout;
    uint8_t loopWord;
    loopWord = wmtp->wmix->loopWordRtp;
    //初始化rtp
    ss = rtp_socket(path, port, 0);
    if (!ss)
    {
        fprintf(stderr, "rtp_socket: err\n");
        return;
    }
    //初始化消息
    msgPath = (char *)&wmtp->param[strlen(path) + 6 + 1];
    if (msgPath && msgPath[0])
    {
        //创建消息挂靠路径
        if (access(msgPath, F_OK) != 0)
            creat(msgPath, 0777);
        //创建消息
        if ((msg_key = ftok(msgPath, WMIX_MSG_ID)) > 0)
            msg_fd = msgget(msg_key, IPC_CREAT | 0666);
    }
    else
        msgPath = NULL;
    //独占 reduceMode
    if (rdce > 1 && wmtp->wmix->reduceMode == 1)
    {
        wmtp->wmix->reduceMode = rdce;
        rdceIsMe = 1;
    }
    else
        rdce = 1;
    //默认缓冲区大小设为1秒字播放字节数
    bytes_p_second = chn * sample / 8 * freq;
    buffSize = bytes_p_second;
    buffSize2 = WMIX_CHANNELS * WMIX_SAMPLE / 8 * WMIX_FREQ;
    totalPow = (double)buffSize2 / buffSize;
    //
    buffSize = chn * sample / 8 * 1024;
    buffSize2 = WMIX_CHANNELS * WMIX_SAMPLE / 8 * 1024;
    totalWait = buffSize2 * 4;
    //
    if (wmtp->wmix->debug)
        printf(
            "<< RTP-RECV: %s:%d start >>\n"
            "   通道数: %d\n"
            "   采样位数: %d bit\n"
            "   采样率: %d Hz\n",
            path, port, chn, sample, freq);
    //
    src.U8 = buff;
    head.U8 = 0;
    tick = 0;
    //线程计数
    wmtp->wmix->thread_play += 1;
    //
    while (wmtp->wmix->run &&
           loopWord == wmtp->wmix->loopWordRtp)
    {
        //msg 检查
        if (msg_fd)
        {
            if (msgrcv(msg_fd, &msg,
                       WMIX_MSG_BUFF_SIZE,
                       0, IPC_NOWAIT) < 1 &&
                errno != ENOMSG) //消息队列被关闭
            {
                if (wmtp->wmix->debug)
                    printf("RTP-RECV exit: %d msgrecv err/%d\n", msg_fd, errno);
                break;
            }
        }
        //往aacBuff读入数据
        ret = rtp_recv(ss, &rtpPacket, (uint32_t *)&retSize);
        if (ret > 0)
        {
            memcpy(&aacBuff[7], &rtpPacket.payload[4], retSize);
            aac_createHeader(aacBuff, chn, freq, 0x7FF, retSize);
            retLen = aac_decode(
                &aacDec,
                aacBuff, retSize + 7,
                buff, &datUse,
                (int *)&chn, (int *)&freq);
        }
        else
            retLen = -1;
        //播放文件
        if (retLen > 0)
        {
            //等播放指针赶上写入进度
            // timeout = 0;
            // while(wmtp->wmix->run && timeout++ < 200 &&
            //     loopWord == wmtp->wmix->loopWordRtp &&
            //     tick > wmtp->wmix->tick &&
            //     tick - wmtp->wmix->tick > totalWait)
            //     delayus(5000);
            if (!wmtp->wmix->run)
                break;
            //写入循环缓冲区
            head = wmix_load_wavStream(
                wmtp->wmix,
                src, retLen,
                freq,
                chn,
                sample, head, rdce, &tick);
            //写入的总字节数统计
            bpsCount += retLen;
            total += retLen;
            total2 = total * totalPow;
            //播放时间
            if (bpsCount > bytes_p_second)
            {
                bpsCount -= bytes_p_second;
                second = total / bytes_p_second;
                if (wmtp->wmix->debug)
                    printf("  RTP-RECV: %s:%d %02d:%02d\n", path, port, second / 60, second % 60);
            }
            //
            continue;
        }
        else
            delayus(5000);
    }
    //
    if (wmtp->wmix->debug)
        printf(">> RTP-RECV: %s:%d end <<\n", path, port);
    //删除文件
    if (msg_fd)
        msgctl(msg_fd, IPC_RMID, NULL);
    if (msgPath)
        remove(msgPath);
    close(ss->fd);
    free(ss);
    if (aacDec)
        aac_decodeRelease(&aacDec);
    //线程计数
    wmtp->wmix->thread_play -= 1;
    //
    if (wmtp->param)
        free(wmtp->param);
    //关闭 reduceMode
    if (rdceIsMe)
        wmtp->wmix->reduceMode = 1;
    free(wmtp);
}
#endif //if(WMIX_AAC)

#if (RTP_ONE_SR)
static SocketStruct *rtp_sr = NULL;
#endif
void wmix_rtp_send_pcma_thread(WMixThread_Param *wmtp)
{
    char *path = (char *)&wmtp->param[6];
    char *msgPath;
    key_t msg_key;
    int msg_fd = 0;
    WMix_Msg msg;
    //
    uint8_t chn = wmtp->param[0];
    uint8_t sample = wmtp->param[1];
    uint16_t freq = (wmtp->param[2] << 8) | wmtp->param[3];
    uint16_t port = (wmtp->param[4] << 8) | wmtp->param[5];
    //
    size_t buffSize;
    WMix_Point src, dist;
    ssize_t ret, total = 0;
    uint32_t second = 0, bytes_p_second, bpsCount = 0;
    //
    int ctrl = 0;
#if (!RTP_ONE_SR)
    SocketStruct *rtp_sr = NULL;
#endif
    //
    __time_t tick1, tick2;
    //
    int16_t record_addr = -1;
    unsigned char *buff;
    //
    RtpPacket rtpPacket;
    //
    uint8_t loopWord;
    loopWord = wmtp->wmix->loopWordRecord;
    //
    //参数检查,是否在允许的变参范围内
    if (freq != WMIX_FREQ)
    {
        fprintf(stderr, "wmix_rtp_send_pcma_thread: freq err, %dHz != %dHz(machine)\n", freq, WMIX_FREQ);
        return;
    }
    if (sample != WMIX_SAMPLE)
    {
        fprintf(stderr, "wmix_rtp_send_pcma_thread: sample err, must be %dbit(machine)\n", WMIX_SAMPLE);
        return;
    }
    if (chn != 1 && chn != 2)
    {
        fprintf(stderr, "wmix_rtp_send_pcma_thread: channels err, must be 1 or 2\n");
        return;
    }
    //初始化rtp
    if (!rtp_sr)
        rtp_sr = rtp_socket(path, port, 1);
    if (!rtp_sr)
    {
        fprintf(stderr, "rtp_socket: err\n");
        return;
    }
    rtp_header(&rtpPacket, 0, 0, 0, RTP_VESION, RTP_PAYLOAD_TYPE_PCMA, 1, 0, 0, 0);
    //初始化消息
    msgPath = (char *)&wmtp->param[strlen(path) + 6 + 1];
    if (msgPath && msgPath[0])
    {
        //创建消息挂靠路径
        if (access(msgPath, F_OK) != 0)
            creat(msgPath, 0777);
        //创建消息
        if ((msg_key = ftok(msgPath, WMIX_MSG_ID)) > 0)
            msg_fd = msgget(msg_key, IPC_CREAT | 0666);
    }
    else
        msgPath = NULL;
    //生成sdp文件
    rtp_create_sdp(
        "/tmp/record.sdp",
        path, port, chn, freq,
        RTP_PAYLOAD_TYPE_PCMA);
    //
    bytes_p_second = WMIX_CHANNELS * WMIX_SAMPLE / 8 * WMIX_FREQ;
    //每次从ai读取字节数
    buffSize = 320;
    buff = malloc(2 * WMIX_SAMPLE / 8 * 1024);
    //
    if (wmtp->wmix->debug)
        printf(
            "<< RTP-SEND-PCM: %s:%d start >>\n"
            "   通道数: %d\n"
            "   采样位数: %d bit\n"
            "   采样率: %d Hz\n",
            path, port, chn, sample, freq);
    //线程计数
    wmtp->wmix->thread_record += 1;
    //
    while (wmtp->wmix->run &&
           loopWord == wmtp->wmix->loopWordRecord)
    {
        //msg 检查
        if (msg_fd)
        {
            if (msgrcv(msg_fd, &msg,
                       WMIX_MSG_BUFF_SIZE,
                       0, IPC_NOWAIT) < 1)
            {
                if (errno != ENOMSG) //消息队列被关闭
                {
                    if (wmtp->wmix->debug)
                        printf("RTP-SEND-PCM exit: %d msgrecv err/%d\n", msg_fd, errno);
                    break;
                }
            }
            else
            {
                printf("RTP-SEND-PCM: msg recv %ld\n", msg.type);
                //控制信号
                if (msg.type == 0) //运行
                    ctrl = 0;
                else if (msg.type == 1) //停止
                    ctrl = 1;
                else if (msg.type == 2) //重连
                {
                    port = (msg.value[0] << 8) | msg.value[1];
                    path = (char *)&msg.value[2];
                    //
                    pthread_mutex_lock(&rtp_sr->lock);
                    close(rtp_sr->fd);
                    rtp_sr->fd = socket(AF_INET, SOCK_DGRAM, 0);
                    rtp_sr->addr.sin_port = htons(port);
                    rtp_sr->addr.sin_addr.s_addr = (in_addr_t)inet_addr(path);
                    rtp_sr->addrSize = sizeof(rtp_sr->addr);
                    bind(rtp_sr->fd, (const struct sockaddr *)&rtp_sr->addr, rtp_sr->addrSize);
                    pthread_mutex_unlock(&rtp_sr->lock);
                    //
                    ctrl = 0;
                }
            }
        }
        //
        tick1 = getTickUs();
        //
        ret = wmix_mem_read2((int16_t *)buff, buffSize / 2, &record_addr, true) * 2;
        if (ret > 0)
        {
            if (ctrl == 0)
            {
                bpsCount += ret;
                total += ret;
                //录制时间
                if (bpsCount > bytes_p_second)
                {
                    bpsCount -= bytes_p_second;
                    second = total / bytes_p_second;
                    if (wmtp->wmix->debug)
                        printf("  RTP-SEND-PCM: %s:%d %02d:%02d\n", path, port, second / 60, second % 60);
                }
            }
            //
            ret = PCM2G711a((char *)buff, (char *)rtpPacket.payload, ret, 0);
            if (ctrl == 0)
            {
                if (rtp_send(rtp_sr, &rtpPacket, ret) < ret)
                {
                    fprintf(stderr, "wmix_rtp_send_pcma_thread: rtp_send err !!\n");
                    delayus(1000000);
                    //重连
                    pthread_mutex_lock(&rtp_sr->lock);
                    close(rtp_sr->fd);
                    rtp_sr->fd = socket(AF_INET, SOCK_DGRAM, 0);
                    bind(rtp_sr->fd, (const struct sockaddr *)&rtp_sr->addr, rtp_sr->addrSize);
                    pthread_mutex_unlock(&rtp_sr->lock);
                    //
                    continue;
                }
                rtpPacket.rtpHeader.timestamp += ret;
            }
            //
            tick2 = getTickUs();
            if (tick2 > tick1 && tick2 - tick1 < 18000)
                delayus(18000 - (tick2 - tick1));
        }
        else
        {
            fprintf(stderr, "wmix_rtp_send_pcma_thread: read err %d\n", (int)ret);
            break;
        }
    }
    //
    if (wmtp->wmix->debug)
        printf(">> RTP-SEND-PCM: %s:%d end <<\n", path, port);
    //线程计数
    wmtp->wmix->thread_record -= 1;
    //
    if (msg_fd)
        msgctl(msg_fd, IPC_RMID, NULL);
    if (msgPath)
        remove(msgPath);
#if (WMIX_MODE == 1)
    free(buff);
#endif
#if (!RTP_ONE_SR)
    close(rtp_sr->fd);
    free(rtp_sr);
#endif
    //
    if (wmtp->param)
        free(wmtp->param);
    free(wmtp);
}

void wmix_rtp_recv_pcma_thread(WMixThread_Param *wmtp)
{
    char *path = (char *)&wmtp->param[6];
    char *msgPath;
    key_t msg_key;
    int msg_fd = 0;
    WMix_Msg msg;
    //
    uint8_t chn = wmtp->param[0];
    uint8_t sample = wmtp->param[1];
    uint16_t freq = (wmtp->param[2] << 8) | wmtp->param[3];
    uint16_t port = (wmtp->param[4] << 8) | wmtp->param[5];
    uint32_t bytes_p_second;
    //
    int ret = 0;
    uint8_t buff[1024];
    uint32_t buffSize, buffSize2;
    WMix_Point head, src;
    uint32_t tick, total = 0, total2 = 0, totalWait;
    uint32_t second = 0, bpsCount = 0;
    double totalPow;
    uint8_t rdce = 2, rdceIsMe = 0;
    //
    int ctrl = 0;
#if (!RTP_ONE_SR)
    SocketStruct *rtp_sr = NULL;
#endif
    //
    RtpPacket rtpPacket;
    int retSize;
    //
    // int timeout;
    uint8_t loopWord;
    loopWord = wmtp->wmix->loopWordRtp;
    //初始化rtp
    if (!rtp_sr)
        rtp_sr = rtp_socket(path, port, 0);
    if (!rtp_sr)
    {
        fprintf(stderr, "rtp_socket: err\n");
        return;
    }
    //初始化消息
    msgPath = (char *)&wmtp->param[strlen(path) + 6 + 1];
    if (msgPath && msgPath[0])
    {
        //创建消息挂靠路径
        if (access(msgPath, F_OK) != 0)
            creat(msgPath, 0777);
        //创建消息
        if ((msg_key = ftok(msgPath, WMIX_MSG_ID)) > 0)
            msg_fd = msgget(msg_key, IPC_CREAT | 0666);
    }
    else
        msgPath = NULL;
    //独占 reduceMode
    if (rdce > 1 && wmtp->wmix->reduceMode == 1)
    {
        wmtp->wmix->reduceMode = rdce;
        rdceIsMe = 1;
    }
    else
        rdce = 1;
    //默认缓冲区大小设为1秒字播放字节数
    bytes_p_second = chn * sample / 8 * freq;
    buffSize = bytes_p_second;
    buffSize2 = WMIX_CHANNELS * WMIX_SAMPLE / 8 * WMIX_FREQ;
    totalPow = (double)buffSize2 / buffSize;
    totalWait = 320 * 4; //buffSize2/2;
    //
    if (wmtp->wmix->debug)
        printf(
            "<< RTP-RECV-PCM: %s:%d start >>\n"
            "   通道数: %d\n"
            "   采样位数: %d bit\n"
            "   采样率: %d Hz\n",
            path, port, chn, sample, freq);
    //
    src.U8 = buff;
    head.U8 = 0;
    tick = 0;
    //线程计数
    wmtp->wmix->thread_play += 1;
    //
    while (wmtp->wmix->run &&
           loopWord == wmtp->wmix->loopWordRtp)
    {
        //msg 检查
        if (msg_fd)
        {
            if (msgrcv(msg_fd, &msg,
                       WMIX_MSG_BUFF_SIZE,
                       0, IPC_NOWAIT) < 1)
            {
                if (errno != ENOMSG) //消息队列被关闭
                {
                    if (wmtp->wmix->debug)
                        printf("RTP-RECV-PCM exit: %d msgrecv err/%d\n", msg_fd, errno);
                    break;
                }
            }
            else
            {
                printf("RTP-RECV-PCM: msg recv %ld\n", msg.type);
                //控制信号
                if (msg.type == 0) //运行
                    ctrl = 0;
                else if (msg.type == 1) //停止
                    ctrl = 1;
                else if (msg.type == 2) //重连
                {
                    port = (msg.value[0] << 8) | msg.value[1];
                    path = (char *)&msg.value[2];
                    //
                    pthread_mutex_lock(&rtp_sr->lock);
                    close(rtp_sr->fd);
                    rtp_sr->fd = socket(AF_INET, SOCK_DGRAM, 0);
                    rtp_sr->addr.sin_port = htons(port);
                    rtp_sr->addr.sin_addr.s_addr = inet_addr(path);
                    rtp_sr->addrSize = sizeof(rtp_sr->addr);
                    bind(rtp_sr->fd, (const struct sockaddr *)&rtp_sr->addr, rtp_sr->addrSize);
                    pthread_mutex_unlock(&rtp_sr->lock);
                    //
                    ctrl = 0;
                }
            }
        }
        //等播放指针赶上写入进度
        // timeout = 0;
        // while(wmtp->wmix->run && timeout++ < 200 &&
        //     loopWord == wmtp->wmix->loopWordRtp &&
        //     tick > wmtp->wmix->tick &&
        //     tick - wmtp->wmix->tick > totalWait)
        // {
        //     rtp_recv(rtp_sr, &rtpPacket, &retSize);
        //     delayus(5000);
        // }
        if (!wmtp->wmix->run)
            break;
        //读rtp数据
        ret = rtp_recv(rtp_sr, &rtpPacket, (uint32_t *)&retSize);
        if (ret > 0 && retSize > 0)
            ret = G711a2PCM((char *)rtpPacket.payload, (char *)buff, retSize, 0);
        else
            ret = -1;
        //播放文件
        if (ret > 0)
        {
            //写入循环缓冲区
            head = wmix_load_wavStream(
                wmtp->wmix,
                src, ret,
                freq,
                chn,
                sample, head, rdce, &tick);
            //写入的总字节数统计
            bpsCount += ret;
            total += ret;
            total2 = total * totalPow;
            //播放时间
            if (bpsCount > bytes_p_second)
            {
                bpsCount -= bytes_p_second;
                second = total / bytes_p_second;
                if (wmtp->wmix->debug)
                    printf("  RTP-RECV-PCM: %s:%d %02d:%02d\n", path, port, second / 60, second % 60);
            }
            //
            continue;
        }
        else
            delayus(10000);
    }
    //
    if (wmtp->wmix->debug)
        printf(">> RTP-RECV-PCM: %s:%d end <<\n", path, port);
    //删除文件
    if (msg_fd)
        msgctl(msg_fd, IPC_RMID, NULL);
    if (msgPath)
        remove(msgPath);
#if (!RTP_ONE_SR)
    close(rtp_sr->fd);
    free(rtp_sr);
#endif
    //线程计数
    wmtp->wmix->thread_play -= 1;
    //
    if (wmtp->param)
        free(wmtp->param);
    //关闭 reduceMode
    if (rdceIsMe)
        wmtp->wmix->reduceMode = 1;
    free(wmtp);
}

void wmix_load_audio_thread(WMixThread_Param *wmtp)
{
    char *name = (char *)wmtp->param;
    uint16_t len = strlen((char *)wmtp->param);
    //
    char *msgPath;
    key_t msg_key;
    int msg_fd = 0;
    //
    bool run = true, joinQueue = false;
    //
    int queue = -1;
    //
    uint8_t loopWord;
    loopWord = wmtp->wmix->loopWord;
    //线程计数
    wmtp->wmix->thread_play += 1;
    //
    msgPath = (char *)&wmtp->param[len + 1];
    if (msgPath && msgPath[0])
    {
        //创建消息挂靠路径
        if (access(msgPath, F_OK) != 0)
            creat(msgPath, 0777);
        //创建消息
        if ((msg_key = ftok(msgPath, WMIX_MSG_ID)) > 0)
            msg_fd = msgget(msg_key, IPC_CREAT | 0666);
    }
    else
        msgPath = NULL;
    //排队(循环播放和背景消减除时除外)
    if (((wmtp->flag & 0xFF) == 9 || (wmtp->flag & 0xFF) == 10) && ((wmtp->flag >> 8) & 0xFF) == 0 && ((wmtp->flag >> 16) & 0xFF) == 0)
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
    //
    if (run)
    {
        if (joinQueue)
            wmtp->wmix->onPlayCount += 1;
        //
        if (len > 3 &&
            (name[len - 3] == 'a' || name[len - 3] == 'A') &&
            (name[len - 2] == 'a' || name[len - 2] == 'A') &&
            (name[len - 1] == 'c' || name[len - 1] == 'C'))
#if (WMIX_AAC)
            wmix_load_aac(wmtp->wmix, name, msg_fd, (wmtp->flag >> 8) & 0xFF, (wmtp->flag >> 16) & 0xFF);
#else
            ;
#endif
        else if (len > 3 &&
                 (name[len - 3] == 'm' || name[len - 3] == 'M') &&
                 (name[len - 2] == 'p' || name[len - 2] == 'P') &&
                 name[len - 1] == '3')
#if (WMIX_MP3)
            wmix_load_mp3(wmtp->wmix, name, msg_fd, (wmtp->flag >> 8) & 0xFF, (wmtp->flag >> 16) & 0xFF);
#else
            ;
#endif
        else
            wmix_load_wav(wmtp->wmix, name, msg_fd, (wmtp->flag >> 8) & 0xFF, (wmtp->flag >> 16) & 0xFF);
        //
        if (joinQueue)
            wmtp->wmix->onPlayCount -= 1;
    }
    //
    if (queue >= 0)
        wmtp->wmix->queue.head += 1;
    //
    if (msg_fd)
        msgctl(msg_fd, IPC_RMID, NULL);
    if (msgPath)
        remove(msgPath);
    //线程计数
    wmtp->wmix->thread_play -= 1;
    //
    if (wmtp->param)
        free(wmtp->param);
    free(wmtp);
}

void wmix_msg_thread(WMixThread_Param *wmtp)
{
    WMix_Struct *wmix = wmtp->wmix;
    WMix_Msg msg;
    ssize_t ret;
    bool err_exit = false;
    //刚启动,playRun和recordRun都为false,这里置9999,不再清理
    int playTickTimeout = 9999, recordTickTimeout = 9999;

    //路径检查 //F_OK 是否存在 R_OK 是否有读权限 W_OK 是否有写权限 X_OK 是否有执行权限
    if (access(WMIX_MSG_PATH, F_OK) != 0)
        mkdir(WMIX_MSG_PATH, 0777);
    //再次检查
    if (access(WMIX_MSG_PATH, F_OK) != 0)
    {
        fprintf(stderr, "wmix_msg_thread: msg path not found\n");
        return;
    }
    //清空文件夹
    system(WMIX_MSG_PATH_CLEAR);
    //权限处理
    system(WMIX_MSG_PATH_AUTHORITY);
    //获得管道
    if ((wmix->msg_key = ftok(WMIX_MSG_PATH, WMIX_MSG_ID)) == -1)
    {
        fprintf(stderr, "wmix_msg_thread: ftok err\n");
        return;
    }
    //清空队列
    if ((wmix->msg_fd = msgget(wmix->msg_key, 0666)) != -1)
        msgctl(wmix->msg_fd, IPC_RMID, NULL);
    //重新创建队列
    if ((wmix->msg_fd = msgget(wmix->msg_key, IPC_CREAT | 0666)) == -1)
    {
        fprintf(stderr, "wmix_msg_thread: msgget err\n");
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
                printf("\r\nwmix_msg_thread: msg %ld -- val[0] %d\n", msg.type & 0xFF, msg.value[0]);

            switch (msg.type & 0xFF)
            {
            //音量设置
            case WMT_VOLUME:
                sys_volume_set(msg.value[0], msg.value[1]);
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
                wmix_throwOut_thread(wmix,
                                     msg.type,
                                     msg.value,
                                     WMIX_MSG_BUFF_SIZE,
                                     &wmix_load_audio_thread);
                break;
            //fifo播放wav流
            case WMT_FIFO_PLAY:
                wmix_throwOut_thread(wmix,
                                     msg.type,
                                     msg.value,
                                     WMIX_MSG_BUFF_SIZE,
                                     &wmix_load_wav_fifo_thread);
                break;
            //复位
            case WMT_RESET:
                wmix->loopWord += 1;
                wmix->run = false;
                break;
            //fifo录音wav流
            case WMT_FIFO_RECORD:
                wmix_throwOut_thread(wmix,
                                     msg.type,
                                     msg.value,
                                     WMIX_MSG_BUFF_SIZE,
                                     &wmix_record_wav_fifo_thread);
                break;
            //录音wav文件
            case WMT_RECORD_WAV:
                wmix_throwOut_thread(wmix,
                                     msg.type,
                                     msg.value,
                                     WMIX_MSG_BUFF_SIZE,
                                     &wmix_record_wav_thread);
                break;
            //清空播放列表
            case WMT_CLEAN_LIST:
                wmix->loopWord += 1;
                break;
            //rtp send pcma
            case WMT_RTP_SEND_PCMA:
                wmix_throwOut_thread(wmix,
                                     msg.type,
                                     msg.value,
                                     WMIX_MSG_BUFF_SIZE,
                                     &wmix_rtp_send_pcma_thread);
                break;
            //rtp recv pcma
            case WMT_RTP_RECV_PCMA:
                wmix_throwOut_thread(wmix,
                                     msg.type,
                                     msg.value,
                                     WMIX_MSG_BUFF_SIZE,
                                     &wmix_rtp_recv_pcma_thread);
                break;
#if (WMIX_AAC)
            //录音aac文件
            case WMT_RECORD_AAC:
                wmix_throwOut_thread(wmix,
                                     msg.type,
                                     msg.value,
                                     WMIX_MSG_BUFF_SIZE,
                                     &wmix_record_aac_thread);
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
            //开关log
            case WMT_LOG_SW:
                if (msg.value[0])
                    wmix->debug = true;
                else
                    wmix->debug = false;
                break;
            }
            continue;
        }
        //在别的地方重开了该程序的副本
        else if (ret < 1 && errno != ENOMSG)
        {
            if (wmix->debug)
                printf("wmix_msg_thread exit: %d msgrecv err/%d\n", wmix->msg_fd, errno);
            err_exit = true;
            break;
        }
        //长时间没有播放任务,关闭播放器
        if (wmix->thread_play == 0 && wmix->shmemRun == 0)
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
                    printf("wmix play: clear\n");
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
                printf("wmix play: start\n");
            wmix->playRun = true;
        }
        //长时间没有录音任务,关闭录音
        if (wmix->thread_record == 0 && wmix->shmemRun == 0)
        {
            //连续5秒没有录音线程,清tick
            if (recordTickTimeout < 5000)
                recordTickTimeout += 10;
            else
            {
                if (recordTickTimeout != 9999)
                {
                    // printf("wmix record: clear\n"); //不在这里打印
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
        //
        delayus(10000);
    }
    //删除队列
    msgctl(wmix->msg_fd, IPC_RMID, NULL);
    //
    if (wmix->debug)
        printf("wmix_msg_thread exit\n");
    //
    if (wmtp->param)
        free(wmtp->param);
    free(wmtp);
    //
    if (err_exit)
    {
        signal_callback(SIGINT);
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
    double dataToTime = 1000000 / (WMIX_CHANNELS * WMIX_SAMPLE / 8 * WMIX_FREQ);
    uint8_t *playBuff;
#if (WMIX_MODE == 1)
    //要足够装下一包 WMIX_INTERVAL_MS 时间采集量的数据
    uint8_t write_buff[WMIX_PKG_SIZE];
#else
    SNDPCMContainer_t *playback = wmtp->wmix->playback;
#endif
    //按时间间隔计算每次发包大小,帧数
    uint32_t frame_num = WMIX_FRAME_NUM;
    //按时间间隔计算每次发包大小,字节数
    uint32_t pkg_size = WMIX_PKG_SIZE;

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

#if (WMIX_MODE == 0)
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
#endif
            tick1 = getTickUs();

#if (WMIX_MODE == 1)
            memset(write_buff, 0, sizeof(write_buff));
            for (count = countTotal = 0, dist.U8 = playBuff = write_buff; countTotal < pkg_size * 4;) //每次最多传x8帧
#else
            memset(playback->data_buf, 0, playback->chunk_bytes);
            for (count = countTotal = 0, dist.U8 = playBuff = playback->data_buf; countTotal < pkg_size * 4;) //每次最多传x4帧
#endif
            {
#if (WMIX_CHANNELS == 1)
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
                if (count == pkg_size)
                {
                    //发包计数
                    countTotal += pkg_size;

#if (WMIX_WEBRTC_NS)
                    //噪音抑制
                    if (wmix->webrtcEnable[WR_NS_PA] && WMIX_FREQ <= 32000)
                    {
                        if (wmix->webrtcPoint[WR_NS_PA] == NULL)
                            wmix->webrtcPoint[WR_NS_PA] = ns_init(WMIX_CHANNELS, WMIX_FREQ);
                        if (wmix->webrtcPoint[WR_NS_PA])
                        {
                            //开始转换
                            ns_process(
                                wmix->webrtcPoint[WR_NS_PA],
                                (int16_t *)playBuff,
                                (int16_t *)playBuff,
                                frame_num);
                        }
                    }
#endif

#ifdef AEC_FILE_STREAM_TEST
#if (WMIX_WEBRTC_AEC)
                    //回声消除,文件流干扰测试
                    if (wmix->webrtcEnable[WR_AEC] && WMIX_FREQ <= 32000)
                    {
                        if (wmix->webrtcPoint[WR_AEC] == NULL)
                            wmix->webrtcPoint[WR_AEC] = aec_init(WMIX_CHANNELS, WMIX_FREQ, WMIX_INTERVAL_MS);
                        if (wmix->webrtcPoint[WR_AEC])
                        {
                            memset(fileBuff, 0, sizeof(fileBuff));
                            ret = read(fd, fileBuff, pkg_size);
                            if (ret < pkg_size)
                                lseek(fd, 44, SEEK_SET);
                            p1 = (int16_t *)fileBuff;
                            p2 = (int16_t *)playBuff;
                            for (i = 0; i < frame_num; i++)
                            {
                                // p1[i] >>= 1;
                                p2[i] += p1[i];
                            }
                            aec_process2(
                                wmix->webrtcPoint[WR_AEC],
                                (int16_t *)fileBuff,
                                (int16_t *)playBuff,
                                (int16_t *)playBuff,
                                frame_num,
                                0); //WMIX_INTERVAL_MS/2);
                        }
                    }
#endif
#endif

                    playPkgBuff_add(playBuff);

                    //开始播放
#if (WMIX_MODE == 1)
                    hiaudio_ao_write((int16_t *)playBuff, frame_num);
#else
                    SNDWAV_WritePcm(playback, frame_num);
#endif

#ifndef AEC_FILE_STREAM_TEST
#ifdef WMIX_RECORD_PLAY_SYNC
                    wmix_shmem_write_circle(wmtp);
#endif
#endif

                    //重置数据和指针
                    memset(playBuff, 0, pkg_size);
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
        else
        {
            memset(playBuff, 0, pkg_size);
            playPkgBuff_add(playBuff);

#ifdef WMIX_RECORD_PLAY_SYNC
            wmix_shmem_write_circle(wmtp);
#endif
            //没在播放状态的延时
            delayus(WMIX_INTERVAL_MS * 1000);
            tickT = 0;
        }
        //失能释放
#if (WMIX_WEBRTC_NS)
        if (!wmix->webrtcEnable[WR_NS_PA] && wmix->webrtcPoint[WR_NS_PA])
        {
            ns_release(wmix->webrtcPoint[WR_NS_PA]);
            wmix->webrtcPoint[WR_NS_PA] = NULL;
        }
#endif
    }
#ifdef AEC_FILE_STREAM_TEST
    close(fd);
#endif
    //
#ifdef WMIX_RECORD_PLAY_SYNC
    wmix_shmem_write_circle(wmtp);
#endif
    //
    if (wmix->debug)
        printf("wmix_play_thread exit\n");
    //
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
        //等待线程关闭
        //等待各指针不再有人使用
        timeout = 200; //2秒超时
        do
        {
            if (timeout-- < 1)
                break;
            delayus(10000);
        } while (wmix->thread_sys > 0 ||
                 wmix->thread_play > 0 ||
                 wmix->thread_record > 0);
        //
#if (WMIX_MODE == 1)
        hiaudio_exit();
#else
        if (wmix->playback)
            wmix_alsa_release(wmix->playback);
        if (wmix->recordback)
            wmix_alsa_release(wmix->recordback);
#endif
        // pthread_mutex_destroy(&wmix->lock);
        free(wmix);
    }
}

WMix_Struct *wmix_init(void)
{
    WMix_Struct *wmix = NULL;

    //路径检查 //F_OK 是否存在 R_OK 是否有读权限 W_OK 是否有写权限 X_OK 是否有执行权限
    if (access(WMIX_MSG_PATH, F_OK) != 0)
        mkdir(WMIX_MSG_PATH, 0777);

#if (WMIX_MODE == 1)
    if (hiaudio_ao_init(WMIX_CHANNELS, WMIX_SAMPLE, WMIX_FREQ))
        return NULL;
    if (hiaudio_ai_init(WMIX_CHANNELS, WMIX_SAMPLE, WMIX_FREQ, WMIX_FREQ / 1000 * WMIX_INTERVAL_MS))
        return NULL;
#else
    SNDPCMContainer_t *playback = wmix_alsa_init(WMIX_CHANNELS, WMIX_SAMPLE, WMIX_FREQ, 'p');
    if (!playback)
        return NULL;
    SNDPCMContainer_t *recordback = NULL; //wmix_alsa_init(WMIX_CHANNELS, WMIX_SAMPLE, WMIX_FREQ, 'c');

#endif
    wmix = (WMix_Struct *)calloc(1, sizeof(WMix_Struct));
    wmix->buff = (uint8_t *)calloc(WMIX_BUFF_SIZE + 4, sizeof(uint8_t));
#if (WMIX_MODE != 1)
    wmix->playback = playback;
    wmix->recordback = recordback;
#endif
    wmix->start.U8 = wmix->head.U8 = wmix->tail.U8 = wmix->buff;
    wmix->end.U8 = wmix->buff + WMIX_BUFF_SIZE;

    // pthread_mutex_init(&wmix->lock, NULL);

    wmix->run = true;
    // wmix->playRun = true;
    // wmix->recordRun = true;
    wmix->reduceMode = 1;

#ifndef WMIX_RECORD_PLAY_SYNC
    wmix_throwOut_thread(wmix, 0, NULL, 0, &wmix_shmem_write_circle);
#endif
    // wmix_throwOut_thread(wmix, 0, NULL, 0, &wmix_shmem_read_circle);
    wmix_throwOut_thread(wmix, 0, NULL, 0, &wmix_msg_thread);
    wmix_throwOut_thread(wmix, 0, NULL, 0, &wmix_play_thread);

    wmix->webrtcEnable[WR_VAD] = 0;
    wmix->webrtcEnable[WR_AEC] = 1;
    wmix->webrtcEnable[WR_NS] = 0;
    wmix->webrtcEnable[WR_NS_PA] = 0;
    wmix->webrtcEnable[WR_AGC] = 0;

    printf("\n---- WMix info -----\n"
           "   chn: %d\n"
           "   freq: %d Hz\n"
           "   sample: %d bit\n"
           "   total: -- Bytes\n"
           "   webrtc: vad/%d, aec/%d, ns/%d, ns_pa/%d agc/%d\n",
           WMIX_CHANNELS, WMIX_FREQ, WMIX_SAMPLE,
           wmix->webrtcEnable[WR_VAD],
           wmix->webrtcEnable[WR_AEC],
           wmix->webrtcEnable[WR_NS],
           wmix->webrtcEnable[WR_NS_PA],
           wmix->webrtcEnable[WR_AGC]);

    signal(SIGINT, signal_callback);
    signal(SIGTERM, signal_callback);

    return wmix;
}

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
        if (sum < -32768)
            return -32768;
        else if (sum > 32767)
            return 32767;
        else
            return sum;
    }
}

WMix_Point wmix_load_wavStream(
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
    //
#if (WMIX_MODE == 0)
    uint16_t correct = WMIX_CHANNELS * WMIX_FREQ * 16 / 8 / 5;
#else
    uint16_t correct = 0;
#endif
    //
    if (!wmix || !wmix->run || !pSrc.U8 || srcU8Len < 1)
        return pHead;
    //
    if (!pHead.U8 || (*tick) < wmix->tick)
    {
        pHead.U8 = wmix->head.U8 + correct;
        (*tick) = wmix->tick + correct;
        //循环处理
        if (pHead.U8 >= wmix->end.U8)
            pHead.U8 = wmix->start.U8;
    }
    //
    if (reduce == wmix->reduceMode)
        rdce = &rdce1;
    //---------- 参数一致 直接拷贝 ----------
    if (freq == WMIX_FREQ &&
        channels == WMIX_CHANNELS &&
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
            //
#if (WMIX_CHANNELS != 1)
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
                            //
                            pSrc.S16++;
                            pSrc.S16++;
                            //
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
#if (WMIX_CHANNELS != 1)
                            *pHead.S16 = volumeAdd(*pHead.S16, *pSrc.S16 / (*rdce));
                            pHead.S16++;
                            tickAdd += 2;
#endif
                            pSrc.S16++;
                            //
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
                            //
                            pSrc.S16++;
                            //
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
#if (WMIX_CHANNELS != 1)
                            *pHead.S16 = volumeAdd(*pHead.S16, *pSrc.S16 / (*rdce));
                            pHead.S16++;
                            tickAdd += 2;
#endif
                            pSrc.S16++;
                            //
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
            // printf("smallFreq: head = %ld , divPow = %f, divCount = %f, freqErr/%d, freq/%d\n",
            //     pHead.U8 - wmix->start.U8,
            //     divPow, divCount, freqErr, freq);
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
#if (WMIX_CHANNELS != 1)
                            // *pHead.S16 = volumeAdd(*pHead.S16, *pSrc.S16/(*rdce));
                            *pHead.S16 = volumeAdd(*pHead.S16, repairBuff[repairBuffCount] / (*rdce));
                            pHead.S16++;
                            tickAdd += 2;
#endif
                            //
                            divCount -= 1.0;
                            //
                            repairBuffCount += 1;
                        }
                        else
                        {
                            //拷贝一帧数据
                            *pHead.S16 = volumeAdd(*pHead.S16, *pSrc.S16 / (*rdce));
                            pHead.S16++;
                            pSrc.S16++;
                            tickAdd += 2;
#if (WMIX_CHANNELS != 1)
                            *pHead.S16 = volumeAdd(*pHead.S16, *pSrc.S16 / (*rdce));
                            pHead.S16++;
                            tickAdd += 2;
#endif
                            pSrc.S16++;
                            //
                            divCount += divPow;
                            count += 4;

                            //填充数据均值滤波
                            if (divCount >= 1.0)
                            {
                                divCount2 = (int)divCount + 1;
                                repairTemp = *(pSrc.S16 - 2);
                                repairStep = (float)((*pSrc.S16) - repairTemp) / divCount2;
                                // printf(">> S/%d, E/%d, ERR/%.2f\n", repairTemp, (*pSrc.S16), repairStep);
                                for (repairBuffCount = 0, repairStepSum = repairStep; repairBuffCount < divCount2;)
                                {
                                    repairBuff[repairBuffCount] = repairTemp + repairStepSum;
                                    // printf("%d\n", repairBuff[repairBuffCount]);
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
                        //
                        if (divCount >= 1.0)
                        {
                            //拷贝一帧数据 pSrc指针不动
                            // *pHead.S16 = volumeAdd(*pHead.S16, *pSrc.S16/(*rdce));
                            *pHead.S16 = volumeAdd(*pHead.S16, repairBuff[repairBuffCount] / (*rdce));
                            pHead.S16++;
                            tickAdd += 2;
#if (WMIX_CHANNELS != 1)
                            // *pHead.S16 = volumeAdd(*pHead.S16, *pSrc.S16/(*rdce));
                            *pHead.S16 = volumeAdd(*pHead.S16, repairBuff[repairBuffCount] / (*rdce));
                            pHead.S16++;
                            tickAdd += 2;
#endif
                            //
                            divCount -= 1.0;
                            //
                            repairBuffCount += 1;
                        }
                        else
                        {
                            //拷贝一帧数据
                            *pHead.S16 = volumeAdd(*pHead.S16, *pSrc.S16 / (*rdce));
                            pHead.S16++;
                            tickAdd += 2;
#if (WMIX_CHANNELS != 1)
                            *pHead.S16 = volumeAdd(*pHead.S16, *pSrc.S16 / (*rdce));
                            pHead.S16++;
                            tickAdd += 2;
#endif
                            pSrc.S16++;
                            //
                            divCount += divPow;
                            count += 2;

                            //填充数据均值滤波
                            if (divCount >= 1.0)
                            {
                                divCount2 = (int)divCount + 1;
                                repairTemp = *(pSrc.S16 - 1);
                                repairStep = (float)((*pSrc.S16) - repairTemp) / divCount2;
                                // printf(">> S/%d, E/%d, ERR/%.2f\n", repairTemp, (*pSrc.S16), repairStep);
                                for (repairBuffCount = 0, repairStepSum = repairStep; repairBuffCount < divCount2;)
                                {
                                    repairBuff[repairBuffCount] = repairTemp + repairStepSum;
                                    // printf("%d\n", repairBuff[repairBuffCount]);
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
        // tickAdd += correct;
        pHead.U8 = wmix->head.U8 + tickAdd;
        tickAdd += wmix->tick;
        //
        if (pHead.U8 >= wmix->end.U8)
            pHead.U8 -= WMIX_BUFF_SIZE;
    }
    else
        tickAdd += (*tick);
    //
    *tick = tickAdd;
    //
    return pHead;
}

void wmix_load_wav(
    WMix_Struct *wmix,
    char *wavPath,
    int msg_fd,
    uint8_t reduce,
    uint8_t repeatInterval)
{
    int fd = 0;
    ssize_t ret = 0;
    uint8_t *buff = NULL;
    //每次读
    uint32_t buffSize, buffSize2;
    //wav文件头信息
    WAVContainer_t wav;
    //写循环缓冲区
    WMix_Point src, head;
    //播放计时
    uint32_t tick, total = 0, total2 = 0, totalWait;
    uint32_t second = 0, bpsCount = 0;
    double totalPow;
    //背景消减和重复播放延时
    uint8_t rdce = reduce + 1, rdceIsMe = 0;
    uint16_t repeat = (uint16_t)repeatInterval * 10;
    //消息通信
    WMix_Msg msg;
    //系统更新loopWord时,会关闭该条播放
    int timeout;
    uint8_t loopWord;
    loopWord = wmix->loopWord;
    //
    if (!wmix || !wmix->run || !wavPath)
        return;
    //
    if ((fd = open(wavPath, O_RDONLY)) <= 0)
    {
        fprintf(stderr, "wmix_load_wav: %s open err\n", wavPath);
        return;
    }
    if (WAV_ReadHeader(fd, &wav) < 0)
    {
        fprintf(stderr, "Error WAV_Parse [%s]\n", wavPath);
        close(fd);
        return;
    }
    //
    if (wmix->debug)
        printf(
            "<< PLAY-WAV: %s start >>\n"
            "   通道数: %d\n"
            "   采样位数: %d bit\n"
            "   采样率: %d Hz\n"
            "   每秒字节: %d Bytes\n"
            "   重播间隔: %d sec\n"
            "   tick: %d\n"
            "   msgid: %d\n",
            wavPath,
            wav.format.channels,
            wav.format.sample_length,
            wav.format.sample_rate,
            wav.format.bytes_p_second,
            repeat / 10,
            wmix->tick,
            msg_fd);
    //独占 reduceMode
    if (rdce > 1 && wmix->reduceMode == 1)
    {
        wmix->reduceMode = rdce;
        rdceIsMe = 1;
    }
    else
        rdce = 1;
    //默认缓冲区大小设为1秒字播放字节数
    buffSize = wav.format.bytes_p_second;
    buffSize2 = WMIX_CHANNELS * WMIX_SAMPLE / 8 * WMIX_FREQ;
    totalPow = (double)buffSize2 / buffSize;
    totalWait = buffSize2 / 2;
    //把每秒数据包拆得越细, 打断速度越快
    //以下拆包的倍数必须能同时被 wav.format.sample_rate 和 WMIX_FREQ 整除 !!
    if (wav.format.sample_rate % 4 == 0)
    {
        buffSize /= 4;
        buffSize2 /= 4;
        totalWait = buffSize2;
    }
    else if (wav.format.sample_rate % 3 == 0)
    {
        buffSize /= 3;
        buffSize2 /= 3;
        totalWait = buffSize2;
    }
    else
    {
        buffSize /= 2;
        buffSize2 /= 2;
        totalWait = buffSize2 / 2;
    }
    //
    buff = (uint8_t *)calloc(buffSize, sizeof(uint8_t));
    //
    src.U8 = buff;
    head.U8 = 0;
    tick = 0;
    //
    while (wmix->run && loopWord == wmix->loopWord)
    {
        //msg 检查
        if (msg_fd)
        {
            if (msgrcv(msg_fd, &msg,
                       WMIX_MSG_BUFF_SIZE,
                       0, IPC_NOWAIT) < 1 &&
                errno != ENOMSG) //消息队列被关闭
            {
                if (wmix->debug)
                    printf("PLAY-WAV exit: %d msgrecv err/%d\n", msg_fd, errno);
                break;
            }
        }
        //播放文件
        ret = read(fd, buff, buffSize);
        if (ret > 0)
        {
            //等播放指针赶上写入进度
            timeout = 0;
            while (wmix->run && timeout++ < 200 &&
                   loopWord == wmix->loopWord &&
                   tick > wmix->tick &&
                   tick - wmix->tick > totalWait)
                delayus(5000);
            if (!wmix->run || loopWord != wmix->loopWord)
                break;
            //写入循环缓冲区
            head = wmix_load_wavStream(
                wmix,
                src, ret,
                wav.format.sample_rate,
                wav.format.channels,
                wav.format.sample_length, head, rdce, &tick);
            //写入的总字节数统计
            bpsCount += ret;
            total += ret;
            total2 = total * totalPow;
            //播放时间
            if (bpsCount > wav.format.bytes_p_second)
            {
                bpsCount -= wav.format.bytes_p_second;
                second = total / wav.format.bytes_p_second;
                if (wmix->debug)
                    printf("  PLAY-WAV: %s %02d:%02d\n", wavPath, second / 60, second % 60);
            }
            //
            if (head.U8 == 0)
            {
                if (wmix->debug)
                    printf("PLAY-WAV exit: head.U8 = 0\n");
                break;
            }
        }
        else if (repeat)
        {
            //关闭 reduceMode
            if (rdceIsMe && wmix->reduceMode == rdce)
                wmix->reduceMode = 1;
            //
            lseek(fd, 44, SEEK_SET);
            //
            for (ret = 0; ret < repeat; ret++)
            {
                delayus(100000);
                //
                if (!wmix->run || loopWord != wmix->loopWord)
                    break;
                //
                if (msg_fd)
                {
                    if (msgrcv(msg_fd, &msg,
                               WMIX_MSG_BUFF_SIZE,
                               0, IPC_NOWAIT) < 1 &&
                        errno != ENOMSG) //消息队列被关闭
                    {
                        if (wmix->debug)
                            printf("PLAY-WAV exit: %d msgrecv err/%d\n", msg_fd, errno);
                        break;
                    }
                }
            }
            //
            if (ret != repeat)
            {
                ret = -1;
                break;
            }
            //重启 reduceMode
            if (rdceIsMe && wmix->reduceMode == 1)
                wmix->reduceMode = rdce;
            //
            if (wmix->debug)
                printf("<< PLAY-WAV: %s start >>\n   通道数: %d\n   采样位数: %d bit\n   采样率: %d Hz\n   每秒字节: %d Bytes\n   重播间隔: %d sec\n",
                       wavPath,
                       wav.format.channels,
                       wav.format.sample_length,
                       wav.format.sample_rate,
                       wav.format.bytes_p_second,
                       repeat / 10);
            //
            total = total2 = bpsCount = 0;
            src.U8 = buff;
            head.U8 = wmix->head.U8;
            tick = 0;
        }
        else
            break;
    }
    //
    close(fd);
    free(buff);
    //
    if (wmix->debug)
        printf(">> PLAY-WAV: %s end <<\n", wavPath);
    //关闭 reduceMode
    if (rdceIsMe && wmix->reduceMode == rdce)
        wmix->reduceMode = 1;
}

#if (WMIX_AAC)
void wmix_load_aac(
    WMix_Struct *wmix,
    char *aacPath,
    int msg_fd,
    uint8_t reduce,
    uint8_t repeatInterval)
{
    int fd = 0;
    ssize_t ret = 0;
    //写循环缓冲区
    WMix_Point src, head;
    //播放计时
    uint32_t tick, total = 0, total2 = 0, totalWait;
    uint32_t second = 0, bpsCount = 0;
    double totalPow;
    //背景消减和重复播放延时
    uint8_t rdce = reduce + 1, rdceIsMe = 0;
    uint16_t repeat = (uint16_t)repeatInterval * 10;
    //消息通信
    WMix_Msg msg;
    //aac解码句柄
    void *aacDec = NULL;
    uint8_t out[8192];
    //音频基本参数
    int chn;
    int sample = 16;
    int freq;
    uint32_t bytes_p_second;
    //系统更新loopWord时,会关闭该条播放
    int timeout;
    uint8_t loopWord;
    loopWord = wmix->loopWord;
    //
    if (!wmix || !wmix->run || !aacPath)
        return;
    //
    if ((fd = open(aacPath, O_RDONLY)) <= 0)
    {
        fprintf(stderr, "wmix_load_aac: %s open err\n", aacPath);
        return;
    }
    //初始化解码器
    ret = aac_decode2(&aacDec, fd, out, (int *)&chn, (int *)&freq);
    if (ret < 0)
    {
        fprintf(stderr, "aac_decode2: err\n");
        close(fd);
        return;
    }
    //
    bytes_p_second = chn * sample / 8 * freq;
    //
    if (wmix->debug)
        printf(
            "<< PLAY-AAC: %s start >>\n"
            "   通道数: %d\n"
            "   采样位数: %d bit\n"
            "   采样率: %d Hz\n"
            "   每秒字节: %d Bytes\n"
            "   重播间隔: %d sec\n",
            aacPath,
            chn,
            sample,
            freq,
            bytes_p_second,
            repeat / 10);
    //独占 reduceMode
    if (rdce > 1 && wmix->reduceMode == 1)
    {
        wmix->reduceMode = rdce;
        rdceIsMe = 1;
    }
    else
        rdce = 1;
    //
    totalPow = (double)(WMIX_CHANNELS * WMIX_SAMPLE / 8 * WMIX_FREQ) / bytes_p_second;
    //
    totalWait = bytes_p_second / 2;
    //
    src.U8 = out;
    head.U8 = 0;
    tick = 0;
    //
    while (wmix->run &&
           loopWord == wmix->loopWord)
    {
        //msg 检查
        if (msg_fd)
        {
            if (msgrcv(msg_fd, &msg,
                       WMIX_MSG_BUFF_SIZE,
                       0, IPC_NOWAIT) < 1 &&
                errno != ENOMSG) //消息队列被关闭
            {
                if (wmix->debug)
                    printf("PLAY-AAC exit: %d msgrecv err/%d\n", msg_fd, errno);
                break;
            }
        }
        //播放文件
        if (ret > 0)
        {
            //等播放指针赶上写入进度
            timeout = 0;
            while (wmix->run && timeout++ < 200 &&
                   loopWord == wmix->loopWord &&
                   tick > wmix->tick &&
                   tick - wmix->tick > totalWait)
                delayus(5000);
            if (!wmix->run || loopWord != wmix->loopWord)
                break;
            //写入循环缓冲区
            head = wmix_load_wavStream(
                wmix,
                src, ret,
                freq,
                chn,
                sample, head, rdce, &tick);
            //写入的总字节数统计
            bpsCount += ret;
            total += ret;
            total2 = total * totalPow;
            //播放时间
            if (bpsCount > bytes_p_second)
            {
                bpsCount -= bytes_p_second;
                second = total / bytes_p_second;
                if (wmix->debug)
                    printf("  PLAY-AAC: %s %02d:%02d\n", aacPath, second / 60, second % 60);
            }
            //
            if (head.U8 == 0)
                break;
        }
        else if (ret == 0)
            ;
        else if (repeat)
        {
            //关闭 reduceMode
            if (rdceIsMe && wmix->reduceMode == rdce)
                wmix->reduceMode = 1;
            //
            lseek(fd, 0, SEEK_SET);
            //
            for (ret = 0; ret < repeat; ret++)
            {
                delayus(100000);
                //
                if (!wmix->run || loopWord != wmix->loopWord)
                    break;
                //
                if (msg_fd)
                {
                    if (msgrcv(msg_fd, &msg,
                               WMIX_MSG_BUFF_SIZE,
                               0, IPC_NOWAIT) < 1 &&
                        errno != ENOMSG) //消息队列被关闭
                    {
                        if (wmix->debug)
                            printf("PLAY-AAC exit: %d msgrecv err/%d\n", msg_fd, errno);
                        break;
                    }
                }
            }
            //
            if (ret != repeat)
            {
                ret = -1;
                break;
            }
            //重启 reduceMode
            if (rdceIsMe && wmix->reduceMode == 1)
                wmix->reduceMode = rdce;
            //
            if (wmix->debug)
                printf("<< PLAY-AAC: %s start >>\n   通道数: %d\n   采样位数: %d bit\n   采样率: %d Hz\n   每秒字节: %d Bytes\n   重播间隔: %d sec\n",
                       aacPath,
                       chn,
                       sample,
                       freq,
                       bytes_p_second,
                       repeat / 10);
            //
            total = total2 = bpsCount = 0;
            src.U8 = out;
            head.U8 = wmix->head.U8;
            tick = 0;
        }
        else
            break;
        //
        ret = aac_decode2(&aacDec, fd, out, (int *)&chn, (int *)&freq);
    }
    //
    close(fd);
    if (aacDec)
        aac_decodeRelease(&aacDec);
    //
    if (wmix->debug)
        printf(">> PLAY-AAC: %s end <<\n", aacPath);
    //关闭 reduceMode
    if (rdceIsMe && wmix->reduceMode == rdce)
        wmix->reduceMode = 1;
}
#endif

#if (WMIX_MP3)
typedef struct
{
    // char *msgPath;//消息队列挂靠路径
    char *mp3Path;
    //
    WMix_Msg msg;
    // key_t msg_key;
    int msg_fd;
    //
    void *fdm;     //mmap首地址
    uint32_t seek; //fdm跳过多少才到mp3数据段
    uint32_t size; //实际mp3数据段长度
    //
    WMix_Point head, src; //同步循环缓冲区的head指针 和 指向数据的src指针
    WMix_Struct *wmix;
    //
    uint32_t tick;                     //循环缓冲区head指针走过了多少字节,用于防止数据写如缓冲区太快超过head指针
    uint8_t loopWord;                  //每个播放线程的循环标志都要与该值一致,否则循环结束,用于打断全局播放
    uint32_t total, total2, totalWait; //total:文件读取字节数 total2:等比例转换为输出格式后的字节数
    double totalPow;                   // totalPow = total2/total; total2 = total*totalPow
    //
    uint32_t bps, bpsCount; //bps:每秒字节数 bpsCount计数用于计算当前播放时间
    //
    uint8_t rdce;    //reduce
    uint16_t repeat; //repeatInterval*10
    uint8_t rdceIsMe;
} WMix_Mp3;

static int16_t mad_scale(mad_fixed_t sample)
{
    sample += (1L << (MAD_F_FRACBITS - 16));
    if (sample >= MAD_F_ONE)
        sample = MAD_F_ONE - 1;
    else if (sample < -MAD_F_ONE)
        sample = -MAD_F_ONE;
    return sample >> (MAD_F_FRACBITS + 1 - 16);
}

enum mad_flow mad_output(void *data, struct mad_header const *header, struct mad_pcm *pcm)
{
    WMix_Mp3 *wmm = data;
    uint32_t count = 0, second;
    int16_t *val = (int16_t *)&pcm->samples[0][count];
    int timeout;
    //
    if (!wmm->wmix->run || wmm->loopWord != wmm->wmix->loopWord)
        return MAD_FLOW_STOP;
    //参数初始化
    if (wmm->head.U8 == 0)
    {
        wmm->bps = pcm->channels * 16 / 8 * header->samplerate;
        wmm->totalPow = (double)(WMIX_CHANNELS * WMIX_SAMPLE / 8 * WMIX_FREQ) / wmm->bps;
        wmm->totalWait = wmm->bps * wmm->totalPow / 2;
        //
        if (wmm->wmix->debug)
            printf(
                "<< PLAY-MP3: %s start >>\n"
                "   通道数: %d\n"
                "   采样位数: %d bit\n"
                "   采样率: %d Hz\n"
                "   每秒字节: %d Bytes\n"
                "   重播间隔: %d sec\n",
                wmm->mp3Path,
                pcm->channels, 16,
                header->samplerate,
                wmm->bps,
                wmm->repeat / 10);
        //
        wmm->total = wmm->total2 = wmm->bpsCount = 0;
        wmm->head.U8 = 0;
        wmm->tick = 0;
    }
    //msg 检查
    if (wmm->msg_fd)
    {
        if (msgrcv(wmm->msg_fd,
                   &wmm->msg,
                   WMIX_MSG_BUFF_SIZE,
                   0, IPC_NOWAIT) < 1 &&
            errno != ENOMSG) //消息队列被关闭
        {
            if (wmm->wmix->debug)
                printf("PLAY-MP3 exit: %d msgrecv err/%d\n", wmm->msg_fd, errno);
            // break;
            return MAD_FLOW_STOP;
        }
    }
    //
    if (pcm->channels == 2)
    {
        for (count = 0; count < pcm->length; count++)
        {
            *val++ = mad_scale(pcm->samples[0][count]);
            *val++ = mad_scale(pcm->samples[1][count]);
        }
        count = pcm->length * 4;
    }
    else if (pcm->channels == 1)
    {
        for (count = 0; count < pcm->length; count++)
        {
            *val++ = mad_scale(pcm->samples[0][count]);
            // *val++ = mad_scale(pcm->samples[1][count]);
        }
        count = pcm->length * 2;
    }
    else
        return MAD_FLOW_STOP;
    //等待消化
    timeout = 0;
    while (wmm->wmix->run && timeout++ < 200 &&
           wmm->loopWord == wmm->wmix->loopWord &&
           wmm->tick > wmm->wmix->tick &&
           wmm->tick - wmm->wmix->tick > wmm->totalWait)
        delayus(5000);
    if (!wmm->wmix->run || wmm->loopWord != wmm->wmix->loopWord)
        return MAD_FLOW_STOP;
    //写入到循环缓冲区
    wmm->src.U8 = (uint8_t *)&pcm->samples[0][0];
    wmm->head = wmix_load_wavStream(
        wmm->wmix,
        wmm->src,
        count,
        header->samplerate,
        pcm->channels,
        16,
        wmm->head, wmm->rdce, &wmm->tick);
    //总字节数计数
    wmm->bpsCount += count;
    wmm->total += count;
    wmm->total2 = wmm->total * wmm->totalPow;
    //播放时间
    if (wmm->bpsCount > wmm->bps)
    {
        wmm->bpsCount -= wmm->bps;
        second = wmm->total / wmm->bps;
        if (wmm->wmix->debug)
            printf("  PLAY-MP3: %s %02d:%02d\n", wmm->mp3Path, second / 60, second % 60);
    }
    //
    if (wmm->head.U8 == 0)
        return MAD_FLOW_STOP;
    //
    return MAD_FLOW_CONTINUE;
}

enum mad_flow mad_input(void *data, struct mad_stream *stream)
{
    WMix_Mp3 *wmm = data;
    uint8_t count;
    if (wmm->size > 0)
    {
        mad_stream_buffer(stream, wmm->fdm + wmm->seek, wmm->size);
        //
        if (wmm->repeat)
        {
            if (wmm->head.U8) //已经播放完一遍了
            {
                //关闭 reduceMode
                if (wmm->rdceIsMe && wmm->wmix->reduceMode == wmm->rdce)
                    wmm->wmix->reduceMode = 1;
                //
                for (count = 0; count < wmm->repeat; count++)
                {
                    delayus(100000);
                    //
                    if (!wmm->wmix->run || wmm->loopWord != wmm->wmix->loopWord)
                        return MAD_FLOW_STOP;
                    //msg 检查
                    if (wmm->msg_fd)
                    {
                        if (msgrcv(wmm->msg_fd,
                                   &wmm->msg,
                                   WMIX_MSG_BUFF_SIZE,
                                   0, IPC_NOWAIT) < 1 &&
                            errno != ENOMSG) //消息队列被关闭
                        {
                            if (wmm->wmix->debug)
                                printf("PLAY-MP3 exit: %d msgrecv err/%d\n", wmm->msg_fd, errno);
                            return MAD_FLOW_STOP;
                        }
                    }
                }
                //重启 reduceMode
                if (wmm->rdceIsMe && wmm->wmix->reduceMode == 1)
                    wmm->wmix->reduceMode = wmm->rdce;
            }
        }
        else
            wmm->size = 0;
        //
        wmm->head.U8 = 0;
        return MAD_FLOW_CONTINUE;
    }
    return MAD_FLOW_STOP;
}

enum mad_flow mad_error(void *data, struct mad_stream *stream, struct mad_frame *frame)
{
    fprintf(stderr, "decoding error 0x%04x (%s)\n",
            stream->error,
            mad_stream_errorstr(stream));
    return MAD_FLOW_CONTINUE;
}

void wmix_load_mp3(
    WMix_Struct *wmix,
    char *mp3Path,
    int msg_fd,
    uint8_t reduce,
    uint8_t repeatInterval)
{
    struct stat sta;
    int fd;
    //
    WMix_Mp3 wmm;
    struct mad_decoder decoder;

    //参数准备
    memset(&wmm, 0, sizeof(WMix_Mp3));
    wmm.wmix = wmix;
    wmm.mp3Path = mp3Path;
    wmm.repeat = (uint16_t)repeatInterval * 10;
    wmm.loopWord = wmix->loopWord;
    wmm.rdceIsMe = 0;
    wmm.rdce = reduce + 1;
    wmm.msg_fd = msg_fd;
    //
    if ((fd = open(mp3Path, O_RDONLY)) <= 0)
    {
        fprintf(stderr, "wmix_load_mp3: open %s err\n", mp3Path);
        return;
    }
    if (fstat(fd, &sta) == -1 || sta.st_size == 0)
    {
        fprintf(stderr, "wmix_load_mp3: stat %s err\n", mp3Path);
        close(fd);
        return;
    }

    //跳过id3标签
    wmm.seek = id3_len(mp3Path);
    wmm.size = sta.st_size - wmm.seek;

    //
    wmm.fdm = mmap(0, sta.st_size, PROT_READ, MAP_SHARED, fd, 0);
    if (wmm.fdm == MAP_FAILED || !wmm.fdm)
    {
        fprintf(stderr, "wmix_load_mp3: mmap err\n");
        close(fd);
        return;
    }

    //独占 reduceMode
    if (wmm.rdce > 1 && wmix->reduceMode == 1)
    {
        wmix->reduceMode = wmm.rdce;
        wmm.rdceIsMe = 1;
    }
    else
        wmm.rdce = 1;

    //configure input, output, and error functions
    mad_decoder_init(&decoder, &wmm,
                     mad_input, 0 /* header */, 0 /* filter */, mad_output,
                     mad_error, 0 /* message */);

    //start decoding
    mad_decoder_run(&decoder, MAD_DECODER_MODE_SYNC);

    //release the decoder
    mad_decoder_finish(&decoder);

    //关闭 reduceMode
    if (wmm.rdceIsMe && wmix->reduceMode == wmm.rdce)
        wmix->reduceMode = 1;

    //
    close(fd);
    munmap(wmm.fdm, sta.st_size);
    //
    if (wmix->debug)
        printf(">> PLAY-MP3: %s end <<\n", mp3Path);
}
#endif

//--------------- wmix main ---------------

void help(char *argv0)
{
    printf(
        "\n"
        "Usage: %s [option]\n"
        "\n"
        "Opition:\n"
        "  -d : 显示debug信息\n"
        "  -v volume : 设置音量0~10\n"
        "  -? --help : 显示帮助\n"
        "\n"
        "软件版本: %s\n"
        "\n"
        "Example:\n"
        "  %s &\n"
        "\n",
        argv0, WMIX_VERSION, argv0);
}

#if (WMIX_MERGE_MODE == 2)
void _wmix_loop(WMixThread_Param *wmtp)
{
    sleep(1);
    while (1)
    {
        //--- 重启 ---
        if (wmtp->wmix->run == 0 &&
            wmtp->wmix->thread_sys == 0 &&
            wmtp->wmix->thread_record == 0 &&
            wmtp->wmix->thread_play == 0)
        {
            delayus(500000);
            wmtp->wmix->run = true;
#ifndef WMIX_RECORD_PLAY_SYNC
            wmix_throwOut_thread(wmtp->wmix, 0, NULL, 0, &wmix_shmem_write_circle);
#endif
            // wmix_throwOut_thread(wmtp->wmix, 0, NULL, 0, &wmix_shmem_read_circle);
            wmix_throwOut_thread(wmtp->wmix, 0, NULL, 0, &wmix_msg_thread);
            wmix_throwOut_thread(wmtp->wmix, 0, NULL, 0, &wmix_play_thread);
        }
        delayus(500000);
    }
}
void wmix_start()
{
    main_wmix = wmix_init();
    wmix_throwOut_thread(main_wmix, 0, NULL, 0, &_wmix_loop);
}
#else
void wmix_getSignal(int id)
{
    printf("wmix signal: %d\n", id);
    if (id == SIGINT)
    {
        wmix_exit(main_wmix);
        exit(0);
    }
}
int main(int argc, char **argv)
{
    int i, volume = 10;
    char *p, *path = NULL;

    //传入参数处理
    if (argc > 1)
    {
        for (i = 1; i < argc; i++)
        {
            if (strstr(argv[1], "-?") || strstr(argv[1], "help"))
            {
                help(argv[0]);
                return 0;
            }
        }
    }

    main_wmix = wmix_init();

    signal(SIGPIPE, wmix_getSignal);
    signal(SIGINT, wmix_getSignal);

    if (main_wmix)
    {
        //传入参数处理
        if (argc > 1)
        {
            for (i = 1; i < argc; i++)
            {
                p = argv[i];
                //
                if (strlen(argv[i]) == 2 && strstr(argv[i], "-d"))
                {
                    main_wmix->debug = true;
                }
                else if (strlen(argv[i]) == 2 && strstr(argv[i], "-v") && i + 1 < argc)
                {
                    sscanf(argv[++i], "%d", &volume);
                    if (volume < 0)
                        volume = 0;
                    else if (volume > 10)
                        volume = 10;
                    sys_volume_set(volume, 10);
                }
                else if (strstr(p, ".wav") || strstr(p, ".mp3") || strstr(p, ".aac"))
                    path = p;
            }
            //
            if (path)
            {
                wmix_throwOut_thread(main_wmix,
                                     3,
                                     (uint8_t *)path,
                                     WMIX_MSG_BUFF_SIZE,
                                     &wmix_load_audio_thread);
            }
        }

        sleep(1);
        while (1)
        {
            //--- 重启 ---
            if (main_wmix->run == 0 &&
                main_wmix->thread_sys == 0 &&
                main_wmix->thread_record == 0 &&
                main_wmix->thread_play == 0)
            {
                delayus(500000);
                main_wmix->run = true;
#ifndef WMIX_RECORD_PLAY_SYNC
                wmix_throwOut_thread(main_wmix, 0, NULL, 0, &wmix_shmem_write_circle);
#endif
                // wmix_throwOut_thread(main_wmix, 0, NULL, 0, &wmix_shmem_read_circle);
                wmix_throwOut_thread(main_wmix, 0, NULL, 0, &wmix_msg_thread);
                wmix_throwOut_thread(main_wmix, 0, NULL, 0, &wmix_play_thread);
            }
            delayus(500000);
        }
    }
    return 0;
}
#endif
