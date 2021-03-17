
# 平台选择
#   0: 通用平台(ubuntu,ARM,树莓派等) --> 基于alsa
#   1: 海思hi3516平台 --> 不支持 MAKE_ALSA MAKE_AAC
#   2: 君正T31平台 --> 不支持 MAKE_ALSA MAKE_AAC MAKE_MP3
MAKE_PLATFORM = 0

##### 通用平台配置 #####
ifeq ($(MAKE_PLATFORM),0)
# ARM等平台需要交叉编译器时启用改行
# cross = arm-linux-gnueabihf
OBJ += ./platform/alsa/alsa_plat.c
CINC += -I./platform/alsa -I./platform/alsa/include
CLIBS += -L./platform/alsa/lib
CFLAGS +=
endif

##### 海思hi3516平台配置 #####
ifeq ($(MAKE_PLATFORM),1)
cross = arm-himix200-linux
# cross = arm-himix100-linux
MAKE_ALSA = 0
MAKE_AAC = 0
OBJ += ./platform/hi3516_plat.c
CINC += -I./platform/hi3516 -I./platform/hi3516/include
CLIBS += -L./platform/hi3516/lib
CFLAGS +=
endif

##### 君正T31平台配置 #####
ifeq ($(MAKE_PLATFORM),2)
cross = mips-linux-gnu
MAKE_ALSA = 0
MAKE_AAC = 0
MAKE_MP3 = 0
OBJ += ./platform/t31_plat.c
CINC += -I./platform/t31 -I./platform/t31/include
CLIBS += -L./platform/t31/lib
CFLAGS += -Wl,-gc-sections
CFLAGS += -muclibc # 使用 uclibc 时添加该项
endif

# 说明 '?=' 是指之前没有赋过值则使用当前值

# 置1时编译外部alsa库,否则使用编译器自带
MAKE_ALSA ?= 1
# 启用mp3播放支持 0/关 1/启用
MAKE_MP3 ?= 1
# 启用aac播放/录音 0/关 1/启用
MAKE_AAC ?= 1
# 启用webrtc_vad人声识别 0/关 1/启用
MAKE_WEBRTC_VAD ?= 1
# 启用webrtc_aec回声消除 0/关 1/启用
# (需启用 MAKE_WEBRTC_VAD, 否则编译 wmix 时报错)
MAKE_WEBRTC_AEC ?= 1
# 启用webrtc_ns噪音抑制 0/关 1/启用
MAKE_WEBRTC_NS ?= 1
# 启用webrtc_agc自动增益 0/关 1/启用
MAKE_WEBRTC_AGC ?= 1
# speex开源音频库 [测试中...]
MAKE_SPEEX ?= 0
# speexbeta3.aec回声消除库,请和 MAKE_WEBRTC_AEC 互斥启用 [测试中...]
MAKE_SPEEX_BETA3 ?= 0
# 启用FFT(快速傅立叶变换)采样点个数,0/关闭,其它/开启 [测试中...]
# 必须为2的x次方,如4,8,16...513,1024
MAKE_FFT_SAMPLE ?= 0 #1024

# system
HOST = 
CC = gcc
ROOT=$(shell pwd)
ifdef cross
	HOST = $(cross)
	CC = $(cross)-gcc
endif

# define
DEF += -DMAKE_PLATFORM=$(MAKE_PLATFORM)
DEF += -DMAKE_MP3=$(MAKE_MP3)
DEF += -DMAKE_AAC=$(MAKE_AAC)
DEF += -DMAKE_WEBRTC_VAD=$(MAKE_WEBRTC_VAD)
DEF += -DMAKE_WEBRTC_AEC=$(MAKE_WEBRTC_AEC)
DEF += -DMAKE_WEBRTC_NS=$(MAKE_WEBRTC_NS)
DEF += -DMAKE_WEBRTC_AGC=$(MAKE_WEBRTC_AGC)
DEF += -DMAKE_SPEEX=$(MAKE_SPEEX)
DEF += -DMAKE_SPEEX_BETA3=$(MAKE_SPEEX_BETA3)
DEF += -DMAKE_FFT_SAMPLE=$(MAKE_FFT_SAMPLE)

# base
OBJ += ./src/wmix.c
OBJ += ./src/wmix_mem.c
OBJ += ./src/wmix_task.c
OBJ += ./src/wav.c
OBJ += ./src/rtp.c
OBJ += ./src/g711codec.c
OBJ += ./src/webrtc.c
OBJ += ./src/speexlib.c
OBJ += ./src/delay.c

# ui [测试中...]
OBJ += ./ui/fbmap.c
OBJ += ./ui/wave.c
OBJ += ./ui/bmp.c

