#include <stdio.h>
#include <stdlib.h>

#include <bcm_host.h>
#include <vcos_logging.h>

#include <ilclient.h>

#include <interface/vmcs_host/khronos/IL/OMX_Core.h>
#include <interface/vmcs_host/khronos/IL/OMX_Component.h>

#include "OMX_utils.h"

#ifdef OMX_SKIP64BIT
OMX_TICKS ToOMXTime(int64_t pts)
{
    OMX_TICKS ticks;
    ticks.nLowPart = pts;
    ticks.nHighPart = pts >> 32;
    return ticks;
}
#else
#define FromOMXTime(x) (x)
#endif

int OMX_configureClockComponentForPlayback(COMPONENT_T *clockComponent)
{
  OMX_COMPONENTTYPE *clock = ilclient_get_handle(clockComponent);

  OMX_TIME_CONFIG_ACTIVEREFCLOCKTYPE refClock;

  refClock.nSize = sizeof(OMX_TIME_CONFIG_ACTIVEREFCLOCKTYPE);
  refClock.nVersion.nVersion = OMX_VERSION;
  refClock.eClock = OMX_TIME_RefClockVideo; // OMX_CLOCKPORT0;

  int err = OMX_SetConfig(ilclient_get_handle(clockComponent),OMX_IndexConfigTimeActiveRefClock, &refClock);

  if(err != OMX_ErrorNone)
  {
      fprintf(stderr, "[OMX_configureClockComponentForPlayback] COMXCoreComponent::SetConfig - %s failed with omx_err(0x%x)\n","clock", err);
      return -1;
  }

  OMX_TIME_CONFIG_SCALETYPE scaleType;
  scaleType.nSize = sizeof(OMX_TIME_CONFIG_SCALETYPE);
  scaleType.nVersion.nVersion = OMX_VERSION;
  scaleType.xScale = 0x00010000;

  err = OMX_SetConfig(ilclient_get_handle(clockComponent),OMX_IndexConfigTimeScale, &scaleType);

  if(err != OMX_ErrorNone){
    fprintf(stderr, "[OMX_configureClockComponentForPlayback] COMXCoreComponent::SetConfig - %s failed with omx_err(0x%x)\n","clock", err);
    return -2;
  }
}

int OMX_setVideoDecoderInputFormat(COMPONENT_T *component,unsigned int fpsscale,unsigned int fpsrate,unsigned int img_width,unsigned int img_height)
{
    int err;

    // set input video format
    printf("[OMX_setVideoDecoderInputFormat] Setting video decoder format\n");
    OMX_VIDEO_PARAM_PORTFORMATTYPE videoPortFormat;

    memset(&videoPortFormat, 0, sizeof(OMX_VIDEO_PARAM_PORTFORMATTYPE));
    videoPortFormat.nSize = sizeof(OMX_VIDEO_PARAM_PORTFORMATTYPE);
    videoPortFormat.nVersion.nVersion = OMX_VERSION;
    videoPortFormat.nPortIndex = 130;

    err = OMX_GetParameter(ilclient_get_handle(component),
        OMX_IndexParamVideoPortFormat, &videoPortFormat);
    if (err != OMX_ErrorNone) {
        fprintf(stderr, "Error getting video decoder format %s\n", OMX_err2str(err));
        return -1;
    }

    videoPortFormat.nPortIndex = 130;
    videoPortFormat.nIndex = 0;
    videoPortFormat.eCompressionFormat = OMX_VIDEO_CodingAVC;
    videoPortFormat.eColorFormat = OMX_COLOR_FormatUnused;
    videoPortFormat.xFramerate = 0;

    if (fpsscale > 0 && fpsrate > 0)
        videoPortFormat.xFramerate = (long long)(1<<16)*fpsrate / fpsscale;
    else
        videoPortFormat.xFramerate = 25 * (1<<16);
    
    printf("[OMX_setVideoDecoderInputFormat] FPS num %d den %d\n", fpsrate, fpsscale);
    printf("[OMX_setVideoDecoderInputFormat] Set frame rate to %d\n", videoPortFormat.xFramerate);

    err = OMX_SetParameter(ilclient_get_handle(component),OMX_IndexParamVideoPortFormat, &videoPortFormat);

    if (err != OMX_ErrorNone) {
        fprintf(stderr, "[OMX_setVideoDecoderInputFormat] Error setting video decoder format %s\n", OMX_err2str(err));
        return -2;
    } else
        printf("Video decoder format set up ok\n");
    

    OMX_PARAM_PORTDEFINITIONTYPE portParam;

    memset(&portParam, 0, sizeof( OMX_PARAM_PORTDEFINITIONTYPE));
    portParam.nSize = sizeof( OMX_PARAM_PORTDEFINITIONTYPE);
    portParam.nVersion.nVersion = OMX_VERSION;

    portParam.nPortIndex = 130;

    err =  OMX_GetParameter(ilclient_get_handle(component),OMX_IndexParamPortDefinition, &portParam);

    if(err != OMX_ErrorNone)
    {
        fprintf(stderr, "COMXVideo::Open error OMX_IndexParamPortDefinition omx_err(0x%08x)\n", err);
        return -3;
    }

    printf("[OMX_setVideoDecoderInputFormat] Default framerate %d\n", portParam.format.video.xFramerate);

    portParam.nPortIndex = 130;

    portParam.format.video.nFrameWidth  = img_width;
    portParam.format.video.nFrameHeight = img_height;

    err =  OMX_SetParameter(ilclient_get_handle(component),OMX_IndexParamPortDefinition, &portParam);

    if(err != OMX_ErrorNone)
    {
        fprintf(stderr, "COMXVideo::Open error OMX_IndexParamPortDefinition omx_err(0x%08x)\n", err);
        return -4;
    }

    return 0;
}

