#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#define PI 3.14159265

#include <math.h>
#include <assert.h>
#include <unistd.h>

#include <GLES/gl.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>

#include <ilclient.h>

#include <interface/vmcs_host/khronos/IL/OMX_Core.h>
#include <interface/vmcs_host/khronos/IL/OMX_Component.h>

#include <bcm_host.h>
#include <vcos_logging.h>

#include "OMX_utils.h"
#include "monitorThread.h"
#include "commandThread.h"
#include "inputThread.h"

#define VCOS_LOG_CATEGORY (&il_ffmpeg_log_category)
static VCOS_LOG_CAT_T il_ffmpeg_log_category;

/*
 * Frame buffer and its monitor, declared in inputThread
 */
extern unsigned int iframeBufferPos;
extern iframeBuffer[IFRAME_BUFFER_LENGTH];

extern pthread_mutex_t newDataAvailableMutex;
extern pthread_cond_t newDataAvailableCond;
extern unsigned int flagNewDataAvailable;

extern unsigned int flagIsFirst;

/*
 * Source configuration variables, declared in the command Thread
 *
 * Will be modified by control thread, so always use them protected by their sync variables
 */
extern int sourceUDPsocket;
extern int sourceUDPport;
extern char sourceFILEname[FILENAME_SIZE];
extern sourceType_t sourceType;

extern unsigned int flagQuit;

extern pthread_mutex_t sourceMutex;
extern pthread_cond_t sourceCond;
extern unsigned int flagSourceChanged;
extern unsigned int flagSourceLoaded;

static OMX_BUFFERHEADERTYPE* eglBuffer = NULL;

static void* eglImage = 0;

float p[4][2] = {
  {-1,-1},
  {1,-1},
  {1,1},
  {-1,1}
};

float o[4][2] = {
  {0,1},
  {1,1},
  {1,0},
  {0,0}
};

typedef struct
{
   uint32_t screen_width;
   uint32_t screen_height;
   EGLDisplay display;
   EGLSurface surface;
   EGLContext context;
   GLuint tex;
   int alreadyInit;

} QUAD_STATE_T;

static QUAD_STATE_T _state, *state=&_state;

unsigned int uWidth;
unsigned int uHeight;

static void eos_callback(void *userdata, COMPONENT_T *comp, OMX_U32 data)
{
  fprintf(stderr,"[%s][%s] Got eos event from %s\n",__FILE__,__FUNCTION__);fflush(stderr);
}

static void error_callback(void *userdata, COMPONENT_T *comp, OMX_U32 data)
{
  fprintf(stderr,"[%s][%s][ERROR] OMX error %s\n",__FILE__,__FUNCTION__,OMX_err2str(data));fflush(stderr);
}

static void port_settings_callback(void *userdata, COMPONENT_T *comp, OMX_U32 data)
{
  fprintf(stderr,"[%s] Got port Settings event\n",__FILE__,__FUNCTION__);fflush(stderr);
}

static void empty_buffer_done_callback(void *userdata, COMPONENT_T *comp)
{
  fprintf(stderr,"[%s] Got empty buffer done\n",__FUNCTION__);fflush(stderr);
}

static OMX_ERRORTYPE copy_into_buffer_and_empty(uint8_t *content,int size,int flagIsFirst,COMPONENT_T *component,OMX_BUFFERHEADERTYPE *buff_header)
{
  OMX_ERRORTYPE r;

  int isFull;

  int buff_size = buff_header->nAllocLen;

  // int size = pkt->size;
  // uint8_t *content = pkt->data;

  while (size > 0) 
  {
    isFull = size >=buff_header->nFilledLen;

    if(isFull)
      fprintf(stderr, "[%s][%s][ERROR] Buffer full, force flush \n",__FILE__,__FUNCTION__);fflush(stderr);

    buff_header->nFilledLen = (size > buff_size-1) ? buff_size-1 : size;
    buff_header->nOffset = 0;

    fprintf(stderr, "[%s][%s] Copying a chunk of %d/%d bytes\n",__FILE__,__FUNCTION__,size,buff_size);fflush(stderr);

    memset(buff_header->pBuffer, 0x0, buff_size);
    memcpy(buff_header->pBuffer, content, buff_header->nFilledLen);

    fprintf(stderr, "[%s][%s] Done copying a chunk of %d/%d bytes\n",__FILE__,__FUNCTION__,size,buff_size);fflush(stderr);      
    
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

    if (isFull || (size <= 0 && sourceType != SOURCE_UDP))
        buff_header->nFlags |= OMX_BUFFERFLAG_ENDOFFRAME;

    // printf("  DTS is %s %ld\n", "str", pkt->dts);
    // printf("  PTS is %s %ld\n", "str", pkt->pts);

    if (flagIsFirst/*pkt->dts == 0*/)
    {
        // printf("START\n");fflush(0);
        buff_header->nFlags = OMX_BUFFERFLAG_STARTTIME;
        // buff_header->nTimeStamp = ToOMXTime((uint64_t)0);
        // buff_header->nFlags |= OMX_BUFFERFLAG_TIME_UNKNOWN;
    }
    else 
    {
      /* With this bufferless player we d'ont need timestamps */
        // buff_header->nTimeStamp = ToOMXTime((uint64_t)0);        
        buff_header->nFlags = OMX_BUFFERFLAG_TIME_UNKNOWN;
        // buff_header->nTimeStamp = ToOMXTime((uint64_t) ((pkt->pts+timestampOffset) * 1000000/ time_base_den));
        // printf("Time stamp %d\n", buff_header->nTimeStamp);
    }

    fprintf(stderr, "[%s][%s] Empty buffer\n",__FILE__,__FUNCTION__,buff_size);fflush(stderr);      

    r = OMX_EmptyThisBuffer(ilclient_get_handle(component),buff_header);

    fprintf(stderr, "[%s][%s] Done\n",__FILE__,__FUNCTION__,buff_size);fflush(stderr);      

    if (r != OMX_ErrorNone)
        fprintf(stderr, "[%s][%s] Empty buffer error %s\n",__FILE__,__FUNCTION__,OMX_err2str(r));fflush(stderr);
    // else {
    //     // printf("Emptying buffer %p\n", buff_header);
    // }

    fprintf(stderr, "[%s][%s] Residual size %d\n",__FILE__,__FUNCTION__,size);fflush(stderr);      

    if (size > 0)
        buff_header = ilclient_get_input_buffer(component,130,1 /* block */);
    
  }

  fprintf(stderr, "[%s][%s] Returning \n",__FILE__,__FUNCTION__);fflush(stderr);        
  
  return r;
}

