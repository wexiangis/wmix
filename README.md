# ----- 简单介绍 -----

* 基于alsa库开发的音频混音器、音频托管程序, 由主程序+客户端程序组成, 支持客户端自行开发;

* 在此基础上移植了大量第三方音频处理库, 使其支持 mp3、aac、降噪等功能;

---

# ----- 编译和使用说明 -----

1. 编译第三方依赖库

* make libs

2. 编译, 生成主程序 wmix 和客户端程序 wmixMsg, --help可以查看使用说明

* make

3. 主程序抛后台 (先拷贝 ./libs/lib/lib* 到 /usr/lib/ ), -d 表示打印debug信息

* wmix -d &

4. 录音10秒到wav文件 (设备要具备录音条件)

* wmixMsg -r ./xxx.wav -rt 10

5. 播放音频文件, -v 10 表示用最大音量(0~10)

* wmixMsg ./xxx.wav -v 10

6. 清空播放列表

* wmixMsg -k 0

---

# ----- 常见参数配置 -----

* 修改编译器, 编辑 Makefile 第一行 cross 内容, 注释掉表示使用 gcc

* 修改声道、频率, 在 src/wmix.h

---

# ----- rtp对讲测试(先把主程序抛后台) -----

## 工具推pcm文件流,wmix播放步骤：

1. 使用工具推流,注意pcm格式只支持单声道8000Hz

* ./rtpSendPCM ./audio/1x8000.wav 0 127.0.0.1 9832 >> /dev/null &

2. 接收rtp pcm数据流并播放

* ./wmixMsg -rtpr 127.0.0.1 9832 -bind

3. 结束收流,关闭工具

* ./wmixMsg -ka
* fg
* ctrl + c

## wmix录音推pcm流,工具收流保存文件(设备要可以录音)：

1. 开始录音推流

* ./wmixMsg -rtps 127.0.0.1 9832

2. 接收rtp pcm数据流并播放

* ./rtpRecvPCM ./save.wav 1 127.0.0.1 9832 >> /dev/null &

3. 结束推流,关闭工具

* ./wmixMsg -ka
* fg
* ctrl + c

## 局域网内两设备对讲,假设A设备IP为 192.168.43.180 使用端口9832：

1. A设备使用bind方式先开启rtp收/发流

* ./wmixMsg -rtpr 192.168.43.180 9832 -bind
* ./wmixMsg -rtps 192.168.43.180 9832 -bind

2. B设备开启rtp发/收流,此时两设备可以互讲互听

* ./wmixMsg -rtps 192.168.43.180 9832
* ./wmixMsg -rtpr 192.168.43.180 9832

3. 结束rtp对讲

* ./wmixMsg -ka

---

# ----- 选择目标库的启用 -----

* 编辑 Makefile 选择启用 MAKE_XXX, 0 关闭, 1 启用

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

  * 支持单、双声道, 8000Hz ~ 32000Hz

  * 依赖库 libwebrtcvad(裁剪自WebRtc库)

---

* MAKE_WEBRTC_NS: 

  * 噪音抑制, 录、播音均可使用

  * 支持单、双声道, 8000Hz ~ 32000Hz

  * 依赖库 libwebrtcns(裁剪自WebRtc库)

---

* MAKE_WEBRTC_AEC:  

  * 回声消除, 边播音边录音时, 把录到的播音数据消去

  * 支持单、双声道, 8000Hz ~ 16000Hz (设备的录音质量要求较高, CPU算力要求较高)

  * 依赖库 libwebrtcaec(裁剪自WebRtc库)

---

* MAKE_WEBRTC_AGC: 

  * 自动增益, 录音音量增益

  * 支持单、双声道, 8000Hz ~ 32000Hz

  * 依赖库 libwebrtcagc(裁剪自WebRtc库)

---

# ----- 树莓派 -----

* 在 Makefile 改用 cross:=arm-linux-gnueabihf 再编译, MAKE_WEBRTC_AEC 库用 gcc 编译不过;

* ps. 树莓派用 gcc 和 arm-linux-gnueabihf-gcc 编译的应用是通用的
