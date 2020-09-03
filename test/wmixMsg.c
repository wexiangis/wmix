
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
        "Usage: %s [option] fileName\n"
        "\n"
        "Option:\n"
        "  -l : 排队模式,排到最后一位(默认模式)\n"
        "  -i : 排队模式,排到第一位\n"
        "  -m : 混音模式(不设置时为排队模式)\n"
        "  -b : 打断模式(不设置时为排队模式)\n"
        "  -t interval : 循环播放模式,间隔秒,取值[1~255]\n"
        "  -d reduce : 背景音削减倍数,取值[1~255]\n"
        "\n"
        "  -v volume : 音量设置0~10\n"
        "  -vr volume : 录音音量设置0~10\n"
        "  -va volumeAgc : 录音音量增益设置0~100, 启用 -agc 时有效\n"
        "\n"
        "  -k id : 关闭指定id的语音,id=0时清空播放列表\n"
        "  -ka : 关闭所有播放、录音、fifo、rtp\n"
        "\n"
        "  -r : 录音模式,wav格式(默认单通道/16bits/8000Hz/5秒)\n"
        "  -raac : 录音模式,aac格式(默认单通道/16bits/8000Hz/5秒)\n"
        "  -rc chn : 指定录音通道数[1,2]\n"
        "  -rr freq : 指定录音频率[8000,11025,16000,22050,32000,44100]\n"
        "  -rt time : 指定录音时长秒\n"
        "\n"
        "  -rtpr ip port : 启动rtp pcma接收播音,固定单声道8000Hz\n"
        "  -rtps ip port : 启动rtp pcma录音发送,固定单声道8000Hz\n"
        "                : 生成/tmp/record.sdp,用vlc播放时,端口必须设置为9832\n"
        "  -rtpr-aac ip port : 启动rtp aac接收播音,需使用-rc,-rr准确指定通道和频率\n"
        "  -rtps-aac ip port : 启动rtp aac录音发送,需使用-rc,-rr准确指定通道和频率\n"
        "                    : 生成/tmp/record-aac.sdp,用vlc播放时,端口必须设置为9832\n"
        "  -rtp-bind : rtp以bind绑定端口\n"
        "\n"
        "  -vad 0/1 : 关/开 webrtc.vad 人声识别,录音辅助,在没人说话时主动静音\n"
        "  -aec 0/1 : 关/开 webrtc.aec 回声消除\n"
        "  -ns 0/1 : 关/开 webrtc.ns 噪音抑制(录音)\n"
        "  -ns_pa 0/1 : 关/开 webrtc.ns 噪音抑制(播音)\n"
        "  -agc 0/1 : 关/开 webrtc.agc 自动增益\n"
        "  -rw 0/1 : 关/开 自收发测试\n"
        "\n"
        "  -ctl id ctl_type : 给目标id的线程发控制状态,ctl_type定义如下\n"
        "              1    : WCT_CLEAR 清控制状态 \n"
        "              2    : WCT_STOP 结束线程 \n"
        "              3    : WCT_RESET 重置/重连(rtp) \n"
        "              4    : WCT_SILENCE 静音,使用0数据运行 \n"
        "\n"
        "  -log 0/1 : 关闭/显示log\n"
        "  -reset : 重置混音器\n"
        "  -info path: 打印信息,path可以指定终端或输出文件的路径,path可以不带\n"
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

void warn(char *param, int value_num)
{
    printf(
        "\n  param err !! \n"
        "\n"
        "  %s 需要跟 %d 个参数\n"
        "\n",
        param, value_num);
    exit(0);
}

