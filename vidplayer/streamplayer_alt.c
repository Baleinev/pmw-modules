#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

#include <interface/vmcs_host/khronos/IL/OMX_Core.h>
#include <interface/vmcs_host/khronos/IL/OMX_Component.h>

#include <bcm_host.h>
#include <vcos_logging.h>

#define VCOS_LOG_CATEGORY (&il_ffmpeg_log_category)
static VCOS_LOG_CAT_T il_ffmpeg_log_category;

#include <ilclient.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#define INBUF_SIZE 4096
#define AUDIO_INBUF_SIZE 20480
#define AUDIO_REFILL_THRESH 4096

#define MAXBUF 65536

char buffer[MAXBUF];
int sock,buflen;
unsigned sinlen;
struct sockaddr_in sock_in;
struct sockaddr_in recv_sock;

void printState(OMX_HANDLETYPE handle) 
{
    OMX_STATETYPE state;
    OMX_ERRORTYPE err;

    err = OMX_GetState(handle, &state);
    if (err != OMX_ErrorNone) {
        fprintf(stderr, "Error on getting state\n");
        exit(1);
    }
    switch (state) {
    case OMX_StateLoaded:           printf("[printState] StateLoaded\n"); break;
    case OMX_StateIdle:             printf("[printState] StateIdle\n"); break;
    case OMX_StateExecuting:        printf("[printState] StateExecuting\n"); break;
    case OMX_StatePause:            printf("[printState] StatePause\n"); break;
    case OMX_StateWaitForResources: printf("[printState] StateWait\n"); break;
    case OMX_StateInvalid:          printf("[printState] StateInvalid\n"); break;
    default:                        printf("[printState] State unknown\n"); break;
    }
}

