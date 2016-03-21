#
# Makefile to build raberrytv with OpenMAX both for audio and video output
# 
OBJECTS= vidplayer.o

CC=gcc
CFLAGS=     -Isrc -I. -Isrc/modules/ -I.. -I../../lib -I/opt/vc/include -I/opt/vc/src/hello_pi/libs/ilclient -I/opt/vc/include/interface/vcos/pthreads \
            -DSTANDALONE -D__STDC_CONSTANT_MACROS -D__STDC_LIMIT_MACROS -DTARGET_POSIX -D_LINUX -fPIC -DPIC -D_REENTRANT \
	    -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64 -U_FORTIFY_SOURCE -Wall -g -DHAVE_LIBOPENMAX=2 \
	    -DOMX -DOMX_SKIP64BIT -ftree-vectorize -pipe -DUSE_EXTERNAL_OMX -DHAVE_LIBBCM_HOST -DUSE_EXTERNAL_LIBBCM_HOST \
	    -DUSE_VCHIQ_ARM -Wno-psabi -DHAVE_CONFIG_H
	    
LDFLAGS=    -L/opt/vc/src/hello_pi/libs/ilclient -L/opt/vc/lib
	    
LDLIBS= -lpthread -lopenmaxil -lbcm_host -lvcos -lvchiq_arm -lilclient -lavformat -lavfilter -lavcodec -lavutil

vidplayer.o: vidplayer.c
	$(CC) $(CFLAGS) -c vidplayer.c

clean:
	rm -f *.o
	rm -f vidplayer

all: $(OBJECTS)
	$(CC) $(CFLAGS) -o vidplayer $(OBJECTS) $(LDFLAGS) $(LDLIBS)
