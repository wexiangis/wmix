/*
 *  平台对接文件
 */
#include <stdio.h>
#include <stdint.h>
#include <alsa/asoundlib.h>

typedef struct SNDPCMContainer
{
    snd_pcm_t *handle;
    snd_output_t *log;
    snd_pcm_format_t format;
    int chn;
    int frame_size; //每帧字节数(chn*2字节)
    int data_size;  //每次写入字节数
    uint8_t *data;
    long volume;     //录播音音量
} SNDPCMContainer_t;

int SNDWAV_SetParams(SNDPCMContainer_t *obj, int freq, int chn, int sample)
{
    snd_pcm_hw_params_t *hwparams;
    uint32_t exact_rate;
    uint32_t buffer_time, period_time;

    snd_pcm_uframes_t period_size; //每次写入帧数
    snd_pcm_uframes_t buffer_size; //缓冲区大小

    // 分配snd_pcm_hw_params_t结构体tack.
    snd_pcm_hw_params_alloca(&hwparams);
    // 初始化hwparams
    if (snd_pcm_hw_params_any(obj->handle, hwparams) < 0)
    {
        fprintf(stderr, "Error snd_pcm_hw_params_any\r\n");
        return -1;
    }
    //初始化访问权限
    if (snd_pcm_hw_params_set_access(obj->handle, hwparams, SND_PCM_ACCESS_RW_INTERLEAVED) < 0)
    {
        fprintf(stderr, "Error snd_pcm_hw_params_set_access\r\n");
        return -1;
    }

    // 初始化采样格式,16位 Set sample format
    if (sample == 8)
        obj->format = SND_PCM_FORMAT_S8;
    else if (sample == 16)
        obj->format = SND_PCM_FORMAT_S16_LE;
    else if (sample == 24)
        obj->format = SND_PCM_FORMAT_S24_LE;
    else if (sample == 32)
        obj->format = SND_PCM_FORMAT_S32_LE;
    else
        return -1;
    if (snd_pcm_hw_params_set_format(obj->handle, hwparams, obj->format) < 0)
    {
        fprintf(stderr, "Error snd_pcm_hw_params_set_format\r\n");
        return -1;
    }

    // 设置通道数量 Set number of channels
    if (snd_pcm_hw_params_set_channels(obj->handle, hwparams, chn) < 0)
    {
        fprintf(stderr, "Error snd_pcm_hw_params_set_channels\r\n");
        return -1;
    }
    obj->chn = chn;

    //设置采样率，如果硬件不支持我们设置的采样率，将使用最接近的
    exact_rate = freq;
    if (snd_pcm_hw_params_set_rate_near(obj->handle, hwparams, &exact_rate, 0) < 0)
    {
        fprintf(stderr, "Error snd_pcm_hw_params_set_rate_near\r\n");
        return -1;
    }
    if (freq != exact_rate)
    {
        fprintf(stderr, "The rate %d Hz is not supported by your hardware.\n ==> Using %d Hz instead.\r\n",
                freq, exact_rate);
    }

    if (snd_pcm_hw_params_get_buffer_time_max(hwparams, &buffer_time, 0) < 0)
    {
        fprintf(stderr, "Error snd_pcm_hw_params_get_buffer_time_max\r\n");
        return -1;
    }

    //ubuntu下该值会非常大,需限制
    if (buffer_time > 500000)
        buffer_time = 500000;
    period_time = buffer_time / 4;

    // buffer_time = 371519; //atmel 44100,22050,11025
    // buffer_time = WMIX_INTERVAL_MS * 1000 * 2;
    // period_time = period_time * 8;

    if (snd_pcm_hw_params_set_buffer_time_near(obj->handle, hwparams, &buffer_time, 0) < 0)
    {
        fprintf(stderr, "Error snd_pcm_hw_params_set_buffer_time_near\r\n");
        return -1;
    }

    if (snd_pcm_hw_params_set_period_time_near(obj->handle, hwparams, &period_time, 0) < 0)
    {
        fprintf(stderr, "Error snd_pcm_hw_params_set_period_time_near\r\n");
        return -1;
    }

    snd_pcm_hw_params_get_period_size(hwparams, &period_size, 0);
    snd_pcm_hw_params_get_buffer_size(hwparams, &buffer_size);
    if (period_size == buffer_size)
    {
        fprintf(stderr, "Can't use period equal to buffer size (%lu == %lu)\r\n", period_size, buffer_size);
        return -1;
    }

    // Set hw params
    if (snd_pcm_hw_params(obj->handle, hwparams) < 0)
    {
        fprintf(stderr, "Error snd_pcm_hw_params(handle, params)\r\n");
        return -1;
    }

    obj->frame_size = snd_pcm_format_physical_width(obj->format) * chn / 8;
    obj->data_size = period_size * obj->frame_size;

    printf("\n---- SNDWAV_SetParams -----\r\n"
           "  chn: %d \r\n"
           "  sample: %d bit\r\n"
           "  frame_size: %d bytes \r\n" //每帧字节数
           "  data_size: %d bytes \r\n", //每次写入字节数
           chn, sample,
           obj->frame_size,
           obj->data_size);

    // Allocate audio data buffer
    // if (chn == 1)
    //     obj->data = (uint8_t *)malloc(obj->data_size * 2 + 1);
    // else
        obj->data = (uint8_t *)malloc(obj->data_size + 1);
    if (!obj->data)
    {
        fprintf(stderr, "Error malloc: [data]\r\n");
        return -1;
    }

    return 0;
}

