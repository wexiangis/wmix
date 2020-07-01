# ----- 编译和使用说明 -----

1. 编译第三方依赖库

* make libs

2. 编译, 生成主程序 wmix 和客户端程序 wmixMsg, --help可以查看使用说明

* make

3. 主程序抛后台 (先拷贝 ./libs/lib/*.so 到运行环境), -d 表示打印debug信息

* wmix -d &

4. 录音10秒到wav文件 (设备要具备录音条件)

* wmixMsg -r ./xxx.wav -rt 10

5. 播放音频文件, -v 10 表示用最大音量(0~10)

* wmixMsg ./xxx.wav -v 10

6. 关闭所有播放

* wmixMsg -k 0

---

# ----- 常见参数配置 -----

* 修改编译器: 编辑 Makefile 第一行 cross 内容, 注释掉表示使用 gcc

* 修改声道、频率: 在 src/wmix.h

---

# ----- 选择目标库的启用 -----

* 编辑 Makefile 选择启用 MAKE_XXX, 为 1 时启用

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

* MAKE_WEBRTC_aec:  

  * 回声消除, 边播音边录音时, 把录到的播音数据消去

  * 支持单、双声道, 8000Hz ~ 16000Hz (CPU算力要求较高)

  * 依赖库 libwebrtcaec(裁剪自WebRtc库)

---

* MAKE_WEBRTC_agc: 

  * 自动增益, 录音音量增益

  * 支持单、双声道, 8000Hz ~ 32000Hz

  * 依赖库 libwebrtcagc(裁剪自WebRtc库)

---