int OMX_createComponent(ILCLIENT_T  *handle, char *componentName, COMPONENT_T **component,ILCLIENT_CREATE_FLAGS_T flags)
{
  int err;

  printf("[%s] ilclient_create_component %s \n",__FUNCTION__,componentName);fflush(0);

  err = ilclient_create_component(handle,component,componentName,flags);

  if (err == -1) {
      fprintf(stderr, "[OMX_createComponent] %s create failed\n",componentName);
      return 1;
  }
  printf("[%s] New state: %s\n",__FUNCTION__,OMX_getStateString(ilclient_get_handle(*component)));

  printf("[%s] ilclient_change_component_state  %s\n",__FUNCTION__,componentName);fflush(0);

  err = ilclient_change_component_state(*component,OMX_StateIdle);

  if (err < 0) {
      fprintf(stderr, "[OMX_createComponent] Couldn't change %s state to Idle\n",componentName);
      return 1;
  }
  printf("[%s] New state: %s\n",__FUNCTION__,OMX_getStateString(ilclient_get_handle(*component)));
}

int OMX_sendDecoderConfig(COMPONENT_T *component,char *extradata,int extradatasize)
{
    OMX_ERRORTYPE omx_err   = OMX_ErrorNone;

    if(extradatasize > 0 && extradata != NULL)
    {
        OMX_BUFFERHEADERTYPE *omx_buffer = ilclient_get_input_buffer(component,130,1);

        if(omx_buffer == NULL)
        {
            fprintf(stderr, "[%s][ERROR] Buffer error 0x%08x", __func__, omx_err);
            return -1;
        }

        omx_buffer->nOffset = 0;
        omx_buffer->nFilledLen = extradatasize;

        if(omx_buffer->nFilledLen > omx_buffer->nAllocLen)
        {
            fprintf(stderr, "[%s][ERROR] omx_buffer->nFilledLen > omx_buffer->nAllocLen",  __func__);
            return -2;
        }

        memset((unsigned char *)omx_buffer->pBuffer, 0x0, omx_buffer->nAllocLen);
        memcpy((unsigned char *)omx_buffer->pBuffer, extradata, omx_buffer->nFilledLen);

        omx_buffer->nFlags = OMX_BUFFERFLAG_CODECCONFIG | OMX_BUFFERFLAG_ENDOFFRAME;

        omx_err =  OMX_EmptyThisBuffer(ilclient_get_handle(component),omx_buffer);

        if (omx_err != OMX_ErrorNone)
        {
            fprintf(stderr, "[%s][ERROR] OMX_EmptyThisBuffer() failed with result(0x%x)\n", __func__, omx_err);
            return -3;
        } else
            printf("[%s] Config sent, emptying buffer %d\n",__func__,extradatasize);
        
    }
    return 0;
}

