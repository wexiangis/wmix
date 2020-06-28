# ----- 编译和使用说明 -----

1. 编译第三方依赖库 (如果没有libs目录则手动创建一个)

* make libs

2. 编译 wmix(生成主程序) 和 wmixMsg(客户端程序, --help可以查看使用说明)

* make

3. 主程序抛后台(先拷贝 ./libs/lib/*.so 到运行环境), -d 表示打印debug信息

* wmix -d &

4. 录音10秒到wav文件

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

* 编辑 Makefile 选择启用 MAKE_MP3、MAKE_AAC、MAKE_WEBRTC_VAD (为1时启用)

* MAKE_MP3: 支持mp3播放, 依赖库 libmad

* MAKE_AAC: 支持aac播放、录音, 依赖库 libfaac libfaad

* MAKE_WEBRTC_VAD: 人声识别, 用于录音没人说话时主动静音, 依赖库 libwebrtcvad(裁剪自WebRtc库)

* MAKE_WEBRTC_NS: 噪音抑制(录、播音均使用), 依赖库 libwebrtcns(裁剪自WebRtc库))

* MAKE_WEBRTC_aec: 回声消除 测试中... 依赖库 libwebrtcaec(裁剪自WebRtc库))

* MAKE_WEBRTC_agc: 自动增益 测试中... 依赖库 libwebrtcagc(裁剪自WebRtc库)
