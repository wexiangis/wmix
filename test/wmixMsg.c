
/**************************************************
 * 
 *  wmix客户端，用于命令行音频操作
 * 
 *  支持录音、播音、音量调节、log开关、rtp收发测试等等
 * 
 **************************************************/
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "wmix_user.h"

void help(char *argv0)
{
    printf(
        "\n"
        "Usage: %s [option] audioFile\n"
        "\n"
        "Opition:\n"
        "  -l : 排队模式,排到最后一位(默认模式)\n"
        "  -i : 排队模式,排到第一位\n"
        "  -m : 混音模式(不设置时为排队模式)\n"
        "  -b : 打断模式(不设置时为排队模式)\n"
        "  -t interval : 循环播放模式,间隔秒,取值[1~255]\n"
        "  -d reduce : 背景音削减倍数,取值[1~255]\n"
        "  -v volume : 音量设置0~10\n"
        "  -vr volume : 录音音量设置0~10\n"
        "  -va volumeAgc : 录音音量增益设置0~100, 启用 -agc 时有效\n"
        "  -k id : 关闭指定id的语音,id=0时关闭所有\n"
        "  -r : 录音模式,wav格式(默认单通道/16bits/8000Hz/5秒)\n"
        "  -raac : 录音模式,aac格式(默认单通道/16bits/8000Hz/5秒)\n"
        "  -rc chn : 指定录音通道数[1,2]\n"
        "  -rr freq : 指定录音频率[8000,11025,16000,22050,32000,44100]\n"
        "  -rt time : 指定录音时长秒\n"
        "  -rtps ip port : 启动rtp录音发送,使用-rc,-rr可以配置通道和频率参数\n"
        "                : 生成/tmp/record.sdp\n"
        "  -rtpr ip port : 启动rtp接收播音,使用-rc,-rr可以配置通道和频率参数\n"
        "  -log 0/1 : 关闭/显示log\n"
        "  -reset : 重置混音器\n"
        "  -vad 0/1 : 关/开 webrtc.vad 人声识别,录音辅助,在没人说话时主动静音\n"
        "  -aec 0/1 : 关/开 webrtc.aec 回声消除\n"
        "  -ns 0/1 : 关/开 webrtc.ns 噪音抑制(录音)\n"
        "  -ns_pa 0/1 : 关/开 webrtc.ns 噪音抑制(播音)\n"
        "  -agc 0/1 : 关/开 webrtc.agc 自动增益\n"
        "  -rw 0/1 : 关/开 自收发测试\n"
        "  -info : 打印信息\n"
        "  -? --help : 显示帮助\n"
        "\n"
        "Return:\n"
        "  0/OK <0/ERROR >0/id use to \"-k id\"\n"
        "\n"
        "Version: %s\n"
        "\n"
        "Example:\n"
        "  %s -v 10\n"
        "  %s ./music.wav\n"
        "  %s ./music.wav -t 1\n"
        "  %s -r ./record.wav\n"
        "\n",
        argv0, WMIX_VERSION, argv0, argv0, argv0, argv0);
}