void alsa_ao_vol_set(void *objAo, int vol)
{
    snd_mixer_t *mixer;
    snd_mixer_elem_t *pcm_element;
    SNDPCMContainer_t *obj = (SNDPCMContainer_t *)objAi;
    //打底音量
    const int alsa_ao_base = 5;
    //范围限制
    if (vol > 10)
        obj->volume = 10;
    else if (vol < 0)
        obj->volume = 0;
    else
        obj->volume = vol;
    //初始化
    snd_mixer_open(&mixer, 0);
    snd_mixer_attach(mixer, "default");
    snd_mixer_selem_register(mixer, NULL, NULL);
    snd_mixer_load(mixer);
    //找到Pcm对应的element
    pcm_element = snd_mixer_first_elem(mixer);
    //设置音量范围,最大：0-10
    snd_mixer_selem_set_playback_volume_range(
        pcm_element, 0, 10 + alsa_ao_base);
    //设置左右声道音量
    snd_mixer_selem_set_playback_volume_all(
        pcm_element, obj->volume == 0 ? 0 : obj->volume + alsa_ao_base);
    //检查设置
    snd_mixer_selem_get_playback_volume(
        pcm_element, SND_MIXER_SCHN_FRONT_LEFT, &obj->volume);
    //处理事件
    snd_mixer_handle_events(mixer);
    snd_mixer_close(mixer);
}

void alsa_ai_vol_set(void *objAi, int vol)
{
    snd_mixer_t *mixer;
    snd_mixer_elem_t *pcm_element;
    SNDPCMContainer_t *obj = (SNDPCMContainer_t *)objAi;
    //范围限制
    if (vol > 10)
        obj->volume = 10;
    else if (vol < 0)
        obj->volume = 0;
    else
        obj->volume = vol;
    //初始化
    snd_mixer_open(&mixer, 0);
    snd_mixer_attach(mixer, "default");
    snd_mixer_selem_register(mixer, NULL, NULL);
    snd_mixer_load(mixer);
    //找到Pcm对应的element
    pcm_element = snd_mixer_first_elem(mixer);                    // 取得第一个 element，也就是 Master
    snd_mixer_selem_set_capture_volume_range(pcm_element, 0, 10); // 设置音量范围：0-10之间
    //设置左右声道音量
    snd_mixer_selem_set_capture_volume_all(pcm_element, obj->volume);
    //检查设置
    snd_mixer_selem_get_capture_volume(pcm_element, SND_MIXER_SCHN_FRONT_LEFT, &obj->volume);
    //处理事件
    snd_mixer_handle_events(mixer);
    snd_mixer_close(mixer);
}

int alsa_ao_vol_get(void *objAo)
{
    return ((SNDPCMContainer_t *)objAo)->volume;
}

int alsa_ai_vol_get(void *objAi)
{
    return ((SNDPCMContainer_t *)objAi)->volume;
}

