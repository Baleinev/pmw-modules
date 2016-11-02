#include <stdio.h>
#include <stdlib.h>

#include <bcm_host.h>
#include <vcos_logging.h>

#include <ilclient.h>

#include <interface/vmcs_host/khronos/IL/OMX_Core.h>
#include <interface/vmcs_host/khronos/IL/OMX_Component.h>

#include "vidplayer.h"
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
    ERR( "[OMX_configureClockComponentForPlayback] COMXCoreComponent::SetConfig - %s failed with omx_err(0x%x)\n","clock", err);
    return -1;
  }

  OMX_TIME_CONFIG_SCALETYPE scaleType;
  scaleType.nSize = sizeof(OMX_TIME_CONFIG_SCALETYPE);
  scaleType.nVersion.nVersion = OMX_VERSION;
  scaleType.xScale = 0x00010000;

  err = OMX_SetConfig(ilclient_get_handle(clockComponent),OMX_IndexConfigTimeScale, &scaleType);

  if(err != OMX_ErrorNone){
    ERR( "[OMX_configureClockComponentForPlayback] COMXCoreComponent::SetConfig - %s failed with omx_err(0x%x)\n","clock", err);
    return -2;
  }

  return 0;
}

int OMX_setVideoDecoderInputFormat(COMPONENT_T *component,unsigned int fpsscale,unsigned int fpsrate,unsigned int img_width,unsigned int img_height)
{
    int err;

    // set input video format
    LOG("[OMX_setVideoDecoderInputFormat] Setting video decoder format");

    OMX_VIDEO_PARAM_PORTFORMATTYPE videoPortFormat;

    memset(&videoPortFormat, 0, sizeof(OMX_VIDEO_PARAM_PORTFORMATTYPE));
    videoPortFormat.nSize = sizeof(OMX_VIDEO_PARAM_PORTFORMATTYPE);
    videoPortFormat.nVersion.nVersion = OMX_VERSION;
    videoPortFormat.nPortIndex = 130;

    err = OMX_GetParameter(ilclient_get_handle(component),OMX_IndexParamVideoPortFormat, &videoPortFormat);

    if (err != OMX_ErrorNone) {
        ERR("Error getting video decoder format %s", OMX_err2str(err));
        return -1;
    }

    videoPortFormat.nPortIndex = 130;
    videoPortFormat.nIndex = 0;
    videoPortFormat.eCompressionFormat = OMX_VIDEO_CodingAVC;
    videoPortFormat.eColorFormat = OMX_COLOR_FormatUnused;

    if(fpsscale != 0 && fpsrate != 0)
    {
      if (fpsscale > 0 && fpsrate > 0)
          videoPortFormat.xFramerate = (long long)(1<<16)*fpsrate / fpsscale;
      else
          videoPortFormat.xFramerate = 25 * (1<<16);

      LOG("[OMX_setVideoDecoderInputFormat] FPS num %d den %d", fpsrate, fpsscale);
      LOG("[OMX_setVideoDecoderInputFormat] Set frame rate to %d", videoPortFormat.xFramerate);
    }
    else
      videoPortFormat.xFramerate = 60;

    err = OMX_SetParameter(ilclient_get_handle(component),OMX_IndexParamVideoPortFormat, &videoPortFormat);

    if (err != OMX_ErrorNone) {
        ERR( "[OMX_setVideoDecoderInputFormat] Error setting video decoder format %s", OMX_err2str(err));
        return -2;
    } else
        LOG("Video decoder format set up ok");
    

    OMX_PARAM_PORTDEFINITIONTYPE portParam;

    memset(&portParam, 0, sizeof( OMX_PARAM_PORTDEFINITIONTYPE));
    portParam.nSize = sizeof( OMX_PARAM_PORTDEFINITIONTYPE);
    portParam.nVersion.nVersion = OMX_VERSION;

    portParam.nPortIndex = 130;

    err =  OMX_GetParameter(ilclient_get_handle(component),OMX_IndexParamPortDefinition, &portParam);

    if(err != OMX_ErrorNone)
    {
        ERR( "[OMX_setVideoDecoderInputFormat] OMX_IndexParamPortDefinition: %s", OMX_err2str(err));
        return -3;
    }

    if(img_width != 0 && img_height != 0)
    {
      portParam.nPortIndex = 130;

      portParam.format.video.nFrameWidth  = img_width;
      portParam.format.video.nFrameHeight = img_height;

      err =  OMX_SetParameter(ilclient_get_handle(component),OMX_IndexParamPortDefinition, &portParam);

      if(err != OMX_ErrorNone)
      {
          ERR( "[OMX_setVideoDecoderInputFormat] OMX_IndexParamPortDefinition %s", OMX_err2str(err));
          return -4;
      }
    }
    return 0;
}