static void my_fill_buffer_done(void* data, COMPONENT_T* comp)
{
  if (OMX_FillThisBuffer(ilclient_get_handle(comp), eglBuffer) != OMX_ErrorNone)
    sprintf(stderr,"[%s][%s][ERROR] OMX_FillThisBuffer failed in callback\n",__FILE__,__FUNCTION__);
}

static void redraw_scene(QUAD_STATE_T *state)
{
   // printf("[redraw_scene] Draw\n");

  //  glLoadIdentity();
  //  // move camera back to see the cube
  //  glTranslatef(0.f, 0.f, -state->distance);
   
  //  // Start with a clear screen
  //  glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );

  //  // Need to rotate textures - do this by rotating each cube face
  //  glRotatef(270.f, 0.f, 0.f, 1.f ); // front face normal along z axis

  //  // // draw first 4 vertices
  //  // glDrawArrays( GL_TRIANGLE_STRIP, 0, 4);

  // glEnableClientState(GL_TEXTURE_COORD_ARRAY);

  // glBindTexture(GL_TEXTURE_2D, bufferText);

  // glVertexPointer(3, GL_FLOAT, 0, orthoVtx2);
  // glTexCoordPointer(4, GL_FLOAT, 0, orthotex);

  GLfloat orthoMat[16] =
  {
    1.0f/(state->screen_width/2.0f),0,0,0,
    0,1.0f/(state->screen_height/2.0f),0,0,
    0,0,1,0,
    0,0,0,1
  };

/* 
 * Indices order:
 *
 * 3 -- 2
 * |    |
 * 0 -- 1
 *
 * Must be drawn clockwise
 */

  GLubyte orthoIndices[] = {1,0,2,2,0,3};

  GLfloat orthoVtx[16] = 
  {
    (state->screen_width/2.0f)*p[0][0], (state->screen_height/2.0f)*p[0][1], 0.0f,
    (state->screen_width/2.0f)*p[1][0], (state->screen_height/2.0f)*p[1][1], 0.0f,
    (state->screen_width/2.0f)*p[2][0], (state->screen_height/2.0f)*p[2][1], 0.0f,
    (state->screen_width/2.0f)*p[3][0], (state->screen_height/2.0f)*p[3][1], 0.0f
  };
  
  float ax = p[2][0] - p[0][0];
  float ay = p[2][1] - p[0][1];
  float bx = p[3][0] - p[1][0];
  float by = p[3][1] - p[1][1];

  float cross = ax * by - ay * bx;

  float cy = p[0][1] - p[1][1];
  float cx = p[0][0] - p[1][0];

  float s = (ax * cy - ay * cx) / cross;
  float t = (bx * cy - by * cx) / cross;

  float q0 = 1 / (1 - t);
  float q1 = 1 / (1 - s);
  float q2 = 1 / t;
  float q3 = 1 / s;  
  
  GLfloat orthoTex[16] =
  {
    o[0][0]*q0, o[0][1]*q0, 0.0f*q0,1.0f*q0,
    o[1][0]*q1, o[1][1]*q1, 0.0f*q1,1.0f*q1,
    o[2][0]*q2, o[2][1]*q2, 0.0f*q2,1.0f*q2,
    o[3][0]*q3, o[3][1]*q3, 0.0f*q3,1.0f*q3
  };       
  
  glViewport(0, 0, state->screen_width, state->screen_height);

  glPushMatrix();

  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();

  glLoadMatrixf(orthoMat);

  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();

  glDisable(GL_BLEND);

  glEnableClientState(GL_VERTEX_ARRAY);
  glEnableClientState(GL_TEXTURE_COORD_ARRAY);

  glBindTexture(GL_TEXTURE_2D, state->tex);

  glVertexPointer(3, GL_FLOAT, 0, orthoVtx);
  glTexCoordPointer(4, GL_FLOAT, 0, orthoTex);

  glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_BYTE, orthoIndices);

  glDisableClientState(GL_TEXTURE_COORD_ARRAY);
  glDisableClientState(GL_VERTEX_ARRAY);

  glBindTexture(GL_TEXTURE_2D, 0);

  glPopMatrix();

  eglSwapBuffers(state->display, state->surface);
}