char *err2str(int err) {
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

void eos_callback(void *userdata, COMPONENT_T *comp, OMX_U32 data) {
    printf("Got eos event\n");
}

void error_callback(void *userdata, COMPONENT_T *comp, OMX_U32 data) {
    printf("OMX error %s\n", err2str(data));
}

void port_settings_callback(void *userdata, COMPONENT_T *comp, OMX_U32 data) {
    printf("Got port Settings event\n");
}

void empty_buffer_done_callback(void *userdata, COMPONENT_T *comp) {
    printf("Got empty buffer done\n");
}

OMX_ERRORTYPE copy_into_buffer_and_empty(COMPONENT_T *component, OMX_BUFFERHEADERTYPE *buff_header) {

    OMX_ERRORTYPE r;

    r = OMX_EmptyThisBuffer(ilclient_get_handle(component),buff_header);

    if (r != OMX_ErrorNone) {
      fprintf(stderr, "[copy_into_buffer_and_empty][ERROR] Empty buffer error %s\n",
		  err2str(r));
    } 
    else
    {
      printf("[copy_into_buffer_and_empty] Emptying buffer %p\n", buff_header);
    }
    return r;
}

int img_width, img_height;

uint8_t extradatasize;
void *extradata;

OMX_ERRORTYPE set_video_decoder_config(COMPONENT_T *component)
{
  OMX_ERRORTYPE omx_err   = OMX_ErrorNone;

  /* Send decoder config */
  if(extradatasize > 0 && extradata != NULL)
  {
    OMX_BUFFERHEADERTYPE *omx_buffer = ilclient_get_input_buffer(component,130,1 /* block */);

    if(omx_buffer == NULL)
    {
    	fprintf(stderr, "[SendDecoderConfig] %s - buffer error 0x%08x", __func__, omx_err);
      return 0;
    }

    omx_buffer->nOffset = 0;
    omx_buffer->nFilledLen = extradatasize;

    if(omx_buffer->nFilledLen > omx_buffer->nAllocLen)
    {
      fprintf(stderr, "[SendDecoderConfig] %s - omx_buffer->nFilledLen > omx_buffer->nAllocLen",  __func__);
      return 0;
    }

    memset((unsigned char *)omx_buffer->pBuffer, 0x0, omx_buffer->nAllocLen);
    memcpy((unsigned char *)omx_buffer->pBuffer, extradata, omx_buffer->nFilledLen);

    omx_buffer->nFlags = OMX_BUFFERFLAG_CODECCONFIG | OMX_BUFFERFLAG_ENDOFFRAME;
  
    omx_err =  OMX_EmptyThisBuffer(ilclient_get_handle(component),omx_buffer);

    if (omx_err != OMX_ErrorNone)
    {
      fprintf(stderr, "[SendDecoderConfig] %s - OMX_EmptyThisBuffer() failed with result(0x%x)\n", __func__, omx_err);
      return 0;
    } 
    else
    {
      printf("Config sent, emptying buffer %d\n", extradatasize);
    }
  }
  return 1;
}

OMX_ERRORTYPE set_video_decoder_input_format(COMPONENT_T *component)
{
  int err;

  // set input video format
  printf("[set_video_decoder_input_format] Setting video decoder format\n");
  OMX_VIDEO_PARAM_PORTFORMATTYPE videoPortFormat;

  memset(&videoPortFormat, 0, sizeof(OMX_VIDEO_PARAM_PORTFORMATTYPE));
  videoPortFormat.nSize = sizeof(OMX_VIDEO_PARAM_PORTFORMATTYPE);
  videoPortFormat.nVersion.nVersion = OMX_VERSION;
  videoPortFormat.nPortIndex = 130;

  err = OMX_GetParameter(ilclient_get_handle(component),
  OMX_IndexParamVideoPortFormat, &videoPortFormat);
  if (err != OMX_ErrorNone) {
  fprintf(stderr, "[set_video_decoder_input_format][ERROR] Error getting video decoder format %s\n", err2str(err));
  return err;
  }

  videoPortFormat.nPortIndex = 130;
  videoPortFormat.nIndex = 0;
  videoPortFormat.eCompressionFormat = OMX_VIDEO_CodingAVC;
  videoPortFormat.eColorFormat = OMX_COLOR_FormatUnused;
  videoPortFormat.xFramerate = 0;

  err = OMX_SetParameter(ilclient_get_handle(component),OMX_IndexParamVideoPortFormat, &videoPortFormat);

  if (err != OMX_ErrorNone) {
  fprintf(stderr, "[set_video_decoder_input_format][ERROR] Error setting video decoder format %s\n", err2str(err));
  return err;
  } else {
  printf("[set_video_decoder_input_format] Video decoder format set up ok\n");
  }

  OMX_PARAM_PORTDEFINITIONTYPE portParam;

  memset(&portParam, 0, sizeof( OMX_PARAM_PORTDEFINITIONTYPE));

  portParam.nSize = sizeof( OMX_PARAM_PORTDEFINITIONTYPE);
  portParam.nVersion.nVersion = OMX_VERSION;
  portParam.nPortIndex = 130;

  err =  OMX_GetParameter(ilclient_get_handle(component),OMX_IndexParamPortDefinition, &portParam);
  
  if(err != OMX_ErrorNone)
  {
    fprintf(stderr, "[set_video_decoder_input_format] COMXVideo::Open error OMX_IndexParamPortDefinition omx_err(0x%08x)\n", err);
    return err;
  }

  portParam.nPortIndex = 130;
  portParam.format.video.nFrameWidth  = img_width;
  portParam.format.video.nFrameHeight = img_height;

  err =  OMX_SetParameter(ilclient_get_handle(component),OMX_IndexParamPortDefinition, &portParam);
  
  if(err != OMX_ErrorNone)
  {
    fprintf(stderr, "[set_video_decoder_input_format] COMXVideo::Open error OMX_IndexParamPortDefinition omx_err(0x%08x)\n", err);
    return err;
  }

  return OMX_ErrorNone;
}

void setup_decodeComponent(ILCLIENT_T  *handle, char *decodeComponentName, COMPONENT_T **decodeComponent) 
{
  int err;

  err = ilclient_create_component(
    handle,
    decodeComponent,
    decodeComponentName,
    ILCLIENT_DISABLE_ALL_PORTS|ILCLIENT_ENABLE_INPUT_BUFFERS|ILCLIENT_ENABLE_OUTPUT_BUFFERS
  );

  if (err == -1) {
    fprintf(stderr, "[setup_decodeComponent][ERROR] DecodeComponent create failed\n");
    exit(1);
  }
  printState(ilclient_get_handle(*decodeComponent));

  err = ilclient_change_component_state(*decodeComponent,OMX_StateIdle);

  if (err < 0) {
    fprintf(stderr, "[setup_decodeComponent][ERROR] Couldn't change state to Idle\n");
    exit(1);
  }
  printState(ilclient_get_handle(*decodeComponent));

  /* Must be before we enable buffers */
  set_video_decoder_input_format(*decodeComponent);
}

void setup_renderComponent(ILCLIENT_T  *handle, char *renderComponentName, COMPONENT_T **renderComponent)
{
  int err;

  err = ilclient_create_component(
    handle,
    renderComponent,
    renderComponentName,
    ILCLIENT_DISABLE_ALL_PORTS|ILCLIENT_ENABLE_INPUT_BUFFERS
  );

  if (err == -1) {
    fprintf(stderr, "[setup_renderComponent][ERROR] RenderComponent create failed\n");
    exit(1);
  }
  printState(ilclient_get_handle(*renderComponent));

  err = ilclient_change_component_state(*renderComponent,OMX_StateIdle);

  if (err < 0) {
    fprintf(stderr, "[setup_renderComponent][ERROR] Couldn't change state to Idle\n");
    exit(1);
  }
  printState(ilclient_get_handle(*renderComponent));
}

void setup_receiveSocket(unsigned int port)
{
  int status;

  memset(buffer, 0, MAXBUF);  

  sinlen = sizeof(struct sockaddr_in);

  memset(&sock_in, 0, sinlen);

  sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  printf("[setup_receiveSocket] Sock Status %d\n", sock);

  sock_in.sin_family = AF_INET;
  sock_in.sin_port = htons(port);
  sock_in.sin_addr.s_addr =  htonl(INADDR_ANY);

  status = bind(sock, (struct sockaddr *)&sock_in, sinlen);
  printf("[setup_receiveSocket] Bind Status %d, errno: %d\n", status, errno);

  // status = getsockname(sock, (struct sockaddr *)&sock_in, &sinlen);
  // printf("[setup_receiveSocket] Sock port %d\n",htons(sock_in.sin_port));

  buflen = MAXBUF;
  // printf("[setup_receiveSocket] sendto Status = %d\n", status);

  // shutdown(sock, 2);
  // close(sock);
}

void startClock(COMPONENT_T *clockComponent) {

    OMX_ERRORTYPE err = OMX_ErrorNone;
    OMX_TIME_CONFIG_CLOCKSTATETYPE clockState;

    memset(&clockState, 0, sizeof( OMX_TIME_CONFIG_CLOCKSTATETYPE));
    clockState.nSize = sizeof( OMX_TIME_CONFIG_CLOCKSTATETYPE);
    clockState.nVersion.nVersion = OMX_VERSION;

    err = OMX_GetConfig(ilclient_get_handle(clockComponent), 
        OMX_IndexConfigTimeClockState, &clockState);
    if (err != OMX_ErrorNone) {
        fprintf(stderr, "Error getting clock state %s\n", err2str(err));
        return;
    }
    clockState.eState = OMX_TIME_ClockStateRunning;
    err = OMX_SetConfig(ilclient_get_handle(clockComponent), 
        OMX_IndexConfigTimeClockState, &clockState);
    if (err != OMX_ErrorNone) {
        fprintf(stderr, "Error starting clock %s\n", err2str(err));
        return;
    }
}

void setup_clockComponent(ILCLIENT_T  *handle, char *clockComponentName, COMPONENT_T **clockComponent) {

    int err;

    err = ilclient_create_component(handle,
        clockComponent,
        clockComponentName,
        ILCLIENT_DISABLE_ALL_PORTS
        );

    if (err == -1) {
        fprintf(stderr, "ClockComponent create failed\n");
        exit(1);
    }

    printState(ilclient_get_handle(*clockComponent));

    err = ilclient_change_component_state(*clockComponent,
        OMX_StateIdle);
    if (err < 0) {
        fprintf(stderr, "Couldn't change state to Idle\n");
        exit(1);
    }
    printState(ilclient_get_handle(*clockComponent));
    // printClockState(*clockComponent);

    OMX_COMPONENTTYPE*clock = ilclient_get_handle(*clockComponent);

    OMX_TIME_CONFIG_ACTIVEREFCLOCKTYPE refClock;
    refClock.nSize = sizeof(OMX_TIME_CONFIG_ACTIVEREFCLOCKTYPE);
    refClock.nVersion.nVersion = OMX_VERSION;
    refClock.eClock = OMX_TIME_RefClockVideo; // OMX_CLOCKPORT0;

    err = OMX_SetConfig(ilclient_get_handle(*clockComponent),OMX_IndexConfigTimeActiveRefClock, &refClock);

    if(err != OMX_ErrorNone) {
        fprintf(stderr, "COMXCoreComponent::SetConfig - %s failed with omx_err(0x%x)\n", 
            "clock", err);
    }

    OMX_TIME_CONFIG_SCALETYPE scaleType;
    scaleType.nSize = sizeof(OMX_TIME_CONFIG_SCALETYPE);
    scaleType.nVersion.nVersion = OMX_VERSION;
    scaleType.xScale = 0x00010000;

    err = OMX_SetConfig(ilclient_get_handle(*clockComponent), 
        OMX_IndexConfigTimeScale, &scaleType);
    if(err != OMX_ErrorNone) {
        fprintf(stderr, "COMXCoreComponent::SetConfig - %s failed with omx_err(0x%x)\n", 
            "clock", err);
    }
}
int prems = 0;

int main(int argc, char** argv)
{
	/* 
  * Setup listening socket on given port 
  */
  printf("[main] Port %d\n", atoi(argv[1]));

	setup_receiveSocket(atoi(argv[1]));

  char *decodeComponentName;
  char *renderComponentName;
  char *clockComponentName;

  int err;
  ILCLIENT_T  *handle;
  COMPONENT_T *decodeComponent;
  COMPONENT_T *renderComponent;
  COMPONENT_T *clockComponent;

  OMX_BUFFERHEADERTYPE *buff_header;

  decodeComponentName = "video_decode";
  renderComponentName = "video_render";
  clockComponentName = "clock";

  bcm_host_init();

  handle = ilclient_init();
  // 
  vcos_log_set_level(VCOS_LOG_CATEGORY, VCOS_LOG_TRACE);
  
  if (handle == NULL) {
    fprintf(stderr, "[main][ERROR] IL client init failed\n");
    exit(1);
  }

  if (OMX_Init() != OMX_ErrorNone) {
    ilclient_destroy(handle);
    fprintf(stderr, "[main][ERROR] OMX init failed\n");
    exit(1);
  }

  ilclient_set_error_callback(handle,error_callback,NULL);
  ilclient_set_eos_callback(handle,eos_callback,NULL);
  ilclient_set_port_settings_callback(handle,port_settings_callback,NULL);
  ilclient_set_empty_buffer_done_callback(handle,empty_buffer_done_callback,NULL);

  setup_decodeComponent(handle, decodeComponentName, &decodeComponent);
  setup_renderComponent(handle, renderComponentName, &renderComponent);
  setup_clockComponent(handle, clockComponentName, &clockComponent);


  /* Input port */
  err = ilclient_enable_port_buffers(decodeComponent, 130, NULL, NULL, NULL);
  
  if (err < 0) {
    fprintf(stderr, "[main][ERROR] Couldn't enable buffers\n");
    exit(1);
  }

  ilclient_enable_port(decodeComponent, 130);

  err = ilclient_change_component_state(decodeComponent,OMX_StateExecuting);

  if (err < 0) {
    fprintf(stderr, "[main][ERROR] Couldn't change state to Executing\n");
    exit(1);
  }

  printState(ilclient_get_handle(decodeComponent));

  /* Wait for first packet, containing extradata */
  printf("[main] Waiting for decoder config...\n");
  fflush(0);

  // extradatasize = recvfrom(sock, buffer, buflen, 0, (struct sockaddr *)&sock_in, &sinlen);
  // extradata = (void *)buffer;

  // printf("[main] Got %d bytes for config\n",extradatasize);
  // fflush(0);

  /* Configure stream */
  // set_video_decoder_config(decodeComponent);

  /* Set up the tunnel between decode and render ports */

  TUNNEL_T tunnel;
  
  set_tunnel(&tunnel, decodeComponent, 131, renderComponent, 90);

  if((err = ilclient_setup_tunnel(&tunnel, 0, 0)) < 0)
  {
    fprintf(stderr, "[main][ERROR] Error setting up tunnel %X\n", err);
    exit(1);
  } 
  else
    printf("[main] Decode tunnel set up ok\n");

  TUNNEL_T clockTunnel;

  set_tunnel(&clockTunnel, clockComponent, 80, schedulerComponent, 12);
  
  if ((err = ilclient_setup_tunnel(&clockTunnel, 0, 0)) < 0) {
    fprintf(stderr, "Error setting up clock tunnel %X\n", err);
    exit(1);
  } else 
    printf("Clock tunnel set up ok\n");
  
  startClock(clockComponent);


  OMX_SendCommand(ilclient_get_handle(decodeComponent),OMX_CommandPortEnable, 131, NULL);
   
  ilclient_enable_port(decodeComponent, 131);

// enable the clock output ports
  OMX_SendCommand(ilclient_get_handle(clockComponent),OMX_CommandPortEnable, 80, NULL);

  ilclient_enable_port(clockComponent, 80);

  /* Enable the render input ports */

  OMX_SendCommand(ilclient_get_handle(renderComponent),OMX_CommandPortEnable, 90, NULL);

  ilclient_enable_port(renderComponent, 90);

  /* Set both components to executing state */

  err = ilclient_change_component_state(decodeComponent,OMX_StateExecuting);
  
  if (err < 0)
  {
    fprintf(stderr, "[main][ERROR] Couldn't change state to executing\n");
    exit(1);
  }

  err = ilclient_change_component_state(renderComponent,OMX_StateExecuting);

  if (err < 0)
  {
    fprintf(stderr, "[main][ERROR] Couldn't change state to executiong\n");
    exit(1);
  }

  err = ilclient_change_component_state(clockComponent,OMX_StateExecuting);
  
  if (err < 0) {
      fprintf(stderr, "[main][ERROR] Couldn't change state to Executing\n");
      exit(1);
  }  

  socklen_t receiveSockaddrLen = sizeof(recv_sock);  

  while(1)
  {
    /* Get h264 data */
    int receivedSize = recvfrom(sock, buffer, buflen, 0, (struct sockaddr *)&recv_sock, &receiveSockaddrLen);

		printf("[main] Read pkt size :%d\n",receivedSize);

    fflush(0);

		/* 
    * Do we have a decode input buffer we can fill and empty?
    */
		buff_header = ilclient_get_input_buffer(decodeComponent,130,1 /* block */);

    int buff_size = buff_header->nAllocLen;

    if(receivedSize < buff_size)
      memcpy((unsigned char *)buff_header->pBuffer,buffer,receivedSize);
    else
    {
      printf("[main][ERROR] Buffer not big enough %d %d\n", buff_size, receivedSize);
      exit(-1);      
    }    
	
    buff_header->nFilledLen   = receivedSize;
    buff_header->nFlags       = 0;
    buff_header->nFlags      |= OMX_BUFFERFLAG_ENDOFFRAME;

    if(prems)
    {
      buff_header->nFlags |= OMX_BUFFERFLAG_STARTTIME;
      prems = 0;
    }
    else
      buff_header->nFlags |= OMX_BUFFERFLAG_TIME_UNKNOWN;

    if(buff_header != NULL)
      copy_into_buffer_and_empty(decodeComponent,buff_header);
      
  }

  ilclient_wait_for_event(renderComponent,OMX_EventBufferFlag, 90, 0, OMX_BUFFERFLAG_EOS, 0,ILCLIENT_BUFFER_FLAG_EOS, 10000);

  printf("EOS on render\n");

  exit(0);
} 