int OMX_createComponent(ILCLIENT_T  *handle, char *componentName, COMPONENT_T **component,ILCLIENT_CREATE_FLAGS_T flags)
{
  int err;

  DBG("[%s] ilclient_create_component %s \n",__FUNCTION__,componentName);fflush(0);

  err = ilclient_create_component(handle,component,componentName,flags);

  if (err == -1) {
      ERR("[OMX_createComponent] %s create failed\n",componentName);
      return 1;
  }
  DBG("[%s] New state: %s\n",__FUNCTION__,OMX_getStateString(ilclient_get_handle(*component)));

  DBG("[%s] ilclient_change_component_state  %s\n",__FUNCTION__,componentName);fflush(0);

  err = ilclient_change_component_state(*component,OMX_StateIdle);

  if (err < 0) {
      ERR("[OMX_createComponent] Couldn't change %s state to Idle\n",componentName);
      return 1;
  }
  DBG("[%s] New state: %s\n",__FUNCTION__,OMX_getStateString(ilclient_get_handle(*component)));

  return 0;
}

int OMX_sendDecoderConfig(COMPONENT_T *component,char *extradata,int extradatasize)
{
    OMX_ERRORTYPE omx_err   = OMX_ErrorNone;

    if(extradatasize > 0 && extradata != NULL)
    {
        OMX_BUFFERHEADERTYPE *omx_buffer = ilclient_get_input_buffer(component,130,1);

        if(omx_buffer == NULL)
        {
            ERR( "[%s][ERROR] Buffer error 0x%08x", __func__, omx_err);
            return -1;
        }

        omx_buffer->nOffset = 0;
        omx_buffer->nFilledLen = extradatasize;

        if(omx_buffer->nFilledLen > omx_buffer->nAllocLen)
        {
            ERR( "[%s][ERROR] omx_buffer->nFilledLen > omx_buffer->nAllocLen",  __func__);
            return -2;
        }

        memset((unsigned char *)omx_buffer->pBuffer, 0x0, omx_buffer->nAllocLen);
        memcpy((unsigned char *)omx_buffer->pBuffer, extradata, omx_buffer->nFilledLen);

        omx_buffer->nFlags = OMX_BUFFERFLAG_CODECCONFIG | OMX_BUFFERFLAG_ENDOFFRAME;

        omx_err =  OMX_EmptyThisBuffer(ilclient_get_handle(component),omx_buffer);

        if (omx_err != OMX_ErrorNone)
        {
            ERR( "[%s][ERROR] OMX_EmptyThisBuffer() failed with result(0x%x)\n", __func__, omx_err);
            return -3;
        } else
            DBG("[%s] Config sent, emptying buffer %d\n",__func__,extradatasize);
        
    }
    return 0;
}


static const char states[][128] = {
  "StateLoaded",
  "StateIdle",
  "StateExecuting",
  "StatePause",
  "StateWait",
  "StateInvalid",
  "State unknown"};

