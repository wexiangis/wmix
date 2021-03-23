# 根目录
ROOT = $(shell pwd)
PLATFORM = $(ROOT)/platform

# 说明 ?= 表示前面没有赋过值则使用当前赋值

# 平台选择
#   0: 通用平台(ubuntu,ARM,树莓派等) --> 基于alsa
#   1: 海思hi3516平台 --> 不支持 MAKE_ALSA MAKE_AAC
#   2: 君正T31平台 --> 不支持 MAKE_ALSA MAKE_AAC MAKE_MP3
MAKE_PLATFORM ?= 0

##### 通用平台配置 #####
ifeq ($(MAKE_PLATFORM),0)
# ARM等平台需要交叉编译器时启用改行
# cross ?= arm-linux-gnueabihf
OBJ += $(PLATFORM)/alsa/alsa_plat.c
CINC += -I$(PLATFORM)/alsa
CINC += -I$(PLATFORM)/alsa/include
CLIBS += -L$(PLATFORM)/alsa/lib
CFLAGS +=
endif

##### 海思hi3516平台配置 #####
ifeq ($(MAKE_PLATFORM),1)
cross ?= arm-himix200-linux
# cross ?= arm-himix100-linux
# 不支持
MAKE_ALSA = 0
MAKE_AAC = 0
# 平台文件编译配置
OBJ += $(PLATFORM)/hi3516/hi3516_plat.c
CINC += -I$(PLATFORM)/hi3516
CINC += -I$(PLATFORM)/hi3516/include
CLIBS += -L$(PLATFORM)/hi3516/lib
CFLAGS +=
endif

##### 君正T31平台配置 #####
ifeq ($(MAKE_PLATFORM),2)
cross ?= mips-linux-gnu
# 不支持
MAKE_ALSA = 0
MAKE_AAC = 0
MAKE_MP3 = 0
# 平台文件编译配置
OBJ += $(PLATFORM)/t31/t31_plat.c
OBJ += $(PLATFORM)/t31/lib/libalog.a
OBJ += $(PLATFORM)/t31/lib/libimp.a
CINC += -I$(PLATFORM)/t31
CINC += -I$(PLATFORM)/t31/include
CLIBS += -L$(PLATFORM)/t31/lib
CFLAGS += -lalog -limp -laudioProcess
CFLAGS += -Wl,-gc-sections -lrt -ldl
# CFLAGS += -muclibc # 使用 uclibc 时添加该项
# CFLAGS-MSG += -muclibc # 使用 uclibc 时添加该项
# 选配
MAKE_WEBRTC_VAD = 0
MAKE_WEBRTC_AEC = 0
MAKE_WEBRTC_NS = 0
MAKE_WEBRTC_AGC = 0
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
# 必须为2的x次方,如4,8,16...512,1024
MAKE_MATH_FFT ?= 0 #1024
# UI文件编译 [测试中...]
MAKE_UI ?= 0

