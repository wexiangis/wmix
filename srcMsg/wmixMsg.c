
/**************************************************
 * 
 *  wmix客户端，用于命令行音频操作
 * 
 *  支持录音、播音、音量调节、log开关、rtp收发测试等等
 * 
 **************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "wmix_user.h"

void help(char *argv0)
{
    printf(
        "\n"
        "Usage: %s [option] audioPath\n"
        "\n"
        "Option:\n"
        "  -l : 排队模式,排到最后一位(默认模式)\n"
        "  -i : 排队模式,排到第一位\n"
        "  -m : 混音模式(不设置时为排队模式)\n"
        "  -b : 打断模式(不设置时为排队模式)\n"
        "  -t interval : 循环播放模式,间隔秒,取值[1~255]\n"
        "  -n repeat : 重复播放次数,取值[1~127]\n"
        "  -d reduce : 背景音削减倍数,取值[1~15]\n"
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
        "  -rtpr-aac ip port : 启动rtp aac接收播音,需使用-rc,-rr准确指定通道和频率\n"
        "  -rtps-aac ip port : 启动rtp aac录音发送,需使用-rc,-rr准确指定通道和频率\n"
        "\n"
        "  -bind : rtp以bind绑定端口(此时rtp设定的ip必须为本机ip;两设备对讲时,必须有一方为bind方式)\n"
        "\n"
        "  -rtp local_ip remote_ip port : 启动rtp pcma双通对讲,固定单声道8000Hz\n"
        "  -rtp-aac local_ip remote_ip port : 启动rtp aac双通对讲,需使用-rc,-rr准确指定通道和频率\n"
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
        "  -list : 打印所有任务信息\n"
        "  -info : 打印运行信息\n"
        "  -console path : 重定向打印信息输出路径,path示例: /dev/console /dev/ttyAMA0 或者文件\n"
        "  -? --help : 显示帮助\n"
        "\n"
        "Extra:\n"
        "  -note wavPath : 保存混音数据池的数据流到wav文件,写0关闭(如-note 0)\n"
        "  -fft path : 1.指定fb设备路径(如/dev/fb0)连续输出幅频/相频图像,写0关闭(如-fft 0)\n"
        "            : 2.指定.bmp文件路径(如./fft.bmp)输出幅频/相频图像\n"
        "            : <将根据path内容自动选择模式>\n"
        "\n"
        "Test:\n"
        "  -tm : 测试 wmix_mem_1x8000 接口,录制5秒的1x8000Hz的.pcm文件\n"
        "  -tm2 : 测试 wmix_mem_origin 接口,录制5秒的原始参数的.pcm文件\n"
        "  -tfi : 测试 wmix_fifo_record 接口,录制5秒的8000Hz单声道的.pcm文件\n"
        "  -tfi2 : 测试 wmix_fifo_record 接口aac模式,录制5秒的原始参数的.aac文件\n"
        "\n"
        "Return:\n"
        "  = 0: ok\n"
        "  < 0: error\n"
        "  > 0: id, use to \"-k id\"\n"
        "\n"
        "Version: %s\n"
        "\n"
        "Example:\n"
        "  %s -v 10\n"
        "  %s ./music.wav\n"
        "  %s -r ./record.wav\n"
        "\n",
        argv0, WMIX_VERSION, argv0, argv0, argv0);
}

/*
 *  测试 wmix_mem_XXX 接口
 *  参数:
 *      file: 保存录音文件(建议.pcm后缀)
 *      rt: 录音时长,单位秒
 *      mode: 1/wmix_mem_1x8000 2/wmix_mem_origin
 */
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
void wmix_mem_test(char *file, int rt, int mode)
{
    int fd, ret;
    int16_t addr = -1, buff[512];
    uint32_t tick;

    if (!file || rt < 1 || mode < 0)
        return;

    if ((fd = open(file, O_WRONLY | O_CREAT | O_TRUNC, 0666)) < 1)
        return;
    //开启共享内存
    wmix_mem_open();
    tick = wmix_tickUs();
    //录音写文件
    do {
        if (mode)
            ret = wmix_mem_origin(buff, 512, &addr, true);
        else
            ret = wmix_mem_1x8000(buff, 512, &addr, true);
        if (ret > 0)
        {
            printf("wmix_mem_test(%d): read %d frame\r\n", mode, ret);
            write(fd, buff, ret * 2);
        }
    } while(wmix_tickUs() < tick + rt * 1000000);
    //关闭共享内存
    wmix_mem_close();
    close(fd);
}
void wmix_fifo_test(char *file, int rc, int rr, int rt, int mode)
{
    int fd, fd_read, ret;
    int16_t buff[1024];
    uint32_t tick;

    if (!file || rt < 1 || mode < 0)
        return;

    if ((fd = open(file, O_WRONLY | O_CREAT | O_TRUNC, 0666)) < 1)
        return;
    //开启fifo流
    fd_read = wmix_fifo_record(rc, rr, mode);
    if (fd_read < 1)
    {
        close(fd);
        return;
    }
    tick = wmix_tickUs();
    //录音写文件
    do {
        ret = read(fd_read, buff, sizeof(buff));
        if (ret > 0)
        {
            printf("wmix_fifo_test(%d.%d.%d): read %d frame\r\n", rc, rr, mode, ret);
            write(fd, buff, ret);
        }
    } while(wmix_tickUs() < tick + rt * 1000000);
    //关闭流
    close(fd_read);
    close(fd);
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

    int interval = 0; //重读播音标志和间隔(秒) -t
    int reduce = 0; //背景削减量 -d
    int repeat = 0; //重复播放次数 -n

    int volume = -1, volumeMic = -1, volumeAgc = -1; //播音-v,录音-vr,增益音量-va 0~10

    bool kill_all = false; //关闭所有人物 -ka
    int kill_id = -1; //指定关闭任务 -k id
    int order = 0; //排序方式 0/-l 1/-i 2/-m -1/-b

    int rt = 5, rc = 1, rr = 8000; //指定录音的 时长(秒)、通道、频率
    int recordType = 0; //录音格式： 0/wav 1/aac

    char *rtp_ip; //rtp(g711格式) 全双工配置
    int rtp_port = 9999;
    bool rtps = false;
    bool rtpr = false;

    char *rtp_aac_ip; //rtp(aac格式) 全双工配置
    int rtp_aac_port = 9999;
    bool rtps_aac = false;
    bool rtpr_aac = false;

    bool rtp_bind = false; //服务端使用

    char *rtp_local_ip; //rtp(g711格式) 半双工配置
    char *rtp_remote_ip;
    int rtp_remote_port;
    bool rtpsr = false;

    char *rtp_aac_local_ip; //rtp(aac格式) 半双工配置
    char *rtp_aac_remote_ip;
    int rtp_aac_remote_port;
    bool rtpsr_aac = false;

    char *notePath = NULL; //保留混音器播音数据 -note path

    int log = -1; //是否使能log -log
    bool reset = false; //复位标志 -reset
    bool list = false; //列出当前所有任务 -list

    int ctrl_id = -1; //指定控制任务 -ctl id type
    int ctrl_type = -1; //控制类型 -ctl id type

    bool info = false; //显示混音器当前状态信息 -info

    char *consolePath = NULL; //重定向混音器log输出路径 -console path

    char *fft = NULL; //暂未使用

    int vad = -1, aec = -1, ns = -1, ns_pa = -1, agc = -1, rw = -1;

    //目标播放、录音文件
    char *audioPath = NULL;

    char tmpPath[128] = {0};
    char tmpPath2[128] = {0};

    int ret_id = 0; //返回播放、录音任务id

    //测试 wmix_mem_read/wmix_mem_origin 接口录音文件 -tm/-tm2 path
    //可用 -rt 配置录音时长
    int tm_test_mode = -1;

    //测试 wmix_fifo_record 接口录音文件 -tfi/-tfi2 path
    //可用 -rt 配置录音时长
    int tfi_test_mode = -1;

    int argvLen;
    if (argc < 2)
    {
        help(argv[0]);
        return 0;
    }

    for (i = 1; i < argc; i++)
    {
        argvLen = strlen(argv[i]);

        if (argvLen == 2 && strstr(argv[i], "-r"))
        {
            record = true;
            recordType = 0;
        }
        else if (argvLen == 4 && strstr(argv[i], "-log"))
        {
            if (i + 1 < argc)
                sscanf(argv[++i], "%d", &log);
            else
                warn("-log", 1);
        }
        else if (argvLen == 5 && strstr(argv[i], "-raac"))
        {
            record = true;
            recordType = 1;
        }
        else if (argvLen == 3 && strstr(argv[i], "-rt"))
        {
            if (i + 1 < argc)
                sscanf(argv[++i], "%d", &rt);
            else
                warn("-rt", 1);
        }
        else if (argvLen == 3 && strstr(argv[i], "-rc"))
        {
            if (i + 1 < argc)
                sscanf(argv[++i], "%d", &rc);
            else
                warn("-rc", 1);
        }
        else if (argvLen == 3 && strstr(argv[i], "-rr"))
        {
            if (i + 1 < argc)
                sscanf(argv[++i], "%d", &rr);
            else
                warn("-rr", 1);
        }
        else if (argvLen == 2 && strstr(argv[i], "-b"))
        {
            order = -1;
        }
        else if (argvLen == 2 && strstr(argv[i], "-m"))
        {
            order = 2;
        }
        else if (argvLen == 2 && strstr(argv[i], "-i"))
        {
            order = 1;
        }
        else if (argvLen == 2 && strstr(argv[i], "-l"))
        {
            order = 0;
        }
        else if (argvLen == 2 && strstr(argv[i], "-t"))
        {
            if (i + 1 < argc)
            {
                sscanf(argv[++i], "%d", &interval);
                if (interval > 255)
                    interval = 255;
            }
            else
                warn("-t", 1);
        }
        else if (argvLen == 2 && strstr(argv[i], "-n"))
        {
            if (i + 1 < argc)
            {
                sscanf(argv[++i], "%d", &repeat);
                if (repeat > 127)
                    repeat = 127;
            }
            else
                warn("-n", 1);
        }
        else if (argvLen == 2 && strstr(argv[i], "-d"))
        {
            if (i + 1 < argc)
            {
                sscanf(argv[++i], "%d", &reduce);
                if (reduce > 15)
                    reduce = 15;
            }
            else
                warn("-d", 1);
        }
        else if (argvLen == 2 && strstr(argv[i], "-v"))
        {
            if (i + 1 < argc)
                sscanf(argv[++i], "%d", &volume);
            else
                warn("-v", 1);
        }
        else if (argvLen == 3 && strstr(argv[i], "-vr"))
        {
            if (i + 1 < argc)
                sscanf(argv[++i], "%d", &volumeMic);
            else
                warn("-vr", 1);
        }
        else if (argvLen == 3 && strstr(argv[i], "-va"))
        {
            if (i + 1 < argc)
                sscanf(argv[++i], "%d", &volumeAgc);
            else
                warn("-va", 1);
        }
        else if (argvLen == 2 && strstr(argv[i], "-k"))
        {
            if (i + 1 < argc)
                sscanf(argv[++i], "%d", &kill_id);
            else
                warn("-k", 1);
        }
        else if (argvLen == 3 && strstr(argv[i], "-ka"))
        {
            kill_all = true;
        }
        else if (argvLen == 5 && strstr(argv[i], "-rtps"))
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
        else if (argvLen == 5 && strstr(argv[i], "-rtpr"))
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
        else if (argvLen == 9 && strstr(argv[i], "-rtps-aac"))
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
        else if (argvLen == 9 && strstr(argv[i], "-rtpr-aac"))
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
        else if (argvLen == 4 && strstr(argv[i], "-rtp"))
        {
            if (i + 3 < argc)
            {
                rtp_local_ip = argv[++i];
                rtp_remote_ip = argv[++i];
                sscanf(argv[++i], "%d", &rtp_remote_port);
                rtpsr = true;
            }
            else
                warn("-rtp", 3);
        }
        else if (argvLen == 8 && strstr(argv[i], "-rtp-aac"))
        {
            if (i + 3 < argc)
            {
                rtp_aac_local_ip = argv[++i];
                rtp_aac_remote_ip = argv[++i];
                sscanf(argv[++i], "%d", &rtp_aac_remote_port);
                rtpsr_aac = true;
            }
            else
                warn("-rtp-aac", 3);
        }
        else if (argvLen == 5 && strstr(argv[i], "-bind"))
        {
            rtp_bind = true;
        }
        else if (argvLen == 4 && strstr(argv[i], "-vad"))
        {
            if (i + 1 < argc)
                sscanf(argv[++i], "%d", &vad);
            else
                warn("-vad", 1);
        }
        else if (argvLen == 4 && strstr(argv[i], "-aec"))
        {
            if (i + 1 < argc)
                sscanf(argv[++i], "%d", &aec);
            else
                warn("-aec", 1);
        }
        else if (argvLen == 3 && strstr(argv[i], "-ns"))
        {
            if (i + 1 < argc)
                sscanf(argv[++i], "%d", &ns);
            else
                warn("-ns", 1);
        }
        else if (argvLen == 6 && strstr(argv[i], "-ns_pa"))
        {
            if (i + 1 < argc)
                sscanf(argv[++i], "%d", &ns_pa);
            else
                warn("-ns_pa", 1);
        }
        else if (argvLen == 4 && strstr(argv[i], "-agc"))
        {
            if (i + 1 < argc)
                sscanf(argv[++i], "%d", &agc);
            else
                warn("-agc", 1);
        }
        else if (argvLen == 3 && strstr(argv[i], "-rw"))
        {
            if (i + 1 < argc)
                sscanf(argv[++i], "%d", &rw);
            else
                warn("-rw", 1);
        }
        else if (argvLen == 4 && strstr(argv[i], "-ctl"))
        {
            if (i + 2 < argc)
            {
                sscanf(argv[++i], "%d", &ctrl_id);
                sscanf(argv[++i], "%d", &ctrl_type);
            }
            else
                warn("-ctl", 2);
        }
        else if (argvLen == 5 && strstr(argv[i], "-note"))
        {
            if (i + 1 < argc)
                notePath = argv[++i];
            else
                warn("-note", 1);
        }
        else if (argvLen == 4 && strstr(argv[i], "-fft"))
        {
            if (i + 1 < argc)
                fft = argv[++i];
            else
                warn("-fft", 1);
        }
        else if (argvLen == 6 && strstr(argv[i], "-reset"))
        {
            reset = true;
        }
        else if (argvLen == 5 && strstr(argv[i], "-list"))
        {
            list = true;
        }
        else if (argvLen == 5 && strstr(argv[i], "-info"))
        {
            info = true;
        }
        else if (argvLen == 3 && strstr(argv[i], "-tm"))
        {
            tm_test_mode = 0;
        }
        else if (argvLen == 4 && strstr(argv[i], "-tm2"))
        {
            tm_test_mode = 1;
        }
        else if (argvLen == 4 && strstr(argv[i], "-tfi"))
        {
            tfi_test_mode = 0;
        }
        else if (argvLen == 5 && strstr(argv[i], "-tfi2"))
        {
            tfi_test_mode = 1;
        }
        else if (argvLen == 8 && strstr(argv[i], "-console"))
        {
            if (i + 1 < argc)
                consolePath = argv[++i];
            else
                warn("-console", 1);
        }
        else if (strstr(argv[i], "-?") || strstr(argv[i], "-help"))
        {
            help(argv[0]);
            return 0;
        }
        else
        {
            audioPath = argv[i];
        }
    }

    if (reset)
    {
        wmix_reset();
        return 0;
    }

    if (list)
    {
        wmix_list();
        helpFalg = false;
    }

    if (info)
    {
        wmix_info();
        helpFalg = false;
    }

    if (consolePath)
    {
        wmix_console(consolePath);
        helpFalg = false;
    }

    if (fft)
    {
        wmix_fft((fft[0] == '0' && strlen(fft) == 1) ? NULL : fft);
        helpFalg = false;
    }

    if (notePath)
    {
        wmix_note((notePath[0] == '0' && strlen(notePath) == 1) ? NULL : notePath);
        helpFalg = false;
    }

    if (volume >= 0)
    {
        wmix_set_volume(volume);
        helpFalg = false;
        usleep(100000);
    }

    if (volumeMic >= 0)
    {
        wmix_set_volumeMic(volumeMic);
        helpFalg = false;
        usleep(100000);
    }

    if (volumeAgc >= 0)
    {
        wmix_set_volumeAgc(volumeAgc);
        helpFalg = false;
        usleep(100000);
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

    if (ctrl_id > 0 && ctrl_type > 0)
    {
        wmix_ctrl(ctrl_id, ctrl_type);
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

    if (rtpr)
    {
        ret_id = wmix_rtp_recv(rtp_ip, rtp_port, rc, rr, 0, rtp_bind, reduce);
        helpFalg = false;
        usleep(100000);
    }
    if (rtps)
    {
        ret_id = wmix_rtp_send(rtp_ip, rtp_port, rc, rr, 0, rtp_bind);
        helpFalg = false;
        usleep(100000);
    }

    if (rtpr_aac)
    {
        ret_id = wmix_rtp_recv(rtp_aac_ip, rtp_aac_port, rc, rr, 1, rtp_bind, reduce);
        helpFalg = false;
        usleep(100000);
    }
    if (rtps_aac)
    {
        ret_id = wmix_rtp_send(rtp_aac_ip, rtp_aac_port, rc, rr, 1, rtp_bind);
        helpFalg = false;
        usleep(100000);
    }

    if (rtpsr)
    {
        ret_id = wmix_rtp_recv(rtp_local_ip, rtp_remote_port, rc, rr, 0, true, reduce);
        usleep(100000);
        ret_id = wmix_rtp_send(rtp_remote_ip, rtp_remote_port, rc, rr, 0, false);
        usleep(100000);
        helpFalg = false;
    }

    if (rtpsr_aac)
    {
        ret_id = wmix_rtp_recv(rtp_aac_local_ip, rtp_aac_remote_port, rc, rr, 1, true, reduce);
        usleep(100000);
        ret_id = wmix_rtp_send(rtp_aac_remote_ip, rtp_aac_remote_port, rc, rr, 1, false);
        usleep(100000);
        helpFalg = false;
    }

    if (audioPath && audioPath[0] == '.')
    {
        if (getcwd(tmpPath, sizeof(tmpPath)))
        {
            snprintf(tmpPath2, sizeof(tmpPath2), "%s/%s", tmpPath, audioPath);
            audioPath = tmpPath2;
        }
    }

    if (audioPath && audioPath[0])
    {
        if (tm_test_mode >= 0)
            wmix_mem_test(audioPath, rt, tm_test_mode);
        else if (tfi_test_mode >= 0)
            wmix_fifo_test(audioPath, rc, rr, rt, tfi_test_mode);
        else if (record)
            wmix_record(audioPath, rc, rr, rt, recordType);
        else
            ret_id = wmix_play(audioPath, reduce, interval, repeat, order);
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