char * OMX_getStateString(OMX_HANDLETYPE handle)
{

  OMX_STATETYPE state;
  OMX_ERRORTYPE err;

  err = OMX_GetState(handle, &state);
  
  if (err != OMX_ErrorNone) {
      ERR( "Error on getting state\n");
      exit(1);
  }

  switch (state) {
      case OMX_StateLoaded:           return states[0];
      case OMX_StateIdle:             return states[1];
      case OMX_StateExecuting:        return states[2];
      case OMX_StatePause:            return states[3];
      case OMX_StateWaitForResources: return states[4];
      case OMX_StateInvalid:          return states[5];
      default:                        return states[6];
  }
}

static const char errors[][128] = {
  "OMX_ErrorInsufficientResources",
  "OMX_ErrorUndefined",
  "OMX_ErrorInvalidComponentName",
  "OMX_ErrorComponentNotFound",
  "OMX_ErrorInvalidComponent",
  "OMX_ErrorBadParameter",
  "OMX_ErrorNotImplemented",
  "OMX_ErrorUnderflow",
  "OMX_ErrorOverflow",
  "OMX_ErrorHardware",
  "OMX_ErrorInvalidState",
  "OMX_ErrorStreamCorrupt",
  "OMX_ErrorPortsNotCompatible",
  "OMX_ErrorResourcesLost",
  "OMX_ErrorNoMore",
  "OMX_ErrorVersionMismatch",
  "OMX_ErrorNotReady",
  "OMX_ErrorTimeout",
  "OMX_ErrorSameState",
  "OMX_ErrorResourcesPreempted",
  "OMX_ErrorPortUnresponsiveDuringAllocation",
  "OMX_ErrorPortUnresponsiveDuringDeallocation",
  "OMX_ErrorPortUnresponsiveDuringStop",
  "OMX_ErrorIncorrectStateTransition",
  "OMX_ErrorIncorrectStateOperation",
  "OMX_ErrorUnsupportedSetting",
  "OMX_ErrorUnsupportedIndex",
  "OMX_ErrorBadPortIndex",
  "OMX_ErrorPortUnpopulated",
  "OMX_ErrorComponentSuspended",
  "OMX_ErrorDynamicResourcesUnavailable",
  "OMX_ErrorMbErrorsInFrame",
  "OMX_ErrorFormatNotDetected",
  "OMX_ErrorContentPipeOpenFailed",
  "OMX_ErrorContentPipeCreationFailed",
  "OMX_ErrorSeperateTablesUsed",
  "OMX_ErrorTunnelingUnsupported",
  "Unknow error"};

