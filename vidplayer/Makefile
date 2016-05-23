#
# Makefile to build raberrytv with OpenMAX both for audio and video output
# 
OBJECTS= vidplayer.o streamplayer.o

INCDIR=$(STAGING_DIR)/usr/include/
HELLOPIDIR=$(STAGING_DIR)/usr/src/hello_pi/

LIBDIR=$(TARGET_DIR)/usr/lib/

CC=gcc
CXX=g++
CFLAGS= -I . \
		-I $(INCDIR)interface/vcos \
		-I $(INCDIR)interface/vcos/pthreads \
		-I $(INCDIR)interface/vmcs_host/khronos/IL \
		-I $(INCDIR)interface/vmcs_host/linux \
		-I $(HELLOPIDIR)libs/ilclient \
        -DSTANDALONE -D__STDC_CONSTANT_MACROS -D__STDC_LIMIT_MACROS -DTARGET_POSIX -D_LINUX -fPIC -DPIC -D_REENTRANT \
	    -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64 -U_FORTIFY_SOURCE -Wall -g -DHAVE_LIBOPENMAX=2 \
	    -DOMX -DOMX_SKIP64BIT -ftree-vectorize -pipe -DUSE_EXTERNAL_OMX -DHAVE_LIBBCM_HOST -DUSE_EXTERNAL_LIBBCM_HOST \
	    -DUSE_VCHIQ_ARM -Wno-psabi -DHAVE_CONFIG_H
	    
LDFLAGS=  -L$(LIBDIR)
	    
LDLIBS= -lm -lpthread -lvcos -lvchiq_arm -lGLESv2 -lEGL -lopenmaxil -lbcm_host -lvcos -lvchostif -lvchiq_arm -lilclient -lavformat -lavfilter -lavcodec -lavutil

cJSON/cJSON.o: cJSON/cJSON.o
	$(MAKE) -C cJSON

monitor_mapFile.o: monitor_mapFile.c
	$(CC) $(CFLAGS) -c monitor_mapFile.c  

vidplayer.o: vidplayer.c
	$(CC) $(CFLAGS) -c vidplayer.c

streamplayer.o: streamplayer.c
	$(CC) $(CFLAGS) -c streamplayer.c

OMX_utils.o: OMX_utils.c
	$(CC) $(CFLAGS) -c OMX_utils.c

clean:
	rm -f *.o
	rm -f vidplayer
	rm -f streamplayer

all: streamplayer.o vidplayer.o OMX_utils.o monitor_mapFile.o cJSON/cJSON.o
	$(CC) $(CFLAGS) -o vidplayer vidplayer.o OMX_utils.o monitor_mapFile.o cJSON/cJSON.o $(LDFLAGS) $(LDLIBS)
	$(CC) $(CFLAGS) -o streamplayer streamplayer.o $(LDFLAGS) $(LDLIBS)