static SNDPCMContainer_t *_alsa_init(int channels, int sample, int freq, char p_or_c)
{
    char devicename[] = "default";
    SNDPCMContainer_t *playback = (SNDPCMContainer_t *)calloc(1, sizeof(SNDPCMContainer_t));

    //Creates a new output object using an existing stdio \c FILE pointer.
    if (snd_output_stdio_attach(&playback->log, stderr, 0) < 0)
    {
        fprintf(stderr, "Error snd_output_stdio_attach\r\n");
        goto Err;
    }
    // 打开PCM，最后一个参数为0意味着标准配置 SND_PCM_ASYNC
    if (snd_pcm_open(
            &playback->handle,
            devicename,
            p_or_c == 'c' ? SND_PCM_STREAM_CAPTURE : SND_PCM_STREAM_PLAYBACK, 0) < 0)
    {
        fprintf(stderr, "Error snd_pcm_open [ %s]\r\n", devicename);
        goto Err;
    }
    //配置PCM参数
    if (SNDWAV_SetParams(playback, freq, channels, sample) < 0)
    {
        fprintf(stderr, "Error set_snd_pcm_params\r\n");
        goto Err;
    }
    snd_pcm_dump(playback->handle, playback->log);
    //默认音量
    if (p_or_c == 'c')
        alsa_ai_vol_set(playback, 10);
    else
        alsa_ao_vol_set(playback, 10);
    return playback;

Err:

    if (playback->data)
        free(playback->data);
    if (playback->log)
        snd_output_close(playback->log);
    if (playback->handle)
        snd_pcm_close(playback->handle);
    free(playback);

    return NULL;
}

void *alsa_ao_init(int chn, int freq)
{
    return _alsa_init(chn, 16, freq, 'p');
}
void *alsa_ai_init(int chn, int freq)
{
    return _alsa_init(chn, 16, freq, 'c');
}

int alsa_ao_write(void *objAo, uint8_t *data, int len)
{
    SNDPCMContainer_t *obj = (SNDPCMContainer_t *)objAo;
    int ret;
    int result = 0;
    int frame_num = len / obj->frame_size;

    while (frame_num > 0)
    {
        ret = snd_pcm_writei(obj->handle, data, frame_num);
        //返回异常,recover处理
        if (ret < 0)
            ret = snd_pcm_recover(obj->handle, ret, 0);

        if (ret == -EAGAIN || (ret >= 0 && (size_t)ret < frame_num))
        {
            snd_pcm_wait(obj->handle, 1000);
        }
        else if (ret == -EPIPE)
        {
            snd_pcm_prepare(obj->handle);
            // fprintf(stderr, "W-Error: Buffer Underrun\r\n");
        }
        else if (ret == -ESTRPIPE)
        {
            fprintf(stderr, "W-Error: Need suspend\r\n");
        }
        else if (ret < 0)
        {
            fprintf(stderr, "W-Error snd_pcm_writei: [%s]", snd_strerror(ret));
            return -1;
        }

        if (frame_num < ret)
            break;

        if (ret > 0)
        {
            result += ret;
            frame_num -= ret;
            data += ret * obj->frame_size;
        }
    }
    //返回实际处理字节数
    return result * obj->frame_size;
}

int alsa_ai_read(void *objAi, uint8_t *data, int len)
{
    SNDPCMContainer_t *obj = (SNDPCMContainer_t *)objAi;
    int ret;
    int result = 0;
    int frame_num = len / obj->frame_size;

    while (frame_num > 0)
    {
        ret = snd_pcm_readi(obj->handle, data, frame_num);
        //返回异常,recover处理
        if (ret < 0)
            ret = snd_pcm_recover(obj->handle, ret, 0);
        //其它问题处理
        if (ret == -EAGAIN || (ret >= 0 && (size_t)ret < frame_num))
        {
            snd_pcm_wait(obj->handle, 1000);
        }
        else if (ret == -EPIPE)
        {
            snd_pcm_prepare(obj->handle);
            // fprintf(stderr, "R-Error: Buffer Underrun\r\n");
        }
        else if (ret == -ESTRPIPE)
        {
            fprintf(stderr, "R-Error: Need suspend\r\n");
        }
        else if (ret < 0)
        {
            fprintf(stderr, "R-Error: SNDWAV_ReadPcm: [%s]\r\n", snd_strerror(ret));
            return -1;
        }
        //帧读够了
        if (frame_num < ret)
            break;
        //帧计数
        if (ret > 0)
        {
            result += ret;
            frame_num -= ret;
            //按实际读取的帧数移动 uint8 数据指针
            data += ret * obj->frame_size;
        }
    }
    //返回实际处理字节数
    return result * obj->frame_size;
}

static void _alsa_exit(SNDPCMContainer_t *playback)
{
    if (playback)
    {
        snd_pcm_drain(playback->handle);
        if (playback->data)
            free(playback->data);
        if (playback->log)
            snd_output_close(playback->log);
        if (playback->handle)
            snd_pcm_close(playback->handle);
        free(playback);
    }
}
void alsa_ao_exit(void *objAo)
{
    _alsa_exit((SNDPCMContainer_t *)objAo);
}
void alsa_ai_exit(void *objAi)
{
    _alsa_exit((SNDPCMContainer_t *)objAi);
}
