BIN=raspi2png
OBJS=raspi2png.o cJSON.o

INCDIR=$(STAGING_DIR)/usr/include/
LIBDIR=$(TARGET_DIR)/usr/lib/

CFLAGS=-DSTANDALONE -D__STDC_CONSTANT_MACROS -D__STDC_LIMIT_MACROS -DTARGET_POSIX -D_LINUX -fPIC -DPIC -D_REENTRANT -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64 -U_FORTIFY_SOURCE -Wall -g -DHAVE_LIBOPENMAX=2 -DOMX -DOMX_SKIP64BIT -ftree-vectorize -pipe -DUSE_EXTERNAL_OMX -DHAVE_LIBBCM_HOST -DUSE_EXTERNAL_LIBBCM_HOST -DUSE_VCHIQ_ARM -Wno-psabi

LDFLAGS=-L$(LIBDIR)  -lcurl -lpng -ljpeg -lvchostif -lbcm_host -lvcos -lvchiq_arm -lpthread -lrt -lm

INCLUDES= -I . \
		-I $(INCDIR) \
		-I $(INCDIR)interface/vcos \
		-I $(INCDIR)interface/vcos/pthreads \
		-I $(INCDIR)interface/vmcs_host/khronos/IL \
		-I $(INCDIR)interface/vmcs_host/linux \
		-I $(HELLOPIDIR)libs/ilclient 

all: $(BIN)

%.o: %.c
	@rm -f $@ 
	$(CC) -fpermissive $(CFLAGS) $(INCLUDES) -g -c $< -o $@ -Wno-deprecated-declarations

$(BIN): $(OBJS)
	$(CXX) -o $@ -Wl,--whole-archive $(OBJS) $(LDFLAGS) -Wl,--no-whole-archive -rdynamic

clean:
	for i in $(OBJS); do (if test -e "$$i"; then ( rm $$i ); fi ); done
	@rm -f $(BIN)

