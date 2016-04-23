BIN=skyrocket.bin

OBJS=rsMath/rsTrigonometry.c rsMath/rsMatrix.cpp rsMath/rsQuat.cpp rsMath/rsVec.cpp mire.o cJSON.o rsMath.o skyrocket.o skyrocket_flare.o skyrocket_particle.o skyrocket_smoke.o skyrocket_shockwave.o skyrocket_textures.o  driver.o 

CFLAGS=-DSTANDALONE -D__STDC_CONSTANT_MACROS -D__STDC_LIMIT_MACROS -DTARGET_POSIX -D_LINUX -fPIC -DPIC -D_REENTRANT -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64 -U_FORTIFY_SOURCE -Wall -g -DHAVE_LIBOPENMAX=2 -DOMX -DOMX_SKIP64BIT -ftree-vectorize -pipe -DUSE_EXTERNAL_OMX -DHAVE_LIBBCM_HOST -DUSE_EXTERNAL_LIBBCM_HOST -DUSE_VCHIQ_ARM -Wno-psabi

LDFLAGS=-L$(LIBDIR)  -lGLESv2 -lEGL  -lvchostif -lbcm_host -lvcos -lvchiq_arm -lpthread -lrt -lm 

INCLUDES= -I . \
		-I $(INCDIR)interface/vcos \
		-I $(INCDIR)interface/vcos/pthreads \
		-I $(INCDIR)interface/vmcs_host/khronos/IL \
		-I $(INCDIR)interface/vmcs_host/linux \
		-I $(HELLOPIDIR)libs/ilclient 

all: $(BIN)

%.o: %.c
	@rm -f $@ 
	$(CC) -fpermissive $(CFLAGS) $(INCLUDES) -g -c $< -o $@ -Wno-deprecated-declarations

%.o: %.cpp
	@rm -f $@ 
	$(CXX) -fpermissive $(CFLAGS) $(INCLUDES) -g -c $< -o $@ -Wno-deprecated-declarations

fireworks: $(OBJS)
	$(CXX) -o $@ -Wl,--whole-archive $(OBJS) $(LDFLAGS) -Wl,--no-whole-archive -rdynamic

%.a: $(OBJS)
	$(AR) r $@ $^

clean:
	for i in $(OBJS); do (if test -e "$$i"; then ( rm $$i ); fi ); done
	@rm -f $(BIN)




#
# Makefile to build raberrytv with OpenMAX both for audio and video output
# 
# OBJECTS= vidplayer.o

# INCDIR=$(STAGING_DIR)/usr/include/
# HELLOPIDIR=$(STAGING_DIR)/usr/src/hello_pi/

# LIBDIR=$(TARGET_DIR)/usr/lib/

# CC=gcc
# CFLAGS= -I . \
# 		-I $(INCDIR)interface/vcos \
# 		-I $(INCDIR)interface/vcos/pthreads \
# 		-I $(INCDIR)interface/vmcs_host/khronos/IL \
# 		-I $(INCDIR)interface/vmcs_host/linux \
# 		-I $(HELLOPIDIR)libs/ilclient \
#         -DSTANDALONE -D__STDC_CONSTANT_MACROS -D__STDC_LIMIT_MACROS -DTARGET_POSIX -D_LINUX -fPIC -DPIC -D_REENTRANT \
# 	    -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64 -U_FORTIFY_SOURCE -Wall -g -DHAVE_LIBOPENMAX=2 \
# 	    -DOMX -DOMX_SKIP64BIT -ftree-vectorize -pipe -DUSE_EXTERNAL_OMX -DHAVE_LIBBCM_HOST -DUSE_EXTERNAL_LIBBCM_HOST \
# 	    -DUSE_VCHIQ_ARM -Wno-psabi -DHAVE_CONFIG_H
	    
# LDFLAGS=  -L$(LIBDIR)
	    
# LDLIBS= -lpthread -lopenmaxil -lbcm_host -lvcos -lvchiq_arm -lilclient

# vidplayer.o: vidplayer.c
# 	$(CC) $(CFLAGS) -c vidplayer.c

# clean:
# 	rm -f *.o
# 	rm -f vidplayer

# all: $(OBJECTS)
# 	$(CC) $(CFLAGS) -o vidplayer $(OBJECTS) $(LDFLAGS) $(LDLIBS)