int main(int argc, char **argv)
{
    int i;

    bool helpFalg = true; //提示help使能

    bool record = false; //播音模式

    int interval = 0;
    int reduce = 0;

    int volume = -1, volumeMic = -1, volumeAgc = -1;

    bool kill_all = false;
    int kill_id = -1, ctrl_id = -1, ret_id = 0;
    int order = 0;

    int rt = 5, rc = 1, rr = 8000;
    bool rAcc = false;

    char *rtp_ip;
    int rtp_port = 9999;
    bool rtps = false;
    bool rtpr = false;

    char *rtp_aac_ip;
    int rtp_aac_port = 9999;
    bool rtps_aac = false;
    bool rtpr_aac = false;

    bool rtp_bind = false;

    int log = -1;
    bool reset = false;

    int ctrl = -1;

    bool info = false;
    char infoPath[128] = {0};

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
            rAcc = false;
        }
        else if (strlen(argv[i]) == 4 && strstr(argv[i], "-log"))
        {
            if (i + 1 < argc)
                sscanf(argv[++i], "%d", &log);
            else
                warn("-log", 1);
        }
        else if (strlen(argv[i]) == 5 && strstr(argv[i], "-raac"))
        {
            record = true;
            rAcc = true;
        }
        else if (strlen(argv[i]) == 3 && strstr(argv[i], "-rt"))
        {
            if (i + 1 < argc)
                sscanf(argv[++i], "%d", &rt);
            else
                warn("-rt", 1);
        }
        else if (strlen(argv[i]) == 3 && strstr(argv[i], "-rc"))
        {
            if (i + 1 < argc)
                sscanf(argv[++i], "%d", &rc);
            else
                warn("-rc", 1);
        }
        else if (strlen(argv[i]) == 3 && strstr(argv[i], "-rr"))
        {
            if (i + 1 < argc)
                sscanf(argv[++i], "%d", &rr);
            else
                warn("-rr", 1);
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
        else if (strlen(argv[i]) == 2 && strstr(argv[i], "-t"))
        {
            if (i + 1 < argc)
                sscanf(argv[++i], "%d", &interval);
            else
                warn("-t", 1);
        }
        else if (strlen(argv[i]) == 2 && strstr(argv[i], "-d"))
        {
            if (i + 1 < argc)
                sscanf(argv[++i], "%d", &reduce);
            else
                warn("-d", 1);
        }
        else if (strlen(argv[i]) == 2 && strstr(argv[i], "-v"))
        {
            if (i + 1 < argc)
                sscanf(argv[++i], "%d", &volume);
            else
                warn("-v", 1);
        }
        else if (strlen(argv[i]) == 3 && strstr(argv[i], "-vr"))
        {
            if (i + 1 < argc)
                sscanf(argv[++i], "%d", &volumeMic);
            else
                warn("-vr", 1);
        }
        else if (strlen(argv[i]) == 3 && strstr(argv[i], "-va"))
        {
            if (i + 1 < argc)
                sscanf(argv[++i], "%d", &volumeAgc);
            else
                warn("-va", 1);
        }
        else if (strlen(argv[i]) == 2 && strstr(argv[i], "-k"))
        {
            if (i + 1 < argc)
                sscanf(argv[++i], "%d", &kill_id);
            else
                warn("-k", 1);
        }
        else if (strlen(argv[i]) == 3 && strstr(argv[i], "-ka"))
        {
            kill_all = true;
        }
        else if (strlen(argv[i]) == 5 && strstr(argv[i], "-rtps"))
        {
            if (i + 2 < argc)
            {
                rtp_ip = argv[++i];
                sscanf(argv[++i], "%d", &rtp_port);
                rtps = true;
            }
            else
                warn("-rtps", 2);
        }
        else if (strlen(argv[i]) == 5 && strstr(argv[i], "-rtpr"))
        {
            if (i + 2 < argc)
            {
                rtp_ip = argv[++i];
                sscanf(argv[++i], "%d", &rtp_port);
                rtpr = true;
            }
            else
                warn("-rtpr", 2);
        }
        else if (strlen(argv[i]) == 9 && strstr(argv[i], "-rtps-aac"))
        {
            if (i + 2 < argc)
            {
                rtp_aac_ip = argv[++i];
                sscanf(argv[++i], "%d", &rtp_aac_port);
                rtps_aac = true;
            }
            else
                warn("-rtps-aac", 2);
        }
        else if (strlen(argv[i]) == 9 && strstr(argv[i], "-rtpr-aac"))
        {
            if (i + 2 < argc)
            {
                rtp_aac_ip = argv[++i];
                sscanf(argv[++i], "%d", &rtp_aac_port);
                rtpr_aac = true;
            }
            else
                warn("-rtpr-aac", 2);
        }
        else if (strlen(argv[i]) == 9 && strstr(argv[i], "-rtp-bind"))
        {
            rtp_bind = true;
        }
        else if (strlen(argv[i]) == 4 && strstr(argv[i], "-vad"))
        {
            if (i + 1 < argc)
                sscanf(argv[++i], "%d", &vad);
            else
                warn("-vad", 1);
        }
        else if (strlen(argv[i]) == 4 && strstr(argv[i], "-aec"))
        {
            if (i + 1 < argc)
                sscanf(argv[++i], "%d", &aec);
            else
                warn("-aec", 1);
        }
        else if (strlen(argv[i]) == 3 && strstr(argv[i], "-ns"))
        {
            if (i + 1 < argc)
                sscanf(argv[++i], "%d", &ns);
            else
                warn("-ns", 1);
        }
        else if (strlen(argv[i]) == 6 && strstr(argv[i], "-ns_pa"))
        {
            if (i + 1 < argc)
                sscanf(argv[++i], "%d", &ns_pa);
            else
                warn("-ns_pa", 1);
        }
        else if (strlen(argv[i]) == 4 && strstr(argv[i], "-agc"))
        {
            if (i + 1 < argc)
                sscanf(argv[++i], "%d", &agc);
            else
                warn("-agc", 1);
        }
        else if (strlen(argv[i]) == 3 && strstr(argv[i], "-rw"))
        {
            if (i + 1 < argc)
                sscanf(argv[++i], "%d", &rw);
            else
                warn("-rw", 1);
        }
        else if (strlen(argv[i]) == 4 && strstr(argv[i], "-ctl"))
        {
            if (i + 2 < argc)
            {
                sscanf(argv[++i], "%d", &ctrl_id);
                sscanf(argv[++i], "%d", &ctrl);
            }
            else
                warn("-ctl", 2);
        }
        else if (strlen(argv[i]) == 6 && strstr(argv[i], "-reset"))
        {
            reset = true;
        }
        else if (strlen(argv[i]) == 5 && strstr(argv[i], "-info"))
        {
            info = true;
            if (i + 1 < argc)
            {
                memset(infoPath, 0, sizeof(infoPath));
                strcpy(infoPath, argv[++i]);
            }
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
        wmix_info(infoPath);
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

    if (ctrl_id > 0 && ctrl > 0)
    {
        wmix_ctrl(ctrl_id, ctrl);
        helpFalg = false;
    }

    if (kill_id >= 0)
    {
        wmix_play_kill(kill_id);
        helpFalg = false;
    }

    if (kill_all)
    {
        wmix_kill_all();
        helpFalg = false;
    }

    if (rtps)
    {
        ret_id = wmix_rtp_send(rtp_ip, rtp_port, rc, rr, 0, rtp_bind);
        helpFalg = false;
    }
    if (rtpr)
    {
        ret_id = wmix_rtp_recv(rtp_ip, rtp_port, rc, rr, 0, rtp_bind);
        helpFalg = false;
    }

    if (rtps_aac)
    {
        ret_id = wmix_rtp_send(rtp_aac_ip, rtp_aac_port, rc, rr, 1, rtp_bind);
        helpFalg = false;
    }
    if (rtpr_aac)
    {
        ret_id = wmix_rtp_recv(rtp_aac_ip, rtp_aac_port, rc, rr, 1, rtp_bind);
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
            wmix_record(filePath, rc, 16, rr, rt, rAcc);
        else
            ret_id = wmix_play(filePath, reduce, interval, order);
        helpFalg = false;
    }

    if (helpFalg)
    {
        printf("\n  param err !! \r\n");
        help(argv[0]);
        return -1;
    }

    if (ret_id > 0)
        printf("id: %d\n", ret_id);

    return ret_id;
}
