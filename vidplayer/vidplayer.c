#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

#define USE_OPENGL_RENDERER

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

#include "OMX_utils.h"
#include "monitor_MAPDIR.h"

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

#define MAPDIR "/etc/MAPDIR.d/"

#define IMAGE_SIZE_WIDTH 1280
#define IMAGE_SIZE_HEIGHT 720

static const GLbyte quadx[4*3] = {
   -12, -20,  10,
   12, -20,  10,
   -12,  20,  10,
   12,  20,  10
};

/** Texture coordinates for the quad. */
static const GLfloat texCoords[4 * 2] = {
   0.f,  0.f,
   0.f,  1.f,
   1.f,  0.f,
   1.f,  1.f
};

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
   GLuint tex2;   
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

void eos_callback(void *userdata, COMPONENT_T *comp, OMX_U32 data)
{
    printf("[%s] Got eos event\n",__FUNCTION__);
}

void error_callback(void *userdata, COMPONENT_T *comp, OMX_U32 data)
{
    printf("[%s][ERROR] OMX error %s\n",__FUNCTION__,OMX_err2str(data));
}

void port_settings_callback(void *userdata, COMPONENT_T *comp, OMX_U32 data)
{
    printf("[%s] Got port Settings event\n",__FUNCTION__);
}

void empty_buffer_done_callback(void *userdata, COMPONENT_T *comp)
{
    printf("[%s] Got empty buffer done\n",__FUNCTION__);
}

int get_file_size(char *fname)
{
    struct stat st;

    if (stat(fname, &st) == -1) {
        perror("Stat'ing img file");
        return -1;
    }
    return(st.st_size);
}

OMX_ERRORTYPE copy_into_buffer_and_empty(AVPacket *pkt,COMPONENT_T *component,OMX_BUFFERHEADERTYPE *buff_header)
{
    OMX_ERRORTYPE r;

    int buff_size = buff_header->nAllocLen;
    int size = pkt->size;
    uint8_t *content = pkt->data;

    while (size > 0) 
    {
        buff_header->nFilledLen = (size > buff_header->nAllocLen-1) ? buff_header->nAllocLen-1 : size;
        
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
                OMX_err2str(r));
        } else {
            printf("Emptying buffer %p\n", buff_header);
        }

        if (size > 0) {
            buff_header = ilclient_get_input_buffer(component,130,1 /* block */);
        }
    }
    return r;
}

int setup_demuxer(const char *filename, int *frame_width,int *frame_height)
{

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

        *frame_width         = video_stream->codec->width;
        *frame_height        = video_stream->codec->height;
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

        // AVCodec *codec = avcodec_find_decoder(video_stream->codec->codec_id);

        // if (codec) {
        //     printf("Codec name %s\n", codec->name);
        // }
    }
    return 0;
}

#ifdef USE_OPENGL_RENDERER

void my_fill_buffer_done(void* data, COMPONENT_T* comp)
{
  if (OMX_FillThisBuffer(ilclient_get_handle(comp), eglBuffer) != OMX_ErrorNone)
   {
      printf("[%s] OMX_FillThisBuffer failed in callback\n",__FUNCTION__);
   }
}

static void redraw_scene(CUBE_STATE_T *state)
{
   printf("[redraw_scene] Draw\n");

   glLoadIdentity();
   // move camera back to see the cube
   glTranslatef(0.f, 0.f, -state->distance);
   
   // Start with a clear screen
   glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );

   // Need to rotate textures - do this by rotating each cube face
   glRotatef(270.f, 0.f, 0.f, 1.f ); // front face normal along z axis

   // draw first 4 vertices
   glDrawArrays( GL_TRIANGLE_STRIP, 0, 4);

   eglSwapBuffers(state->display, state->surface);
}