char * OMX_err2str(int err)
{

  switch (err) {
      case OMX_ErrorInsufficientResources: return errors[0];
      case OMX_ErrorUndefined: return errors[1];
      case OMX_ErrorInvalidComponentName: return errors[2];
      case OMX_ErrorComponentNotFound: return errors[3];
      case OMX_ErrorInvalidComponent: return errors[4];
      case OMX_ErrorBadParameter: return errors[5];
      case OMX_ErrorNotImplemented: return errors[6];
      case OMX_ErrorUnderflow: return errors[7];
      case OMX_ErrorOverflow: return errors[8];
      case OMX_ErrorHardware: return errors[9];
      case OMX_ErrorInvalidState: return errors[10];
      case OMX_ErrorStreamCorrupt: return errors[11];
      case OMX_ErrorPortsNotCompatible: return errors[12];
      case OMX_ErrorResourcesLost: return errors[13];
      case OMX_ErrorNoMore: return errors[14];
      case OMX_ErrorVersionMismatch: return errors[15];
      case OMX_ErrorNotReady: return errors[16];
      case OMX_ErrorTimeout: return errors[17];
      case OMX_ErrorSameState: return errors[18];
      case OMX_ErrorResourcesPreempted: return errors[19];
      case OMX_ErrorPortUnresponsiveDuringAllocation: return errors[20];
      case OMX_ErrorPortUnresponsiveDuringDeallocation: return errors[21];
      case OMX_ErrorPortUnresponsiveDuringStop: return errors[22];
      case OMX_ErrorIncorrectStateTransition: return errors[23];
      case OMX_ErrorIncorrectStateOperation: return errors[24];
      case OMX_ErrorUnsupportedSetting: return errors[25];
      case OMX_ErrorUnsupportedIndex: return errors[26];
      case OMX_ErrorBadPortIndex: return errors[27];
      case OMX_ErrorPortUnpopulated: return errors[28];
      case OMX_ErrorComponentSuspended: return errors[29];
      case OMX_ErrorDynamicResourcesUnavailable: return errors[30];
      case OMX_ErrorMbErrorsInFrame:return errors[31];
      case OMX_ErrorFormatNotDetected: return errors[32];
      case OMX_ErrorContentPipeOpenFailed: return errors[33];
      case OMX_ErrorContentPipeCreationFailed:return errors[34];
      case OMX_ErrorSeperateTablesUsed:return errors[35];
      case OMX_ErrorTunnelingUnsupported: return errors[36];
      default:return errors[37];
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
        ERR( "[OMX_printClockState][ERROR] getting clock state %s\n", OMX_err2str(err));
        return;
    }

    switch (clockState.eState) {
        case OMX_TIME_ClockStateRunning:
        DBG("[OMX_printClockState] Clock running\n");
    break;
        case OMX_TIME_ClockStateWaitingForStartTime:
        DBG("[OMX_printClockState] Clock waiting for start time\n");
    break;
        case OMX_TIME_ClockStateStopped:
        DBG("[OMX_printClockState] Clock stopped\n");
    break;
    default:
        DBG("Clock in other state\n");
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
      ERR( "[%s][ERROR] Error getting clock state %s\n", __FUNCTION__,OMX_err2str(err));
      return -1;
  }

  clockState.eState = OMX_TIME_ClockStateRunning;
  
  err = OMX_SetConfig(ilclient_get_handle(clockComponent),OMX_IndexConfigTimeClockState, &clockState);

  if (err != OMX_ErrorNone) {
      ERR( "[%s][ERROR] Error starting clock %s\n",  __FUNCTION__,OMX_err2str(err));
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

int OMX_stopClock(COMPONENT_T *clockComponent)
{
  OMX_TIME_CONFIG_CLOCKSTATETYPE clockState;

   memset(&clockState, 0, sizeof(clockState));

   clockState.nSize = sizeof(clockState);
   clockState.nVersion.nVersion = OMX_VERSION;
   clockState.eState = OMX_TIME_ClockStateStopped;
   clockState.nWaitMask = 1;

   if(clock != NULL && OMX_SetParameter(ilclient_get_handle(clockComponent), OMX_IndexConfigTimeClockState, &clockState) != OMX_ErrorNone)
      return -1;

    return 0;
}


int OMX_changeStateToExecuting(COMPONENT_T *component)
{
  ilclient_change_component_state(component,OMX_StateExecuting);

  return 0;
}

int OMX_changeStateToIdle(COMPONENT_T *component)
{
  ilclient_change_component_state(component,OMX_StateIdle);

  return 0;
}

int OMX_send_EOS_to_decoder(COMPONENT_T *component)
{
  OMX_ERRORTYPE omx_err   = OMX_ErrorNone;

  OMX_BUFFERHEADERTYPE *omx_buffer = ilclient_get_input_buffer(component,130,1);

  if(omx_buffer == NULL)
  {
      ERR( "[%s][ERROR] Buffer error 0x%08x", __func__, omx_err);
      return -1;
  }

  omx_buffer->nOffset = 0;
  omx_buffer->nFilledLen = 0;
  omx_buffer->nFlags = OMX_BUFFERFLAG_TIME_UNKNOWN | OMX_BUFFERFLAG_EOS;
  
  OMX_EmptyThisBuffer(ILC_GET_HANDLE(component), omx_buffer);

  return 0;
}
