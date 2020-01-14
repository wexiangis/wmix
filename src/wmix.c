
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/stat.h>
#include <sys/mman.h>

#include "wmix.h"
#include "wav.h"
#if(WMIX_MP3)
#include "mad.h"
#endif
#include "id3.h"
#include "rtp.h"
#include "aac.h"
#include "g711codec.h"

static WMix_Struct *main_wmix = NULL;

static void signal_callback(int signo)
{
    if (SIGINT == signo || SIGTERM == signo)
    {
        wmix_exit(main_wmix);
#if(WMIX_MODE==1)
        hiaudio_exit();
#endif
    }
    exit(0);
}

#include <sys/time.h>
void delayus(unsigned int us)
{
    struct timeval delay;
    delay.tv_sec = us/1000000;
    delay.tv_usec = us%1000000;
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
#if(WMIX_MODE==1)
    if(count > div)
        count = div;
    if(count == 0)
        return hiaudio_set_volume(-120);
    else
        return hiaudio_set_volume(5-(div-count)*5);
#else
    snd_mixer_t *mixer;
    snd_mixer_elem_t *pcm_element;
    long volume_value = (long)(count>div?div:count);
    long volume_div = (long)div;
    //
    snd_mixer_open(&mixer, 0);
    snd_mixer_attach(mixer, "default");
    snd_mixer_selem_register(mixer, NULL, NULL);
    snd_mixer_load(mixer);
    //找到Pcm对应的element
    pcm_element = snd_mixer_first_elem(mixer);/* 取得第一个 element，也就是 Master */
    snd_mixer_selem_set_playback_volume_range(pcm_element, 0, volume_div);//设置音量范围：0-100之间
    //左音量
    if(snd_mixer_selem_set_playback_volume(pcm_element,SND_MIXER_SCHN_FRONT_LEFT,volume_value) < 0)
        return -1;
    //右音量
    if(snd_mixer_selem_set_playback_volume(pcm_element,SND_MIXER_SCHN_FRONT_RIGHT,volume_value) < 0)
       return -1;
    //
    snd_mixer_selem_get_playback_volume(pcm_element,SND_MIXER_SCHN_FRONT_LEFT,&volume_value);//获取音量
    printf("volume: %ld / %ld\r\n", volume_value, volume_div);
    //处理事件
    snd_mixer_handle_events(mixer);
    snd_mixer_close(mixer);
    //
    return volume_value;
#endif
}