int init_ogl(CUBE_STATE_T *state)
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

   if(state->display==EGL_NO_DISPLAY)
   {
    printf("[init_ogl][ERROR] Cannot eglGetDisplay\n");fflush(0);
    return -1;
   }
   // assert(state->display!=EGL_NO_DISPLAY);

   // initialize the EGL display connection
   result = eglInitialize(state->display, NULL, NULL);
   // assert(EGL_FALSE != result);
   
   if(EGL_FALSE == result)
   {
    printf("[init_ogl][ERROR] Cannot eglInitialize\n");fflush(0);
    return -1;
   }

   // get an appropriate EGL frame buffer configuration
   // this uses a BRCM extension that gets the closest match, rather than standard which returns anything that matches
   result = eglSaneChooseConfigBRCM(state->display, attribute_list, &config, 1, &num_config);
   if(EGL_FALSE == result)
   {
    printf("[init_ogl][ERROR] Cannot eglSaneChooseConfigBRCM\n");fflush(0);
    return -1;
   }

   // create an EGL rendering context
   state->context = eglCreateContext(state->display, config, EGL_NO_CONTEXT, NULL);
   // assert(state->context!=EGL_NO_CONTEXT);
   if(state->context==EGL_NO_CONTEXT)
   {
    printf("[init_ogl][ERROR] Cannot eglCreateContext\n");fflush(0);
    return -1;
   }
   // create an EGL window surface
   success = graphics_get_display_size(0 /* LCD */, &state->screen_width, &state->screen_height);
   // assert( success >= 0 );
   if(success < 0)
   {
    printf("[init_ogl][ERROR] Cannot graphics_get_display_size\n");fflush(0);
    return -1;
   }   

   printf("[init_ogl] Screen size %d x %d\n",state->screen_width,state->screen_height);fflush(0);

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

   vc_dispmanx_update_submit_sync(dispman_update);
      
   state->surface = eglCreateWindowSurface( state->display, config, &nativewindow, NULL);
   if(state->surface == EGL_NO_SURFACE)
   {
    printf("[init_ogl][ERROR] Cannot eglCreateWindowSurface\n");fflush(0);
    return -1;
   }  

   // connect the context to the surface
   result = eglMakeCurrent(state->display, state->surface, state->surface, state->context);
   if(result == EGL_FALSE)
   {
    printf("[init_ogl][ERROR] Cannot eglMakeCurrent\n");fflush(0);
    return -1;
   }     

   // Set background color and clear buffers
   glClearColor(0.15f, 0.25f, 0.35f, 1.0f);

   // Enable back face culling.
   glEnable(GL_CULL_FACE);

   glMatrixMode(GL_MODELVIEW);
}

static void init_textures(CUBE_STATE_T *state)
{
   // glGenTextures(1, &state->tex);
   glGenTextures(1, &state->tex2);

   // glBindTexture(GL_TEXTURE_2D, state->tex);

   //  unsigned char pixels[] = {
   //      128,0,0,255,   0,0,182,255,
   //      0,128,182,255,   0,128,0,255,
   //      128,0,0,255,   0,0,182,255,
   //      0,128,182,255,   0,128,0,255,
   //      128,0,0,255,   0,0,182,255,
   //      0,128,182,255,   0,128,0,255
   //  };
   //  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, 6, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);

   // // glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, IMAGE_SIZE_HEIGHT, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

   // glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
   // glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
   // glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
   // glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

   // glTexCoordPointer(2, GL_FLOAT, 0, texCoords);
   // glEnableClientState(GL_TEXTURE_COORD_ARRAY);

   // glEnable(GL_TEXTURE_2D);

   /***************************************************/

   glBindTexture(GL_TEXTURE_2D, state->tex2);   
   glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, IMAGE_SIZE_WIDTH, IMAGE_SIZE_HEIGHT, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

   // /* Create EGL Image */
   eglImage = eglCreateImageKHR(state->display,state->context,EGL_GL_TEXTURE_2D_KHR,(EGLClientBuffer)state->tex2,0);
    
   if (eglImage == EGL_NO_IMAGE_KHR)
   {
      printf("[init_textures][ERROR] eglCreateImageKHR failed.\n");
      exit(1);
   }

   // setup overall texture environment
   // glTexCoordPointer(2, GL_FLOAT, 0, texCoords);
   // glEnableClientState(GL_TEXTURE_COORD_ARRAY);

   // glEnable(GL_TEXTURE_2D);

   // Bind texture surface to current vertices
   // glBindTexture(GL_TEXTURE_2D, state->tex);
   // glEnable(GL_TEXTURE_2D);   

   // setup overall texture environment
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

   glTexCoordPointer(2, GL_FLOAT, 0, texCoords);
   glEnableClientState(GL_TEXTURE_COORD_ARRAY);

   glEnable(GL_TEXTURE_2D);

   // Bind texture surface to current vertices
   // glBindTexture(GL_TEXTURE_2D, state->tex);   
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
   // reset model position
   glMatrixMode(GL_MODELVIEW);
   glLoadIdentity();
   glTranslatef(0.f, 0.f, -50.f);

   // reset model rotation
   state->distance = 40.f;
}

