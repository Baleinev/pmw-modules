#include <stdio.h>
#include <stdlib.h>

#include <bcm_host.h>
#include <vcos_logging.h>

#include <ilclient.h>

#include <interface/vmcs_host/khronos/IL/OMX_Core.h>
#include <interface/vmcs_host/khronos/IL/OMX_Component.h>

#ifdef OMX_SKIP64BIT
OMX_TICKS ToOMXTime(int64_t pts);
#else
#define FromOMXTime(x) (x)
#endif

int OMX_configureClockComponentForPlayback(COMPONENT_T *clockComponent);

int OMX_setVideoDecoderInputFormat(COMPONENT_T *component,unsigned int fpsscale,unsigned int fpsrate,unsigned int img_width,unsigned int img_height);

int OMX_createComponent(ILCLIENT_T  *handle, char *componentName, COMPONENT_T **component,ILCLIENT_CREATE_FLAGS_T flags);

int OMX_sendDecoderConfig(COMPONENT_T *component,char *extradata,int extradatasize);

char * OMX_getStateString(OMX_HANDLETYPE handle);

char * OMX_err2str(int err);

void OMX_printClockState(COMPONENT_T *clockComponent);

int OMX_startClock(COMPONENT_T *clockComponent);

int OMX_initClock(COMPONENT_T *clockComponent);

int OMX_changeStateToExecuting(COMPONENT_T *component);

int OMX_changeStateToIdle(COMPONENT_T *component);