char * OMX_getStateString(OMX_HANDLETYPE handle)
{
    OMX_STATETYPE state;
    OMX_ERRORTYPE err;

    err = OMX_GetState(handle, &state);
    if (err != OMX_ErrorNone) {
        fprintf(stderr, "Error on getting state\n");
        exit(1);
    }
    switch (state) {
        case OMX_StateLoaded:           return "StateLoaded";
        case OMX_StateIdle:             return "StateIdle";
        case OMX_StateExecuting:        return "StateExecuting";
        case OMX_StatePause:            return "StatePause";
        case OMX_StateWaitForResources: return "StateWait";
        case OMX_StateInvalid:          return "StateInvalid";
        default:                        return "State unknown";
    }
}

char * OMX_err2str(int err)
{

    switch (err) {
        case OMX_ErrorInsufficientResources: return "OMX_ErrorInsufficientResources";
        case OMX_ErrorUndefined: return "OMX_ErrorUndefined";
        case OMX_ErrorInvalidComponentName: return "OMX_ErrorInvalidComponentName";
        case OMX_ErrorComponentNotFound: return "OMX_ErrorComponentNotFound";
        case OMX_ErrorInvalidComponent: return "OMX_ErrorInvalidComponent";
        case OMX_ErrorBadParameter: return "OMX_ErrorBadParameter";
        case OMX_ErrorNotImplemented: return "OMX_ErrorNotImplemented";
        case OMX_ErrorUnderflow: return "OMX_ErrorUnderflow";
        case OMX_ErrorOverflow: return "OMX_ErrorOverflow";
        case OMX_ErrorHardware: return "OMX_ErrorHardware";
        case OMX_ErrorInvalidState: return "OMX_ErrorInvalidState";
        case OMX_ErrorStreamCorrupt: return "OMX_ErrorStreamCorrupt";
        case OMX_ErrorPortsNotCompatible: return "OMX_ErrorPortsNotCompatible";
        case OMX_ErrorResourcesLost: return "OMX_ErrorResourcesLost";
        case OMX_ErrorNoMore: return "OMX_ErrorNoMore";
        case OMX_ErrorVersionMismatch: return "OMX_ErrorVersionMismatch";
        case OMX_ErrorNotReady: return "OMX_ErrorNotReady";
        case OMX_ErrorTimeout: return "OMX_ErrorTimeout";
        case OMX_ErrorSameState: return "OMX_ErrorSameState";
        case OMX_ErrorResourcesPreempted: return "OMX_ErrorResourcesPreempted";
        case OMX_ErrorPortUnresponsiveDuringAllocation: return "OMX_ErrorPortUnresponsiveDuringAllocation";
        case OMX_ErrorPortUnresponsiveDuringDeallocation: return "OMX_ErrorPortUnresponsiveDuringDeallocation";
        case OMX_ErrorPortUnresponsiveDuringStop: return "OMX_ErrorPortUnresponsiveDuringStop";
        case OMX_ErrorIncorrectStateTransition: return "OMX_ErrorIncorrectStateTransition";
        case OMX_ErrorIncorrectStateOperation: return "OMX_ErrorIncorrectStateOperation";
        case OMX_ErrorUnsupportedSetting: return "OMX_ErrorUnsupportedSetting";
        case OMX_ErrorUnsupportedIndex: return "OMX_ErrorUnsupportedIndex";
        case OMX_ErrorBadPortIndex: return "OMX_ErrorBadPortIndex";
        case OMX_ErrorPortUnpopulated: return "OMX_ErrorPortUnpopulated";
        case OMX_ErrorComponentSuspended: return "OMX_ErrorComponentSuspended";
        case OMX_ErrorDynamicResourcesUnavailable: return "OMX_ErrorDynamicResourcesUnavailable";
        case OMX_ErrorMbErrorsInFrame: return "OMX_ErrorMbErrorsInFrame";
        case OMX_ErrorFormatNotDetected: return "OMX_ErrorFormatNotDetected";
        case OMX_ErrorContentPipeOpenFailed: return "OMX_ErrorContentPipeOpenFailed";
        case OMX_ErrorContentPipeCreationFailed: return "OMX_ErrorContentPipeCreationFailed";
        case OMX_ErrorSeperateTablesUsed: return "OMX_ErrorSeperateTablesUsed";
        case OMX_ErrorTunnelingUnsupported: return "OMX_ErrorTunnelingUnsupported";
        default: return "unknown error";
    }
}