static int init_ogl(QUAD_STATE_T *state)
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
    sprintf(stderr,"[%s][%s][ERROR] Cannot eglGetDisplay\n",__FILE__,__FUNCTION__);

    return -1;
   }
   // assert(state->display!=EGL_NO_DISPLAY);

   // initialize the EGL display connection
   result = eglInitialize(state->display, NULL, NULL);
   // assert(EGL_FALSE != result);
   
   if(EGL_FALSE == result)
   {
    sprintf(stderr,"[%s][%s][ERROR] Cannot eglInitialize\n",__FILE__,__FUNCTION__);fflush(0);

    return -1;
   }

   // get an appropriate EGL frame buffer configuration
   // this uses a BRCM extension that gets the closest match, rather than standard which returns anything that matches
   result = eglSaneChooseConfigBRCM(state->display, attribute_list, &config, 1, &num_config);
   
   if(EGL_FALSE == result)
   {
    sprintf(stderr,"[%s][%s][ERROR] Cannot eglSaneChooseConfigBRCM\n",__FILE__,__FUNCTION__);fflush(0);

    return -1;
   }

   // create an EGL rendering context
   state->context = eglCreateContext(state->display, config, EGL_NO_CONTEXT, NULL);
   // assert(state->context!=EGL_NO_CONTEXT);

   if(state->context==EGL_NO_CONTEXT)
   {
    sprintf(stderr,"[%s][%s][ERROR] Cannot eglCreateContext\n",__FILE__,__FUNCTION__);fflush(0);

    return -1;
   }
   // create an EGL window surface
   success = graphics_get_display_size(0 /* LCD */, &state->screen_width, &state->screen_height);
   // assert( success >= 0 );

   if(success < 0)
   {
    sprintf(stderr,"[%s][%s][ERROR]Cannot graphics_get_display_size\n",__FILE__,__FUNCTION__);fflush(0);

    return -1;
   }   

   printf("[%s][%s] Screen size %d x %d\n",__FILE__,__FUNCTION__,state->screen_width,state->screen_height);fflush(0);

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
    sprintf(stderr,"[%s][%s][ERROR] Cannot eglCreateWindowSurface\n",__FILE__,__FUNCTION__);fflush(0);

    return -1;
   }  

   // connect the context to the surface
   result = eglMakeCurrent(state->display, state->surface, state->surface, state->context);

   if(result == EGL_FALSE)
   {
    sprintf(stderr,"[%s][%s][ERROR] Cannot eglMakeCurrent\n",__FILE__,__FUNCTION__);fflush(0);

    return -1;
   }     

    glClearColor (0.0f, 0.0f, 0.0f, 0.0f);
    
    glDisable(GL_LIGHTING);
    glDisable (GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    
    glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);

    glEnable(GL_POINT_SMOOTH);
    glEnable(GL_LINE_SMOOTH);
    glHint(GL_POINT_SMOOTH_HINT, GL_NICEST);
    glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);

    glMatrixMode(GL_MODELVIEW); 
}

static void init_textures(QUAD_STATE_T *state,unsigned int textureWidth,unsigned int textureHeight)
{
  // glGenTextures(1, &state->tex);
  if(state->alreadyInit)
  {
    eglDestroyImageKHR(state->display,eglImage);  
    glDeleteTextures(1,state->tex);
  }

   glGenTextures(1, &state->tex);

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

   glBindTexture(GL_TEXTURE_2D, state->tex);   
   glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, textureWidth, textureHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

   // /* Create EGL Image */
   eglImage = eglCreateImageKHR(state->display,state->context,EGL_GL_TEXTURE_2D_KHR,(EGLClientBuffer)state->tex,0);
    
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

   // glTexCoordPointer(2, GL_FLOAT, 0, texCoords);
   // glEnableClientState(GL_TEXTURE_COORD_ARRAY);



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
      fprintf(stderr, "[%s][%s][ERROR] eglDestroyImageKHR failed\n",__FILE__,__FUNCTION__);
  }

  // clear screen
  glClear( GL_COLOR_BUFFER_BIT );
  eglSwapBuffers(state->display, state->surface);

  // Release OpenGL resources
  eglMakeCurrent( state->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT );
  eglDestroySurface( state->display, state->surface );
  eglDestroyContext( state->display, state->context );
  eglTerminate( state->display );

  bcm_host_deinit();   

  printf("[%s][%s] Close callback completed\n",__FILE__,__FUNCTION__);
}

