# cross:=arm-linux-gnueabihf
# cross:=arm-himix200-linux
# cross:=arm-himix100-linux

# 置1时编译外部alsa库,否则使用编译器自带
MAKE_ALSA=1

# 选择启用mp3播放支持 0/关 1/启用
MAKE_MP3=0

# 选择启用aac播放/录音 0/关 1/启用
MAKE_AAC=0

# 选择启用webrtc_vad人声识别 0/关 1/启用
MAKE_WEBRTC_VAD=1

# 选择启用webrtc_aec回声消除 0/关 1/启用
MAKE_WEBRTC_AEC=1

# 选择启用webrtc_aecm回声消除(移动版) 0/关 1/启用
MAKE_WEBRTC_AECM=1

# 选择启用webrtc_ns噪音抑制 0/关 1/启用
MAKE_WEBRTC_NS=1

# 选择启用webrtc_agc自动增益 0/关 1/启用
MAKE_WEBRTC_AGC=1

# speex开源音频库
MAKE_SPEEX=0

host:=
cc:=gcc

ifdef cross
	host=$(cross)
	cc=$(cross)-gcc
endif

ROOT=$(shell pwd)

# BASE
obj-wmix+= ./src/wmix.c ./src/wmix.h
obj-wmix+= ./src/wav.c ./src/wav.h
obj-wmix+= ./src/rtp.c ./src/rtp.h
obj-wmix+= ./src/g711codec.c ./src/g711codec.h
obj-wmix+= ./src/webrtc.c ./src/webrtc.h

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
obj-flags+= -lwebrtcaec
targetlib+= libwebrtcaec
endif

# WEBRTC_AECM LIB
ifeq ($(MAKE_WEBRTC_AECM),1)
obj-flags+= -lwebrtcaecm
targetlib+= libwebrtcaecm
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

obj-wmixmsg+=./test/wmix_user.c \
		./test/wmix_user.h \
		./test/wmixMsg.c

obj-sendpcm+=./test/sendPCM.c \
		./src/rtp.c \
		./src/rtp.h \
		./src/g711codec.c \
		./src/g711codec.h

obj-recvpcm+=./test/recvPCM.c \
		./src/rtp.c \
		./src/rtp.h \
		./src/g711codec.c \
		./src/g711codec.h

obj-sendaac+=./test/sendAAC.c \
		./src/rtp.c \
		./src/rtp.h \
		./src/aac.c \
		./src/aac.h

obj-recvaac+=./test/recvAAC.c \
		./src/rtp.c \
		./src/rtp.h \
		./src/aac.c \
		./src/aac.h

target: wmixmsg
	@$(cc) -Wall -o wmix $(obj-wmix) -I./src -L$(ROOT)/libs/lib -I$(ROOT)/libs/include $(obj-flags) -DMAKE_MP3=$(MAKE_MP3) -DMAKE_AAC=$(MAKE_AAC) -DMAKE_WEBRTC_VAD=$(MAKE_WEBRTC_VAD)
	@echo "---------- all complete !! ----------"

wmixmsg:
	@$(cc) -Wall -o wmixMsg $(obj-wmixmsg) -lpthread

fifo:
	@$(cc) -Wall -o fifo -lpthread -I./test -I./src ./test/fifo.c ./test/wmix_user.c ./src/wav.c -lpthread

sendRecvTest:
	@$(cc) -Wall -o sendpcm $(obj-sendpcm) -I./src
	@$(cc) -Wall -o recvpcm $(obj-recvpcm) -I./src
	@$(cc) -Wall -o sendaac $(obj-sendaac) -I./src -L$(ROOT)/libs/lib -I$(ROOT)/libs/include -lfaac -lfaad
	@$(cc) -Wall -o recvaac $(obj-recvaac) -I./src -L$(ROOT)/libs/lib -I$(ROOT)/libs/include -lfaac -lfaad

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
	cd - && \
	rm $(ROOT)/libs/faad2-2.8.8 -rf

libwebrtcvad:
	@tar -xzf $(ROOT)/pkg/webrtc_cut.tar.gz -C $(ROOT)/libs && \
	cd $(ROOT)/libs/webrtc_cut && \
	./build_vad_so.sh $(cc) && \
	cp ./install/* ../ -rf && \
	cd - && \
	rm $(ROOT)/libs/webrtc_cut -rf

libwebrtcaec:
	@tar -xzf $(ROOT)/pkg/webrtc_cut.tar.gz -C $(ROOT)/libs && \
	cd $(ROOT)/libs/webrtc_cut && \
	./build_aec_so.sh $(cc) && \
	cp ./install/* ../ -rf && \
	cd - && \
	rm $(ROOT)/libs/webrtc_cut -rf

libwebrtcaecm:
	@tar -xzf $(ROOT)/pkg/webrtc_cut.tar.gz -C $(ROOT)/libs && \
	cd $(ROOT)/libs/webrtc_cut && \
	./build_aecm_so.sh $(cc) && \
	cp ./install/* ../ -rf && \
	cd - && \
	rm $(ROOT)/libs/webrtc_cut -rf

libwebrtcns:
	@tar -xzf $(ROOT)/pkg/webrtc_cut.tar.gz -C $(ROOT)/libs && \
	cd $(ROOT)/libs/webrtc_cut && \
	./build_ns_so.sh $(cc) && \
	cp ./install/* ../ -rf && \
	cd - && \
	rm $(ROOT)/libs/webrtc_cut -rf

libwebrtcagc:
	@tar -xzf $(ROOT)/pkg/webrtc_cut.tar.gz -C $(ROOT)/libs && \
	cd $(ROOT)/libs/webrtc_cut && \
	./build_agc_so.sh $(cc) && \
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

cleanall: clean
	@rm -rf $(ROOT)/libs/* -rf

clean:
	@rm -rf ./wmix ./wmixMsg ./sendpcm ./recvpcm ./sendaac ./recvaac ./fifo