int main(int argc, char **argv)
{
    int i;
    bool helpFalg = true;

    bool record = false; //播音模式
    int interval = 0;
    int reduce = 0;
    int volume = -1, volumeMic = -1, volumeAgc = -1;
    int id = -1;
    int order = 0;
    int rt = 5, rc = 1, rr = 8000;
    char *ip;
    int port = 9999;
    bool useAAC = false;
    bool rtps = false;
    bool rtpr = false;
    int log = -1;
    bool reset = false;
    bool info = false;

    int vad = -1, aec = -1, ns = -1, ns_pa = -1, agc = -1, rw = -1;

    char *filePath = NULL;
    char tmpPath[128] = {0};
    char tmpPath2[128] = {0};

    if (argc < 2)
    {
        help(argv[0]);
        return 0;
    }

    for (i = 1; i < argc; i++)
    {
        if (strlen(argv[i]) == 2 && strstr(argv[i], "-r"))
        {
            record = true;
            useAAC = false;
        }
        else if (strlen(argv[i]) == 4 && strstr(argv[i], "-log") && i + 1 < argc)
        {
            sscanf(argv[++i], "%d", &log);
        }
        else if (strlen(argv[i]) == 5 && strstr(argv[i], "-raac"))
        {
            record = true;
            useAAC = true;
        }
        else if (strlen(argv[i]) == 3 && strstr(argv[i], "-rt") && i + 1 < argc)
        {
            sscanf(argv[++i], "%d", &rt);
        }
        else if (strlen(argv[i]) == 3 && strstr(argv[i], "-rc") && i + 1 < argc)
        {
            sscanf(argv[++i], "%d", &rc);
        }
        else if (strlen(argv[i]) == 3 && strstr(argv[i], "-rr") && i + 1 < argc)
        {
            sscanf(argv[++i], "%d", &rr);
        }
        else if (strlen(argv[i]) == 2 && strstr(argv[i], "-b"))
        {
            order = -1;
        }
        else if (strlen(argv[i]) == 2 && strstr(argv[i], "-m"))
        {
            order = 2;
        }
        else if (strlen(argv[i]) == 2 && strstr(argv[i], "-i"))
        {
            order = 1;
        }
        else if (strlen(argv[i]) == 2 && strstr(argv[i], "-l"))
        {
            order = 0;
        }
        else if (strlen(argv[i]) == 2 && strstr(argv[i], "-t") && i + 1 < argc)
        {
            sscanf(argv[++i], "%d", &interval);
        }
        else if (strlen(argv[i]) == 2 && strstr(argv[i], "-d") && i + 1 < argc)
        {
            sscanf(argv[++i], "%d", &reduce);
        }
        else if (strlen(argv[i]) == 2 && strstr(argv[i], "-v") && i + 1 < argc)
        {
            sscanf(argv[++i], "%d", &volume);
        }
        else if (strlen(argv[i]) == 3 && strstr(argv[i], "-vr") && i + 1 < argc)
        {
            sscanf(argv[++i], "%d", &volumeMic);
        }
        else if (strlen(argv[i]) == 3 && strstr(argv[i], "-va") && i + 1 < argc)
        {
            sscanf(argv[++i], "%d", &volumeAgc);
        }
        else if (strlen(argv[i]) == 2 && strstr(argv[i], "-k") && i + 1 < argc)
        {
            sscanf(argv[++i], "%d", &id);
        }
        else if (strlen(argv[i]) == 5 && strstr(argv[i], "-rtps") && i + 2 < argc)
        {
            ip = argv[++i];
            sscanf(argv[++i], "%d", &port);
            rtps = true;
        }
        else if (strlen(argv[i]) == 5 && strstr(argv[i], "-rtpr") && i + 2 < argc)
        {
            ip = argv[++i];
            sscanf(argv[++i], "%d", &port);
            rtpr = true;
        }
        else if (strlen(argv[i]) == 4 && strstr(argv[i], "-vad") && i + 1 < argc)
        {
            sscanf(argv[++i], "%d", &vad);
        }
        else if (strlen(argv[i]) == 4 && strstr(argv[i], "-aec") && i + 1 < argc)
        {
            sscanf(argv[++i], "%d", &aec);
        }
        else if (strlen(argv[i]) == 3 && strstr(argv[i], "-ns") && i + 1 < argc)
        {
            sscanf(argv[++i], "%d", &ns);
        }
        else if (strlen(argv[i]) == 6 && strstr(argv[i], "-ns_pa") && i + 1 < argc)
        {
            sscanf(argv[++i], "%d", &ns_pa);
        }
        else if (strlen(argv[i]) == 4 && strstr(argv[i], "-agc") && i + 1 < argc)
        {
            sscanf(argv[++i], "%d", &agc);
        }
        else if (strlen(argv[i]) == 3 && strstr(argv[i], "-rw") && i + 1 < argc)
        {
            sscanf(argv[++i], "%d", &rw);
        }
        else if (strlen(argv[i]) == 6 && strstr(argv[i], "-reset"))
        {
            reset = true;
        }
        else if (strlen(argv[i]) == 5 && strstr(argv[i], "-info"))
        {
            info = true;
        }
        else if (strstr(argv[i], "-?") || strstr(argv[i], "-help"))
        {
            help(argv[0]);
            return 0;
        }
        else
        {
            filePath = argv[i];
        }
    }

    if (reset)
    {
        wmix_reset();
        return 0;
    }

    if (info)
    {
        wmix_info();
        helpFalg = false;
    }

    if (volume >= 0)
    {
        wmix_set_volume(volume);
        helpFalg = false;
    }

    if (volumeMic >= 0)
    {
        wmix_set_volumeMic(volumeMic);
        helpFalg = false;
    }

    if (volumeAgc >= 0)
    {
        wmix_set_volumeAgc(volumeAgc);
        helpFalg = false;
    }

    if (log >= 0)
    {
        wmix_log(log ? true : false);
        helpFalg = false;
    }

    if (vad >= 0)
    {
        wmix_webrtc_vad(vad ? true : false);
        helpFalg = false;
    }
    if (aec >= 0)
    {
        wmix_webrtc_aec(aec ? true : false);
        helpFalg = false;
    }
    if (ns >= 0)
    {
        wmix_webrtc_ns(ns ? true : false);
        helpFalg = false;
    }
    if (ns_pa >= 0)
    {
        wmix_webrtc_ns_pa(ns_pa ? true : false);
        helpFalg = false;
    }
    if (agc >= 0)
    {
        wmix_webrtc_agc(agc ? true : false);
        helpFalg = false;
    }

    if (rw >= 0)
    {
        wmix_rw_test(rw ? true : false);
        helpFalg = false;
    }

    if (id >= 0)
    {
        wmix_play_kill(id);
        helpFalg = false;
    }
    id = 0;

    if (rtps)
    {
        id = wmix_rtp_send(ip, port, rc, rr, 0);
        helpFalg = false;
    }
    if (rtpr)
    {
        id = wmix_rtp_recv(ip, port, rc, rr, 0);
        helpFalg = false;
    }

    if (filePath && filePath[0] == '.')
    {
        if (getcwd(tmpPath, sizeof(tmpPath)))
        {
            snprintf(tmpPath2, sizeof(tmpPath2), "%s/%s", tmpPath, filePath);
            filePath = tmpPath2;
        }
    }

    if (filePath && filePath[0])
    {
        if (record)
            wmix_record(filePath, rc, 16, rr, rt, useAAC);
        else
            id = wmix_play(filePath, reduce, interval, order);
        helpFalg = false;
    }

    if (helpFalg)
    {
        printf("\nparam err !!\n");
        help(argv[0]);
        return -1;
    }

    if (id > 0)
        printf("id: %d\n", id);

    return id;
}