#endif


int main(int argc, char** argv) {

  unsigned int frame_width,frame_height;

  int err;
  
  ILCLIENT_T  *handle;
  
  COMPONENT_T *decodeComponent;
  COMPONENT_T *schedulerComponent;
  COMPONENT_T *clockComponent;
  COMPONENT_T *renderComponent;

  pthread_t threadMonitorFile; 

  TUNNEL_T decodeTunnel,schedulerTunnel,clockTunnel;    

  OMX_BUFFERHEADERTYPE *buff_header;

  printf("[main] Init host...\n");fflush(0);

  bcm_host_init();

  printf("[main] Setting up demuxer...\n");fflush(0);

  setup_demuxer(argv[1],&frame_width,&frame_height);

  printf("[%s] Init ilclient...\n",__FUNCTION__);fflush(0);    

  handle = ilclient_init();

  vcos_log_set_level(VCOS_LOG_CATEGORY, VCOS_LOG_TRACE);

  if (handle == NULL) {
      fprintf(stderr, "[%s] IL client init failed\n",__FUNCTION__);
      exit(1);
  }

  printf("[main] Init host...\n");fflush(0);    

  if (OMX_Init() != OMX_ErrorNone) {
      ilclient_destroy(handle);
      fprintf(stderr, "OMX init failed\n");
      exit(1);
  }

  printf("[%s] Setting callbacks...\n",__FUNCTION__);fflush(0);        

  ilclient_set_error_callback(handle,error_callback,NULL);
  ilclient_set_eos_callback(handle,eos_callback,NULL);
  ilclient_set_port_settings_callback(handle,port_settings_callback,NULL);
  ilclient_set_empty_buffer_done_callback(handle,empty_buffer_done_callback,NULL);

#ifdef USE_OPENGL_RENDERER

  ilclient_set_fill_buffer_done_callback(handle, my_fill_buffer_done, NULL);

/* Now we have the video size, init opengl */

  // Clear application state
  memset( state, 0, sizeof( *state ) );

  printf("[%s] Init OpenGL...\n",__FUNCTION__);fflush(0);       

  // Start OGLES
  init_ogl(state);

  printf("[%s] Init model world...\n",__FUNCTION__);fflush(0);       

  // Setup the model world
  init_model_proj(state);

  printf("[%s] Init textures...\n",__FUNCTION__);fflush(0);    

  // initialise the OGLES texture(s)
  init_textures(state);

#endif

  printf("[%s] Create components...\n",__FUNCTION__);fflush(0);    


/* Create all OMX components*/

  OMX_createComponent(handle, "video_decode", &decodeComponent,ILCLIENT_DISABLE_ALL_PORTS | ILCLIENT_ENABLE_INPUT_BUFFERS | ILCLIENT_ENABLE_OUTPUT_BUFFERS);
#ifdef USE_OPENGL_RENDERER
  OMX_createComponent(handle, "egl_render", &renderComponent,ILCLIENT_DISABLE_ALL_PORTS|ILCLIENT_ENABLE_OUTPUT_BUFFERS); 
#else
  OMX_createComponent(handle, "video_render", &renderComponent,ILCLIENT_DISABLE_ALL_PORTS|ILCLIENT_ENABLE_INPUT_BUFFERS);
#endif
  OMX_createComponent(handle, "clock", &clockComponent,ILCLIENT_DISABLE_ALL_PORTS);

  printf("[%s] Init clock...\n",__FUNCTION__);fflush(0);    

  OMX_initClock(clockComponent);

  OMX_createComponent(handle, "video_scheduler", &schedulerComponent, ILCLIENT_DISABLE_ALL_PORTS | ILCLIENT_ENABLE_INPUT_BUFFERS);
     
  printf("[%s] Set tunnel...\n",__FUNCTION__);fflush(0);      

  /* Create the tunnel */

  set_tunnel(&decodeTunnel, decodeComponent, 131, schedulerComponent, 10);
#ifdef USE_OPENGL_RENDERER
  set_tunnel(&schedulerTunnel, schedulerComponent, 11, renderComponent, 220); 
#else
  set_tunnel(&schedulerTunnel, schedulerComponent, 11, renderComponent, 90); 
#endif    
  set_tunnel(&clockTunnel, clockComponent, 80, schedulerComponent, 12);

  /* Start the clock first */    
  
  printf("[%s] Start clock...\n",__FUNCTION__);fflush(0);          

  OMX_startClock(clockComponent);

  if ((err = ilclient_setup_tunnel(&clockTunnel, 0, 0)) < 0) {
    fprintf(stderr, "[%s][ERROR] setting up clock tunnel %X\n",__FUNCTION__, err);
    exit(1);
  } else {
      printf("[%s] Clock tunnel set up ok\n",__FUNCTION__);fflush(0);
  }

  printf("[%s] OMX_printClockState before: \n",__FUNCTION__);fflush(0);
  OMX_printClockState(clockComponent);    

  OMX_changeStateToExecuting(clockComponent);

  // ilclient_change_component_state(decodeComponent, OMX_StateIdle);

  printf("[%s] OMX_printClockState after: \n",__FUNCTION__);fflush(0);
  OMX_printClockState(clockComponent);    


  /* Decode component:
  *
  * 1: send config
  * 2: enable port & port buffers
  * 3: execute 
  * 4: set tunnel
  */

  OMX_setVideoDecoderInputFormat(decodeComponent,fpsscale,fpsrate,frame_width,frame_height);

  printf("[%s] decodeComponent to executing state...\n",__FUNCTION__);fflush(0);          

/* Set all components to executing state */

  OMX_changeStateToExecuting(decodeComponent);  

  /* Not medatory ? */
  // OMX_sendDecoderConfig(decodeComponent, NULL);

  ilclient_enable_port_buffers(decodeComponent, 130, NULL, NULL, NULL);
  ilclient_enable_port(decodeComponent, 130);

// enable the decode output ports
  OMX_SendCommand(ilclient_get_handle(decodeComponent), OMX_CommandPortEnable, 131, NULL);
  ilclient_enable_port(decodeComponent, 131);

// enable the clock output ports
  OMX_SendCommand(ilclient_get_handle(clockComponent),OMX_CommandPortEnable, 80, NULL);
  ilclient_enable_port(clockComponent, 80);

// enable the scheduler ports
  OMX_SendCommand(ilclient_get_handle(schedulerComponent), OMX_CommandPortEnable, 10, NULL);
  ilclient_enable_port(schedulerComponent, 10);

  OMX_SendCommand(ilclient_get_handle(schedulerComponent), OMX_CommandPortEnable, 11, NULL);
  ilclient_enable_port(schedulerComponent, 11);

  OMX_SendCommand(ilclient_get_handle(schedulerComponent), OMX_CommandPortEnable, 12, NULL);
  ilclient_enable_port(schedulerComponent, 12);

// enable the render input ports

#ifndef USE_OPENGL_RENDERER 

  OMX_SendCommand(ilclient_get_handle(renderComponent),OMX_CommandPortEnable, 90, NULL);
  ilclient_enable_port(renderComponent, 90); 

#endif

//   // if (ilclient_change_component_state(decodeComponent,OMX_StateExecuting) < 0) {
//   //     fprintf(stderr, "[%s][ERROR] Couldn't change state to Executing\n",__FUNCTION__);
//   //     exit(1);
//   // }

  if ((err = ilclient_setup_tunnel(&decodeTunnel, 0, 0)) < 0) {
      fprintf(stderr, "Error setting up decode tunnel %X\n", err);
      exit(1);
  } else {
      printf("Decode tunnel set up ok\n");
  }

  printf("[%s] schedulerComponent to executing state...\n",__FUNCTION__);fflush(0);          

  if ((err = ilclient_setup_tunnel(&schedulerTunnel, 0, 1000)) < 0) {
      fprintf(stderr, "Error setting up scheduler tunnel %X\n", err);
      exit(1);
  } else {
      printf("Scheduler tunnel set up ok\n");
  }

  OMX_changeStateToExecuting(schedulerComponent);

  printf("[%s] Before Egl config \n",__FUNCTION__);

  printf("[%s] Decode status: %s\n",__FUNCTION__,OMX_getStateString(ilclient_get_handle(decodeComponent)));
  printf("[%s] Clock status: %s\n",__FUNCTION__,OMX_getStateString(ilclient_get_handle(clockComponent)));
  printf("[%s] Scheduler status: %s\n",__FUNCTION__,OMX_getStateString(ilclient_get_handle(schedulerComponent)));
  printf("[%s] Render status: %s\n",__FUNCTION__,OMX_getStateString(ilclient_get_handle(renderComponent)));


#ifndef USE_OPENGL_RENDERER 

  OMX_changeStateToExecuting(renderComponent);

#else

  // Enable the output port and tell egl_render to use the texture as a buffer
  //ilclient_enable_port(egl_render, 221); THIS BLOCKS SO CANT BE USED
  if (OMX_SendCommand(ILC_GET_HANDLE(renderComponent), OMX_CommandPortEnable, 221, NULL) != OMX_ErrorNone)
  {
     printf("[%s] OMX_CommandPortEnable failed.\n",__FUNCTION__);
     exit(1);
  }

  if (OMX_UseEGLImage(ILC_GET_HANDLE(renderComponent), &eglBuffer, 221, NULL, eglImage) != OMX_ErrorNone)
  {
     printf("[%s] OMX_UseEGLImage failed.\n",__FUNCTION__);
     exit(1);
  }

  printf("[%s] renderComponent to executing state...\n",__FUNCTION__);fflush(0);          

  OMX_changeStateToExecuting(renderComponent);

  printf("[%s] OMX_FillThisBuffer egl\n",__FUNCTION__);fflush(0);          

  // Request egl_render to write data to the texture buffer
  if(OMX_FillThisBuffer(ilclient_get_handle(renderComponent), eglBuffer) != OMX_ErrorNone)
  {
     printf("[%s] OMX_FillThisBuffer failed.\n",__FUNCTION__);
     exit(1);
  }

#endif

  printf("[%s] Before Egl config \n",__FUNCTION__);

  printf("[%s] Decode status: %s\n",__FUNCTION__,OMX_getStateString(ilclient_get_handle(decodeComponent)));
  printf("[%s] Clock status: %s\n",__FUNCTION__,OMX_getStateString(ilclient_get_handle(clockComponent)));
  printf("[%s] Scheduler status: %s\n",__FUNCTION__,OMX_getStateString(ilclient_get_handle(schedulerComponent)));
  printf("[%s] Render status: %s\n",__FUNCTION__,OMX_getStateString(ilclient_get_handle(renderComponent)));

  pthread_create(&threadMonitorFile,NULL,monitorFile,(void *)MAPDIR);

    int64_t lastPts;

// now work through the file
    while(1)
    {
        while (av_read_frame(pFormatCtx, &pkt) >= 0)
        {
            printf("Read pkt after port settings %d\n", pkt.size);fflush(0);
            // fwrite(pkt.data, 1, pkt.size, out);

            if (pkt.stream_index != video_stream_idx) {
                continue;
            }
            printf("  is video pkt\n");fflush(0);

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

#ifdef USE_OPENGL_RENDERER             

            redraw_scene(state);            

#endif

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
