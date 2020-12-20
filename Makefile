# cross:=arm-linux-gnueabihf
# cross:=arm-himix200-linux
# cross:=arm-himix100-linux

# ps. 树莓派 MAKE_WEBRTC_AEC 库要用 cross:=arm-linux-gnueabihf 来编译

# 置1时编译外部alsa库,否则使用编译器自带
MAKE_ALSA=1

# 选择启用mp3播放支持 0/关 1/启用
MAKE_MP3=1

# 选择启用aac播放/录音 0/关 1/启用
MAKE_AAC=1

# 选择启用webrtc_vad人声识别 0/关 1/启用
MAKE_WEBRTC_VAD=1

# 选择启用webrtc_aec回声消除 0/关 1/启用
# (需启用 MAKE_WEBRTC_VAD, 否则编译 wmix 时报错)
MAKE_WEBRTC_AEC=0

# 选择启用webrtc_ns噪音抑制 0/关 1/启用
MAKE_WEBRTC_NS=1

# 选择启用webrtc_agc自动增益 0/关 1/启用
MAKE_WEBRTC_AGC=1

# speex开源音频库
MAKE_SPEEX=0

# speexbeta3.aec回声消除库,请和 MAKE_WEBRTC_AEC 互斥启用
MAKE_SPEEX_BETA3=0

host:=
cc:=gcc

ifdef cross
	host=$(cross)
	cc=$(cross)-gcc
endif

ROOT=$(shell pwd)

# define
DEF= -DMAKE_MP3=$(MAKE_MP3)
DEF+= -DMAKE_AAC=$(MAKE_AAC)
DEF+= -DMAKE_WEBRTC_VAD=$(MAKE_WEBRTC_VAD)
DEF+= -DMAKE_WEBRTC_AEC=$(MAKE_WEBRTC_AEC)
DEF+= -DMAKE_WEBRTC_NS=$(MAKE_WEBRTC_NS)
DEF+= -DMAKE_WEBRTC_AGC=$(MAKE_WEBRTC_AGC)
DEF+= -DMAKE_SPEEX=$(MAKE_SPEEX)
DEF+= -DMAKE_SPEEX_BETA3=$(MAKE_SPEEX_BETA3)

# base
obj-wmix+= ./src/wmix.c ./src/wmix.h
obj-wmix+= ./src/wav.c ./src/wav.h
obj-wmix+= ./src/rtp.c ./src/rtp.h
obj-wmix+= ./src/g711codec.c ./src/g711codec.h
obj-wmix+= ./src/webrtc.c ./src/webrtc.h
obj-wmix+= ./src/speexlib.c ./src/speexlib.h

# -lxxx
obj-flags= -lm -lpthread -lasound -ldl

# 选择要编译的库列表
targetlib=

# ALSA LIB
ifeq ($(MAKE_ALSA),1)
targetlib+= libalsa
endif

# MP3 LIB
ifeq ($(MAKE_MP3),1)
obj-wmix+=./src/id3.c ./src/id3.h
obj-flags+= -lmad
targetlib+= libmad
endif

# AAC LIB
ifeq ($(MAKE_AAC),1)
obj-wmix+=./src/aac.c ./src/aac.h
obj-flags+= -lfaac -lfaad
targetlib+= libfaac libfaad
endif

# WEBRTC_VAD LIB
ifeq ($(MAKE_WEBRTC_VAD),1)
obj-flags+= -lwebrtcvad
targetlib+= libwebrtcvad
endif

# WEBRTC_AEC LIB
ifeq ($(MAKE_WEBRTC_AEC),1)
targetlib+= libwebrtcaec
obj-flags+= -lwebrtcaec -lwebrtcaecm
endif

# WEBRTC_NS LIB
ifeq ($(MAKE_WEBRTC_NS),1)
obj-flags+= -lwebrtcns
targetlib+= libwebrtcns
endif

# WEBRTC_AGC LIB
ifeq ($(MAKE_WEBRTC_AGC),1)
obj-flags+= -lwebrtcagc
targetlib+= libwebrtcagc
endif

# SPEEX LIB
ifeq ($(MAKE_SPEEX),1)
obj-flags+= -lspeex
targetlib+= libspeex
endif

# SPEEX_BETA3 LIB
ifeq ($(MAKE_SPEEX_BETA3),1)
obj-flags+= -logg -lspeex -lspeexdsp
targetlib+= libogg libspeexbeta3
endif

obj-wmixmsg+=./test/wmix_user.c \
		./test/wmix_user.h \
		./test/wmixMsg.c

obj-rtpsendpcm+=./test/rtpSendPCM.c \
		./src/rtp.c \
		./src/rtp.h \
		./src/g711codec.c \
		./src/g711codec.h

obj-rtprecvpcm+=./test/rtpRecvPCM.c \
		./src/rtp.c \
		./src/rtp.h \
		./src/g711codec.c \
		./src/g711codec.h \
		./src/wav.c \
		./src/wav.h

obj-rtpsendaac+=./test/rtpSendAAC.c \
		./src/rtp.c \
		./src/rtp.h \
		./src/aac.c \
		./src/aac.h

obj-rtprecvaac+=./test/rtpRecvAAC.c \
		./src/rtp.c \
		./src/rtp.h \
		./src/aac.c \
		./src/aac.h

target: wmixmsg rtpTest
	@$(cc) -Wall -o wmix $(obj-wmix) -I./src -L$(ROOT)/libs/lib -I$(ROOT)/libs/include $(obj-flags) $(DEF)
	@echo "---------- all complete !! ----------"

wmixmsg:
	@$(cc) -Wall -o wmixMsg $(obj-wmixmsg) -lpthread