void OMX_printClockState(COMPONENT_T *clockComponent)
{
    OMX_ERRORTYPE err = OMX_ErrorNone;
    OMX_TIME_CONFIG_CLOCKSTATETYPE clockState;

    memset(&clockState, 0, sizeof( OMX_TIME_CONFIG_CLOCKSTATETYPE));

    clockState.nSize = sizeof( OMX_TIME_CONFIG_CLOCKSTATETYPE);
    clockState.nVersion.nVersion = OMX_VERSION;

    err = OMX_GetConfig(ilclient_get_handle(clockComponent), 

    OMX_IndexConfigTimeClockState, &clockState);

    if (err != OMX_ErrorNone) {
        fprintf(stderr, "[OMX_printClockState][ERROR] getting clock state %s\n", OMX_err2str(err));
        return;
    }

    switch (clockState.eState) {
        case OMX_TIME_ClockStateRunning:
        printf("[OMX_printClockState] Clock running\n");
    break;
        case OMX_TIME_ClockStateWaitingForStartTime:
        printf("[OMX_printClockState] Clock waiting for start time\n");
    break;
        case OMX_TIME_ClockStateStopped:
        printf("[OMX_printClockState] Clock stopped\n");
    break;
    default:
        printf("Clock in other state\n");
    }
}

int OMX_startClock(COMPONENT_T *clockComponent)
{
  OMX_ERRORTYPE err = OMX_ErrorNone;
  OMX_TIME_CONFIG_CLOCKSTATETYPE clockState;

  memset(&clockState, 0, sizeof( OMX_TIME_CONFIG_CLOCKSTATETYPE));

  clockState.nSize = sizeof( OMX_TIME_CONFIG_CLOCKSTATETYPE);
  clockState.nVersion.nVersion = OMX_VERSION;

  err = OMX_GetConfig(ilclient_get_handle(clockComponent),OMX_IndexConfigTimeClockState, &clockState);

  if (err != OMX_ErrorNone) {
      fprintf(stderr, "[%s][ERROR] Error getting clock state %s\n", __FUNCTION__,OMX_err2str(err));
      return -1;
  }

  clockState.eState = OMX_TIME_ClockStateRunning;
  
  err = OMX_SetConfig(ilclient_get_handle(clockComponent),OMX_IndexConfigTimeClockState, &clockState);

  if (err != OMX_ErrorNone) {
      fprintf(stderr, "[%s][ERROR] Error starting clock %s\n",  __FUNCTION__,OMX_err2str(err));
      return -2;
  }

  return 0;
}

int OMX_initClock(COMPONENT_T *clockComponent)
{
  OMX_TIME_CONFIG_CLOCKSTATETYPE clockState;

   memset(&clockState, 0, sizeof(clockState));

   clockState.nSize = sizeof(clockState);
   clockState.nVersion.nVersion = OMX_VERSION;
   clockState.eState = OMX_TIME_ClockStateWaitingForStartTime;
   clockState.nWaitMask = 1;

   if(clock != NULL && OMX_SetParameter(ilclient_get_handle(clockComponent), OMX_IndexConfigTimeClockState, &clockState) != OMX_ErrorNone)
      return -1;

    return 0;
}

int OMX_changeStateToExecuting(COMPONENT_T *component)
{
  ilclient_change_component_state(component,OMX_StateExecuting);
}

int OMX_changeStateToIdle(COMPONENT_T *component)
{
  ilclient_change_component_state(component,OMX_StateIdle);
}