#if(WMIX_MODE==0)

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

    switch (LE_SHORT(wav->format.sample_length)) {
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
 *       rcount ： 读取的大小
 * 返回: 0：正常 -1:错误
 * 说明: 无
 ******************************************************************************/
int SNDWAV_ReadPcm(SNDPCMContainer_t *sndpcm, size_t rcount)
{
    int r;
    size_t result = 0;
    size_t count = rcount;
    uint8_t *data = sndpcm->data_buf;

    if (count != sndpcm->chunk_size) {
        count = sndpcm->chunk_size;
    }

    while (count > 0) {
        r = snd_pcm_readi(sndpcm->handle, data, count);

        if (r == -EAGAIN || (r >= 0 && (size_t)r < count)) {
            snd_pcm_wait(sndpcm->handle, 1000);
        } else if (r == -EPIPE) {
            snd_pcm_prepare(sndpcm->handle);
            // fprintf(stderr, "Error: Buffer Underrun\n");
        } else if (r == -ESTRPIPE) {
            fprintf(stderr, "Error: Need suspend\n");
        } else if (r < 0) {
            fprintf(stderr, "Error: snd_pcm_writei: [%s]\n", snd_strerror(r));
            return -1;
        }

        if (r > 0) {
            result += r;
            count -= r;
            data += r * sndpcm->bits_per_frame / 8;
        }
    }
    return rcount;
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
    int r;
    int result = 0;
    uint8_t *data = sndpcm->data_buf;

    if (wcount < sndpcm->chunk_size) {
        snd_pcm_format_set_silence(sndpcm->format,
            data + wcount * sndpcm->bits_per_frame / 8,
            (sndpcm->chunk_size - wcount) * sndpcm->channels);
        wcount = sndpcm->chunk_size;
    }
    while (wcount > 0) {
        r = snd_pcm_writei(sndpcm->handle, data, wcount);
        if (r == -EAGAIN || (r >= 0 && (size_t)r < wcount)) {
            snd_pcm_wait(sndpcm->handle, 1000);
        } else if (r == -EPIPE) {
            snd_pcm_prepare(sndpcm->handle);
            // fprintf(stderr, "Error: Buffer Underrun\n");
        } else if (r == -ESTRPIPE) {
            fprintf(stderr, "Error: Need suspend\n");
        } else if (r < 0) {
            fprintf(stderr, "Error snd_pcm_writei: [%s]", snd_strerror(r));
            return -1;
        }
        if (r > 0) {
            result += r;
            wcount -= r;
            data += r * sndpcm->bits_per_frame / 8;
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
    if (snd_pcm_hw_params_any(sndpcm->handle, hwparams) < 0) {
        fprintf(stderr, "Error snd_pcm_hw_params_any\n");
        return -1;
    }
    //初始化访问权限
    if (snd_pcm_hw_params_set_access(sndpcm->handle, hwparams, SND_PCM_ACCESS_RW_INTERLEAVED) < 0) {
        fprintf(stderr, "Error snd_pcm_hw_params_set_access\n");
        return -1;
    }

    /* 初始化采样格式,16位 Set sample format */
    if (SNDWAV_P_GetFormat(wav, &format) < 0) {
        fprintf(stderr, "Error get_snd_pcm_format\n");
        return -1;
    }
    if (snd_pcm_hw_params_set_format(sndpcm->handle, hwparams, format) < 0) {
        fprintf(stderr, "Error snd_pcm_hw_params_set_format\n");
        return -1;
    }
    sndpcm->format = format;

    /* 设置通道数量 Set number of channels */
    if (snd_pcm_hw_params_set_channels(sndpcm->handle, hwparams, LE_SHORT(wav->format.channels)) < 0) {
        fprintf(stderr, "Error snd_pcm_hw_params_set_channels\n");
        return -1;
    }
    sndpcm->channels = LE_SHORT(wav->format.channels);
    //设置采样率，如果硬件不支持我们设置的采样率，将使用最接近的
    /* Set sample rate. If the exact rate is not supported */
    /* by the hardware, use nearest possible rate.         */
    exact_rate = LE_INT(wav->format.sample_rate);
    if (snd_pcm_hw_params_set_rate_near(sndpcm->handle, hwparams, &exact_rate, 0) < 0) {
        fprintf(stderr, "Error snd_pcm_hw_params_set_rate_near\n");
        return -1;
    }
    if (LE_INT(wav->format.sample_rate) != exact_rate) {
        fprintf(stderr, "The rate %d Hz is not supported by your hardware.\n ==> Using %d Hz instead.\n",
            LE_INT(wav->format.sample_rate), exact_rate);
    }

    if (snd_pcm_hw_params_get_buffer_time_max(hwparams, &buffer_time, 0) < 0) {
        fprintf(stderr, "Error snd_pcm_hw_params_get_buffer_time_max\n");
        return -1;
    }
    if (buffer_time > 500000) buffer_time = 500000;
    period_time = buffer_time / 4;

    if (snd_pcm_hw_params_set_buffer_time_near(sndpcm->handle, hwparams, &buffer_time, 0) < 0) {
        fprintf(stderr, "Error snd_pcm_hw_params_set_buffer_time_near\n");
        return -1;
    }

    if (snd_pcm_hw_params_set_period_time_near(sndpcm->handle, hwparams, &period_time, 0) < 0) {
        fprintf(stderr, "Error snd_pcm_hw_params_set_period_time_near\n");
        return -1;
    }

    /* Set hw params */
    if (snd_pcm_hw_params(sndpcm->handle, hwparams) < 0) {
        fprintf(stderr, "Error snd_pcm_hw_params(handle, params)\n");
        return -1;
    }

    snd_pcm_hw_params_get_period_size(hwparams, &sndpcm->chunk_size, 0);
    snd_pcm_hw_params_get_buffer_size(hwparams, &sndpcm->buffer_size);
    if (sndpcm->chunk_size == sndpcm->buffer_size) {
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
#if(WMIX_CHANNELS == 1)
    sndpcm->data_buf = (uint8_t *)malloc(sndpcm->chunk_bytes*2+1);
#else
    sndpcm->data_buf = (uint8_t *)malloc(sndpcm->chunk_bytes+1);
#endif
    if (!sndpcm->data_buf) {
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

    if (WAV_WriteHeader(fd, wav) < 0) {
        return ;
    }

    rest = wav->chunk.length;
    while (rest > 0) {
        c = (rest <= (int64_t)sndpcm->chunk_bytes) ? (size_t)rest : sndpcm->chunk_bytes;
        frame_size = c * 8 / sndpcm->bits_per_frame;
        if (SNDWAV_ReadPcm(sndpcm, frame_size) != frame_size)
            break;

        if (write(fd, sndpcm->data_buf, c) != c) {
            fprintf(stderr, "Error SNDWAV_Record[write]\n");
            return ;
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
int record_wav(char *filename,uint32_t duration_time, uint8_t chn, uint8_t sample, uint16_t freq)
{
    // char *filename;
    char *devicename = "default";
    int fd;
    WAVContainer_t wav;
    SNDPCMContainer_t record;

    memset(&record, 0x0, sizeof(record));

    // filename = argv[1];
    remove(filename);
    if ((fd = open(filename, O_WRONLY | O_CREAT, 0644)) == -1) {
        fprintf(stderr, "Error open: [%s]\n", filename);
        return -1;
    }

    if (snd_output_stdio_attach(&record.log, stderr, 0) < 0) {
        fprintf(stderr, "Error snd_output_stdio_attach\n");
        goto Err;
    }

    if (snd_pcm_open(&record.handle, devicename, SND_PCM_STREAM_CAPTURE, 0) < 0) {
        fprintf(stderr, "Error snd_pcm_open [ %s]\n", devicename);
        goto Err;
    }

    WAV_Params(&wav, duration_time, chn, sample, freq);

    if (SNDWAV_SetParams(&record, &wav) < 0) {
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
    if (record.data_buf) free(record.data_buf);
    if (record.log) snd_output_close(record.log);
    if (record.handle) snd_pcm_close(record.handle);
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
                return ;
            }
            if (ret == 0)
                break;
            load += ret;
        } while ((size_t)load < sndpcm->chunk_bytes);
            
        // printf("load = %ld, written = %ld\n", load, written+load);

        if(bit_count)
        {
            for(i = 0, valueP = (int16_t*)sndpcm->data_buf; i < load/2; i++)
            {
                if(valueP[i]&0x8000)
                    valueP[i] = (0xFFFF<<(16-bit_count)) | (valueP[i]>>bit_count);
                else
                    valueP[i] = valueP[i]>>bit_count;
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
    WAVContainer_t wav;//wav文件头信息
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
    if (snd_pcm_hw_params_any(sndpcm->handle, hwparams) < 0) {
        fprintf(stderr, "Error snd_pcm_hw_params_any\n");
        return -1;
    }
    //初始化访问权限
    if (snd_pcm_hw_params_set_access(sndpcm->handle, hwparams, SND_PCM_ACCESS_RW_INTERLEAVED) < 0) {
        fprintf(stderr, "Error snd_pcm_hw_params_set_access\n");
        return -1;
    }

    /* 初始化采样格式,16位 Set sample format */
    if(sample == 8)
        format = SND_PCM_FORMAT_S8;
    else if(sample == 16)
        format = SND_PCM_FORMAT_S16_LE;
    else if(sample == 24)
        format = SND_PCM_FORMAT_S24_LE;
    else if(sample == 32)
        format = SND_PCM_FORMAT_S32_LE;
    else
        return -1;
    
    if (snd_pcm_hw_params_set_format(sndpcm->handle, hwparams, format) < 0) {
        fprintf(stderr, "Error snd_pcm_hw_params_set_format\n");
        return -1;
    }
    sndpcm->format = format;

    /* 设置通道数量 Set number of channels */
    if (snd_pcm_hw_params_set_channels(sndpcm->handle, hwparams, channels) < 0) {
        fprintf(stderr, "Error snd_pcm_hw_params_set_channels\n");
        return -1;
    }
    sndpcm->channels = channels;
    //设置采样率，如果硬件不支持我们设置的采样率，将使用最接近的
    /* Set sample rate. If the exact rate is not supported */
    /* by the hardware, use nearest possible rate.         */
    exact_rate = freq;
    if (snd_pcm_hw_params_set_rate_near(sndpcm->handle, hwparams, &exact_rate, 0) < 0) {
        fprintf(stderr, "Error snd_pcm_hw_params_set_rate_near\n");
        return -1;
    }
    if (freq != exact_rate) {
        fprintf(stderr, "The rate %d Hz is not supported by your hardware.\n ==> Using %d Hz instead.\n",
            freq, exact_rate);
    }

    if (snd_pcm_hw_params_get_buffer_time_max(hwparams, &buffer_time, 0) < 0) {
        fprintf(stderr, "Error snd_pcm_hw_params_get_buffer_time_max\n");
        return -1;
    }
    if (buffer_time > 500000) buffer_time = 500000;
    period_time = buffer_time / 4;

    if (snd_pcm_hw_params_set_buffer_time_near(sndpcm->handle, hwparams, &buffer_time, 0) < 0) {
        fprintf(stderr, "Error snd_pcm_hw_params_set_buffer_time_near\n");
        return -1;
    }

    if (snd_pcm_hw_params_set_period_time_near(sndpcm->handle, hwparams, &period_time, 0) < 0) {
        fprintf(stderr, "Error snd_pcm_hw_params_set_period_time_near\n");
        return -1;
    }

    /* Set hw params */
    if (snd_pcm_hw_params(sndpcm->handle, hwparams) < 0) {
        fprintf(stderr, "Error snd_pcm_hw_params(handle, params)\n");
        return -1;
    }

    snd_pcm_hw_params_get_period_size(hwparams, &sndpcm->chunk_size, 0);
    snd_pcm_hw_params_get_buffer_size(hwparams, &sndpcm->buffer_size);
    if (sndpcm->chunk_size == sndpcm->buffer_size) {
        fprintf(stderr, "Can't use period equal to buffer size (%lu == %lu)\n", sndpcm->chunk_size, sndpcm->buffer_size);
        return -1;
    }

    sndpcm->bits_per_sample = snd_pcm_format_physical_width(format);
    sndpcm->bits_per_frame = sndpcm->bits_per_sample * channels;
    sndpcm->chunk_bytes = sndpcm->chunk_size * sndpcm->bits_per_frame / 8;

    /* Allocate audio data buffer */
#if(WMIX_CHANNELS == 1)
    sndpcm->data_buf = (uint8_t *)malloc(sndpcm->chunk_bytes*2+1);
#else
    sndpcm->data_buf = (uint8_t *)malloc(sndpcm->chunk_bytes+1);
#endif
    if (!sndpcm->data_buf){
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
        p_or_c=='c'?SND_PCM_STREAM_CAPTURE:SND_PCM_STREAM_PLAYBACK, 0) < 0)
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
    if(playback)
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

static uint32_t get_tick_err(uint32_t current, uint32_t last)
{
    if(current > last)
        return current - last;
    else
        return last - current;
}

typedef struct{
    WMix_Struct *wmix;
    long flag;
    uint8_t *param;
}WMixThread_Param;

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
    wmtp = (WMixThread_Param*)calloc(1, sizeof(WMixThread_Param));
    wmtp->wmix = wmix;
    wmtp->flag = flag;
    if(paramLen > 0 && param)
    {
        wmtp->param = (uint8_t*)calloc(paramLen, sizeof(uint8_t));
        memcpy(wmtp->param, param, paramLen);
    }else
        wmtp->param = NULL;
    //attr init
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);   //禁用线程同步, 线程运行结束后自动释放
    //抛出线程
    pthread_create(&th, &attr, callback, (void*)wmtp);
    //attr destroy
    pthread_attr_destroy(&attr);
}

void wmix_load_wav_fifo_thread(WMixThread_Param *wmtp)
{
    char *path = (char*)&wmtp->param[4];
    //
    uint8_t chn = wmtp->param[0];
    uint8_t sample = wmtp->param[1];
    uint16_t freq = (wmtp->param[2]<<8) | wmtp->param[3];
    //
    int fd_read;
    uint8_t *buff;
    uint32_t buffSize;
    //
    WMix_Point src;
    ssize_t ret, total = 0, total2 = 0, totalWait;
    double buffSizePow;
    uint32_t tick, second = 0, bytes_p_second, bytes_p_second2, bpsCount = 0;
    uint8_t rdce = ((wmtp->flag>>8)&0xFF)+1, rdceIsMe = 0;
    //
    uint8_t loopWord;
    loopWord = wmtp->wmix->loopWord;
    //
    if (mkfifo(path, 0666) < 0 && errno != EEXIST)
    {
        fprintf(stderr, "wmix_load_wav_fifo_thread: mkfifo err\n");
        return;
    }
    //
    fd_read = open(path, O_RDONLY);
    //独占 reduceMode
    if(rdce > 1 && wmtp->wmix->reduceMode == 1)
    {
        wmtp->wmix->reduceMode = rdce;
        rdceIsMe = 1;
    }
    else
        rdce = 1;
    //
    bytes_p_second = chn*sample/8*freq;
    buffSize = bytes_p_second;
#if(WMIX_MODE==1)
    totalWait = bytes_p_second/2;
#else
    totalWait = wmtp->wmix->playback->chunk_bytes;
#endif
    buff = (uint8_t*)calloc(buffSize, sizeof(uint8_t));
    //
    bytes_p_second2 = WMIX_CHANNELS*WMIX_SAMPLE/8*WMIX_FREQ;
    buffSizePow = (double)bytes_p_second2/bytes_p_second;
    //
    if(wmtp->wmix->debug) printf("<< FIFO-W: %s start >>\n   通道数: %d\n   采样位数: %d bit\n   采样率: %d Hz\n   每秒字节: %d Bytes\n\n", 
        path, chn, sample, freq, bytes_p_second);
    //
    src.U8 = buff;
    tick = wmtp->wmix->tick;
    // wmtp->wmix->vipWrite.U8 = 0;
    //线程计数
    wmtp->wmix->thread_play += 1;
    //
    while(wmtp->wmix->run && 
        loopWord == wmtp->wmix->loopWord)
    {
        ret = read(fd_read, buff, buffSize);
        if(ret > 0)
        {
            //等播放指针赶上写入进度
            if(total2 > totalWait)
            {
                while(wmtp->wmix->run && 
                    loopWord == wmtp->wmix->loopWord && 
                    get_tick_err(wmtp->wmix->tick, tick) < 
                    total2 - totalWait)
                    delayus(10000);
            }
            //
            wmtp->wmix->vipWrite = wmix_load_wavStream(
                wmtp->wmix, 
                src, ret, freq, chn, sample, wmtp->wmix->vipWrite, rdce);
            if(wmtp->wmix->vipWrite.U8 == 0)
                break;
            //
            bpsCount += ret;
            total += ret;
            total2 = total*buffSizePow;
            //播放时间
            if(bpsCount > bytes_p_second)
            {
                bpsCount -= bytes_p_second;
                second = total/bytes_p_second;
                if(wmtp->wmix->debug) printf("  FIFO-W: %s %02d:%02d\n", path, second/60, second%60);
            }
            continue;
        }
        else if(errno != EAGAIN)
            break;
        //
        delayus(1000);
    }
    //用完关闭
    wmtp->wmix->vipWrite.U8 = 0;
    //
    if(wmtp->wmix->debug) printf(">> FIFO-W: %s end <<\n", path);
    //
    close(fd_read);
    //删除文件
    remove(path);
    //
    free(buff);
    //线程计数
    wmtp->wmix->thread_play -= 1;
    //
    if(wmtp->param)
        free(wmtp->param);
    //关闭 reduceMode
    if(rdceIsMe)
        wmtp->wmix->reduceMode = 1;
    free(wmtp);
}

#if(WMIX_MODE==1)
#if(WMIX_CHANNELS == 1)
#define RECORD_DATA_TRANSFER()  \
if(chn == 1)\
{\
    for(count = 0, src.U8 = dist.U8 = buff; count < ret; count+=frame_size)\
    {\
        if(divCount >= 1.0){src.U16++;divCount -= 1.0;}\
        else{*dist.U16++ = *src.U16++;divCount += divPow;}\
    }\
    src.U8 = buff;\
    buffSize2 = (size_t)(dist.U16 - src.U16)*2;\
}\
else\
{\
    memcpy(&buff[ret], buff, ret);\
    for(count = 0, src.U8 = &buff[ret], dist.U8 = buff; count < ret; count+=frame_size)\
    {\
        if(divCount >= 1.0){src.U16++;divCount -= 1.0;}\
        else{*dist.U16++ = *src.U16;*dist.U16++ = *src.U16++;divCount += divPow;}\
    }\
    src.U8 = buff;\
    buffSize2 = (size_t)(dist.U16 - src.U16)*2;\
}
#else
#define RECORD_DATA_TRANSFER()  \
if(chn == 1)\
{\
    for(count = 0, src.U8 = dist.U8 = buff; count < ret; count+=frame_size)\
    {\
        if(divCount >= 1.0){src.U16++;src.U16++;divCount -= 1.0;}\
        else{*dist.U16++ = *src.U16++;src.U16++;divCount += divPow;}\
    }\
    src.U8 = buff;\
    buffSize2 = (size_t)(dist.U16 - src.U16)*2;\
}\
else\
{\
    for(count = 0, src.U8 = dist.U8 = buff; count < ret; count+=frame_size)\
    {\
        if(divCount >= 1.0){src.U32++;divCount -= 1.0;}\
        else{*dist.U32++ = *src.U32++;divCount += divPow;}\
    }\
    src.U8 = buff;\
    buffSize2 = (size_t)(dist.U32 - src.U32)*4;\
}
#endif
#else
#if(WMIX_CHANNELS == 1)
#define RECORD_DATA_TRANSFER()  \
if(chn == 1)\
{\
    for(count = 0, src.U8 = dist.U8 = record->data_buf; count < frame_size; count++)\
    {\
        if(divCount >= 1.0){src.U16++;divCount -= 1.0;}\
        else{*dist.U16++ = *src.U16++;divCount += divPow;}\
    }\
    src.U8 = record->data_buf;\
    buffSize2 = (size_t)(dist.U16 - src.U16)*2;\
}\
else\
{\
    memcpy(&record->data_buf[frame_size], record->data_buf, frame_size);\
    for(count = 0, src.U8 = &record->data_buf[frame_size], dist.U8 = record->data_buf; count < frame_size; count++)\
    {\
        if(divCount >= 1.0){src.U16++;divCount -= 1.0;}\
        else{*dist.U16++ = *src.U16;*dist.U16++ = *src.U16++;divCount += divPow;}\
    }\
    src.U8 = record->data_buf;\
    buffSize2 = (size_t)(dist.U16 - src.U16)*2;\
}
#else
#define RECORD_DATA_TRANSFER()  \
if(chn == 1)\
{\
    for(count = 0, src.U8 = dist.U8 = record->data_buf; count < frame_size; count++)\
    {\
        if(divCount >= 1.0){src.U16++;src.U16++;divCount -= 1.0;}\
        else{*dist.U16++ = *src.U16++;src.U16++;divCount += divPow;}\
    }\
    src.U8 = record->data_buf;\
    buffSize2 = (size_t)(dist.U16 - src.U16)*2;\
}\
else\
{\
    for(count = 0, src.U8 = dist.U8 = record->data_buf; count < frame_size; count++)\
    {\
        if(divCount >= 1.0){src.U32++;divCount -= 1.0;}\
        else{*dist.U32++ = *src.U32++;divCount += divPow;}\
    }\
    src.U8 = record->data_buf;\
    buffSize2 = (size_t)(dist.U32 - src.U32)*4;\
}
#endif
#endif

void signal_get_SIGPIPE(int id){}

void wmix_record_wav_fifo_thread(WMixThread_Param *wmtp)
{
    char *path = (char*)&wmtp->param[4];
    //
    uint8_t chn = wmtp->param[0];
    uint8_t sample = wmtp->param[1];
    uint16_t freq = (wmtp->param[2]<<8) | wmtp->param[3];
    //
    size_t buffSize, buffSize2, frame_size, count;
    WMix_Point src, dist;
    ssize_t ret, total = 0;
    uint32_t second = 0, bytes_p_second, bytes_p_second2, bpsCount = 0;
    float divCount, divPow;
    //
    int fd_write;
#if(WMIX_MODE==1)
    int16_t record_addr;
#if(WMIX_CHANNELS == 1)
    unsigned char buff[1024];
#else
    unsigned char buff[512];
#endif
#else
    SNDPCMContainer_t *record = NULL;
#endif
    //
    uint8_t loopWord;
    loopWord = wmtp->wmix->loopWordRecord;
    //
    if(freq > WMIX_FREQ){
        fprintf(stderr, "wmix_record_wav_fifo_thread: freq err, %dHz > %dHz(machine)\n", freq, WMIX_FREQ);
        return;
    }
    if(sample != WMIX_SAMPLE){
        fprintf(stderr, "wmix_record_wav_fifo_thread: sample err, must be %dbit(machine)\n", WMIX_SAMPLE);
        return;
    }
    if(chn != 1 && chn != 2){
        fprintf(stderr, "wmix_record_wav_fifo_thread: channels err, must be 1 or 2\n");
        return;
    }
    //
#if(WMIX_MODE==1)
    record_addr = hiaudio_ai_init(WMIX_CHANNELS, WMIX_SAMPLE, WMIX_FREQ);
    if(record_addr < 0){
#else
    record = wmix_alsa_init(WMIX_CHANNELS, WMIX_SAMPLE, WMIX_FREQ, 'c');
    if(!record){
#endif
        fprintf(stderr, "wmix_record_wav_fifo_thread: wmix_alsa_init err\n");
        return;
    }
    //
    if (mkfifo(path, 0666) < 0 && errno != EEXIST){
        fprintf(stderr, "wmix_record_wav_fifo_thread: mkfifo err\n");
        return;
    }
    //
    fd_write = open(path, O_WRONLY);
    //
    signal(SIGPIPE, signal_get_SIGPIPE);
    //
    bytes_p_second = WMIX_CHANNELS*WMIX_SAMPLE/8*WMIX_FREQ;
    bytes_p_second2 = chn*sample/8*freq;
#if(WMIX_MODE==1)
    frame_size = WMIX_CHANNELS*WMIX_SAMPLE/8;
#if(WMIX_CHANNELS == 1)
    buffSize = sizeof(buff)/2;
#else
    buffSize = sizeof(buff);
#endif
#else
    buffSize = record->chunk_bytes;
    frame_size = buffSize/(WMIX_CHANNELS*WMIX_SAMPLE/8);
#endif
    //
    divPow = (float)(WMIX_FREQ - freq)/freq;
    divCount = 0;
    //
    if(wmtp->wmix->debug) printf("<< FIFO-R: %s record >>\n   通道数: %d\n   采样位数: %d bit\n   采样率: %d Hz\n   每秒字节: %d Bytes\n   时间长度: -- sec\n\n", 
        path, chn, sample, freq, bytes_p_second2);
    //线程计数
    wmtp->wmix->thread_record += 1;
    //
    while(wmtp->wmix->run && 
        loopWord == wmtp->wmix->loopWordRecord)
    {
#if(WMIX_MODE==1)
        ret = hiaudio_ai_read(buff, buffSize, &record_addr, false);
#else
        ret = SNDWAV_ReadPcm(record, frame_size);
#endif
        if(ret > 0)
        {
            bpsCount += ret;
            total += ret;
            //录制时间
            if(bpsCount > bytes_p_second)
            {
                bpsCount -= bytes_p_second;
                second = total/bytes_p_second;
                if(wmtp->wmix->debug) printf("  FIFO-R: %s %02d:%02d\n", path, second/60, second%60);
            }
            //
            if(!wmtp->wmix->run)
                break;
            //不同频率和通道数的数据处理
            RECORD_DATA_TRANSFER();
            //
#if(WMIX_MODE==1)
            ret = write(fd_write, buff, buffSize2);
#else
            ret = write(fd_write, record->data_buf, buffSize2);
#endif
            if(ret < 0 && errno != EAGAIN)
                break;
        }
        else if(ret < 0)
        {
            fprintf(stderr, "wmix_record_wav_fifo_thread: wmix_record_wav_fifo_thread read err %d\n", ret);
            break;
        }
        else
        {
            delayus(500);
            fsync(fd_write);
        }
    }
    //
#if(WMIX_MODE==1)
    // hiaudio_ai_exit();
#else
    wmix_alsa_release(record);
#endif
    close(fd_write);
    //
    if(wmtp->wmix->debug) printf(">> FIFO-R: %s end <<\n", path);
    //删除文件
    remove(path);
    //线程计数
    wmtp->wmix->thread_record -= 1;
    //
    if(wmtp->param)
        free(wmtp->param);
    free(wmtp);
}

void wmix_record_wav_thread(WMixThread_Param *wmtp)
{
    char *path = (char*)&wmtp->param[6];
    //
    uint8_t chn = wmtp->param[0];
    uint8_t sample = wmtp->param[1];
    uint16_t freq = (wmtp->param[2]<<8) | wmtp->param[3];
    uint16_t duration_time = (wmtp->param[4]<<8) | wmtp->param[5];
    //
    size_t buffSize, buffSize2, frame_size, count;
    WMix_Point src, dist;
    ssize_t ret, total = 0;
    uint32_t second = 0, bytes_p_second, bytes_p_second2, bpsCount = 0, TOTAL;
    float divCount, divPow;
    //
    int fd;
    WAVContainer_t wav;
#if(WMIX_MODE==1)
    int16_t record_addr;
#if(WMIX_CHANNELS == 1)
    unsigned char buff[1024];
#else
    unsigned char buff[512];
#endif
#else
    SNDPCMContainer_t *record = NULL;
#endif
    //
    uint8_t loopWord;
    loopWord = wmtp->wmix->loopWordRecord;
    //
    if(freq > WMIX_FREQ){
        fprintf(stderr, "wmix_record_wav_thread: freq err, %dHz > %dHz(machine)\n", freq, WMIX_FREQ);
        return;
    }
    if(sample != WMIX_SAMPLE){
        fprintf(stderr, "wmix_record_wav_thread: sample err, must be %dbit(machine)\n", WMIX_SAMPLE);
        return;
    }
    if(chn != 1 && chn != 2){
        fprintf(stderr, "wmix_record_wav_thread: channels err, must be 1 or 2\n");
        return;
    }
    //
    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if(fd <= 0){
        fprintf(stderr, "wmix_record_wav_thread: open %s err\n", path);
        return;
    }
    //
    WAV_Params(&wav, duration_time, chn, sample, freq);
    if(WAV_WriteHeader(fd, &wav) < 0){
        close(fd);
        fprintf(stderr, "wmix_record_wav_thread: WAV_WriteHeader err\n");
        return;
    }
    //
#if(WMIX_MODE==1)
    record_addr = hiaudio_ai_init(WMIX_CHANNELS, WMIX_SAMPLE, WMIX_FREQ);
    if(record_addr < 0){
#else
    record = wmix_alsa_init(WMIX_CHANNELS, WMIX_SAMPLE, WMIX_FREQ, 'c');
    if(!record){
#endif
        close(fd);
        fprintf(stderr, "wmix_record_wav_thread: hiaudio_ai_init err\n");
        return;
    }
    //
    bytes_p_second = WMIX_CHANNELS*WMIX_SAMPLE/8*WMIX_FREQ;
    bytes_p_second2 = chn*sample/8*freq;
#if(WMIX_MODE==1)
    frame_size = WMIX_CHANNELS*WMIX_SAMPLE/8;
#if(WMIX_CHANNELS == 1)
    buffSize = sizeof(buff)/2;
#else
    buffSize = sizeof(buff);
#endif
#else
    buffSize = record->chunk_bytes;
    frame_size = buffSize/(WMIX_CHANNELS*WMIX_SAMPLE/8);
#endif
    TOTAL = bytes_p_second*duration_time;
    //
    divPow = (float)(WMIX_FREQ - freq)/freq;
    divCount = 0;
    //
    if(wmtp->wmix->debug) printf("<< RECORD-WAV: %s record >>\n   通道数: %d\n   采样位数: %d bit\n   采样率: %d Hz\n   每秒字节: %d Bytes\n   时间长度: %d sec\n\n", 
        path, chn, sample, freq, bytes_p_second2, duration_time);
    //线程计数
    wmtp->wmix->thread_record += 1;
    //
    while(wmtp->wmix->run && 
        loopWord == wmtp->wmix->loopWordRecord)
    {
        //最后一帧
        if(total + buffSize >= TOTAL)
        {
            buffSize = TOTAL - total;
        }
        //
#if(WMIX_MODE==1)
        ret = hiaudio_ai_read(buff, buffSize, &record_addr, false);
#else
        ret = SNDWAV_ReadPcm(record, frame_size);
#endif
        if(ret > 0)
        {
            //
            bpsCount += ret;
            total += ret;
            //录制时间
            if(bpsCount > bytes_p_second)
            {
                bpsCount -= bytes_p_second;
                second = total/bytes_p_second;
                if(wmtp->wmix->debug) printf("  RECORD-WAV: %s %02d:%02d\n", path, second/60, second%60);
            }
            //
            if(!wmtp->wmix->run)
                break;
            //不同频率和通道数的数据处理
            RECORD_DATA_TRANSFER();
            //
#if(WMIX_MODE==1)
            ret = write(fd, buff, buffSize2);
#else
            ret = write(fd, record->data_buf, buffSize2);
#endif
            if(ret < 0 && errno != EAGAIN)
            {
                fprintf(stderr, "wmix_record_wav_thread: write err %d\n", errno);
                break;
            }
            //
            if(total >= TOTAL)
                break;
        }
        else if(ret < 0)
        {
            fprintf(stderr, "wmix_record_wav_thread: hiaudio_ai_read err %d\n", ret);
            break;
        }
        else
            delayus(1000);
    }
    //
#if(WMIX_MODE==1)
    // hiaudio_ai_exit();
#else
    wmix_alsa_release(record);
#endif
    close(fd);
    //
    if(wmtp->wmix->debug) printf(">> RECORD-WAV: %s end <<\n", path);
    //线程计数
    wmtp->wmix->thread_record -= 1;
    //
    if(wmtp->param)
        free(wmtp->param);
    free(wmtp);
}

#if(WMIX_MODE==1)
void wmix_record_aac_thread(WMixThread_Param *wmtp)
{
    char *path = (char*)&wmtp->param[6];
    //
    uint8_t chn = wmtp->param[0];
    uint8_t sample = wmtp->param[1];
    uint16_t freq = (wmtp->param[2]<<8) | wmtp->param[3];
    uint16_t duration_time = (wmtp->param[4]<<8) | wmtp->param[5];
    //
    size_t buffSize, buffSizeR, buffSize2, frame_size, count;
    WMix_Point src, dist;
    ssize_t ret, total = 0;
    uint32_t second = 0, bytes_p_second, bytes_p_second2, bpsCount = 0, TOTAL;
    float divCount, divPow;
    //
    int fd;
    int16_t record_addr;
    unsigned char *buff, *buff2, *pBuff2_S, *pBuff2_E;
    unsigned char aacBuff[2048];
    void *aacEncFd;
    //
    uint8_t loopWord;
    loopWord = wmtp->wmix->loopWordRecord;
    //
    if(freq > WMIX_FREQ){
        fprintf(stderr, "wmix_record_aac_thread: freq err, %dHz > %dHz(machine)\n", freq, WMIX_FREQ);
        return;
    }
    if(sample != WMIX_SAMPLE){
        fprintf(stderr, "wmix_record_aac_thread: sample err, must be %dbit(machine)\n", WMIX_SAMPLE);
        return;
    }
    if(chn != 1 && chn != 2){
        fprintf(stderr, "wmix_record_aac_thread: channels err, must be 1 or 2\n");
        return;
    }
    //
    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if(fd <= 0){
        fprintf(stderr, "wmix_record_aac_thread: open %s err\n", path);
        return;
    }
    //
    if(!(aacEncFd = hiaudio_aacEnc_init(chn, sample, freq))){
        fprintf(stderr, "hiaudio_aacEnc_init: err\n");
        return;
    }
    //
    record_addr = hiaudio_ai_init(WMIX_CHANNELS, WMIX_SAMPLE, WMIX_FREQ);
    if(record_addr < 0){
        close(fd);
        fprintf(stderr, "wmix_record_aac_thread: wmix_alsa_init err\n");
        return;
    }
    //
    bytes_p_second = WMIX_CHANNELS*WMIX_SAMPLE/8*WMIX_FREQ;
    bytes_p_second2 = chn*sample/8*freq;
    frame_size = WMIX_CHANNELS*WMIX_SAMPLE/8;
    //
    buffSize = WMIX_CHANNELS*WMIX_SAMPLE/8*1024;
    buffSizeR = chn*sample/8*1024;
    //
    buff = malloc(2*WMIX_SAMPLE/8*1024);
    buff2 = malloc(buffSizeR*2);
    pBuff2_S = pBuff2_E = buff2;
    //
    TOTAL = bytes_p_second*duration_time;
    //
    divPow = (float)(WMIX_FREQ - freq)/freq;
    divCount = 0;
    //
    if(wmtp->wmix->debug) printf("<< RECORD-AAC: %s record >>\n   通道数: %d\n   采样位数: %d bit\n   采样率: %d Hz\n   每秒字节: %d Bytes\n   时间长度: %d sec\n\n", 
        path, chn, sample, freq, bytes_p_second2, duration_time);
    //线程计数
    wmtp->wmix->thread_record += 1;
    //
    while(wmtp->wmix->run && 
        loopWord == wmtp->wmix->loopWordRecord)
    {
        //最后一帧
        if(total + buffSize >= TOTAL)
            buffSize = TOTAL - total;
        //
        ret = hiaudio_ai_read(buff, buffSize, &record_addr, true);
        if(ret > 0)
        {
            bpsCount += ret;
            total += ret;
            //录制时间
            if(bpsCount > bytes_p_second)
            {
                bpsCount -= bytes_p_second;
                second = total/bytes_p_second;
                if(wmtp->wmix->debug) printf("  RECORD-AAC: %s %02d:%02d\n", path, second/60, second%60);
            }
            //
            if(!wmtp->wmix->run)
                break;
            //不同频率和通道数的数据处理
            RECORD_DATA_TRANSFER();
            //
            memcpy(pBuff2_E, buff, buffSize2);
            pBuff2_E += buffSize2;
            //
            while(pBuff2_E - pBuff2_S >= buffSizeR)
            {
                ret = hiaudio_aacEnc(aacEncFd, pBuff2_S, aacBuff);
                if(ret > 0)
                {
                    ret = write(fd, aacBuff, ret);
                    if(ret < 0 && errno != EAGAIN)
                    {
                        fprintf(stderr, "wmix_record_aac_thread: write err %d\n", errno);
                        break;
                    }
                }
                pBuff2_S += buffSizeR;
            }
            if(ret < 0 && errno != EAGAIN)
                break;
            //
            memcpy(buff2, pBuff2_S, (size_t)(pBuff2_E-pBuff2_S));
            pBuff2_E = &buff2[pBuff2_E-pBuff2_S];
            pBuff2_S = buff2;
            //
            if(total >= TOTAL)
                break;
        }
        else if(ret < 0)
        {
            fprintf(stderr, "wmix_record_aac_thread: hiaudio_ai_read err %d\n", ret);
            break;
        }
        else
            delayus(10000);
    }
    //
    // hiaudio_ai_exit();
    close(fd);
    free(buff);
    free(buff2);
    //
    if(wmtp->wmix->debug) printf(">> RECORD-AAC: %s end <<\n", path);
    //线程计数
    wmtp->wmix->thread_record -= 1;
    //
    hiaudio_aacEnc_deinit(aacEncFd);
    if(wmtp->param)
        free(wmtp->param);
    free(wmtp);
}

void wmix_rtp_send_aac_thread(WMixThread_Param *wmtp)
{
    char *path = (char*)&wmtp->param[6];
    char *msgPath;
    key_t msg_key;
    int msg_fd;
    WMix_Msg msg;
    //
    uint8_t chn = wmtp->param[0];
    uint8_t sample = wmtp->param[1];
    uint16_t freq = (wmtp->param[2]<<8) | wmtp->param[3];
    uint16_t port = (wmtp->param[4]<<8) | wmtp->param[5];
    //
    size_t buffSize, buffSizeR, buffSize2, frame_size, count;
    WMix_Point src, dist;
    ssize_t ret, total = 0;
    uint32_t second = 0, bytes_p_second, bytes_p_second2, bpsCount = 0;
    float divCount, divPow;
    //
    int16_t record_addr;
    unsigned char *buff, *buff2, *pBuff2_S, *pBuff2_E;
    void *aacEncFd;
    unsigned char aacbuff[2048];
    //
    SocketStruct *ss;
    RtpPacket rtpPacket;
    //
    uint8_t loopWord;
    loopWord = wmtp->wmix->loopWordRecord;
    //
    //参数检查,是否在允许的变参范围内
    if(freq > WMIX_FREQ){
        fprintf(stderr, "wmix_rtp_send_aac_thread: freq err, %dHz > %dHz(machine)\n", freq, WMIX_FREQ);
        return;
    }
    if(sample != WMIX_SAMPLE){
        fprintf(stderr, "wmix_rtp_send_aac_thread: sample err, must be %dbit(machine)\n", WMIX_SAMPLE);
        return;
    }
    if(chn != 1 && chn != 2){
        fprintf(stderr, "wmix_rtp_send_aac_thread: channels err, must be 1 or 2\n");
        return;
    }
    //初始化rtp
    ss = rtp_socket(path, port, 1);
    if(!ss){
        fprintf(stderr, "rtp_socket: err\n");
        return;
    }
    rtp_header(&rtpPacket, 0, 0, 0, RTP_VESION, RTP_PAYLOAD_TYPE_AAC, 1, 0, 0, 0x32411);
    //初始化编码器
    if(!(aacEncFd = hiaudio_aacEnc_init(chn, sample, freq))){
        fprintf(stderr, "hiaudio_aacEnc_init: err\n");
        free(ss);
        return;
    }
    //初始化ai
    record_addr = hiaudio_ai_init(WMIX_CHANNELS, WMIX_SAMPLE, WMIX_FREQ);
    if(record_addr < 0){
        hiaudio_aacEnc_deinit(aacEncFd);
        free(ss);
        fprintf(stderr, "wmix_record_aac_thread: wmix_alsa_init err\n");
        return;
    }
    //初始化消息
    msgPath = (char*)&wmtp->param[strlen(path)+6+1];
    if(msgPath && msgPath[0])
    {
        //创建消息挂靠路径
        if(access(msgPath, F_OK) != 0)
            creat(msgPath, 0777);
        //创建消息
        if((msg_key = ftok(msgPath, WMIX_MSG_ID)) > 0)
            msg_fd = msgget(msg_key, IPC_CREAT|0666);
    }
    else
        msgPath = NULL;
    //生成sdp文件
    rtp_create_sdp(
        "/tmp/record.sdp", 
        path, port, chn, freq,
        RTP_PAYLOAD_TYPE_AAC);
    //
    bytes_p_second = WMIX_CHANNELS*WMIX_SAMPLE/8*WMIX_FREQ;
    bytes_p_second2 = chn*sample/8*freq;
    frame_size = WMIX_CHANNELS*WMIX_SAMPLE/8;
    //每次从ai读取字节数
    buffSize = WMIX_CHANNELS*WMIX_SAMPLE/8*1024;
    buffSizeR = chn*sample/8*1024;
    //
    buff = malloc(2*WMIX_SAMPLE/8*1024);
    buff2 = malloc(buffSizeR*2);
    pBuff2_S = pBuff2_E = buff2;
    //
    divPow = (float)(WMIX_FREQ - freq)/freq;
    divCount = 0;
    //
    if(wmtp->wmix->debug) printf("<< RTP-SEND-AAC: %s:%d start >>\n   通道数: %d\n   采样位数: %d bit\n   采样率: %d Hz\n   每秒字节: %d Bytes\n\n", 
        path, port, chn, sample, freq, bytes_p_second2);
    //线程计数
    wmtp->wmix->thread_record += 1;
    //
    while(wmtp->wmix->run && 
        loopWord == wmtp->wmix->loopWordRecord)
    {
        //msg 检查
        if(msg_fd){
            if(msgrcv(msg_fd, &msg, 
                WMIX_MSG_BUFF_SIZE, 
                0, IPC_NOWAIT) < 1 && 
                errno != ENOMSG) //消息队列被关闭
                break;
        }
        //
        ret = hiaudio_ai_read(buff, buffSize, &record_addr, true);
        if(ret > 0)
        {
            bpsCount += ret;
            total += ret;
            //录制时间
            if(bpsCount > bytes_p_second)
            {
                bpsCount -= bytes_p_second;
                second = total/bytes_p_second;
                if(wmtp->wmix->debug) printf("  RTP-SEND-AAC: %s:%d %02d:%02d\n", path, port, second/60, second%60);
            }
            //
            if(!wmtp->wmix->run)
                break;
            //不同频率和通道数的数据处理
            RECORD_DATA_TRANSFER();
            //
            memcpy(pBuff2_E, buff, buffSize2);
            pBuff2_E += buffSize2;
            //
            while(pBuff2_E - pBuff2_S >= buffSizeR)
            {
                ret = hiaudio_aacEnc(aacEncFd, pBuff2_S, aacbuff);
                if(ret > 0)
                {
                    ret -= 7;
                    memcpy(&rtpPacket.payload[4], &aacbuff[7], ret);
                    ret = rtp_send(ss, &rtpPacket, ret);
                    if(ret < 1)
                    {
                        printf("rtp_send: err\n");
                        break;
                    }
                }
                pBuff2_S += buffSizeR;
                delayus(1000);
            }
            if(ret < 0)
                break;
            //
            memcpy(buff2, pBuff2_S, (size_t)(pBuff2_E-pBuff2_S));
            pBuff2_E = &buff2[pBuff2_E-pBuff2_S];
            pBuff2_S = buff2;
        }
        else if(ret < 0)
        {
            fprintf(stderr, "wmix_rtp_send_aac_thread: hiaudio_ai_read err %d\n", ret);
            break;
        }
    }
    //
    // hiaudio_ai_exit();
    //
    if(wmtp->wmix->debug) printf(">> RTP-SEND-AAC: %s:%d end <<\n", path, port);
    //线程计数
    wmtp->wmix->thread_record -= 1;
    //
    if(msgPath)
        remove(msgPath);
    free(buff);
    free(buff2);
    close(ss->fd);
    free(ss);
    //
    hiaudio_aacEnc_deinit(aacEncFd);
    if(wmtp->param)
        free(wmtp->param);
    free(wmtp);
}

void wmix_rtp_recv_aac_thread(WMixThread_Param *wmtp)
{
    char *path = (char*)&wmtp->param[6];
    char *msgPath;
    key_t msg_key;
    int msg_fd;
    WMix_Msg msg;
    //
    uint8_t chn = wmtp->param[0];
    uint8_t sample = wmtp->param[1];
    uint16_t freq = (wmtp->param[2]<<8) | wmtp->param[3];
    uint16_t port = (wmtp->param[4]<<8) | wmtp->param[5];
    uint32_t bytes_p_second;
    //
    ssize_t ret = 0;
    uint8_t buff[4096];
    uint32_t buffSize, buffSize2;
    WMix_Point src;
    uint32_t tick, total = 0, total2 = 0, totalWait;
    uint32_t second = 0, bpsCount = 0;
    double totalPow;
    uint8_t rdce = 2, rdceIsMe = 0;
    //
    void *aacDecFd = NULL;
    int readLen, datUse = 0, retLen;
    unsigned char aacBuff[2048];
    //
    SocketStruct *ss;
    RtpPacket rtpPacket;
    int retSize;
    //初始化rtp
    ss = rtp_socket(path, port, 0);
    if(!ss){
        fprintf(stderr, "rtp_socket: err\n");
        return;
    }
    //初始化解码器
    aacDecFd = hiaudio_aacDec_init();
    if(!aacDecFd)
    {
        free(ss);
        fprintf(stderr, "hiaudio_aacDec_init: err\n");
        return;
    }
    //初始化消息
    msgPath = (char*)&wmtp->param[strlen(path)+6+1];
    if(msgPath && msgPath[0])
    {
        //创建消息挂靠路径
        if(access(msgPath, F_OK) != 0)
            creat(msgPath, 0777);
        //创建消息
        if((msg_key = ftok(msgPath, WMIX_MSG_ID)) > 0)
            msg_fd = msgget(msg_key, IPC_CREAT|0666);
    }
    else
        msgPath = NULL;
    //独占 reduceMode
    if(rdce > 1 && wmtp->wmix->reduceMode == 1)
    {
        wmtp->wmix->reduceMode = rdce;
        rdceIsMe = 1;
    }
    else
        rdce = 1;
    //默认缓冲区大小设为1秒字播放字节数
    bytes_p_second = chn*sample/8*freq;
    buffSize = bytes_p_second;
    buffSize2 = WMIX_CHANNELS*WMIX_SAMPLE/8*WMIX_FREQ;
    totalPow = (double)buffSize2/buffSize;
    //
    buffSize = chn*sample/8*1024;
    buffSize2 = WMIX_CHANNELS*WMIX_SAMPLE/8*1024;
    totalWait = buffSize2*8;
    //
    if(wmtp->wmix->debug) printf("<< RTP-RECV: %s:%d start >>\n   通道数: %d\n   采样位数: %d bit\n   采样率: %d Hz\n\n", 
        path, port, chn, sample, freq);
    //
    src.U8 = buff;
    tick = wmtp->wmix->tick;
    // wmtp->wmix->vipWrite.U8 = 0;
    //线程计数
    wmtp->wmix->thread_play += 1;
    //
    while(wmtp->wmix->run)
    {
        //msg 检查
        if(msg_fd){
            if(msgrcv(msg_fd, &msg, 
                WMIX_MSG_BUFF_SIZE, 
                0, IPC_NOWAIT) < 1 && 
                errno != ENOMSG) //消息队列被关闭
                break;
        }
        //往aacBuff读入数据
        ret = rtp_recv(ss, &rtpPacket, &retSize);
        if(ret > 0)
        {
            memcpy(&aacBuff[7], &rtpPacket.payload[4], retSize);
            aac_createHeader(aacBuff, chn, freq, 0x7FF, retSize);
            retLen = hiaudio_aacDec(
                aacDecFd, 
                &chn, 
                &sample, 
                &freq, 
                &datUse, aacBuff, retSize+7, buff);
        }
        else
            retLen = -1;
        //播放文件
        if(retLen > 0)
        {
            //等播放指针赶上写入进度
            if(total2 > totalWait)
            {
                while(wmtp->wmix->run &&
                    get_tick_err(wmtp->wmix->tick, tick) < 
                    total2 - totalWait)
                    delayus(10000);
                if(!wmtp->wmix->run)
                    break;
            }
            //写入循环缓冲区
            wmtp->wmix->vipWrite = wmix_load_wavStream(
                wmtp->wmix, 
                src, retLen, 
                freq, 
                chn, 
                sample, wmtp->wmix->vipWrite, rdce);
            //写入的总字节数统计
            bpsCount += retLen;
            total += retLen;
            total2 = total*totalPow;
            //播放时间
            if(bpsCount > bytes_p_second)
            {
                bpsCount -= bytes_p_second;
                second = total/bytes_p_second;
                if(wmtp->wmix->debug) printf("  RTP-RECV: %s:%d %02d:%02d\n", path, port, second/60, second%60);
            }
            //
            continue;
        }
        else
            delayus(1000);
    }
    //用完关闭
    wmtp->wmix->vipWrite.U8 = 0;
    //
    if(wmtp->wmix->debug) printf(">> RTP-RECV: %s:%d end <<\n", path, port);
    //删除文件
    if(msgPath)
        remove(msgPath);
    close(ss->fd);
    free(ss);
    hiaudio_aacDec_deinit(aacDecFd);
    //线程计数
    wmtp->wmix->thread_play -= 1;
    //
    if(wmtp->param)
        free(wmtp->param);
    //关闭 reduceMode
    if(rdceIsMe)
        wmtp->wmix->reduceMode = 1;
    free(wmtp);
}
#endif

#if(RTP_ONE_SR)
static SocketStruct *rtp_sr = NULL;
#endif
void wmix_rtp_send_pcma_thread(WMixThread_Param *wmtp)
{
    char *path = (char*)&wmtp->param[6];
    char *msgPath;
    key_t msg_key;
    int msg_fd;
    WMix_Msg msg;
    //
    uint8_t chn = wmtp->param[0];
    uint8_t sample = wmtp->param[1];
    uint16_t freq = (wmtp->param[2]<<8) | wmtp->param[3];
    uint16_t port = (wmtp->param[4]<<8) | wmtp->param[5];
    //
    size_t buffSize;
    WMix_Point src, dist;
    ssize_t ret, total = 0;
    uint32_t second = 0, bytes_p_second, bpsCount = 0;
    //
    int ctrl = 0;
#if(!RTP_ONE_SR)
    SocketStruct *rtp_sr = NULL;
#endif
    //
    __time_t tick1, tick2;
    //
#if(WMIX_MODE==1)
    int16_t record_addr;
    unsigned char *buff;
#else
    SNDPCMContainer_t *record = NULL;
#endif
    //
    RtpPacket rtpPacket;
    //
    uint8_t loopWord;
    loopWord = wmtp->wmix->loopWordRecord;
    //
    //参数检查,是否在允许的变参范围内
    if(freq != WMIX_FREQ){
        fprintf(stderr, "wmix_rtp_send_pcma_thread: freq err, %dHz != %dHz(machine)\n", freq, WMIX_FREQ);
        return;
    }
    if(sample != WMIX_SAMPLE){
        fprintf(stderr, "wmix_rtp_send_pcma_thread: sample err, must be %dbit(machine)\n", WMIX_SAMPLE);
        return;
    }
    if(chn != 1 && chn != 2){
        fprintf(stderr, "wmix_rtp_send_pcma_thread: channels err, must be 1 or 2\n");
        return;
    }
    //初始化rtp
    if(!rtp_sr)
        rtp_sr = rtp_socket(path, port, 1);
    if(!rtp_sr){
        fprintf(stderr, "rtp_socket: err\n");
        return;
    }
    rtp_header(&rtpPacket, 0, 0, 0, RTP_VESION, RTP_PAYLOAD_TYPE_PCMA, 1, 0, 0, 0);
    //初始化ai
#if(WMIX_MODE==1)
    record_addr = hiaudio_ai_init(WMIX_CHANNELS, WMIX_SAMPLE, WMIX_FREQ);
    if(record_addr < 0){
#else
    record = wmix_alsa_init(WMIX_CHANNELS, WMIX_SAMPLE, WMIX_FREQ, 'c');
    if(!record){
#endif
#if(!RTP_ONE_SR)
        free(rtp_sr);
#endif
        fprintf(stderr, "wmix_rtp_send_pcma_thread: wmix_alsa_init err\n");
        return;
    }
    //初始化消息
    msgPath = (char*)&wmtp->param[strlen(path)+6+1];
    if(msgPath && msgPath[0])
    {
        //创建消息挂靠路径
        if(access(msgPath, F_OK) != 0)
            creat(msgPath, 0777);
        //创建消息
        if((msg_key = ftok(msgPath, WMIX_MSG_ID)) > 0)
            msg_fd = msgget(msg_key, IPC_CREAT|0666);
    }
    else
        msgPath = NULL;
    //生成sdp文件
    rtp_create_sdp(
        "/tmp/record.sdp", 
        path, port, chn, freq,
        RTP_PAYLOAD_TYPE_PCMA);
    //
    bytes_p_second = WMIX_CHANNELS*WMIX_SAMPLE/8*WMIX_FREQ;
    //每次从ai读取字节数
#if(WMIX_MODE==1)
    buffSize = 320;
    buff = malloc(2*WMIX_SAMPLE/8*1024);
#else
    buffSize = 320*8/record->bits_per_frame;
#endif
    //
    if(wmtp->wmix->debug) printf("<< RTP-SEND-PCM: %s:%d start >>\n   通道数: %d\n   采样位数: %d bit\n   采样率: %d Hz\n\n", 
        path, port, chn, sample, freq);
    //线程计数
    wmtp->wmix->thread_record += 1;
    //
    while(wmtp->wmix->run && 
        loopWord == wmtp->wmix->loopWordRecord)
    {
        //msg 检查
        if(msg_fd){
            if(msgrcv(msg_fd, &msg, 
                WMIX_MSG_BUFF_SIZE, 
                0, IPC_NOWAIT) < 1)
            {
                if(errno != ENOMSG) //消息队列被关闭
                    break;
            }
            else
            {
                printf("RTP-SEND-PCM: msg recv %d\n", msg.type);
                //控制信号
                if(msg.type == 0)//运行
                    ctrl = 0;
                else if(msg.type == 1)//停止
                    ctrl = 1;
                else if(msg.type == 2)//重连
                {
                    port = (msg.value[0]<<8) | msg.value[1];
                    path = msg.value[2];
                    //
                    pthread_mutex_lock(&rtp_sr->lock);
                    close(rtp_sr->fd);
                    rtp_sr->fd = socket(AF_INET, SOCK_DGRAM, 0);
                    rtp_sr->addr.sin_port = htons(port);
                    rtp_sr->addr.sin_addr.s_addr = inet_addr(path);
                    rtp_sr->addrSize = sizeof(rtp_sr->addr);
                    bind(rtp_sr->fd, &rtp_sr->addr, rtp_sr->addrSize);
                    pthread_mutex_unlock(&rtp_sr->lock);
                    //
                    ctrl = 0;
                }
            }
        }
        //
        tick1 = getTickUs();
        //
#if(WMIX_MODE==1)
        ret = hiaudio_ai_read(buff, buffSize, &record_addr, true);
#else
        ret = SNDWAV_ReadPcm(record, buffSize)*8;
#endif
        if(ret > 0)
        {
            if(ctrl == 0)
            {
                bpsCount += ret;
                total += ret;
                //录制时间
                if(bpsCount > bytes_p_second)
                {
                    bpsCount -= bytes_p_second;
                    second = total/bytes_p_second;
                    if(wmtp->wmix->debug) printf("  RTP-SEND-PCM: %s:%d %02d:%02d\n", path, port, second/60, second%60);
                }
            }
            //
#if(WMIX_MODE==1)
            ret = PCM2G711a(buff, rtpPacket.payload, ret, 0);
#else
            ret = PCM2G711a(record->data_buf, rtpPacket.payload, ret, 0);
#endif
            if(ctrl == 0)
            {
                if(rtp_send(rtp_sr, &rtpPacket, ret) < ret)
                {
                    fprintf(stderr, "wmix_rtp_send_pcma_thread: rtp_send err !!\n");
                    delayus(1000000);
                    //重连
                    pthread_mutex_lock(&rtp_sr->lock);
                    close(rtp_sr->fd);
                    rtp_sr->fd = socket(AF_INET, SOCK_DGRAM, 0);
                    bind(rtp_sr->fd, &rtp_sr->addr, rtp_sr->addrSize);
                    pthread_mutex_unlock(&rtp_sr->lock);
                    //
                    continue;
                }
                rtpPacket.rtpHeader.timestamp += ret;
            }
            //
            tick2 = getTickUs();
            if(tick2 > tick1 && tick2 - tick1 < 18000)
                delayus(18000 - (tick2 - tick1));
        }
        else
        {
            fprintf(stderr, "wmix_rtp_send_pcma_thread: hiaudio_ai_read err %d\n", ret);
            break;
        }
    }
    //
#if(WMIX_MODE==1)
    // hiaudio_ai_exit();
#else
    wmix_alsa_release(record);
#endif
    //
    if(wmtp->wmix->debug) printf(">> RTP-SEND-PCM: %s:%d end <<\n", path, port);
    //线程计数
    wmtp->wmix->thread_record -= 1;
    //
    if(msgPath)
        remove(msgPath);
#if(WMIX_MODE==1)
    free(buff);
#endif
#if(!RTP_ONE_SR)
    close(rtp_sr->fd);
    free(rtp_sr);
#endif
    //
    if(wmtp->param)
        free(wmtp->param);
    free(wmtp);
}

void wmix_rtp_recv_pcma_thread(WMixThread_Param *wmtp)
{
    char *path = (char*)&wmtp->param[6];
    char *msgPath;
    key_t msg_key;
    int msg_fd;
    WMix_Msg msg;
    //
    uint8_t chn = wmtp->param[0];
    uint8_t sample = wmtp->param[1];
    uint16_t freq = (wmtp->param[2]<<8) | wmtp->param[3];
    uint16_t port = (wmtp->param[4]<<8) | wmtp->param[5];
    uint32_t bytes_p_second;
    //
    ssize_t ret = 0;
    uint8_t buff[1024];
    uint32_t buffSize, buffSize2;
    WMix_Point src;
    uint32_t tick, total = 0, total2 = 0, totalWait;
    uint32_t second = 0, bpsCount = 0;
    double totalPow;
    uint8_t rdce = 2, rdceIsMe = 0;
    //
    int ctrl = 0;
#if(!RTP_ONE_SR)
    SocketStruct *rtp_sr = NULL;
#endif
    //
    RtpPacket rtpPacket;
    int retSize;
    //初始化rtp
    if(!rtp_sr)
        rtp_sr = rtp_socket(path, port, 0);
    if(!rtp_sr){
        fprintf(stderr, "rtp_socket: err\n");
        return;
    }
    //初始化消息
    msgPath = (char*)&wmtp->param[strlen(path)+6+1];
    if(msgPath && msgPath[0])
    {
        //创建消息挂靠路径
        if(access(msgPath, F_OK) != 0)
            creat(msgPath, 0777);
        //创建消息
        if((msg_key = ftok(msgPath, WMIX_MSG_ID)) > 0)
            msg_fd = msgget(msg_key, IPC_CREAT|0666);
    }
    else
        msgPath = NULL;
    //独占 reduceMode
    if(rdce > 1 && wmtp->wmix->reduceMode == 1)
    {
        wmtp->wmix->reduceMode = rdce;
        rdceIsMe = 1;
    }
    else
        rdce = 1;
    //默认缓冲区大小设为1秒字播放字节数
    bytes_p_second = chn*sample/8*freq;
    buffSize = bytes_p_second;
    buffSize2 = WMIX_CHANNELS*WMIX_SAMPLE/8*WMIX_FREQ;
    totalPow = (double)buffSize2/buffSize;
    totalWait = 320*4;//buffSize2/2;
    //
    if(wmtp->wmix->debug) printf("<< RTP-RECV-PCM: %s:%d start >>\n   通道数: %d\n   采样位数: %d bit\n   采样率: %d Hz\n\n", 
        path, port, chn, sample, freq);
    //
    src.U8 = buff;
    tick = wmtp->wmix->tick;
    // wmtp->wmix->vipWrite.U8 = 0;
    //线程计数
    wmtp->wmix->thread_play += 1;
    //
    while(wmtp->wmix->run)
    {
        //msg 检查
        if(msg_fd){
            if(msgrcv(msg_fd, &msg, 
                WMIX_MSG_BUFF_SIZE, 
                0, IPC_NOWAIT) < 1)
            {
                if(errno != ENOMSG) //消息队列被关闭
                    break;
            }
            else
            {
                printf("RTP-RECV-PCM: msg recv %d\n", msg.type);
                //控制信号
                if(msg.type == 0)//运行
                    ctrl = 0;
                else if(msg.type == 1)//停止
                    ctrl = 1;
                else if(msg.type == 2)//重连
                {
                    port = (msg.value[0]<<8) | msg.value[1];
                    path = msg.value[2];
                    //
                    pthread_mutex_lock(&rtp_sr->lock);
                    close(rtp_sr->fd);
                    rtp_sr->fd = socket(AF_INET, SOCK_DGRAM, 0);
                    rtp_sr->addr.sin_port = htons(port);
                    rtp_sr->addr.sin_addr.s_addr = inet_addr(path);
                    rtp_sr->addrSize = sizeof(rtp_sr->addr);
                    bind(rtp_sr->fd, &rtp_sr->addr, rtp_sr->addrSize);
                    pthread_mutex_unlock(&rtp_sr->lock);
                    //
                    ctrl = 0;
                }
            }
        }
        //等播放指针赶上写入进度
        // if(total2 > totalWait)
        // {
        //     while(wmtp->wmix->run &&
        //         get_tick_err(wmtp->wmix->tick, tick) < 
        //         total2 - totalWait)
        //     {
        //         rtp_recv(rtp_sr, &rtpPacket, &retSize);
        //         delayus(1000);
        //     }
        //     if(!wmtp->wmix->run)
        //         break;
        // }
        //读rtp数据
        ret = rtp_recv(rtp_sr, &rtpPacket, &retSize);
        if(ret > 0 && retSize > 0)
            ret = G711a2PCM(rtpPacket.payload, buff, retSize, 0);
        else
            ret = -1;
        //播放文件
        if(ret > 0)
        {
            //写入循环缓冲区
            wmtp->wmix->vipWrite = wmix_load_wavStream(
                wmtp->wmix, 
                src, ret, 
                freq, 
                chn, 
                sample, wmtp->wmix->vipWrite, rdce);
            //写入的总字节数统计
            bpsCount += ret;
            total += ret;
            total2 = total*totalPow;
            //播放时间
            if(bpsCount > bytes_p_second)
            {
                bpsCount -= bytes_p_second;
                second = total/bytes_p_second;
                if(wmtp->wmix->debug) printf("  RTP-RECV-PCM: %s:%d %02d:%02d\n", path, port, second/60, second%60);
            }
            //
            continue;
        }
        else
            delayus(10000);
    }
    //用完关闭
    wmtp->wmix->vipWrite.U8 = 0;
    //
    if(wmtp->wmix->debug) printf(">> RTP-RECV-PCM: %s:%d end <<\n", path, port);
    //删除文件
    if(msgPath)
        remove(msgPath);
#if(!RTP_ONE_SR)
    close(rtp_sr->fd);
    free(rtp_sr);
#endif
    //线程计数
    wmtp->wmix->thread_play -= 1;
    //
    if(wmtp->param)
        free(wmtp->param);
    //关闭 reduceMode
    if(rdceIsMe)
        wmtp->wmix->reduceMode = 1;
    free(wmtp);
}

void wmix_load_audio_thread(WMixThread_Param *wmtp)
{
    char *name = (char*)wmtp->param;
    uint16_t len = strlen((char*)wmtp->param);
    //
    char *msgPath;
    WMix_Msg msg;
    key_t msg_key;
    int msg_fd = 0;
    //
    bool run = true;
    //
    int queue = -1;
    //
    uint8_t loopWord;
    loopWord = wmtp->wmix->loopWord;
    //线程计数
    wmtp->wmix->thread_play += 1;
    //
    msgPath = (char*)&wmtp->param[len+1];
    if(msgPath && msgPath[0])
    {
        //创建消息挂靠路径
        if(access(msgPath, F_OK) != 0)
            creat(msgPath, 0777);
        //创建消息
        if((msg_key = ftok(msgPath, WMIX_MSG_ID)) > 0)
            msg_fd = msgget(msg_key, IPC_CREAT|0666);
    }
    else
        msgPath = NULL;
    //排队(循环播放和背景消减除时除外)
    if(((wmtp->flag&0xFF) == 9 || (wmtp->flag&0xFF) == 10) 
        && ((wmtp->flag>>8)&0xFF) == 0
        && ((wmtp->flag>>16)&0xFF) == 0)
    {
        run = false;

        if((wmtp->flag&0xFF) == 9 && 
            wmtp->wmix->queue.head != wmtp->wmix->queue.tail)//排头
            queue = wmtp->wmix->queue.head--;
        else
            queue = wmtp->wmix->queue.tail++;
        
        while(wmtp->wmix->run && loopWord == wmtp->wmix->loopWord)
        {
            if(queue == wmtp->wmix->queue.head
                && wmtp->wmix->onPlayCount == 0)
            {
                run = true;
                break;
            }
            delayus(100000);
        }
    }
    //
    if(run)
    {
        wmtp->wmix->onPlayCount += 1;
        //
        if(len > 3 &&
            (name[len-3] == 'a' || name[len-3] == 'A') &&
            (name[len-2] == 'a' || name[len-2] == 'A') &&
            (name[len-1] == 'c' || name[len-1] == 'C'))
#if(WMIX_MODE==1)
            wmix_load_aac(wmtp->wmix, name, msg_fd, (wmtp->flag>>8)&0xFF, (wmtp->flag>>16)&0xFF);
#else
            ;
#endif
#if(WMIX_MP3)
        else if(len > 3 &&
            (name[len-3] == 'm' || name[len-3] == 'M') &&
            (name[len-2] == 'p' || name[len-2] == 'P') &&
            name[len-1] == '3')
            wmix_load_mp3(wmtp->wmix, name, msg_fd, (wmtp->flag>>8)&0xFF, (wmtp->flag>>16)&0xFF);
#endif
        else
            wmix_load_wav(wmtp->wmix, name, msg_fd, (wmtp->flag>>8)&0xFF, (wmtp->flag>>16)&0xFF);
        //
        wmtp->wmix->onPlayCount -= 1;
    }
    //
    if(queue >= 0)
        wmtp->wmix->queue.head += 1;
    //
    if(msgPath)
        remove(msgPath);
    //线程计数
    wmtp->wmix->thread_play -= 1;
    //
    if(wmtp->param)
        free(wmtp->param);
    free(wmtp);
}

void wmix_msg_thread(WMixThread_Param *wmtp)
{
    WMix_Struct *wmix = wmtp->wmix;
    WMix_Msg msg;
    ssize_t ret;
    bool err_exit = false;

    //路径检查 //F_OK 是否存在 R_OK 是否有读权限 W_OK 是否有写权限 X_OK 是否有执行权限
    if(access(WMIX_MSG_PATH, F_OK) != 0)
        mkdir(WMIX_MSG_PATH, 0777);
    //再次检查
    if(access(WMIX_MSG_PATH, F_OK) != 0){
        fprintf(stderr, "wmix_msg_thread: msg path not found\n");
        return;
    }
    //清空文件夹
    system(WMIX_MSG_PATH_CLEAR);
    //权限处理
    system(WMIX_MSG_PATH_AUTHORITY);
    //获得管道
    if((wmix->msg_key = ftok(WMIX_MSG_PATH, WMIX_MSG_ID)) == -1){
        fprintf(stderr, "wmix_msg_thread: ftok err\n");
        return;
    }
    //清空队列
    if((wmix->msg_fd = msgget(wmix->msg_key, 0666)) != -1)
        msgctl(wmix->msg_fd, IPC_RMID, NULL);
    //重新创建队列
    if((wmix->msg_fd = msgget(wmix->msg_key, IPC_CREAT|0666)) == -1){
        fprintf(stderr, "wmix_msg_thread: msgget err\n");
        return;
    }
    //线程计数
    wmix->thread_sys += 1;
    //接收来信
    while(wmix->run)
    {
        memset(&msg, 0, sizeof(WMix_Msg));
        ret = msgrcv(wmix->msg_fd, &msg, WMIX_MSG_BUFF_SIZE, 0, IPC_NOWAIT);//返回队列中的第一个消息 非阻塞方式
        if(ret > 0)
        {
            switch(msg.type&0xFF)
            {
                //音量设置
                case 1:
                    sys_volume_set(msg.value[0], msg.value[1]);
                    break;
                //互斥播放音频
                case 2:
                    wmix->loopWord += 1;
                //混音播放音频
                case 3:
                //排头播放
                case 9:
                //排尾播放
                case 10:
                    wmix_throwOut_thread(wmix, 
                        msg.type, 
                        msg.value, 
                        WMIX_MSG_BUFF_SIZE, 
                        &wmix_load_audio_thread);
                    break;
                //fifo播放wav流
                case 4:
                    wmix_throwOut_thread(wmix, 
                        msg.type, 
                        msg.value, 
                        WMIX_MSG_BUFF_SIZE, 
                        &wmix_load_wav_fifo_thread);
                    break;
                //复位
                case 5:
                    wmix->loopWord += 1;
                    wmix->run = 0;
                    break;
                //fifo录音wav流
                case 6:
                    wmix_throwOut_thread(wmix, 
                        msg.type, 
                        msg.value, 
                        WMIX_MSG_BUFF_SIZE, 
                        &wmix_record_wav_fifo_thread);
                    break;
                //录音wav文件
                case 7:
                    wmix_throwOut_thread(wmix, 
                        msg.type, 
                        msg.value, 
                        WMIX_MSG_BUFF_SIZE, 
                        &wmix_record_wav_thread);
                    break;
                //清空播放列表
                case 8:
                    wmix->loopWord += 1;
                    break;
                //rtp send pcma
                case 11:
                    wmix_throwOut_thread(wmix, 
                        msg.type, 
                        msg.value, 
                        WMIX_MSG_BUFF_SIZE, 
                        &wmix_rtp_send_pcma_thread);
                    break;
                //rtp recv pcma
                case 12:
                    wmix_throwOut_thread(wmix, 
                        msg.type, 
                        msg.value, 
                        WMIX_MSG_BUFF_SIZE, 
                        &wmix_rtp_recv_pcma_thread);
                    break;
#if(WMIX_MODE==1)
                //录音aac文件
                case 13:
                    wmix_throwOut_thread(wmix, 
                        msg.type, 
                        msg.value, 
                        WMIX_MSG_BUFF_SIZE, 
                        &wmix_record_aac_thread);
                    break;
#endif
            }
            continue;
        }
        //在别的地方重开了该程序的副本
        else if(ret < 1 && errno != ENOMSG)
        {
            fprintf(stderr, "wmix_msg_thread: process exist\n");
            err_exit = true;
            break;
        }
        //清tick
        if(wmix->thread_play == 0 && 
            wmix->head.U8 == wmix->tail.U8)
            wmix->tick = 0;
        //
        delayus(10000);
    }
    //删除队列
    msgctl(wmix->msg_fd, IPC_RMID, NULL);
    //
    if(wmix->debug) printf("wmix_msg_thread exit\n");
    //线程计数
    wmix->thread_sys -= 1;
    //
    if(wmtp->param)
        free(wmtp->param);
    free(wmtp);
    //
    if(err_exit)
    {
        signal_callback(SIGINT);
        exit(0);
    }
}

void wmix_play_thread(WMixThread_Param *wmtp)
{
    WMix_Struct *wmix = wmtp->wmix;
    WMix_Point dist;
    uint32_t count;
#if(WMIX_MODE==1)
    uint8_t write_buff[1024+32];
#else
    uint32_t divVal = WMIX_SAMPLE*WMIX_CHANNELS/8;
    SNDPCMContainer_t *playback = wmtp->wmix->playback;
#endif
    //线程计数
    wmix->thread_sys += 1;
    //
    while(wmix->run)
    {
        if(wmix->head.U8 != wmix->tail.U8)
        {
            if(wmix->head.U8 >= wmix->end.U8)
                wmix->head.U8 = wmix->start.U8;

#if(WMIX_MODE==1)
            for(count = 0, dist.U8 = write_buff; 
                wmix->head.U8 != wmix->tail.U8;)
#else
            for(count = 0, dist.U8 = playback->data_buf; 
                wmix->head.U8 != wmix->tail.U8;)
#endif
            {
#if(WMIX_CHANNELS == 1)
                //每次拷贝 2字节
                *dist.U16++ = *wmix->head.U16;
                *wmix->head.U16++ = 0;
                count += 2;
                wmix->tick += 2;
#else
                //每次拷贝 4字节
                *dist.U32++ = *wmix->head.U32;
                *wmix->head.U32++ = 0;
                count += 4;
                wmix->tick += 4;
#endif
                if(wmix->head.U8 >= wmix->end.U8)
                    wmix->head.U8 = wmix->start.U8;
                //
#if(WMIX_MODE==1)
                if(count == 1024)
                {
                    //写入数据
                    hiaudio_ao_write(write_buff, count);
                    dist.U8 = write_buff;
#else
                if(count == playback->chunk_bytes)
                {
                    //写入数据
                    SNDWAV_WritePcm(playback, count/divVal);
                    dist.U8 = playback->data_buf;
#endif
                    count = 0;
                }
            }
            //最后一丁点
            if(count)
            {
                wmix->tick += count;
#if(WMIX_MODE==1)
                hiaudio_ao_write(write_buff, count);
#else
                SNDWAV_WritePcm(playback, count/divVal);
#endif
            }
        }
        //
        delayus(1000);
    }
    //
    if(wmix->debug) printf("wmix_play_thread exit\n");
    //线程计数
    wmix->thread_sys -= 1;
    //
    if(wmtp->param)
        free(wmtp->param);
    free(wmtp);
}

void wmix_exit(WMix_Struct *wmix)
{
    int timeout;
    if(wmix)
    {
        wmix->run = 0;
        //等待线程关闭
        //等待各指针不再有人使用
        timeout = 200;//2秒超时
        do{
            if(timeout-- < 1)
                break;
            delayus(10000);
        }while(wmix->thread_sys > 0 || 
            wmix->thread_play > 0 || 
            wmix->thread_record > 0);
        //
#if(WMIX_MODE==1)
        hiaudio_exit();
#else
        if(wmix->playback)
	        wmix_alsa_release(wmix->playback);
#endif
        //
        // pthread_mutex_destroy(&wmix->lock);
        free(wmix);
    }
}

WMix_Struct *wmix_init(void)
{
    WMix_Struct *wmix = NULL;
    //
    //路径检查 //F_OK 是否存在 R_OK 是否有读权限 W_OK 是否有写权限 X_OK 是否有执行权限
    if(access(WMIX_MSG_PATH, F_OK) != 0)
        mkdir(WMIX_MSG_PATH, 0777);
    //
#if(WMIX_MODE==1)
    if(hiaudio_ao_init(WMIX_CHANNELS, WMIX_SAMPLE, WMIX_FREQ))
        return NULL;
    if(hiaudio_ai_init(WMIX_CHANNELS, WMIX_SAMPLE, WMIX_FREQ))
        return NULL;
#else
	SNDPCMContainer_t *playback = wmix_alsa_init(WMIX_CHANNELS, WMIX_SAMPLE, WMIX_FREQ, 'p');
    //
    if(!playback)
        return NULL;
#endif
    //
	wmix = (WMix_Struct *)calloc(1, sizeof(WMix_Struct));
    wmix->buff = (uint8_t *)calloc(WMIX_BUFF_SIZE+4, sizeof(uint8_t));
#if(WMIX_MODE!=1)
    wmix->playback = playback;
#endif
    wmix->start.U8 = wmix->head.U8 = wmix->tail.U8 = wmix->buff;
    wmix->end.U8 = wmix->buff + WMIX_BUFF_SIZE;
    //
    // pthread_mutex_init(&wmix->lock, NULL);
    wmix->run = 1;
    wmix->reduceMode = 1;
    wmix_throwOut_thread(wmix, 0, NULL, 0, &wmix_msg_thread);
    wmix_throwOut_thread(wmix, 0, NULL, 0, &wmix_play_thread);
    //
    printf("\n---- WMix info -----\n   通道数: %d\n   采样率: %d Hz\n   采样位数: %d bit\n   总数据量: -- Bytes\n", 
        WMIX_CHANNELS, WMIX_FREQ, WMIX_SAMPLE);
    
    signal(SIGINT, signal_callback);
    signal(SIGTERM, signal_callback);

    //
	return wmix;
}

static int16_t volumeAdd(int16_t L1, int16_t L2)
{
    int32_t sum;
    //
    if(L1 == 0)
        return L2;
    else if(L2 == 0)
        return L1;
    else{
        sum = (int32_t)L1 + L2;
        if(sum < -32768)
            return -32768;
        else if(sum > 32767)
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
    uint8_t reduce)
{
    WMix_Point pHead = head, pSrc = src;
    //srcU8Len 计数
    uint32_t count;
    uint8_t *rdce = &wmix->reduceMode, rdce1 = 1;
    //
    if(!wmix || !wmix->run || !pSrc.U8 || srcU8Len < 1)
        return pHead;
    //
    if(!pHead.U8)
        pHead.U8 = wmix->head.U8;
    //
    if(reduce == wmix->reduceMode)
        rdce = &rdce1;
    //---------- 参数一致 直接拷贝 ----------
    if(freq == WMIX_FREQ && 
        channels == WMIX_CHANNELS && 
        sample == WMIX_SAMPLE)
    {
        for(count = 0; count < srcU8Len;)
        {
            //拷贝一帧数据
            *pHead.S16 = volumeAdd(*pHead.S16, *pSrc.S16/(*rdce));
            pHead.S16++;
            pSrc.S16++;
#if(WMIX_CHANNELS == 1)
            //
            count += 2;
#else
            *pHead.S16 = volumeAdd(*pHead.S16, *pSrc.S16/(*rdce));
            pHead.S16++;
            pSrc.S16++;
            //
            count += 4;
#endif
            //循环处理
            if(pHead.U8 >= wmix->end.U8)
                pHead.U8 = wmix->start.U8;
        }
    }
    //---------- 参数不一致 插值拷贝 ----------
    else
    {
        //频率差
        int32_t freqErr = WMIX_FREQ - freq;
        //步差计数 和 步差分量
        float divCount, divPow;
        //音频频率大于默认频率 //--- 重复代码比较多且使用可能极小,为减小函数入栈容量,不写了 ---
        if(freqErr < 0)
        {
            divPow = (float)(-freqErr)/WMIX_FREQ;
            //
            switch(sample)
            {
                case 8:
                    if(channels == 2)
                        ;
                    else if(channels == 1)
                        ;
                    break;
                case 16:
                    if(channels == 2)
                    {
                        for(count = 0, divCount = 0; count < srcU8Len;)
                        {
                            //步差计数已满 跳过帧
                            if(divCount >= 1.0)
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
                                *pHead.S16 = volumeAdd(*pHead.S16, *pSrc.S16/(*rdce));
                                pHead.S16++;
                                pSrc.S16++;
#if(WMIX_CHANNELS != 1)
                                *pHead.S16 = volumeAdd(*pHead.S16, *pSrc.S16/(*rdce));
                                pHead.S16++;
#endif
                                pSrc.S16++;
                                //
                                divCount += divPow;
                                count += 4;
                            }
                            //循环处理
                            if(pHead.U8 >= wmix->end.U8)
                                pHead.U8 = wmix->start.U8;
                        }
                    }
                    else if(channels == 1)
                    {
                        for(count = 0, divCount = 0; count < srcU8Len;)
                        {
                            //步差计数已满 跳过帧
                            if(divCount >= 1.0)
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
                                *pHead.S16 = volumeAdd(*pHead.S16, *pSrc.S16/(*rdce));
                                pHead.S16++;
                                // pSrc.S16++;
#if(WMIX_CHANNELS != 1)
                                *pHead.S16 = volumeAdd(*pHead.S16, *pSrc.S16/(*rdce));
                                pHead.S16++;
#endif
                                pSrc.S16++;
                                //
                                divCount += divPow;
                                count += 2;
                            }
                            //循环处理
                            if(pHead.U8 >= wmix->end.U8)
                                pHead.U8 = wmix->start.U8;
                        }
                    }
                    break;
                case 32:
                    if(channels == 2)
                        ;
                    else if(channels == 1)
                        ;
                    break;
            }
        }
        //音频频率小于等于默认频率
        else
        {
            divPow = (float)freqErr/freq;
            //
            // printf("smallFreq: head = %ld , divPow = %f, divCount = %f, freqErr/%d, freq/%d\n", 
            //     pHead.U8 - wmix->start.U8,
            //     divPow, divCount, freqErr, freq);
            //
            switch(sample)
            {
                //8bit采样 //--- 重复代码比较多且使用可能极小,为减小函数入栈容量,不写了 ---
                case 8:
                    if(channels == 2)
                        ;
                    else if(channels == 1)
                        ;
                    break;
                //16bit采样 //主流的采样方式
                case 16:
                    if(channels == 2)
                    {
                        for(count = 0, divCount = 0; count < srcU8Len;)
                        {
                            //步差计数已满 跳过帧
                            if(divCount >= 1.0)
                            {
                                //循环缓冲区指针继续移动,pSrc指针不动
                                *pHead.S16 = volumeAdd(*pHead.S16, *pSrc.S16/(*rdce));
                                pHead.S16++;
#if(WMIX_CHANNELS != 1)
                                *pHead.S16 = volumeAdd(*pHead.S16, *pSrc.S16/(*rdce));
                                pHead.S16++;
#endif
                                //
                                divCount -= 1.0;
                            }
                            else
                            {
                                //拷贝一帧数据
                                *pHead.S16 = volumeAdd(*pHead.S16, *pSrc.S16/(*rdce));
                                pHead.S16++;
                                pSrc.S16++;
#if(WMIX_CHANNELS != 1)
                                *pHead.S16 = volumeAdd(*pHead.S16, *pSrc.S16/(*rdce));
                                pHead.S16++;
#endif
                                pSrc.S16++;
                                //
                                divCount += divPow;
                                count += 4;
                            }
                            //循环处理
                            if(pHead.U8 >= wmix->end.U8)
                                pHead.U8 = wmix->start.U8;
                        }
                    }
                    else if(channels == 1)
                    {
                        for(count = 0, divCount = 0; count < srcU8Len;)
                        {
                            //
                            if(divCount >= 1.0)
                            {
                                //拷贝一帧数据 pSrc指针不动
                                *pHead.S16 = volumeAdd(*pHead.S16, *pSrc.S16/(*rdce));
                                pHead.S16++;
#if(WMIX_CHANNELS != 1)
                                *pHead.S16 = volumeAdd(*pHead.S16, *pSrc.S16/(*rdce));
                                pHead.S16++;
#endif
                                //
                                divCount -= 1.0;
                            }
                            else
                            {
                                //拷贝一帧数据
                                *pHead.S16 = volumeAdd(*pHead.S16, *pSrc.S16/(*rdce));
                                pHead.S16++;
#if(WMIX_CHANNELS != 1)
                                *pHead.S16 = volumeAdd(*pHead.S16, *pSrc.S16/(*rdce));
                                pHead.S16++;
#endif
                                pSrc.S16++;
                                //
                                divCount += divPow;
                                count += 2;
                            }
                            //循环处理
                            if(pHead.U8 >= wmix->end.U8)
                                pHead.U8 = wmix->start.U8;
                        }
                    }
                    break;
                //32bit采样 //--- 重复代码比较多且使用可能极小,为减小函数入栈容量,不写了 ---
                case 32:
                    if(channels == 2)
                        ;
                    else if(channels == 1)
                        ;
                    break;
            }
        }
    }
    //---------- 修改尾指针 ----------
    if(wmix->vipWrite.U8 == 0)
    {
        if(wmix->tail.U8 < wmix->head.U8)
        {
            if(pHead.U8 < wmix->head.U8 && 
                pHead.U8 > wmix->tail.U8)
                wmix->tail.U8 = pHead.U8;
        }
        else
        {
            if(pHead.U8 < wmix->head.U8)
                wmix->tail.U8 = pHead.U8;
            else if(pHead.U8 > wmix->tail.U8)
                wmix->tail.U8 = pHead.U8;
        }
    }
    else if(head.U8 == wmix->vipWrite.U8)
        wmix->tail.U8 = pHead.U8;
    else
        wmix->tail.U8 = wmix->vipWrite.U8;
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
    uint32_t buffSize, buffSize2;
    WAVContainer_t wav;//wav文件头信息
    WMix_Point src, head;
    uint32_t tick, total = 0, total2 = 0, totalWait;
    uint32_t second = 0, bpsCount = 0;
    double totalPow;
    uint8_t rdce = reduce+1, rdceIsMe = 0;
    uint16_t repeat = (uint16_t)repeatInterval*10;
    //
    WMix_Msg msg;
    //
    uint8_t loopWord;
    loopWord = wmix->loopWord;
    //
    if(!wmix || !wmix->run || !wavPath)
        return;
    //
    if((fd = open(wavPath, O_RDONLY)) <= 0)
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
    if(wmix->debug) printf("<< PLAY-WAV: %s start >>\n   通道数: %d\n   采样位数: %d bit\n   采样率: %d Hz\n   每秒字节: %d Bytes\n   总数据量: %d Bytes\n", 
        wavPath,
        wav.format.channels,
        wav.format.sample_length,
        wav.format.sample_rate,
        wav.format.bytes_p_second,
        wav.chunk.length);
    //独占 reduceMode
    if(rdce > 1 && wmix->reduceMode == 1)
    {
        wmix->reduceMode = rdce;
        rdceIsMe = 1;
    }
    else
        rdce = 1;
    //默认缓冲区大小设为1秒字播放字节数
    buffSize = wav.format.bytes_p_second;
    buffSize2 = WMIX_CHANNELS*WMIX_SAMPLE/8*WMIX_FREQ;
    totalPow = (double)buffSize2/buffSize;
    totalWait = buffSize2/2;
    //把每秒数据包拆得越细, 打断速度越快
    //以下拆包的倍数必须能同时被 wav.format.sample_rate 和 WMIX_FREQ 整除 !!
    if(msg_fd)//在互斥播放模式时才使用
    {
        if(wav.format.sample_rate%4 == 0)
        {
            buffSize /= 4;
            buffSize2 /= 4;
            totalWait = buffSize2;
        }
        else if(wav.format.sample_rate%3 == 0)
        {
            buffSize /= 3;
            buffSize2 /= 3;
            totalWait = buffSize2;
        }
        else
        {
            buffSize /= 2;
            buffSize2 /= 2;
            totalWait = buffSize2/2;
        }
    }
    else
    {
        buffSize /= 2;
        buffSize2 /= 2;
        totalWait = buffSize2/2;
    }
    //
    buff = (uint8_t *)calloc(buffSize, sizeof(uint8_t));
    //
    src.U8 = buff;
    head.U8 = 0;
    tick = wmix->tick;
    //
    while(wmix->run && 
        loopWord == wmix->loopWord)
    {
        //msg 检查
        if(msg_fd){
            if(msgrcv(msg_fd, &msg, 
                WMIX_MSG_BUFF_SIZE, 
                0, IPC_NOWAIT) < 1 && 
                errno != ENOMSG) //消息队列被关闭
                break;
        }
        //播放文件
        ret = read(fd, buff, buffSize);
        if(ret > 0)
        {
            //等播放指针赶上写入进度
            if(total2 > totalWait)
            {
                while(wmix->run && 
                    loopWord == wmix->loopWord &&
                    get_tick_err(wmix->tick, tick) < 
                    total2 - totalWait)
                    delayus(10000);
                if(!wmix->run || loopWord != wmix->loopWord)
                    break;
            }
            //写入循环缓冲区
            head = wmix_load_wavStream(
                wmix, 
                src, ret, 
                wav.format.sample_rate, 
                wav.format.channels, 
                wav.format.sample_length, head, rdce);
            //写入的总字节数统计
            bpsCount += ret;
            total += ret;
            total2 = total*totalPow;
            //播放时间
            if(bpsCount > wav.format.bytes_p_second)
            {
                bpsCount -= wav.format.bytes_p_second;
                second = total/wav.format.bytes_p_second;
                if(wmix->debug) printf("  PLAY-WAV: %s %02d:%02d\n", wavPath, second/60, second%60);
            }
            //
            if(head.U8 == 0)
                break;
        }
        else if(repeat)
        {
            //关闭 reduceMode
            if(rdceIsMe && wmix->reduceMode == rdce)
                wmix->reduceMode = 1;
            //
            lseek(fd, 44, SEEK_SET);
            //
            for(ret = 0; ret < repeat; ret++)
            {
                delayus(100000);
                //
                if(!wmix->run || loopWord != wmix->loopWord)
                    break;
                //
                if(msg_fd){
                    if(msgrcv(msg_fd, &msg, 
                        WMIX_MSG_BUFF_SIZE, 
                        0, IPC_NOWAIT) < 1 && 
                        errno != ENOMSG) //消息队列被关闭
                        break;
                }
            }
            //
            if(ret != repeat){
                ret = -1;
                break;
            }
            //重启 reduceMode
            if(rdceIsMe && wmix->reduceMode == 1)
                wmix->reduceMode = rdce;
            //
            if(wmix->debug) printf("<< PLAY-WAV: %s start >>\n   通道数: %d\n   采样位数: %d bit\n   采样率: %d Hz\n   每秒字节: %d Bytes\n   总数据量: %d Bytes\n", 
                wavPath,
                wav.format.channels,
                wav.format.sample_length,
                wav.format.sample_rate,
                wav.format.bytes_p_second,
                wav.chunk.length);
            //
            total = total2 = bpsCount = 0;
            src.U8 = buff;
            head.U8 = wmix->head.U8;
            tick = wmix->tick;
        }
        else
            break;
    }
    //
    close(fd);
    free(buff);
    //
    if(wmix->debug) printf(">> PLAY-WAV: %s end <<\n", wavPath);
    //关闭 reduceMode
    if(rdceIsMe && wmix->reduceMode == rdce)
        wmix->reduceMode = 1;
}

#if(WMIX_MODE==1)
void wmix_load_aac(
    WMix_Struct *wmix,
    char *aacPath,
    int msg_fd,
    uint8_t reduce,
    uint8_t repeatInterval)
{
    int fd = 0;
    ssize_t ret = 0;
    uint8_t *buff = NULL;
    uint32_t buffSize, buffSize2;
    WMix_Point src, head;
    uint32_t tick, total = 0, total2 = 0, totalWait;
    uint32_t second = 0, bpsCount = 0;
    double totalPow;
    uint8_t rdce = reduce+1, rdceIsMe = 0;
    uint16_t repeat = (uint16_t)repeatInterval*10;
    //
    WMix_Msg msg;
    //
    uint8_t loopWord;
    //
    void *aacDecFd = NULL;
    unsigned char aacBuff[2048];
    int readLen, datUse = 0, retLen, fileEnd = 0;
    //
    uint8_t chn;
    uint8_t sample;
    uint16_t freq;
    uint16_t port;
    uint32_t bytes_p_second;
    //
    loopWord = wmix->loopWord;
    //
    if(!wmix || !wmix->run || !aacPath)
        return;
    //
    if((fd = open(aacPath, O_RDONLY)) <= 0)
    {
        fprintf(stderr, "wmix_load_wav: %s open err\n", aacPath);
        return;
    }
    //初始化解码器
    aacDecFd = hiaudio_aacDec_init();
    if(!aacDecFd)
    {
        fprintf(stderr, "hiaudio_aacDec_init: err\n");
        close(fd);
        return;
    }
    //读取数据并解析格式
    readLen = read(fd, aacBuff, sizeof(aacBuff));
    buff = (uint8_t *)calloc(4096, sizeof(uint8_t));
    retLen = hiaudio_aacDec(
        aacDecFd, 
        &chn, 
        &sample, 
        &freq, 
        &datUse, aacBuff, readLen, buff);
    if(retLen < 1)
    {
        fprintf(stderr, "hiaudio_aacDec: err\n");
        hiaudio_aacDec_deinit(aacDecFd);
        close(fd);
        return;
    }
    //
    bytes_p_second = chn*sample/8*freq;
    //
    if(wmix->debug) printf("<< PLAY-AAC: %s start >>\n   通道数: %d\n   采样位数: %d bit\n   采样率: %d Hz\n   每秒字节: %d Bytes\n   总数据量: -- Bytes\n", 
        aacPath,
        chn,
        sample,
        freq,
        bytes_p_second);
    //独占 reduceMode
    if(rdce > 1 && wmix->reduceMode == 1)
    {
        wmix->reduceMode = rdce;
        rdceIsMe = 1;
    }
    else
        rdce = 1;
    //默认缓冲区大小设为1秒字播放字节数
    buffSize = bytes_p_second;
    buffSize2 = WMIX_CHANNELS*WMIX_SAMPLE/8*WMIX_FREQ;
    totalPow = (double)buffSize2/buffSize;
    //
    buffSize = chn*sample/8*1024;
    buffSize2 = WMIX_CHANNELS*WMIX_SAMPLE/8*1024;
    totalWait = buffSize2*4;
    //
    src.U8 = buff;
    head.U8 = 0;
    tick = wmix->tick;
    //
    while(wmix->run && 
        loopWord == wmix->loopWord)
    {
        //msg 检查
        if(msg_fd){
            if(msgrcv(msg_fd, &msg, 
                WMIX_MSG_BUFF_SIZE, 
                0, IPC_NOWAIT) < 1 && 
                errno != ENOMSG) //消息队列被关闭
                break;
        }
        //播放文件
        if(retLen > 0)
        {
            //等播放指针赶上写入进度
            if(total2 > totalWait)
            {
                while(wmix->run && 
                    loopWord == wmix->loopWord &&
                    get_tick_err(wmix->tick, tick) < 
                    total2 - totalWait)
                    delayus(10000);
                if(!wmix->run || loopWord != wmix->loopWord)
                    break;
            }
            //写入循环缓冲区
            head = wmix_load_wavStream(
                wmix, 
                src, retLen, 
                freq, 
                chn, 
                sample, head, rdce);
            //写入的总字节数统计
            bpsCount += retLen;
            total += retLen;
            total2 = total*totalPow;
            //播放时间
            if(bpsCount > bytes_p_second)
            {
                bpsCount -= bytes_p_second;
                second = total/bytes_p_second;
                if(wmix->debug) printf("  PLAY-AAC: %s %02d:%02d\n", aacPath, second/60, second%60);
            }
            //
            if(head.U8 == 0)
                break;
        }
        else if(repeat)
        {
            //关闭 reduceMode
            if(rdceIsMe && wmix->reduceMode == rdce)
                wmix->reduceMode = 1;
            //
            lseek(fd, 0, SEEK_SET);
            readLen = 0;
            datUse = 0;
            fileEnd = 0;
            //
            for(ret = 0; ret < repeat; ret++)
            {
                delayus(100000);
                //
                if(!wmix->run || loopWord != wmix->loopWord)
                    break;
                //
                if(msg_fd){
                    if(msgrcv(msg_fd, &msg, 
                        WMIX_MSG_BUFF_SIZE, 
                        0, IPC_NOWAIT) < 1 && 
                        errno != ENOMSG) //消息队列被关闭
                        break;
                }
            }
            //
            if(ret != repeat){
                ret = -1;
                break;
            }
            //重启 reduceMode
            if(rdceIsMe && wmix->reduceMode == 1)
                wmix->reduceMode = rdce;
            //
            if(wmix->debug) printf("<< PLAY-AAC: %s start >>\n   通道数: %d\n   采样位数: %d bit\n   采样率: %d Hz\n   每秒字节: %d Bytes\n   总数据量: -- Bytes\n", 
                aacPath,
                chn,
                sample,
                freq,
                bytes_p_second);
            //
            total = total2 = bpsCount = 0;
            src.U8 = buff;
            head.U8 = wmix->head.U8;
            tick = wmix->tick;
        }
        else
            break;
        //缓冲区aacBuff数据左移
        if(datUse <= readLen)
        {
            readLen -= datUse;
            memcpy(aacBuff, &aacBuff[datUse], readLen);
            datUse = 0;
        }
        //继续往aacBuff读入数据
        if(!fileEnd)
        {
            ret = read(fd, &aacBuff[readLen], sizeof(aacBuff)-readLen);
            if(ret > 0)
                readLen += ret;
            else
                fileEnd = 1;
        }
        //aacBuff还有数据则继续解码
        if(readLen > 0)
        {
            retLen = hiaudio_aacDec(
                aacDecFd, 
                &chn, 
                &sample, 
                &freq, 
                &datUse, aacBuff, readLen, buff);
        }
        else
            retLen = -1;
    }
    //
    close(fd);
    free(buff);
    hiaudio_aacDec_deinit(aacDecFd);
    //
    if(wmix->debug) printf(">> PLAY-AAC: %s end <<\n", aacPath);
    //关闭 reduceMode
    if(rdceIsMe && wmix->reduceMode == rdce)
        wmix->reduceMode = 1;
}
#endif

#if(WMIX_MP3)
typedef struct{
    // char *msgPath;//消息队列挂靠路径
    char *mp3Path;
    //
    WMix_Msg msg;
    // key_t msg_key;
    int msg_fd;
    //
    void *fdm;//mmap首地址
    uint32_t seek;//fdm跳过多少才到mp3数据段
    uint32_t size;//实际mp3数据段长度
    //
    WMix_Point head, src;//同步循环缓冲区的head指针 和 指向数据的src指针
    WMix_Struct *wmix;
    //
    uint32_t tick;//循环缓冲区head指针走过了多少字节,用于防止数据写如缓冲区太快超过head指针
    uint8_t loopWord;//每个播放线程的循环标志都要与该值一致,否则循环结束,用于打断全局播放
    uint32_t total, total2, totalWait;//total:文件读取字节数 total2:等比例转换为输出格式后的字节数
    double totalPow;// totalPow = total2/total; total2 = total*totalPow
    //
    uint32_t bps, bpsCount;//bps:每秒字节数 bpsCount计数用于计算当前播放时间
    //
    uint8_t rdce;//reduce
    uint16_t repeat;//repeatInterval*10
    uint8_t rdceIsMe;
}WMix_Mp3;

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
    int16_t *val = (int16_t*)&pcm->samples[0][count];
    //
    if(!wmm->wmix->run || wmm->loopWord != wmm->wmix->loopWord)
        return MAD_FLOW_STOP;
    //参数初始化
    if(wmm->head.U8 == 0)
    {
        wmm->bps = pcm->channels*16/8*header->samplerate;
        wmm->totalPow = (double)(WMIX_CHANNELS*WMIX_SAMPLE/8*WMIX_FREQ)/wmm->bps;
        wmm->totalWait = wmm->bps*wmm->totalPow/3;
        //
        if(wmm->wmix->debug) printf("<< PLAY-MP3: %s start >>\n   通道数: %d\n   采样位数: %d bit\n   采样率: %d Hz\n   每秒字节: %d Bytes\n   总数据量: -- Bytes\n", 
            wmm->mp3Path,
            pcm->channels, 16,
            header->samplerate,
            wmm->bps);
        //
        wmm->total = wmm->total2 = wmm->bpsCount = 0;
        wmm->head.U8 = 0;
        wmm->tick = wmm->wmix->tick;
    }
    //msg 检查
    if(wmm->msg_fd){
        if(msgrcv(wmm->msg_fd, 
            &wmm->msg, 
            WMIX_MSG_BUFF_SIZE, 
            0, IPC_NOWAIT) < 1 && 
            errno != ENOMSG) //消息队列被关闭
            return MAD_FLOW_STOP;
    }
    //
    if(pcm->channels == 2)
    {
        for(count = 0; count < pcm->length; count++)
        {
            *val++ = mad_scale(pcm->samples[0][count]);
            *val++ = mad_scale(pcm->samples[1][count]);
        }
        count = pcm->length*4;
    }
    else if(pcm->channels == 1)
    {
        for(count = 0; count < pcm->length; count++)
        {
            *val++ = mad_scale(pcm->samples[0][count]);
            // *val++ = mad_scale(pcm->samples[1][count]);
        }
        count = pcm->length*2;
    }
    else
        return MAD_FLOW_STOP;
    //等待消化
    if(wmm->total2 > wmm->totalWait)
    {
        while(wmm->wmix->run && 
            wmm->loopWord == wmm->wmix->loopWord &&
            get_tick_err(wmm->wmix->tick, wmm->tick) < 
            wmm->total2 - wmm->totalWait)
            delayus(10000);
        if(!wmm->wmix->run || wmm->loopWord != wmm->wmix->loopWord)
            return MAD_FLOW_STOP;
    }
    //写入到循环缓冲区
    wmm->src.U8 = (uint8_t*)&pcm->samples[0][0];
    wmm->head = wmix_load_wavStream(
        wmm->wmix, 
        wmm->src, 
        count,
        header->samplerate,
        pcm->channels,
        16,
        wmm->head, wmm->rdce);
    //总字节数计数
    wmm->bpsCount += count;
    wmm->total += count;
    wmm->total2 = wmm->total*wmm->totalPow;
    //播放时间
    if(wmm->bpsCount > wmm->bps)
    {
        wmm->bpsCount -= wmm->bps;
        second = wmm->total/wmm->bps;
        if(wmm->wmix->debug) printf("  PLAY-MP3: %s %02d:%02d\n", wmm->mp3Path, second/60, second%60);
    }
    //
    if(wmm->head.U8 == 0)
        return MAD_FLOW_STOP;
    //
    return MAD_FLOW_CONTINUE;
}

enum mad_flow mad_input(void *data, struct mad_stream *stream)
{
    WMix_Mp3 *wmm = data;
    uint8_t count;
    if(wmm->size > 0)
    {
        mad_stream_buffer(stream, wmm->fdm+wmm->seek, wmm->size);
        //
        if(wmm->repeat)
        {
            if(wmm->head.U8)//已经播放完一遍了
            {
                //关闭 reduceMode
                if(wmm->rdceIsMe && wmm->wmix->reduceMode == wmm->rdce)
                    wmm->wmix->reduceMode = 1;
                //
                for(count = 0; count < wmm->repeat; count++)
                {
                    delayus(100000);
                    //
                    if(!wmm->wmix->run || wmm->loopWord != wmm->wmix->loopWord)
                        return MAD_FLOW_STOP;
                    //msg 检查
                    if(wmm->msg_fd){
                        if(msgrcv(wmm->msg_fd, 
                            &wmm->msg, 
                            WMIX_MSG_BUFF_SIZE, 
                            0, IPC_NOWAIT) < 1 && 
                            errno != ENOMSG) //消息队列被关闭
                            return MAD_FLOW_STOP;
                    }
                }
                //重启 reduceMode
                if(wmm->rdceIsMe && wmm->wmix->reduceMode == 1)
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
    wmm.repeat = (uint16_t)repeatInterval*10;
    wmm.loopWord = wmix->loopWord;
    wmm.rdceIsMe = 0;
    wmm.rdce = reduce+1;
    wmm.msg_fd = msg_fd;
    //
    if((fd = open(mp3Path, O_RDONLY)) <= 0){
        fprintf(stderr, "wmix_load_mp3: open %s err\n", mp3Path);
        return;
    }
    if(fstat(fd, &sta) == -1 || sta.st_size == 0){
        fprintf(stderr, "wmix_load_mp3: stat %s err\n", mp3Path);
        close(fd);
        return;
    }
    
    //跳过id3标签
    wmm.seek = id3_len(mp3Path);
    wmm.size = sta.st_size - wmm.seek;

    //
    wmm.fdm = mmap(0, sta.st_size, PROT_READ, MAP_SHARED, fd, 0);
    if(wmm.fdm == MAP_FAILED || !wmm.fdm){
        fprintf(stderr, "wmix_load_mp3: mmap err\n");
        close(fd);
        return;
    }
    
    //独占 reduceMode
    if(wmm.rdce > 1 && wmix->reduceMode == 1)
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
    if(wmm.rdceIsMe && wmix->reduceMode == wmm.rdce)
        wmix->reduceMode = 1;

    //
    close(fd);
    munmap(wmm.fdm, sta.st_size);
    //
    if(wmix->debug) printf(">> PLAY-MP3: %s end <<\n", mp3Path);
}
#endif //#if(WMIX_MP3)

//--------------- wmix main ---------------

void help(char *argv0)
{
    printf(
        "\n"
        "Usage: %s [option]\n"
        "\n"
        "Opition:\n"
        "  -d : 显示debug信息\n"
        "  -v -? --help : 显示帮助\n"
        "\n"
        "软件版本: %s\n"
        "\n"
        "Example:\n"
        "  %s &\n"
        "\n"
        ,argv0, WMIX_VERSION, argv0);
}

#if(WMIX_MERGE_MODE==1)
void wmix_start()
{
    main_wmix = wmix_init();
}
#else
int main(int argc, char **argv)
{
    if(argc > 1 && 
        (strstr(argv[1], "-v") || 
        strstr(argv[1], "-?") ||
        strstr(argv[1], "help")))
    {
        help(argv[0]);
        return 0;
    }

    main_wmix = wmix_init();

    if(main_wmix)
    {
        if(argc > 1)
        {
            int i;
            char *p, *path = NULL;
            for(i = 1; i < argc; i++)
            {
                p = argv[i];

                if(strlen(p) == 2 && strstr(p, "-d"))
                    main_wmix->debug = true;
                else if(strstr(p, ".wav") ||
                    strstr(p, ".mp3") ||
                    strstr(p, ".aac"))
                    path = p;
            }
            //
            if(path)
            {
                wmix_throwOut_thread(main_wmix, 
                    3, path, 
                    WMIX_MSG_BUFF_SIZE, 
                    &wmix_load_audio_thread);
            }
        }

        sleep(1);
        while(1)
        {
            //--- 重启 ---
            if(main_wmix->run == 0 && 
                main_wmix->thread_sys == 0 && 
                main_wmix->thread_record == 0 && 
                main_wmix->thread_play == 0)
            {
                main_wmix->run = 1;
                wmix_throwOut_thread(main_wmix, 0, NULL, 0, &wmix_msg_thread);
                wmix_throwOut_thread(main_wmix, 0, NULL, 0, &wmix_play_thread);
            }
            delayus(100000);
        }
    }
    return 0;
}
#endif
