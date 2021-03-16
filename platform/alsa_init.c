#include <stdio.h>
#include <stdint.h>
#include <alsa/asoundlib.h>

const int alsa_ao_base = 30;
static int alsa_ao_volume = 100;
static int alsa_ai_volume = 100;

void *alsa_ao_init(int chn, int freq)
{
    return NULL;
}
void *alsa_ai_init(int chn, int freq)
{
    return NULL;
}

int alsa_ao_write(void *objAo, uint8_t *data, int len)
{
    return 0;
}
int alsa_ai_read(void *objAi, uint8_t *data, int len)
{
    return 0;
}

void alsa_ao_vol_set(void *objAo, int vol)
{
    snd_mixer_t *mixer;
    snd_mixer_elem_t *pcm_element;
    //范围限制
    if (vol > 100)
        alsa_ao_volume = 100;
    else if (vol < 0)
        alsa_ao_volume = 0;
    else
        alsa_ao_volume = vol;
    //初始化
    snd_mixer_open(&mixer, 0);
    snd_mixer_attach(mixer, "default");
    snd_mixer_selem_register(mixer, NULL, NULL);
    snd_mixer_load(mixer);
    //找到Pcm对应的element
    pcm_element = snd_mixer_first_elem(mixer);
    snd_mixer_selem_set_playback_volume_range(pcm_element, 0, 100 + alsa_ao_base); //设置音量范围,最大：0-100
    //设置左右声道音量
    snd_mixer_selem_set_playback_volume_all(pcm_element, alsa_ao_volume == 0 ? 0 : alsa_ao_volume + alsa_ao_base);
    //检查设置
    snd_mixer_selem_get_playback_volume(pcm_element, SND_MIXER_SCHN_FRONT_LEFT, &alsa_ao_volume); //获取音量
    //处理事件
    snd_mixer_handle_events(mixer);
    snd_mixer_close(mixer);
}
void alsa_ai_vol_set(void *objAi, int vol)
{
    snd_mixer_t *mixer;
    snd_mixer_elem_t *pcm_element;
    //范围限制
    if (vol > 100)
        alsa_ai_volume = 100;
    else if (vol < 0)
        alsa_ai_volume = 0;
    else
        alsa_ai_volume = vol;
    //初始化
    snd_mixer_open(&mixer, 0);
    snd_mixer_attach(mixer, "default");
    snd_mixer_selem_register(mixer, NULL, NULL);
    snd_mixer_load(mixer);
    //找到Pcm对应的element
    pcm_element = snd_mixer_first_elem(mixer);                     // 取得第一个 element，也就是 Master
    snd_mixer_selem_set_capture_volume_range(pcm_element, 0, 100); // 设置音量范围：0-100之间
    //设置左右声道音量
    snd_mixer_selem_set_capture_volume_all(pcm_element, alsa_ai_volume);
    //检查设置
    snd_mixer_selem_get_capture_volume(pcm_element, SND_MIXER_SCHN_FRONT_LEFT, &alsa_ai_volume); //获取音量
    //处理事件
    snd_mixer_handle_events(mixer);
    snd_mixer_close(mixer);
}

int alsa_ao_vol_get(void *objAo)
{
    return alsa_ao_volume;
}
int alsa_ai_vol_get(void *objAi)
{
    return alsa_ai_volume;
}

void alsa_ao_exit(void *objAo)
{
    ;
}
void alsa_ai_exit(void *objAi)
{
    ;
}
