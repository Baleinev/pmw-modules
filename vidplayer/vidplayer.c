#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

#ifdef USE_OPENGL_RENDERER 

#include <math.h>
#include <assert.h>
#include <unistd.h>

#include <GLES/gl.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>

#endif

#include <interface/vmcs_host/khronos/IL/OMX_Core.h>
#include <interface/vmcs_host/khronos/IL/OMX_Component.h>

#include <bcm_host.h>
#include <vcos_logging.h>

#define VCOS_LOG_CATEGORY (&il_ffmpeg_log_category)
static VCOS_LOG_CAT_T il_ffmpeg_log_category;

#include <ilclient.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/mathematics.h>
#include <libavutil/samplefmt.h>

#define INBUF_SIZE 4096
#define AUDIO_INBUF_SIZE 20480
#define AUDIO_REFILL_THRESH 4096

#ifdef USE_OPENGL_RENDERER 

static OMX_BUFFERHEADERTYPE* eglBuffer = NULL;

static void* eglImage = 0;

typedef struct
{
   uint32_t screen_width;
   uint32_t screen_height;
// OpenGL|ES objects
   EGLDisplay display;
   EGLSurface surface;
   EGLContext context;
   GLuint tex;
// model rotation vector and direction
   GLfloat rot_angle_x_inc;
   GLfloat rot_angle_y_inc;
   GLfloat rot_angle_z_inc;
// current model rotation angles
   GLfloat rot_angle_x;
   GLfloat rot_angle_y;
   GLfloat rot_angle_z;
// current distance from camera
   GLfloat distance;
   GLfloat distance_inc;
} CUBE_STATE_T;

static CUBE_STATE_T _state, *state=&_state;

#endif

char *IMG = "taichi.mp4";

static AVCodecContext *video_dec_ctx = NULL;
static AVStream *video_stream = NULL;
static AVPacket pkt;
AVFormatContext *pFormatCtx = NULL;

unsigned int uWidth;
unsigned int uHeight;

unsigned int fpsscale;
unsigned int fpsrate;
unsigned int time_base_num;
unsigned int time_base_den;

int64_t timestampOffset = 0;

static int video_stream_idx = -1;

uint8_t extradatasize;
void *extradata;

AVCodec *codec;