fifo:
	@$(cc) -Wall -o fifo -lpthread -I./test -I./src ./test/fifo.c ./test/wmix_user.c ./src/wav.c -lpthread

rtpTest:
	@$(cc) -Wall -o rtpSendPCM $(obj-rtpsendpcm) -I./src
	@$(cc) -Wall -o rtpRecvPCM $(obj-rtprecvpcm) -I./src
	@$(cc) -Wall -o rtpSendAAC $(obj-rtpsendaac) -I./src -L$(ROOT)/libs/lib -I$(ROOT)/libs/include -lfaac -lfaad
	@$(cc) -Wall -o rtpRecvAAC $(obj-rtprecvaac) -I./src -L$(ROOT)/libs/lib -I$(ROOT)/libs/include -lfaac -lfaad

libs: $(targetlib)
	@echo "---------- all complete !! ----------"

libalsa:
	@tar -xjf $(ROOT)/pkg/alsa-lib-1.1.9.tar.bz2 -C $(ROOT)/libs && \
	cd $(ROOT)/libs/alsa-lib-1.1.9 && \
	./configure --prefix=$(ROOT)/libs --host=$(host) && \
	make -j4 && make install && \
	cd - && \
	rm $(ROOT)/libs/alsa-lib-1.1.9 -rf

libmad:
	@tar -xzf $(ROOT)/pkg/libmad-0.15.1b.tar.gz -C $(ROOT)/libs && \
	cd $(ROOT)/libs/libmad-0.15.1b && \
	./configure --prefix=$(ROOT)/libs --host=$(host) --enable-speed && \
	sed -i 's/-fforce-mem//g' ./Makefile && \
	make -j4 && make install && \
	cd - && \
	rm $(ROOT)/libs/libmad-0.15.1b -rf

libfaac:
	@tar -xzf $(ROOT)/pkg/faac-1.29.9.2.tar.gz -C $(ROOT)/libs && \
	cd $(ROOT)/libs/faac-1.29.9.2 && \
	./configure --prefix=$(ROOT)/libs --host=$(host) && \
	make -j4 && make install && \
	cd - && \
	rm $(ROOT)/libs/faac-1.29.9.2 -rf

libfaad:
	@tar -xzf $(ROOT)/pkg/faad2-2.8.8.tar.gz -C $(ROOT)/libs && \
	cd $(ROOT)/libs/faad2-2.8.8 && \
	./configure --prefix=$(ROOT)/libs --host=$(host) && \
	make -j4 && make install && \
	sed -i '/#pragma message/c // ignore update tips' $(ROOT)/libs/include/faad.h && \
	cd - && \
	rm $(ROOT)/libs/faad2-2.8.8 -rf

libwebrtcvad:
	@tar -xzf $(ROOT)/pkg/webrtc_cut.tar.gz -C $(ROOT)/libs && \
	cd $(ROOT)/libs/webrtc_cut && \
	bash ./build_vad_so.sh $(cc) && \
	cp ./install/* ../ -rf && \
	cd - && \
	rm $(ROOT)/libs/webrtc_cut -rf

libwebrtcaec:
	@tar -xzf $(ROOT)/pkg/webrtc_cut.tar.gz -C $(ROOT)/libs && \
	cd $(ROOT)/libs/webrtc_cut && \
	bash ./build_aec_so.sh $(cc) && \
	bash ./build_aecm_so.sh $(cc) && \
	cp ./install/* ../ -rf && \
	cd - && \
	rm $(ROOT)/libs/webrtc_cut -rf

libwebrtcns:
	@tar -xzf $(ROOT)/pkg/webrtc_cut.tar.gz -C $(ROOT)/libs && \
	cd $(ROOT)/libs/webrtc_cut && \
	bash ./build_ns_so.sh $(cc) && \
	cp ./install/* ../ -rf && \
	cd - && \
	rm $(ROOT)/libs/webrtc_cut -rf

libwebrtcagc:
	@tar -xzf $(ROOT)/pkg/webrtc_cut.tar.gz -C $(ROOT)/libs && \
	cd $(ROOT)/libs/webrtc_cut && \
	bash ./build_agc_so.sh $(cc) && \
	cp ./install/* ../ -rf && \
	cd - && \
	rm $(ROOT)/libs/webrtc_cut -rf

libspeex:
	@tar -xzf $(ROOT)/pkg/speex-1.2.0.tar.gz -C $(ROOT)/libs && \
	cd $(ROOT)/libs/speex-1.2.0 && \
	./configure --prefix=$(ROOT)/libs --host=$(host) && \
	make -j4 && make install && \
	cd - && \
	rm $(ROOT)/libs/speex-1.2.0 -rf

libspeexbeta3:
	@tar -xzf $(ROOT)/pkg/speex-1.2beta3.tar.gz -C $(ROOT)/libs && \
	cd $(ROOT)/libs/speex-1.2beta3 && \
	./configure --prefix=$(ROOT)/libs --host=$(host) --with-ogg=$(ROOT)/libs && \
	make -j4 && make install && \
	cd - && \
	rm $(ROOT)/libs/speex-1.2beta3 -rf

libogg:
	@tar -xJf $(ROOT)/pkg/libogg-1.3.4.tar.xz -C $(ROOT)/libs && \
	cd $(ROOT)/libs/libogg-1.3.4 && \
	./configure --prefix=$(ROOT)/libs --host=$(host) && \
	make -j4 && make install && \
	cd - && \
	rm $(ROOT)/libs/libogg-1.3.4 -rf

cleanall: clean
	@rm -rf $(ROOT)/libs/* -rf

clean:
	@rm -rf ./wmix ./wmixMsg ./rtpSendPCM ./rtpRecvPCM ./rtpSendAAC ./rtpRecvAAC ./fifo