int main(int argc, char** argv) 
{
  unsigned int frame_width,frame_height;

  int err;
  
  ILCLIENT_T  *handle;
  
  COMPONENT_T *decodeComponent;
  COMPONENT_T *schedulerComponent;
  COMPONENT_T *clockComponent;
  COMPONENT_T *renderComponent;

  pthread_t threadMonitorFile; 
  pthread_t threadCommandSocket;
  pthread_t threadFrameInput;

  TUNNEL_T decodeTunnel,schedulerTunnel,clockTunnel;    

  OMX_BUFFERHEADERTYPE *buff_header;

  printf("[%s][%s] Init host...\n",__FILE__,__FUNCTION__);fflush(0);

  bcm_host_init();

  printf("[%s][%s] Init ilclient...\n",__FILE__,__FUNCTION__);fflush(0);    

  handle = ilclient_init();

  vcos_log_set_level(VCOS_LOG_CATEGORY, VCOS_LOG_TRACE);

  if (handle == NULL) {
      fprintf(stderr, "[%s][%s][ERROR] IL client init failed\n",__FILE__,__FUNCTION__);
      exit(-1);
  }

  printf("[%s][%s] Init host...\n",__FILE__,__FUNCTION__);fflush(0);    

  if (OMX_Init() != OMX_ErrorNone) {
      ilclient_destroy(handle);
      fprintf(stderr, "[%s][%s][ERROR] OMX init failed\n",__FILE__,__FUNCTION__);
      exit(-2);
  }

  printf("[%s][%s] Getting height/with from command line\n",__FILE__,__FUNCTION__);

  frame_width = atoi(argv[1]);
  frame_height = atoi(argv[2]);

  printf("[%s] height/with: %d,%d \n",__FUNCTION__,frame_height,frame_width);  
  
  /* If source is specified */
  if(argc > 3) 
  {
    pthread_mutex_lock(&sourceMutex);

    flagSourceChanged = 1;

    if(strstr(argv[3],"udp://@:") != NULL)
    {
      printf("[%s][%s] Source UDP\n",__FILE__,__FUNCTION__);fflush(0);          
      sourceType = SOURCE_UDP;
      sscanf(argv[3],"udp://@:%d",&sourceUDPport);

      printf("[%s][%s] UDP Port\n",__FILE__,__FUNCTION__,sourceUDPport);fflush(0);                
    }
    else
    {
      printf("[%s][%s] Source FILE\n",__FILE__,__FUNCTION__);fflush(0);                
      sourceType = SOURCE_FILE;
      strncpy(sourceFILEname,argv[3],FILENAME_SIZE);
    }


    printf("[%s][%s] Setting up demuxer with file %s...\n",__FILE__,__FUNCTION__,sourceFILEname);fflush(0);    
    
    pthread_cond_signal(&sourceCond);

    pthread_mutex_unlock(&sourceMutex);    
  }

  printf("[%s][%s] Setting callbacks...\n",__FILE__,__FUNCTION__);fflush(0); 

  ilclient_set_error_callback(handle,error_callback,NULL);
  ilclient_set_eos_callback(handle,eos_callback,NULL);
  ilclient_set_port_settings_callback(handle,port_settings_callback,NULL);
  ilclient_set_empty_buffer_done_callback(handle,empty_buffer_done_callback,NULL);
  ilclient_set_fill_buffer_done_callback(handle, my_fill_buffer_done, NULL);

/* Now we have the video size, init opengl */

  // Clear application state
  memset( state, 0, sizeof( *state ) );

  printf("[%s][%s] Init OpenGL...\n",__FILE__,__FUNCTION__);fflush(0);       

  // Start OGLES
  init_ogl(state);

  printf("[%s][%s] Init textures...\n",__FILE__,__FUNCTION__);fflush(0);    

  // initialise the OGLES texture(s)
  init_textures(state,frame_width,frame_height);

  state->alreadyInit = 1;       

  printf("[%s][%s] Create components...\n",__FILE__,__FUNCTION__);fflush(0);    

/* Create all OMX components*/
  OMX_createComponent(handle, "video_decode", &decodeComponent,ILCLIENT_DISABLE_ALL_PORTS | ILCLIENT_ENABLE_INPUT_BUFFERS);
  OMX_createComponent(handle, "egl_render", &renderComponent,ILCLIENT_DISABLE_ALL_PORTS|ILCLIENT_ENABLE_OUTPUT_BUFFERS); 
  OMX_createComponent(handle, "clock", &clockComponent,ILCLIENT_DISABLE_ALL_PORTS);
  OMX_createComponent(handle, "video_scheduler", &schedulerComponent, ILCLIENT_DISABLE_ALL_PORTS | ILCLIENT_ENABLE_INPUT_BUFFERS);
    
  printf("[%s][%s] Init clock...\n",__FILE__,__FUNCTION__);fflush(0);    
  OMX_initClock(clockComponent);

  printf("[%s][%s] Create tunnel...\n",__FILE__,__FUNCTION__);fflush(0);      
  set_tunnel(&decodeTunnel, decodeComponent, 131, schedulerComponent, 10);
  set_tunnel(&schedulerTunnel, schedulerComponent, 11, renderComponent, 220); 
  set_tunnel(&clockTunnel, clockComponent, 80, schedulerComponent, 12);

  /* Start the clock first */    
  if ((err = ilclient_setup_tunnel(&clockTunnel, 0, 0)) < 0) 
  {
    fprintf(stderr, "[%s][%s][ERROR] setting up clock tunnel %X\n",__FILE__,__FUNCTION__, err);
    exit(1);
  } 
  else 
  {
    printf("[%s][%s] Clock tunnel set up ok\n",__FILE__,__FUNCTION__);
    fflush(0);
  }

  printf("[%s][%s] OMX_printClockState before: \n",__FILE__,__FUNCTION__);
  fflush(0);

  OMX_printClockState(clockComponent);

  OMX_changeStateToExecuting(clockComponent);

  printf("[%s][%s] OMX_printClockState after: \n",__FILE__,__FUNCTION__);
  fflush(0);

  OMX_printClockState(clockComponent);   

  // ilclient_change_component_state(decodeComponent, OMX_StateIdle);
   
  // No need to configure the framerate, as in this mode we don't use b frames

  OMX_setVideoDecoderInputFormat(decodeComponent,0,0,frame_width,frame_height);

  // OMX_setVideoDecoderInputFormat(decodeComponent,0,0,0,0);

  printf("[%s][%s] decodeComponent to executing state...\n",__FILE__,__FUNCTION__);fflush(0);          

/* Set all components to executing state */

  OMX_changeStateToExecuting(decodeComponent);  

  /* Not medatory ? */
  // OMX_sendDecoderConfig(decodeComponent, NULL);

  ilclient_enable_port_buffers(decodeComponent, 130, NULL, NULL, NULL);
  // ilclient_enable_port(decodeComponent, 130);


/* Those init below are never used in reference example on github/raspberrypi

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
*/

// enable the render input ports


//   // if (ilclient_change_component_state(decodeComponent,OMX_StateExecuting) < 0) {
//   //     fprintf(stderr, "[%s][ERROR] Couldn't change state to Executing\n",__FUNCTION__);
//   //     exit(1);
//   // }

  if ((err = ilclient_setup_tunnel(&decodeTunnel, 0, 0)) < 0)
  {
      fprintf(stderr, "[%s][%s][ERROR] Error setting up decode tunnel %X\n",__FILE__,__FUNCTION__,err);
      exit(1);
  }
  else
  {
      printf("[%s][%s] Decode tunnel set up ok\n",__FILE__,__FUNCTION__);
  }

  printf("[%s][%s] schedulerComponent to executing state...\n",__FILE__,__FUNCTION__);fflush(0);          

  if ((err = ilclient_setup_tunnel(&schedulerTunnel, 0, 1000)) < 0) {
      fprintf(stderr, "[%s][%s][ERROR] Error setting up scheduler tunnel %X\n",__FILE__,__FUNCTION__,err);
      exit(1);
  } else {
      printf("[%s][%s] Scheduler tunnel set up ok\n",__FILE__,__FUNCTION__);
  }

  OMX_changeStateToExecuting(schedulerComponent);

  printf("[%s][%s] Before Egl config \n",__FILE__,__FUNCTION__);

  printf("[%s][%s] Decode status: %s\n",__FILE__,__FUNCTION__,OMX_getStateString(ilclient_get_handle(decodeComponent)));
  printf("[%s][%s] Clock status: %s\n",__FILE__,__FUNCTION__,OMX_getStateString(ilclient_get_handle(clockComponent)));
  printf("[%s][%s] Scheduler status: %s\n",__FILE__,__FUNCTION__,OMX_getStateString(ilclient_get_handle(schedulerComponent)));
  printf("[%s][%s] Render status: %s\n",__FILE__,__FUNCTION__,OMX_getStateString(ilclient_get_handle(renderComponent)));


  // Enable the output port and tell egl_render to use the texture as a buffer
  //ilclient_enable_port(egl_render, 221); THIS BLOCKS SO CANT BE USED
  if (OMX_SendCommand(ILC_GET_HANDLE(renderComponent), OMX_CommandPortEnable, 221, NULL) != OMX_ErrorNone)
  {
     printf("[%s][%s] OMX_CommandPortEnable failed.\n",__FILE__,__FUNCTION__);
     exit(1);
  }

  if (OMX_UseEGLImage(ILC_GET_HANDLE(renderComponent), &eglBuffer, 221, NULL, eglImage) != OMX_ErrorNone)
  {
     printf("[%s][%s] OMX_UseEGLImage failed.\n",__FILE__,__FUNCTION__);
     exit(1);
  }

  printf("[%s][%s] renderComponent to executing state...\n",__FILE__,__FUNCTION__);fflush(0);          

  OMX_changeStateToExecuting(renderComponent);

  printf("[%s][%s] OMX_FillThisBuffer egl\n",__FILE__,__FUNCTION__);fflush(0);          

  // Request egl_render to write data to the texture buffer
  if(OMX_FillThisBuffer(ilclient_get_handle(renderComponent), eglBuffer) != OMX_ErrorNone)
  {
     printf("[%s][%s] OMX_FillThisBuffer failed.\n",__FILE__,__FUNCTION__);
     exit(1);
  }

  printf("[%s][%s] Before Egl config \n",__FILE__,__FUNCTION__);

  printf("[%s][%s] Decode status: %s\n",__FILE__,__FUNCTION__,OMX_getStateString(ilclient_get_handle(decodeComponent)));
  printf("[%s][%s] Clock status: %s\n",__FILE__,__FUNCTION__,OMX_getStateString(ilclient_get_handle(clockComponent)));
  printf("[%s][%s] Scheduler status: %s\n",__FILE__,__FUNCTION__,OMX_getStateString(ilclient_get_handle(schedulerComponent)));
  printf("[%s][%s] Render status: %s\n",__FILE__,__FUNCTION__,OMX_getStateString(ilclient_get_handle(renderComponent)));

  // pthread_create(&threadMonitorFile,NULL,monitorFile,(void *)MAPDIR);

  pthread_create(&threadCommandSocket,NULL,commandThread,NULL);
  pthread_create(&threadFrameInput,NULL,inputThread,NULL);
  pthread_create(&threadMonitorFile,NULL,monitorThread,NULL);

  uint8_t * frameData;
  unsigned int frameSize;

 // Startclock is not usefull when we dont need the services of video_scheduler 
  // printf("[%s] Start clock...\n",__FUNCTION__);fflush(0);            

  // OMX_startClock(clockComponent);

  while(!flagQuit)
  {
    /*
     * Wait for the input thread
     */
    pthread_mutex_lock(&newDataAvailableMutex);

    while(!flagNewDataAvailable && !flagQuit)
    {
      fprintf(stderr, "[%s][%s] Here am I, still waiting...\n",__FILE__,__FUNCTION__);fflush(stderr);      
      pthread_cond_wait(&newDataAvailableCond,&newDataAvailableMutex);
      fprintf(stderr, "[%s][%s] Wait loop \n",__FILE__,__FUNCTION__);fflush(stderr);      
    }

    fprintf(stderr, "[%s][%s] Wait finished! \n",__FILE__,__FUNCTION__);fflush(stderr);

    frameSize = iframeBufferPos;
    frameData = iframeBuffer;

    pthread_mutex_unlock(&newDataAvailableMutex);

    if(flagQuit)
      break;

    fprintf(stderr, "[%s][%s] Getting buffer \n",__FILE__,__FUNCTION__);fflush(stderr);    

    // do we have a decode input buffer we can fill and empty?
    buff_header = ilclient_get_input_buffer(decodeComponent,130,1 /* block */);

    fprintf(stderr, "[%s][%s] Copying %d bytes into buffer \n",__FILE__,__FUNCTION__,frameSize);fflush(stderr);        

    if (buff_header != NULL) 
      copy_into_buffer_and_empty(frameData,frameSize,flagIsFirst,decodeComponent,buff_header);
    else
      fprintf(stderr,"[%s][%s][ERROR] No input buffer.\n",__FILE__,__FUNCTION__);fflush(stderr);            

    fprintf(stderr, "[%s][%s] Copying done \n",__FILE__,__FUNCTION__);fflush(stderr);        

    err = ilclient_wait_for_event(decodeComponent,OMX_EventPortSettingsChanged, 131, 0, 0, 1, ILCLIENT_EVENT_ERROR | ILCLIENT_PARAMETER_CHANGED, 0);

    if (err >= 0)
        printf("[%s][ERROR] Another port settings change\n",__FUNCTION__);
    
    /*
     * Notify input thread the data is consummed
     */
    pthread_mutex_lock(&newDataAvailableMutex);

    flagNewDataAvailable = 0;
    pthread_cond_broadcast(&newDataAvailableCond);

    pthread_mutex_unlock(&newDataAvailableMutex);  

    redraw_scene(state);
    


    // pthread_mutex_lock(&playbackMutex);

    // /* Playback paused by command thread? */
    // while(flagPaused)
    // {
    //   glClear( GL_COLOR_BUFFER_BIT );
    //   eglSwapBuffers(state->display, state->surface);              

    //   printf("PAUSE\n");

    //   pthread_cond_wait(&condPaused,&playbackMutex);
    // }

    // /* File changed by command thread? */
    // if(flagSourceChanged)
    // {
    //   printf("SOURCE CHANGED, loading new file\n");

    //   strcpy(currentFile,requestedFile);      
    //   inputSetupSource(currentFile,currentFile,&frame_width,&frame_height); 

    //   gettimeofday(&timestampBegin,NULL);

    //   flagSourceChanged = false;
    // }

    // /* EOF reached -> Looping requested? */
    // if(inputThreadStatus == 0)
    // {
    //   if(confLooping)
    //     inputRewind();
    //   else
    //     flagPaused = 1;
      
    // }

    // pthread_mutex_unlock(&playbackMutex);        
  }

        // printf("Timestamp offset was %lld\n",(long long)timestampOffset);

        /* Open next file for reading */
//         pthread_mutex_lock(&playbackMutex);

//         /* While file is paused, stay here but allow file change  */
//         while(flagPaused)
//         {

//           if(strcmp(currentFile,requestedFile) != 0)
//           {          
//             strcpy(currentFile,requestedFile);

//             printf("Next is %s\n",currentFile);          

//             /* If file is empty */
//             if(strcmp(currentFile,"") != 0)
//             {
//               printf("Next is %s\n",currentFile);          

//               timestampOffset = 0;      

//               gettimeofday(&timestampBegin,NULL);
//             }
//           }
//         }
//         /* Rewind this file */
//         else
//         {
//           // printf("Loop\n");


//           inputRewind();
// // 
//           // timestampOffset += lastPts*time_base_num*1000/time_base_den;

//           // if(avformat_seek_file(&pFormatCtx,video_stream_idx,0,0,999999,AVSEEK_FLAG_BACKWARD | AVSEEK_FLAG_ANY)<0)
//           // {
//           //   printf("Cannot seek\n");            
//             avformat_close_input(&pFormatCtx);
//             inputSetupSource(currentFile,&frame_width,&frame_height);
//           // }
//         }

//         pthread_mutex_unlock(&playbackMutex);

          // avformat_close_input(&pFormatCtx);
      
          // inputSetupSource(currentFile,&frame_width,&frame_height);

    
        // printf("Timestamp offset is now %lld (+= %lld)\n",(long long)timestampOffset,(long long)lastPts);     

    //     OMX_SendCommand(ilclient_get_handle(decodeComponent),OMX_CommandFlush,130,NULL);
    //     OMX_SendCommand(ilclient_get_handle(decodeComponent),OMX_CommandFlush,131,NULL);

    //     printf(".\n");fflush(0);

    //     ilclient_wait_for_event(decodeComponent, OMX_EventCmdComplete, OMX_CommandFlush, 0, 130, 0, ILCLIENT_PORT_FLUSH, -1);
    //     ilclient_wait_for_event(decodeComponent, OMX_EventCmdComplete, OMX_CommandFlush, 0, 131, 0, ILCLIENT_PORT_FLUSH, -1);

    //     printf(".\n");fflush(0);

    //     OMX_send_EOS_to_decoder(decodeComponent);        

    //     OMX_stopClock(clockComponent);

    //     OMX_initClock(clockComponent);        

    //     printf(".\n");fflush(0);

    // /* The following doesn't seem to work if a eos callback is already attached */
    //   ilclient_wait_for_event(renderComponent, OMX_EventBufferFlag, 220, 0, OMX_BUFFERFLAG_EOS, 0,ILCLIENT_BUFFER_FLAG_EOS, 10000);

    //     printf(".\n");fflush(0);

// }


// ilclient_wait_for_event(renderComponent, OMX_EventBufferFlag, 220, 0, OMX_BUFFERFLAG_EOS, 0,ILCLIENT_BUFFER_FLAG_EOS, 100);
  
  printf("[s][s] Flush the render pipe....\n",__FILE__,__FUNCTION__);fflush(0);
  
  // need to flush the renderer to allow video_decode to disable its input port

  TUNNEL_T tunnels[4];

  memset(tunnels, 0, sizeof(tunnels)); 

  tunnels[0]=decodeTunnel;
  tunnels[1]=schedulerTunnel;
  tunnels[2]=clockTunnel;

  ilclient_flush_tunnels(tunnels, 0);

      // ilclient_flush_tunnels(tunnels, 0);





        // OMX_changeStateToIdle(decodeComponent); 

        // OMX_SendCommand(ilclient_get_handle(decodeComponent),  OMX_CommandPortDisable, 130, NULL);        
        // OMX_SendCommand(ilclient_get_handle(decodeComponent),  OMX_CommandPortDisable, 131, NULL);

        // ilclient_wait_for_event(decodeComponent, OMX_EventCmdComplete,  OMX_CommandPortDisable, 0, 130, 0, ILCLIENT_PORT_FLUSH, -1);
        // ilclient_wait_for_event(decodeComponent, OMX_EventCmdComplete,  OMX_CommandPortDisable, 0, 131, 0, ILCLIENT_PORT_FLUSH, -1);  

        // OMX_setVideoDecoderInputFormat(decodeComponent,fpsscale,fpsrate,frame_width,frame_height);

        // OMX_SendCommand(ilclient_get_handle(decodeComponent),  OMX_CommandPortEnable, 130, NULL);        
        // OMX_SendCommand(ilclient_get_handle(decodeComponent),  OMX_CommandPortEnable, 131, NULL);

        // ilclient_wait_for_event(decodeComponent, OMX_EventCmdComplete,  OMX_CommandPortEnable, 0, 130, 0, ILCLIENT_PORT_FLUSH, -1);
        // ilclient_wait_for_event(decodeComponent, OMX_EventCmdComplete,  OMX_CommandPortEnable, 0, 131, 0, ILCLIENT_PORT_FLUSH, -1);  

        // OMX_changeStateToExecuting(decodeComponent);           

            
          //  data_len=0;        
           
          // memset(&cstate, 0, sizeof(cstate));
          // cstate.nSize = sizeof(cstate);
          // cstate.nVersion.nVersion = OMX_VERSION;
          // cstate.eState = OMX_TIME_ClockStateStopped;
          // cstate.nWaitMask = 1;
          //         errorCode= OMX_SetParameter(ILC_GET_HANDLE(clock), OMX_IndexConfigTimeClockState, &cstate);
                  
          // memset(&cstate, 0, sizeof(cstate));
          // cstate.nSize = sizeof(cstate);
          // cstate.nVersion.nVersion = OMX_VERSION;
          // cstate.eState = OMX_TIME_ClockStateWaitingForStartTime;
          // cstate.nWaitMask = 1;
          //         errorCode= OMX_SetParameter(ILC_GET_HANDLE(clock), OMX_IndexConfigTimeClockState, &cstate);
          //     ilclient_change_component_state(clock, OMX_StateExecuting);
          //  first_packet = 1;
          //  rewind(in);


    // ilclient_wait_for_event(renderComponent, 
    //     OMX_EventBufferFlag, 
    //     90, 0, OMX_BUFFERFLAG_EOS, 0,
    //     ILCLIENT_BUFFER_FLAG_EOS, 10000);
    // printf("EOS on render\n");        

  
   printf("+");fflush(0);

   ilclient_disable_port_buffers(decodeComponent, 130, NULL, NULL, NULL);

   ilclient_disable_tunnel(&decodeTunnel);
   ilclient_disable_tunnel(&schedulerTunnel);
   ilclient_disable_tunnel(&clockTunnel);

   printf("+");fflush(0);

   ilclient_teardown_tunnels(tunnels);

   printf("+");fflush(0);

   COMPONENT_T *list[5];   
   memset(list, 0, sizeof(list));

   list[0]=schedulerComponent;
   list[1]=decodeComponent;
   list[2]=renderComponent;
   list[3]=clockComponent;   

   ilclient_state_transition(list, OMX_StateIdle);

   printf("+");fflush(0);

   ilclient_state_transition(list, OMX_StateLoaded);

   printf("+");fflush(0);   

   ilclient_cleanup_components(list);

   printf("+");fflush(0);   

   OMX_Deinit();

   printf("+");fflush(0);   

   ilclient_destroy(handle);

   printf("+");fflush(0);   


//         OMX_send_EOS_to_decoder(decodeComponent);        
        
//         pFormatCtx = NULL;

//         printf("1");fflush(0);

//         inputSetupSource(argv[fileIndex++],&frame_width,&frame_height);

//         printf("2");fflush(0);

//         OMX_changeStateToIdle(decodeComponent); 


//   ilclient_disable_port_buffers(decodeComponent, 130, NULL, NULL, NULL);
//   ilclient_disable_port(decodeComponent, 130);

//   ilclient_disable_port_buffers(decodeComponent, 131, NULL, NULL, NULL);
//   ilclient_disable_port(decodeComponent, 131);  

// // enable the decode output ports
//   OMX_SendCommand(ilclient_get_handle(decodeComponent), OMX_CommandPortDisable, 130, NULL);
//   OMX_SendCommand(ilclient_get_handle(decodeComponent), OMX_CommandPortDisable, 131, NULL);



        

//   printf("[%s] Decode status: %s\n",__FUNCTION__,OMX_getStateString(ilclient_get_handle(decodeComponent)));
//   printf("[%s] Clock status: %s\n",__FUNCTION__,OMX_getStateString(ilclient_get_handle(clockComponent)));
//   printf("[%s] Scheduler status: %s\n",__FUNCTION__,OMX_getStateString(ilclient_get_handle(schedulerComponent)));
//   printf("[%s] Render status: %s\n",__FUNCTION__,OMX_getStateString(ilclient_get_handle(renderComponent)));

//         OMX_setVideoDecoderInputFormat(decodeComponent,fpsscale,fpsrate,frame_width,frame_height);



//   ilclient_enable_port_buffers(decodeComponent, 130, NULL, NULL, NULL);
//   ilclient_enable_port(decodeComponent, 130);

//   ilclient_enable_port_buffers(decodeComponent, 131, NULL, NULL, NULL);
//   ilclient_enable_port(decodeComponent, 131);  

// // enable the decode output ports
//   OMX_SendCommand(ilclient_get_handle(decodeComponent), OMX_CommandPortEnable, 130, NULL);
//   OMX_SendCommand(ilclient_get_handle(decodeComponent), OMX_CommandPortEnable, 131, NULL);



//   init_textures(state,frame_width,frame_height);

//     if (OMX_UseEGLImage(ILC_GET_HANDLE(renderComponent), &eglBuffer, 221, NULL, eglImage) != OMX_ErrorNone)
//   {
//      printf("[%s] OMX_UseEGLImage failed.\n",__FUNCTION__);
//      exit(1);
//   }


//         OMX_changeStateToExecuting(decodeComponent);

//   printf("[%s] Decode status: %s\n",__FUNCTION__,OMX_getStateString(ilclient_get_handle(decodeComponent)));
//   printf("[%s] Clock status: %s\n",__FUNCTION__,OMX_getStateString(ilclient_get_handle(clockComponent)));
//   printf("[%s] Scheduler status: %s\n",__FUNCTION__,OMX_getStateString(ilclient_get_handle(schedulerComponent)));
//   printf("[%s] Render status: %s\n",__FUNCTION__,OMX_getStateString(ilclient_get_handle(renderComponent)));


//         printf("3");fflush(0);


        // avformat_seek_file(pFormatCtx,0,0,0,10,AVSEEK_FLAG_BACKWARD);

    

    exit(0);
}