void printState(OMX_HANDLETYPE handle){
    OMX_STATETYPE state;
    OMX_ERRORTYPE err;

    err = OMX_GetState(handle, &state);
    if (err != OMX_ErrorNone) {
        fprintf(stderr, "Error on getting state\n");
        exit(1);
    }
    switch (state) {
        case OMX_StateLoaded:           printf("StateLoaded\n"); break;
        case OMX_StateIdle:             printf("StateIdle\n"); break;
        case OMX_StateExecuting:        printf("StateExecuting\n"); break;
        case OMX_StatePause:            printf("StatePause\n"); break;
        case OMX_StateWaitForResources: printf("StateWait\n"); break;
        case OMX_StateInvalid:          printf("StateInvalid\n"); break;
        default:                        printf("State unknown\n"); break;
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

void printClockState(COMPONENT_T *clockComponent) {

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
    switch (clockState.eState) {
        case OMX_TIME_ClockStateRunning:
        printf("Clock running\n");
        break;
        case OMX_TIME_ClockStateWaitingForStartTime:
        printf("Clock waiting for start time\n");
        break;
        case OMX_TIME_ClockStateStopped:
        printf("Clock stopped\n");
        break;
        default:
        printf("Clock in other state\n");
    }
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

void eos_callback(void *userdata, COMPONENT_T *comp, OMX_U32 data) {
    printf("Got eos event\n");
}

void error_callback(void *userdata, COMPONENT_T *comp, OMX_U32 data) {
    printf("OMX error %s\n", err2str(data));
}

void port_settings_callback(void *userdata, COMPONENT_T *comp, OMX_U32 data) {
    printf("Got port Settings event\n");
    // exit(0);
}

void empty_buffer_done_callback(void *userdata, COMPONENT_T *comp) {
    printf("Got empty buffer done\n");
}

int get_file_size(char *fname) {

    struct stat st;

    if (stat(fname, &st) == -1) {
        perror("Stat'ing img file");
        return -1;
    }
    return(st.st_size);
}

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

OMX_ERRORTYPE copy_into_buffer_and_empty(AVPacket *pkt,COMPONENT_T *component,OMX_BUFFERHEADERTYPE *buff_header)
{

    OMX_ERRORTYPE r;

    int buff_size = buff_header->nAllocLen;
    int size = pkt->size;
    uint8_t *content = pkt->data;

    while (size > 0) 
    {
        buff_header->nFilledLen = (size > buff_header->nAllocLen-1) ?
        buff_header->nAllocLen-1 : size;
        memset(buff_header->pBuffer, 0x0, buff_header->nAllocLen);
        memcpy(buff_header->pBuffer, content, buff_header->nFilledLen);
        size -= buff_header->nFilledLen;
        content += buff_header->nFilledLen;


        /*
        if (size < buff_size) {
        memcpy((unsigned char *)buff_header->pBuffer, 
        pkt->data, size);
        } else {
        printf("Buffer not big enough %d %d\n", buff_size, size);
        return -1;
        }

        buff_header->nFilledLen = size;
        */

        buff_header->nFlags = 0;

        if (size <= 0) 
            buff_header->nFlags |= OMX_BUFFERFLAG_ENDOFFRAME;

        printf("  DTS is %s %ld\n", "str", pkt->dts);
        printf("  PTS is %s %ld\n", "str", pkt->pts);

        if (pkt->dts == 0) {

            buff_header->nFlags |= OMX_BUFFERFLAG_STARTTIME;

        } else {

            buff_header->nTimeStamp = ToOMXTime((uint64_t) ((pkt->pts+timestampOffset) * 1000000/ time_base_den));

            printf("Time stamp %d\n", buff_header->nTimeStamp);
        }

        r = OMX_EmptyThisBuffer(ilclient_get_handle(component),buff_header);

        if (r != OMX_ErrorNone) {
            fprintf(stderr, "Empty buffer error %s\n",
                err2str(r));
        } else {
            printf("Emptying buffer %p\n", buff_header);
        }

        if (size > 0) {
            buff_header = ilclient_get_input_buffer(component,130,1 /* block */);
        }
    }
    return r;
}

int img_width, img_height;

int SendDecoderConfig(COMPONENT_T *component, FILE *out)
{
    OMX_ERRORTYPE omx_err   = OMX_ErrorNone;

    /* send decoder config */

    if(extradatasize > 0 && extradata != NULL)
    {
        // fwrite(extradata, 1, extradatasize, out);

        OMX_BUFFERHEADERTYPE *omx_buffer = ilclient_get_input_buffer(component,130,1 /* block */);

        if(omx_buffer == NULL)
        {
            fprintf(stderr, "%s - buffer error 0x%08x", __func__, omx_err);
            return 0;
        }

        omx_buffer->nOffset = 0;
        omx_buffer->nFilledLen = extradatasize;

        if(omx_buffer->nFilledLen > omx_buffer->nAllocLen)
        {
            fprintf(stderr, "%s - omx_buffer->nFilledLen > omx_buffer->nAllocLen",  __func__);
            return 0;
        }

        memset((unsigned char *)omx_buffer->pBuffer, 0x0, omx_buffer->nAllocLen);
        memcpy((unsigned char *)omx_buffer->pBuffer, extradata, omx_buffer->nFilledLen);
        omx_buffer->nFlags = OMX_BUFFERFLAG_CODECCONFIG | OMX_BUFFERFLAG_ENDOFFRAME;

        omx_err =  OMX_EmptyThisBuffer(ilclient_get_handle(component),
            omx_buffer);
        if (omx_err != OMX_ErrorNone)
        {
            fprintf(stderr, "%s - OMX_EmptyThisBuffer() failed with result(0x%x)\n", __func__, omx_err);
            return 0;
        } else {
            printf("Config sent, emptying buffer %d\n", extradatasize);
        }
    }
    return 1;
}

OMX_ERRORTYPE set_video_decoder_input_format(COMPONENT_T *component) {

    int err;

    // set input video format
    printf("Setting video decoder format\n");
    OMX_VIDEO_PARAM_PORTFORMATTYPE videoPortFormat;

    memset(&videoPortFormat, 0, sizeof(OMX_VIDEO_PARAM_PORTFORMATTYPE));
    videoPortFormat.nSize = sizeof(OMX_VIDEO_PARAM_PORTFORMATTYPE);
    videoPortFormat.nVersion.nVersion = OMX_VERSION;
    videoPortFormat.nPortIndex = 130;

    err = OMX_GetParameter(ilclient_get_handle(component),
        OMX_IndexParamVideoPortFormat, &videoPortFormat);
    if (err != OMX_ErrorNone) {
        fprintf(stderr, "Error getting video decoder format %s\n", err2str(err));
        return err;
    }

    videoPortFormat.nPortIndex = 130;
    videoPortFormat.nIndex = 0;
    videoPortFormat.eCompressionFormat = OMX_VIDEO_CodingAVC;
    videoPortFormat.eColorFormat = OMX_COLOR_FormatUnused;
    videoPortFormat.xFramerate = 0;

#if 1 // doesn't seem to make any difference!!!
    if (fpsscale > 0 && fpsrate > 0) {
        videoPortFormat.xFramerate = 
        (long long)(1<<16)*fpsrate / fpsscale;
    } else {
        videoPortFormat.xFramerate = 25 * (1<<16);
    }
    printf("FPS num %d den %d\n", fpsrate, fpsscale);
    printf("Set frame rate to %d\n", videoPortFormat.xFramerate);
#endif

    err = OMX_SetParameter(ilclient_get_handle(component),OMX_IndexParamVideoPortFormat, &videoPortFormat);

    if (err != OMX_ErrorNone) {
        fprintf(stderr, "Error setting video decoder format %s\n", err2str(err));
        return err;
    } else {
        printf("Video decoder format set up ok\n");
    }

    OMX_PARAM_PORTDEFINITIONTYPE portParam;
    memset(&portParam, 0, sizeof( OMX_PARAM_PORTDEFINITIONTYPE));
    portParam.nSize = sizeof( OMX_PARAM_PORTDEFINITIONTYPE);
    portParam.nVersion.nVersion = OMX_VERSION;

    portParam.nPortIndex = 130;

    err =  OMX_GetParameter(ilclient_get_handle(component),
        OMX_IndexParamPortDefinition, &portParam);
    if(err != OMX_ErrorNone)
    {
        fprintf(stderr, "COMXVideo::Open error OMX_IndexParamPortDefinition omx_err(0x%08x)\n", err);
        return err;
    }

    printf("Default framerate %d\n", portParam.format.video.xFramerate);

    portParam.nPortIndex = 130;

    portParam.format.video.nFrameWidth  = img_width;
    portParam.format.video.nFrameHeight = img_height;

    err =  OMX_SetParameter(ilclient_get_handle(component),
        OMX_IndexParamPortDefinition, &portParam);
    if(err != OMX_ErrorNone)
    {
        fprintf(stderr, "COMXVideo::Open error OMX_IndexParamPortDefinition omx_err(0x%08x)\n", err);
        return err;
    }

    return OMX_ErrorNone;
}

int setup_demuxer(const char *filename) {

    // Register all formats and codecs
    av_register_all();

    if(avformat_open_input(&pFormatCtx, filename, NULL, NULL)!=0) {
        fprintf(stderr, "Can't get format\n");
        return -1; // Couldn't open file
    }

    // Retrieve stream information
    if (avformat_find_stream_info(pFormatCtx, NULL) < 0) {
        return -1; // Couldn't find stream information
    }

    printf("Format:\n");
    av_dump_format(pFormatCtx, 0, filename, 0);

    int ret;
    ret = av_find_best_stream(pFormatCtx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);

    if (ret >= 0) {
        video_stream_idx = ret;

        video_stream = pFormatCtx->streams[video_stream_idx];
        video_dec_ctx = video_stream->codec;

        img_width         = video_stream->codec->width;
        img_height        = video_stream->codec->height;
        extradata         = video_stream->codec->extradata;
        extradatasize     = video_stream->codec->extradata_size;
        fpsscale          = video_stream->r_frame_rate.den;
        fpsrate           = video_stream->r_frame_rate.num;
        time_base_num         = video_stream->time_base.num;
        time_base_den         = video_stream->time_base.den;

        printf("Rate %d scale %d time base %d %d\n",
            video_stream->r_frame_rate.num,
            video_stream->r_frame_rate.den,
            video_stream->time_base.num,
            video_stream->time_base.den);

        AVCodec *codec = avcodec_find_decoder(video_stream->codec->codec_id);

        if (codec) {
            printf("Codec name %s\n", codec->name);
        }
    }
    return 0;
}

void setup_decodeComponent(ILCLIENT_T  *handle, char *decodeComponentName, COMPONENT_T **decodeComponent) {

    int err;

    err = ilclient_create_component(handle,
        decodeComponent,
        decodeComponentName,
        ILCLIENT_DISABLE_ALL_PORTS
        |
        ILCLIENT_ENABLE_INPUT_BUFFERS
        |
        ILCLIENT_ENABLE_OUTPUT_BUFFERS
        );

    if (err == -1) {
        fprintf(stderr, "DecodeComponent create failed\n");
        exit(1);
    }
    printState(ilclient_get_handle(*decodeComponent));

    err = ilclient_change_component_state(*decodeComponent,
        OMX_StateIdle);
    if (err < 0) {
        fprintf(stderr, "Couldn't change state to Idle\n");
        exit(1);
    }
    printState(ilclient_get_handle(*decodeComponent));

    // must be before we enable buffers
    set_video_decoder_input_format(*decodeComponent);
}

void setup_schedulerComponent(ILCLIENT_T  *handle, char *schedulerComponentName, COMPONENT_T **schedulerComponent) {

    int err;

    err = ilclient_create_component(handle,
        schedulerComponent,
        schedulerComponentName,
        ILCLIENT_DISABLE_ALL_PORTS
        |
        ILCLIENT_ENABLE_INPUT_BUFFERS
        );
    if (err == -1) {
        fprintf(stderr, "SchedulerComponent create failed\n");
        exit(1);
    }
    printState(ilclient_get_handle(*schedulerComponent));

    err = ilclient_change_component_state(*schedulerComponent,
        OMX_StateIdle);
    if (err < 0) {
        fprintf(stderr, "Couldn't change state to Idle\n");
        exit(1);
    }
    printState(ilclient_get_handle(*schedulerComponent));
}

void setup_renderComponentVideoFullscreen(ILCLIENT_T  *handle, char *renderComponentName, COMPONENT_T **renderComponent) {

    int err;

    err = ilclient_create_component(handle,
        renderComponent,
        renderComponentName,
        ILCLIENT_DISABLE_ALL_PORTS
        |
        ILCLIENT_ENABLE_INPUT_BUFFERS
        );

    if (err == -1) {
        fprintf(stderr, "setup_renderComponentVideoFullscreen create failed\n");
        exit(1);
    }
    printState(ilclient_get_handle(*renderComponent));

    err = ilclient_change_component_state(*renderComponent,
        OMX_StateIdle);
    if (err < 0) {
        fprintf(stderr, "Couldn't change state to Idle\n");
        exit(1);
    }
    printState(ilclient_get_handle(*renderComponent));
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
    printClockState(*clockComponent);

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

#ifdef USE_OPENGL_RENDERER

void setup_renderComponentVideoEGL(ILCLIENT_T  *handle, char *renderComponentName, COMPONENT_T **renderComponent) {

    int err;

    err = ilclient_create_component(handle,
        renderComponent,
        renderComponentName,
        ILCLIENT_DISABLE_ALL_PORTS
        |
        ILCLIENT_ENABLE_OUTPUT_BUFFERS
        );

    if (err == -1) {
        fprintf(stderr, "setup_renderComponentVideoEGL create failed\n");
        exit(1);
    }
    printState(ilclient_get_handle(*renderComponent));

    err = ilclient_change_component_state(*renderComponent,
        OMX_StateIdle);
    if (err < 0) {
        fprintf(stderr, "Couldn't change state to Idle\n");
        exit(1);
    }
    printState(ilclient_get_handle(*renderComponent));
}

static void init_ogl(CUBE_STATE_T *state)
{
   int32_t success = 0;
   EGLBoolean result;
   EGLint num_config;

   static EGL_DISPMANX_WINDOW_T nativewindow;

   DISPMANX_ELEMENT_HANDLE_T dispman_element;
   DISPMANX_DISPLAY_HANDLE_T dispman_display;
   DISPMANX_UPDATE_HANDLE_T dispman_update;
   VC_RECT_T dst_rect;
   VC_RECT_T src_rect;

   static const EGLint attribute_list[] =
   {
      EGL_RED_SIZE, 8,
      EGL_GREEN_SIZE, 8,
      EGL_BLUE_SIZE, 8,
      EGL_ALPHA_SIZE, 8,
      EGL_DEPTH_SIZE, 16,
      //EGL_SAMPLES, 4,
      EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
      EGL_NONE
   };
   
   EGLConfig config;

   // get an EGL display connection
   state->display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
   assert(state->display!=EGL_NO_DISPLAY);

   // initialize the EGL display connection
   result = eglInitialize(state->display, NULL, NULL);
   assert(EGL_FALSE != result);

   // get an appropriate EGL frame buffer configuration
   // this uses a BRCM extension that gets the closest match, rather than standard which returns anything that matches
   result = eglSaneChooseConfigBRCM(state->display, attribute_list, &config, 1, &num_config);
   assert(EGL_FALSE != result);

   // create an EGL rendering context
   state->context = eglCreateContext(state->display, config, EGL_NO_CONTEXT, NULL);
   assert(state->context!=EGL_NO_CONTEXT);

   // create an EGL window surface
   success = graphics_get_display_size(0 /* LCD */, &state->screen_width, &state->screen_height);
   assert( success >= 0 );

   dst_rect.x = 0;
   dst_rect.y = 0;
   dst_rect.width = state->screen_width;
   dst_rect.height = state->screen_height;
      
   src_rect.x = 0;
   src_rect.y = 0;
   src_rect.width = state->screen_width << 16;
   src_rect.height = state->screen_height << 16;        

   dispman_display = vc_dispmanx_display_open( 0 /* LCD */);
   dispman_update = vc_dispmanx_update_start( 0 );
         
   dispman_element = vc_dispmanx_element_add ( dispman_update, dispman_display,
      0/*layer*/, &dst_rect, 0/*src*/,
      &src_rect, DISPMANX_PROTECTION_NONE, 0 /*alpha*/, 0/*clamp*/, 0/*transform*/);
      
   nativewindow.element = dispman_element;
   nativewindow.width = state->screen_width;
   nativewindow.height = state->screen_height;
   vc_dispmanx_update_submit_sync( dispman_update );
      
   state->surface = eglCreateWindowSurface( state->display, config, &nativewindow, NULL );
   assert(state->surface != EGL_NO_SURFACE);

   // connect the context to the surface
   result = eglMakeCurrent(state->display, state->surface, state->surface, state->context);
   assert(EGL_FALSE != result);

   // Set background color and clear buffers
   glClearColor(0.15f, 0.25f, 0.35f, 1.0f);

   // Enable back face culling.
   //glEnable(GL_CULL_FACE);

   glMatrixMode(GL_MODELVIEW);
}

static void init_textures(CUBE_STATE_T *state)
{
   //// load three texture buffers but use them on six OGL|ES texture surfaces
   glGenTextures(1, &state->tex);

   glBindTexture(GL_TEXTURE_2D, state->tex);
   glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, IMAGE_SIZE_WIDTH, IMAGE_SIZE_HEIGHT, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

   /* Create EGL Image */
   eglImage = eglCreateImageKHR(
                state->display,
                state->context,
                EGL_GL_TEXTURE_2D_KHR,
                (EGLClientBuffer)state->tex,
                0);
    
   if (eglImage == EGL_NO_IMAGE_KHR)
   {
      printf("eglCreateImageKHR failed.\n");
      exit(1);
   }

   // Start rendering
   pthread_create(&thread1, NULL, video_decode_test, eglImage);

   // setup overall texture environment
   glTexCoordPointer(2, GL_FLOAT, 0, texCoords);
   glEnableClientState(GL_TEXTURE_COORD_ARRAY);

   glEnable(GL_TEXTURE_2D);

   // Bind texture surface to current vertices
   glBindTexture(GL_TEXTURE_2D, state->tex);
}

static void exit_func(void)
// Function to be passed to atexit().
{
   if (eglImage != 0)
   {
      if (!eglDestroyImageKHR(state->display, (EGLImageKHR) eglImage))
         printf("eglDestroyImageKHR failed.");
   }

   // clear screen
   glClear( GL_COLOR_BUFFER_BIT );
   eglSwapBuffers(state->display, state->surface);

   // Release OpenGL resources
   eglMakeCurrent( state->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT );
   eglDestroySurface( state->display, state->surface );
   eglDestroyContext( state->display, state->context );
   eglTerminate( state->display );

   printf("\nClosed\n");
}

static void init_model_proj(CUBE_STATE_T *state)
{
   float nearp = 1.0f;
   float farp = 500.0f;
   float hht;
   float hwd;

   glHint( GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST );

   glViewport(0, 0, (GLsizei)state->screen_width, (GLsizei)state->screen_height);
      
   glMatrixMode(GL_PROJECTION);
   glLoadIdentity();

   hht = nearp * (float)tan(45.0 / 2.0 / 180.0 * M_PI);
   hwd = hht * (float)state->screen_width / (float)state->screen_height;

   glFrustumf(-hwd, hwd, -hht, hht, nearp, farp);
   
   glEnableClientState( GL_VERTEX_ARRAY );
   glVertexPointer( 3, GL_BYTE, 0, quadx );

   reset_model(state);
}

#endif

int main(int argc, char** argv) {

    char *decodeComponentName;
    char *renderComponentName;
    char *schedulerComponentName;
    char *clockComponentName;
    int err;
    ILCLIENT_T  *handle;
    COMPONENT_T *decodeComponent;
    COMPONENT_T *renderComponent;
    COMPONENT_T *schedulerComponent;
    COMPONENT_T *clockComponent;

    if (argc > 1) {
        IMG = argv[1];
    }

    OMX_BUFFERHEADERTYPE *buff_header;

    printf("Setting up demuxer...\n");fflush(0);

    setup_demuxer(IMG);

    decodeComponentName = "video_decode";

#ifdef USE_OPENGL_RENDERER
    renderComponentName = "egl_render";    
#else
    renderComponentName = "video_render";    
#endif

    schedulerComponentName = "video_scheduler";
    clockComponentName = "clock";

    printf("Init host...\n");fflush(0);

    bcm_host_init();

    printf("Init ilclient...\n");fflush(0);    

    handle = ilclient_init();
    vcos_log_set_level(VCOS_LOG_CATEGORY, VCOS_LOG_TRACE);
    if (handle == NULL) {
        fprintf(stderr, "IL client init failed\n");
        exit(1);
    }

    printf("Init host...\n");fflush(0);    

    if (OMX_Init() != OMX_ErrorNone) {
        ilclient_destroy(handle);
        fprintf(stderr, "OMX init failed\n");
        exit(1);
    }


#ifdef USE_OPENGL_RENDERER

    printf("Init egl and opengl...\n");fflush(0);

    /* Create EGL Image */
    eglImage = eglCreateImageKHR(
        state->display,
        state->context,
        EGL_GL_TEXTURE_2D_KHR,
        (EGLClientBuffer)state->tex,
    0);

    if (eglImage == EGL_NO_IMAGE_KHR)
    {
        printf("eglCreateImageKHR failed.\n");
        exit(1);
    }

#endif

    printf("Setting tunnels...\n");fflush(0);        

    ilclient_set_error_callback(handle,
        error_callback,
        NULL);
    ilclient_set_eos_callback(handle,
        eos_callback,
        NULL);
    ilclient_set_port_settings_callback(handle,
        port_settings_callback,
        NULL);
    ilclient_set_empty_buffer_done_callback(handle,
        empty_buffer_done_callback,
        NULL);

    setup_decodeComponent(handle, decodeComponentName, &decodeComponent);

#ifdef USE_OPENGL_RENDERER
    setup_renderComponentVideoEGL(handle, renderComponentName, &renderComponent);    
#else
    setup_renderComponentVideoFullscreen(handle, renderComponentName, &renderComponent);
#endif

    setup_schedulerComponent(handle, schedulerComponentName, &schedulerComponent);
    setup_clockComponent(handle, clockComponentName, &clockComponent);
    // both components now in Idle state, no buffers, ports disabled

    // input port
    err = ilclient_enable_port_buffers(decodeComponent, 130, NULL, NULL, NULL);
    if (err < 0) {
        fprintf(stderr, "Couldn't enable buffers\n");
        exit(1);
    }
    ilclient_enable_port(decodeComponent, 130);

    err = ilclient_change_component_state(decodeComponent,OMX_StateExecuting);

    if (err < 0) {
        fprintf(stderr, "Couldn't change state to Executing\n");
        exit(1);
    }
    printState(ilclient_get_handle(decodeComponent));

    // FILE *out = fopen("tmp.h264", "wb");
    SendDecoderConfig(decodeComponent, NULL/*out*/);

    // printf("Looping...\n");fflush(0);

    /* read frames from the file */
    // while (av_read_frame(pFormatCtx, &pkt) >= 0)
    // {
    //     printf("Read pkt %d\n", pkt.size);

    //     AVPacket orig_pkt = pkt;
        
    //     if (pkt.stream_index == video_stream_idx)
    //     {
    //         printf("read video pkt %d\n", pkt.size);
            
    //         // fwrite(pkt.data, 1, pkt.size, out);

    //         buff_header = ilclient_get_input_buffer(decodeComponent,130,1 /* block */);

    //         if (buff_header != NULL) {
    //             copy_into_buffer_and_empty(&pkt,decodeComponent,buff_header);
    //         } else {
    //             fprintf(stderr, "Couldn't get a buffer\n");
    //         }

    //         err = ilclient_wait_for_event(decodeComponent,OMX_EventPortSettingsChanged, 131, 0, 0, 1,ILCLIENT_EVENT_ERROR | ILCLIENT_PARAMETER_CHANGED, 0);

    //         if (err < 0) {
    //             printf("No port settings change\n");
    //             //exit(1);
    //         } else {
    //             printf("Port settings changed\n");
    //             // exit(0);
    //             break;
    //         }

    //         if (ilclient_remove_event(decodeComponent,OMX_EventPortSettingsChanged, 131, 0, 0, 1) == 0)
    //         {
    //             printf("Removed port settings event\n");
    //             //exit(0);
    //             break;
    //         } else {
    //             printf("No portr settting seen yet\n");
    //         }
    //     }
    //     av_free_packet(&orig_pkt);
    // }

    TUNNEL_T decodeTunnel;
    set_tunnel(&decodeTunnel, decodeComponent, 131, schedulerComponent, 10);
    if ((err = ilclient_setup_tunnel(&decodeTunnel, 0, 0)) < 0) {
        fprintf(stderr, "Error setting up decode tunnel %X\n", err);
        exit(1);
    } else {
        printf("Decode tunnel set up ok\n");
    }

    TUNNEL_T schedulerTunnel;
#ifdef USE_OPENGL_RENDERER
    set_tunnel(&schedulerTunnel, schedulerComponent, 11, renderComponent, 220); 
#else
    set_tunnel(&schedulerTunnel, schedulerComponent, 11, renderComponent, 90); 
#endif
    if ((err = ilclient_setup_tunnel(&schedulerTunnel, 0, 0)) < 0) {
        fprintf(stderr, "Error setting up scheduler tunnel %X\n", err);
        exit(1);
    } else {
        printf("Scheduler tunnel set up ok\n");
    }

    TUNNEL_T clockTunnel;
    set_tunnel(&clockTunnel, clockComponent, 80, schedulerComponent, 12);
    if ((err = ilclient_setup_tunnel(&clockTunnel, 0, 0)) < 0) {
        fprintf(stderr, "Error setting up clock tunnel %X\n", err);
        exit(1);
    } else {
        printf("Clock tunnel set up ok\n");
    }
    startClock(clockComponent);
    printClockState(clockComponent);

// Okay to go back to processing data
// enable the decode output ports

    OMX_SendCommand(ilclient_get_handle(decodeComponent), 
        OMX_CommandPortEnable, 131, NULL);

    ilclient_enable_port(decodeComponent, 131);

// enable the clock output ports
    OMX_SendCommand(ilclient_get_handle(clockComponent), 
        OMX_CommandPortEnable, 80, NULL);

    ilclient_enable_port(clockComponent, 80);

// enable the scheduler ports
    OMX_SendCommand(ilclient_get_handle(schedulerComponent), 
        OMX_CommandPortEnable, 10, NULL);

    ilclient_enable_port(schedulerComponent, 10);

    OMX_SendCommand(ilclient_get_handle(schedulerComponent), 
        OMX_CommandPortEnable, 11, NULL);

    ilclient_enable_port(schedulerComponent, 11);


    OMX_SendCommand(ilclient_get_handle(schedulerComponent), 
        OMX_CommandPortEnable, 12, NULL);

    ilclient_enable_port(schedulerComponent, 12);

// enable the render input ports

#ifdef USE_OPENGL_RENDERER 

    OMX_SendCommand(ilclient_get_handle(renderComponent), OMX_CommandPortEnable, 221, NULL);

    // Enable the output port and tell egl_render to use the texture as a buffer 

    OMX_UseEGLImage(ilclient_get_handle(renderComponent), &eglBuffer, 221, NULL, eglImage);

    // Request egl_render to write data to the texture buffer
    OMX_FillThisBuffer(ilclient_get_handle(renderComponent), eglBuffer);

    ilclient_enable_port(egl_render, 221); //THIS BLOCKS SO CANT BE USED      


#else

    OMX_SendCommand(ilclient_get_handle(renderComponent), 
        OMX_CommandPortEnable, 90, NULL);

    ilclient_enable_port(renderComponent, 90);    

#endif

// set both components to executing state
    err = ilclient_change_component_state(decodeComponent,
        OMX_StateExecuting);
    if (err < 0) {
        fprintf(stderr, "Couldn't change state to Executing\n");
        exit(1);
    }
    err = ilclient_change_component_state(renderComponent,
        OMX_StateExecuting);
    if (err < 0) {
        fprintf(stderr, "Couldn't change state to Executing\n");
        exit(1);
    }

#ifdef USE_OPENGL_RENDERER 

    // Request egl_render to write data to the texture buffer
    if(OMX_FillThisBuffer(ilclient_get_handle(renderComponent), eglBuffer) != OMX_ErrorNone)
    {
       printf("OMX_FillThisBuffer failed.\n");
       exit(1);
    }

#endif

    err = ilclient_change_component_state(schedulerComponent,
        OMX_StateExecuting);
    if (err < 0) {
        fprintf(stderr, "Couldn't change state to Executing\n");
        exit(1);
    }

    err = ilclient_change_component_state(clockComponent,
        OMX_StateExecuting);
    if (err < 0) {
        fprintf(stderr, "Couldn't change state to Executing\n");
        exit(1);
    }

    int64_t lastPts;

// now work through the file
    while(1)
    {
        while (av_read_frame(pFormatCtx, &pkt) >= 0)
        {
            printf("Read pkt after port settings %d\n", pkt.size);
            // fwrite(pkt.data, 1, pkt.size, out);

            if (pkt.stream_index != video_stream_idx) {
                continue;
            }
            printf("  is video pkt\n");

            //printf("  Best timestamp is %d\n", );

            // do we have a decode input buffer we can fill and empty?
            buff_header = ilclient_get_input_buffer(decodeComponent,130,1 /* block */);

            if (buff_header != NULL) {
                copy_into_buffer_and_empty(&pkt,decodeComponent,buff_header);
                
                lastPts = pkt.pts;
            }

            err = ilclient_wait_for_event(decodeComponent,OMX_EventPortSettingsChanged, 131, 0, 0, 1, ILCLIENT_EVENT_ERROR | ILCLIENT_PARAMETER_CHANGED, 0);

            if (err >= 0) {
                printf("Another port settings change\n");
            }

            av_free_packet(&pkt);


        }
        printf("Timestamp offset was %lld\n",(long long)timestampOffset);        

        timestampOffset += lastPts;

        printf("Timestamp offset is now %lld (+= %lld)\n",(long long)timestampOffset,(long long)lastPts);        

        avformat_seek_file(pFormatCtx,0,0,0,10,AVSEEK_FLAG_BACKWARD);
    }    

    ilclient_wait_for_event(renderComponent, 
        OMX_EventBufferFlag, 
        90, 0, OMX_BUFFERFLAG_EOS, 0,
        ILCLIENT_BUFFER_FLAG_EOS, 10000);
    printf("EOS on render\n");

    exit(0);
}
