cross:=arm-linux-gnueabihf
# cross:=arm-himix200-linux
# cross:=arm-himix100-linux

# 选择启用音频库 0/关 1/启用
MAKE_MP3=1
MAKE_AAC=1

host:=
cc:=gcc

ifdef cross
	host=$(cross)
	cc=$(cross)-gcc
endif

ROOT=$(shell pwd)

# BASE
obj-wmix+=./src/wmix.c \
		./src/wmix.h \
		./src/wav.c \
		./src/wav.h \
		./src/rtp.c \
		./src/rtp.h \
		./src/g711codec.c \
		./src/g711codec.h
obj-flags += -lm -lpthread -lasound -ldl

# MP3 LIB
ifeq ($(MAKE_MP3),1)
obj-wmix+=./src/id3.c ./src/id3.h
obj-flags += -lmad
endif

# AAC LIB
ifeq ($(MAKE_AAC),1)
obj-wmix+=./src/aac.c ./src/aac.h
obj-flags += -lfaac -lfaad
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
	@$(cc) -Wall -o wmix $(obj-wmix) -I./src -L$(ROOT)/libs/lib -I$(ROOT)/libs/include $(obj-flags) -DMAKE_MP3=$(MAKE_MP3) -DMAKE_AAC=$(MAKE_AAC)
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

libs: libmad libfaac libfaad
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

cleanall: clean
	@rm -rf $(ROOT)/libs/* -rf

clean:
	@rm -rf ./wmix ./wmixMsg ./sendpcm ./recvpcm ./sendaac ./recvaac ./fifo