# ALSA LIB
ifeq ($(MAKE_ALSA),1)
TARGET-LIBS += libalsa
CFLAGS += -lasound -ldl
endif
# MP3 LIB
ifeq ($(MAKE_MP3),1)
OBJ += $(ROOT)/src/id3.c
CFLAGS += -lmad
TARGET-LIBS += libmad
endif
# AAC LIB
ifeq ($(MAKE_AAC),1)
OBJ += $(ROOT)/src/aac.c
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
# MATH_FFT
ifeq ($(MAKE_MATH_FFT),1)
OBJ += ${wildcard $(ROOT)/math/*.c}
endif
# UI
ifeq ($(MAKE_UI),1)
OBJ += ${wildcard $(ROOT)/ui/*.c}
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
DEF += -DMAKE_MATH_FFT=$(MAKE_MATH_FFT)
DEF += -DMAKE_MATH_UI=$(MAKE_MATH_UI)

# system
GCC ?= gcc
GPP ?= g++
HOST =
ifdef cross
	HOST = $(cross)
	GCC = $(cross)-gcc
	GPP = $(cross)-g++
endif

# base
OBJ += $(ROOT)/src/wmix.c
OBJ += $(ROOT)/src/wmix_mem.c
OBJ += $(ROOT)/src/wmix_task.c
OBJ += $(ROOT)/src/wav.c
OBJ += $(ROOT)/src/rtp.c
OBJ += $(ROOT)/src/g711codec.c
OBJ += $(ROOT)/src/webrtc.c
OBJ += $(ROOT)/src/speexlib.c
OBJ += $(ROOT)/src/delay.c

# wmixMsg
OBJ-MSG += $(ROOT)/test/wmixMsg.c
OBJ-MSG += $(ROOT)/test/wmix_user.c
CFLAGS-MSG += -lpthread

# tools
OBJ-RTPSENDPCM += $(ROOT)/test/rtpSendPCM.c
OBJ-RTPSENDPCM += $(ROOT)/src/rtp.c
OBJ-RTPSENDPCM += $(ROOT)/src/g711codec.c
OBJ-RTPSENDPCM += $(ROOT)/src/wav.c
OBJ-RTPSENDPCM += $(ROOT)/src/delay.c

OBJ-RTPRECVPCM += $(ROOT)/test/rtpRecvPCM.c
OBJ-RTPRECVPCM += $(ROOT)/src/rtp.c
OBJ-RTPRECVPCM += $(ROOT)/src/g711codec.c
OBJ-RTPRECVPCM += $(ROOT)/src/wav.c

OBJ-RTPSENDACC += $(ROOT)/test/rtpSendAAC.c
OBJ-RTPSENDACC += $(ROOT)/src/rtp.c
OBJ-RTPSENDACC += $(ROOT)/src/aac.c

OBJ-RTPRECVACC += $(ROOT)/test/rtpRecvAAC.c
OBJ-RTPRECVACC += $(ROOT)/src/rtp.c
OBJ-RTPRECVACC += $(ROOT)/src/aac.c

# -Ixxx
CINC += -I$(ROOT)/src -I$(ROOT)/math -I$(ROOT)/libs/include
# -Lxxx
CLIBS += -L$(ROOT)/libs/lib
# -lxxx
CFLAGS += -lm -lpthread
# 要编译的库列表
TARGET-LIBS +=

wmix: wmixmsg
	@$(GCC) -Wall -o $(ROOT)/wmix $(OBJ) $(CINC) $(CLIBS) $(CFLAGS) $(DEF)
	@echo "---------- make wmix complete ----------"

wmixmsg:
	@$(GCC) -Wall -o $(ROOT)/wmixMsg $(OBJ-MSG) $(CFLAGS-MSG)
	@echo "---------- make wmixMsg complete ----------"

rtpTest:
	@$(GCC) -Wall -o $(ROOT)/tools/rtpSendPCM $(OBJ-RTPSENDPCM) -I$(ROOT)/src
	@$(GCC) -Wall -o $(ROOT)/tools/rtpRecvPCM $(OBJ-RTPRECVPCM) -I$(ROOT)/src
	@$(GCC) -Wall -o $(ROOT)/tools/rtpSendAAC $(OBJ-RTPSENDACC) -I$(ROOT)/src -L$(ROOT)/libs/lib -I$(ROOT)/libs/include -lfaac -lfaad
	@$(GCC) -Wall -o $(ROOT)/tools/rtpRecvAAC $(OBJ-RTPRECVACC) -I$(ROOT)/src -L$(ROOT)/libs/lib -I$(ROOT)/libs/include -lfaac -lfaad

libs: $(TARGET-LIBS)
	@rm -rf $(ROOT)/libs/lib/*.la && \
	echo "---------- make libs complete ----------"

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
	bash ./build_vad_so.sh $(GCC) && \
	cp ./install/* ../ -rf && \
	cd - && \
	rm $(ROOT)/libs/webrtc_cut -rf

libwebrtcaec:
	@tar -xzf $(ROOT)/pkg/webrtc_cut.tar.gz -C $(ROOT)/libs && \
	cd $(ROOT)/libs/webrtc_cut && \
	bash ./build_aec_so.sh $(GCC) && \
	bash ./build_aecm_so.sh $(GCC) && \
	cp ./install/* ../ -rf && \
	cd - && \
	rm $(ROOT)/libs/webrtc_cut -rf

libwebrtcns:
	@tar -xzf $(ROOT)/pkg/webrtc_cut.tar.gz -C $(ROOT)/libs && \
	cd $(ROOT)/libs/webrtc_cut && \
	bash ./build_ns_so.sh $(GCC) && \
	cp ./install/* ../ -rf && \
	cd - && \
	rm $(ROOT)/libs/webrtc_cut -rf

libwebrtcagc:
	@tar -xzf $(ROOT)/pkg/webrtc_cut.tar.gz -C $(ROOT)/libs && \
	cd $(ROOT)/libs/webrtc_cut && \
	bash ./build_agc_so.sh $(GCC) && \
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
