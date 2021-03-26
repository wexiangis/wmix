# 简单介绍

* 基于alsa库开发的音频混音器、音频托管程序, 由主程序+客户端程序组成, 支持客户端自行开发;

* 在此基础上移植了大量第三方音频处理库, 使其支持 mp3、aac、降噪等功能;

---

# 编译和使用说明

* make libs # 编译第三方依赖库

* make # 生成主程序 wmix 和客户端程序 wmixMsg

* sudo cp ./libs/lib/*.so* /usr/lib/ # 部署运行环境,拷贝依赖库到系统

* wmix -? # 查看主程序传参说明

* wmixMsg -? # 查看客户端程序传参说明

* wmix -d & # 主程序后台运行,其中 -d 表示打印调试信息

* wmixMsg ./audio/1x8000.wav -v 10 # 以最大音量10播放音频文件

* wmixMsg ./audio/2x16000.wav -d 2 # 2倍削减背景音量混音播放当前音频

* wmixMsg -k 0 # 清空当前播放列表

* wmixMsg -r ./xxx.wav -rt 10 # 录音10秒到wav文件(系统需具备录音条件)

---

# 常见参数配置

* 平台选择: 修改 Makefile 中 PLATFORM 的定义

* 声道和频率: 定义在 platform 文件夹下对应平台的 plat.h 中(注意: webrtc不支持过高的频率)

---

# rtp对讲测试(先把主程序抛后台)

## 工具推pcm文件流,wmix播放：

1. 使用工具推流,注意pcm格式只支持单声道8000Hz

* ./rtpSendPCM ./audio/1x8000.wav 0 127.0.0.1 9999 >> /dev/null &

2. 接收rtp pcm数据流并播放

* ./wmixMsg -rtpr 127.0.0.1 9999 -bind

3. 结束收流,关闭工具

* ./wmixMsg -ka
* fg
* ctrl + c

## wmix录音推pcm流,工具收流保存文件(设备要可以录音)：

1. 开始录音推流

* ./wmixMsg -rtps 127.0.0.1 9999

2. 接收rtp pcm数据流并播放

* ./rtpRecvPCM ./save.wav 1 127.0.0.1 9999 >> /dev/null &

3. 结束推流,关闭工具

* ./wmixMsg -ka
* fg
* ctrl + c

## 局域网内两设备对讲,假设A设备IP为 192.168.43.180, B设备IP为 192.168.43.170 使用端口9999：

1. A设备

* ./wmixMsg -rtp 192.168.43.180 192.168.43.170 9999

2. B设备

* ./wmixMsg -rtp 192.168.43.170 192.168.43.180 9999

3. 结束rtp对讲

* ./wmixMsg -ka

## rtp测试注意事项

1. 一发一收测试时,接收方必须为bind方式;

2. 使用bind的IP必须为己方IP,不存在bind别人IP的操作;

3. 不要用127.0.0.1代替本机IP,别的设备会连不进来;

4. 使用-rtps-aac或-rtpr-aac时,需指定声道和频率,如"-rtps-aac 192.168.43.180 9999 -rc 2 -rr 44100"

---

# 选择目标库的启用

* 编辑 Makefile 选择启用各种 MAKE_XXX, 0关闭, 1启用

---

* MAKE_MP3: 

  * 支持mp3播放

  * 依赖库 libmad

---

* MAKE_AAC: 

  * 支持aac播放、录音

  * 依赖库 libfaac libfaad

---

* MAKE_WEBRTC_VAD: 

  * 人声识别, 用于录音没人说话时主动静音

  * 支持单、双声道, 8000Hz、16000Hz、32000Hz

  * 依赖库 libwebrtcvad(裁剪自WebRtc库)

---

* MAKE_WEBRTC_NS: 

  * 噪音抑制, 录、播音均可使用

  * 支持单、双声道, 8000Hz、16000Hz、32000Hz

  * 依赖库 libwebrtcns(裁剪自WebRtc库)

---

* MAKE_WEBRTC_AEC:  

  * 回声消除, 边播音边录音时, 把录到的播音数据消去

  * 支持单、双声道, 8000Hz、16000Hz (设备的录音质量要求较高, CPU算力要求较高)

  * 推荐配置主程序 单声道 8000Hz (CPU占用异常的高)

  * 依赖库 libwebrtcaec(裁剪自WebRtc库)

---

* MAKE_WEBRTC_AGC: 

  * 自动增益, 录音音量增益

  * 支持单、双声道, 8000Hz、16000Hz、32000Hz

  * 依赖库 libwebrtcagc(裁剪自WebRtc库)

---

# 树莓派

* 在 Makefile 改用 cross:=arm-linux-gnueabihf 再编译, MAKE_WEBRTC_AEC 库用 gcc 编译不过;

* ps. 树莓派自带编译器 gcc 和 arm-linux-gnueabihf-gcc 编译的应用是通用的

---

# 新平台接入

* 工程中已把平台依赖接口进行了分离, 当一个新的硬件平台接入时, 只需修改下列项:

* 1. 添加 platform 文件: 在 platform 文件夹中,参考 alsa 添加自己的文件夹并实现 plat.c/h;

* 2. 修改 Makefile: 参考alsa配置, 配置自己的 platform 文件夹编译项,并指定 PLATFORM 为自己平台名称.