# math
OBJ += ./math/fft.c

# wmixMsg
OBJ-MSG += ./test/wmixMsg.c
OBJ-MSG += ./test/wmix_user.c

# tools
OBJ-RTPSENDPCM += ./test/rtpSendPCM.c
OBJ-RTPSENDPCM += ./src/rtp.c
OBJ-RTPSENDPCM += ./src/g711codec.c
OBJ-RTPSENDPCM += ./src/wav.c
OBJ-RTPSENDPCM += ./src/delay.c

OBJ-RTPRECVPCM += ./test/rtpRecvPCM.c
OBJ-RTPRECVPCM += ./src/rtp.c
OBJ-RTPRECVPCM += ./src/g711codec.c
OBJ-RTPRECVPCM += ./src/wav.c

OBJ-RTPSENDACC += ./test/rtpSendAAC.c
OBJ-RTPSENDACC += ./src/rtp.c
OBJ-RTPSENDACC += ./src/aac.c

OBJ-RTPRECVACC += ./test/rtpRecvAAC.c
OBJ-RTPRECVACC += ./src/rtp.c
OBJ-RTPRECVACC += ./src/aac.c

# -Ixxx
CINC += -I./src -I./math -I./platform -I$(ROOT)/libs/include
# -Lxxx
CLIBS += -L$(ROOT)/libs/lib
# -lxxx
CFLAGS += -lm -lpthread

# 选择要编译的库列表
TARGET-LIBS=

# ALSA LIB
ifeq ($(MAKE_ALSA),1)
TARGET-LIBS += libalsa
CFLAGS += -lasound -ldl
endif

# MP3 LIB
ifeq ($(MAKE_MP3),1)
OBJ += ./src/id3.c
CFLAGS += -lmad
TARGET-LIBS += libmad
endif

# AAC LIB
ifeq ($(MAKE_AAC),1)
OBJ += ./src/aac.c
CFLAGS += -lfaac -lfaad
TARGET-LIBS += libfaac libfaad
endif

# WEBRTC_VAD LIB
ifeq ($(MAKE_WEBRTC_VAD),1)
CFLAGS += -lwebrtcvad
TARGET-LIBS += libwebrtcvad
endif

# WEBRTC_AEC LIB
ifeq ($(MAKE_WEBRTC_AEC),1)
TARGET-LIBS += libwebrtcaec
CFLAGS += -lwebrtcaec -lwebrtcaecm
endif

# WEBRTC_NS LIB
ifeq ($(MAKE_WEBRTC_NS),1)
CFLAGS += -lwebrtcns
TARGET-LIBS += libwebrtcns
endif

# WEBRTC_AGC LIB
ifeq ($(MAKE_WEBRTC_AGC),1)
CFLAGS += -lwebrtcagc
TARGET-LIBS += libwebrtcagc
endif

# SPEEX LIB
ifeq ($(MAKE_SPEEX),1)
CFLAGS += -lspeex
TARGET-LIBS += libspeex
endif

# SPEEX_BETA3 LIB
ifeq ($(MAKE_SPEEX_BETA3),1)
CFLAGS += -logg -lspeex -lspeexdsp
TARGET-LIBS += libogg libspeexbeta3
endif

target: wmixmsg
	@$(CC) -Wall -o $(ROOT)/wmix $(OBJ) $(CINC) $(CLIBS) $(CFLAGS) $(DEF)
	@echo "---------- all complete !! ----------"

wmixmsg:
	@$(CC) -Wall -o $(ROOT)/wmixMsg $(OBJ-MSG) -lpthread

rtpTest:
	@$(CC) -Wall -o $(ROOT)/tools/rtpSendPCM $(OBJ-RTPSENDPCM) -I./src
	@$(CC) -Wall -o $(ROOT)/tools/rtpRecvPCM $(OBJ-RTPRECVPCM) -I./src
	@$(CC) -Wall -o $(ROOT)/tools/rtpSendAAC $(OBJ-RTPSENDACC) -I./src -L$(ROOT)/libs/lib -I$(ROOT)/libs/include -lfaac -lfaad
	@$(CC) -Wall -o $(ROOT)/tools/rtpRecvAAC $(OBJ-RTPRECVACC) -I./src -L$(ROOT)/libs/lib -I$(ROOT)/libs/include -lfaac -lfaad

libs: $(TARGET-LIBS)
	@echo "---------- all complete !! ----------"

libalsa:
	@tar -xjf $(ROOT)/pkg/alsa-lib-1.1.9.tar.bz2 -C $(ROOT)/libs && \
	cd $(ROOT)/libs/alsa-lib-1.1.9 && \
	./configure --prefix=$(ROOT)/libs --host=$(HOST) && \
	make -j4 && make install && \
	cd - && \
	rm $(ROOT)/libs/alsa-lib-1.1.9 -rf

libmad:
	@tar -xzf $(ROOT)/pkg/libmad-0.15.1b.tar.gz -C $(ROOT)/libs && \
	cd $(ROOT)/libs/libmad-0.15.1b && \
	./configure --prefix=$(ROOT)/libs --host=$(HOST) --enable-speed && \
	sed -i 's/-fforce-mem//g' ./Makefile && \
	make -j4 && make install && \
	cd - && \
	rm $(ROOT)/libs/libmad-0.15.1b -rf

libfaac:
	@tar -xzf $(ROOT)/pkg/faac-1.29.9.2.tar.gz -C $(ROOT)/libs && \
	cd $(ROOT)/libs/faac-1.29.9.2 && \
	./configure --prefix=$(ROOT)/libs --host=$(HOST) && \
	make -j4 && make install && \
	cd - && \
	rm $(ROOT)/libs/faac-1.29.9.2 -rf

libfaad:
	@tar -xzf $(ROOT)/pkg/faad2-2.8.8.tar.gz -C $(ROOT)/libs && \
	cd $(ROOT)/libs/faad2-2.8.8 && \
	./configure --prefix=$(ROOT)/libs --host=$(HOST) && \
	make -j4 && make install && \
	sed -i '/#pragma message/c // ignore update tips' $(ROOT)/libs/include/faad.h && \
	cd - && \
	rm $(ROOT)/libs/faad2-2.8.8 -rf

libwebrtcvad:
	@tar -xzf $(ROOT)/pkg/webrtc_cut.tar.gz -C $(ROOT)/libs && \
	cd $(ROOT)/libs/webrtc_cut && \
	bash ./build_vad_so.sh $(CC) && \
	cp ./install/* ../ -rf && \
	cd - && \
	rm $(ROOT)/libs/webrtc_cut -rf

libwebrtcaec:
	@tar -xzf $(ROOT)/pkg/webrtc_cut.tar.gz -C $(ROOT)/libs && \
	cd $(ROOT)/libs/webrtc_cut && \
	bash ./build_aec_so.sh $(CC) && \
	bash ./build_aecm_so.sh $(CC) && \
	cp ./install/* ../ -rf && \
	cd - && \
	rm $(ROOT)/libs/webrtc_cut -rf

libwebrtcns:
	@tar -xzf $(ROOT)/pkg/webrtc_cut.tar.gz -C $(ROOT)/libs && \
	cd $(ROOT)/libs/webrtc_cut && \
	bash ./build_ns_so.sh $(CC) && \
	cp ./install/* ../ -rf && \
	cd - && \
	rm $(ROOT)/libs/webrtc_cut -rf

libwebrtcagc:
	@tar -xzf $(ROOT)/pkg/webrtc_cut.tar.gz -C $(ROOT)/libs && \
	cd $(ROOT)/libs/webrtc_cut && \
	bash ./build_agc_so.sh $(CC) && \
	cp ./install/* ../ -rf && \
	cd - && \
	rm $(ROOT)/libs/webrtc_cut -rf

libspeex:
	@tar -xzf $(ROOT)/pkg/speex-1.2.0.tar.gz -C $(ROOT)/libs && \
	cd $(ROOT)/libs/speex-1.2.0 && \
	./configure --prefix=$(ROOT)/libs --host=$(HOST) && \
	make -j4 && make install && \
	cd - && \
	rm $(ROOT)/libs/speex-1.2.0 -rf

libspeexbeta3:
	@tar -xzf $(ROOT)/pkg/speex-1.2beta3.tar.gz -C $(ROOT)/libs && \
	cd $(ROOT)/libs/speex-1.2beta3 && \
	./configure --prefix=$(ROOT)/libs --host=$(HOST) --with-ogg=$(ROOT)/libs && \
	make -j4 && make install && \
	cd - && \
	rm $(ROOT)/libs/speex-1.2beta3 -rf

libogg:
	@tar -xJf $(ROOT)/pkg/libogg-1.3.4.tar.xz -C $(ROOT)/libs && \
	cd $(ROOT)/libs/libogg-1.3.4 && \
	./configure --prefix=$(ROOT)/libs --host=$(HOST) && \
	make -j4 && make install && \
	cd - && \
	rm $(ROOT)/libs/libogg-1.3.4 -rf

cleanall: clean
	@rm $(ROOT)/libs/* -rf

clean:
	@rm $(ROOT)/wmix $(ROOT)/wmixMsg $(ROOT)/tools/* -rf
