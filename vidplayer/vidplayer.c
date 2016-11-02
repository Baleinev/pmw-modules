#define _GNU_SOURCE             /* See feature_test_macros(7) */
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include <math.h>
#include <signal.h>

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
#include "circularBufferCollection.h"

#include "vidplayer.h"

#include "mire.c"

#define VCOS_LOG_CATEGORY (&il_ffmpeg_log_category)
static VCOS_LOG_CAT_T il_ffmpeg_log_category;

int sourceUDPport = 4243;
unsigned int udpWait = DEFAULT_UDP_WAIT;
unsigned int frame_width =1024;
unsigned int frame_height = 768;

/*
 * Frame buffer and its monitor, declared in inputThread
 */
// extern unsigned int iframeBufferPos;
// extern iframeBuffer[IFRAME_BUFFER_LENGTH];
extern CircularBufferCollection *bufferArray;

extern pthread_mutex_t newDataAvailableMutex;
extern pthread_cond_t newDataAvailableCond;
extern unsigned int flagNewDataAvailable;

extern unsigned int flagIsFirst;

extern unsigned int frameBufferCount;

/*
 * Source configuration variables, declared in the command Thread
 *
 * Will be modified by control thread, so always use them protected by their sync variables
 */
extern int sourceUDPsocket;
extern char sourceFILEname[FILENAME_SIZE];
extern sourceType_t sourceType;

extern unsigned int flagQuit;
unsigned int flagFail = 0;
unsigned int flagDrawMire = 0;

extern pthread_mutex_t sourceMutex;
extern pthread_cond_t sourceCond;
extern unsigned int flagSourceChanged;
extern unsigned int flagSourceLoaded;

static OMX_BUFFERHEADERTYPE *eglBuffer[3] = {NULL,NULL,NULL}; // Triple buffering
static void *eglImage[3] = {NULL,NULL,NULL};

unsigned int eglWriteIndex = 0;
unsigned int openglReadIndex = 2;   


unsigned int flagMappingChanged = 0;
unsigned int flagConfChanged = 0;
unsigned int flagBlendingChanged = 0;

float b[4] = {
  0,0,0,0
};

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

GLfloat orthoVtx[12];
GLfloat orthoTex[16];
GLfloat orthoTexOverlay[16];

GLubyte orthoIndices[6] = {1,0,2,2,0,3};

unsigned char blendPixels[1024*768*4];

typedef struct
{
   uint32_t screen_width;
   uint32_t screen_height;
   EGLDisplay display;
   EGLSurface surface;
   EGLContext context;
   GLuint tex[3]; // triple buffering
   GLuint texOverlay;
   GLuint texMire;
   int alreadyInit;

} QUAD_STATE_T;

static QUAD_STATE_T _state, *state=&_state;


static void eos_callback(void *userdata, COMPONENT_T *comp, OMX_U32 data)
{
  ERR("Got eos event from %s",OMX_getStateString(ilclient_get_handle(comp)));

  pthread_mutex_lock(&newDataAvailableMutex);

    flagQuit = 1;

    if(!flagNewDataAvailable)
      pthread_cond_broadcast(&newDataAvailableCond);

    pthread_mutex_unlock(&newDataAvailableMutex);    
}

static void error_callback(void *userdata, COMPONENT_T *comp, OMX_U32 data)
{ 
  ERR("[ERROR] OMX error %d:%s, %s",data,OMX_err2str(data),OMX_getStateString(ilclient_get_handle(comp)));

  if(data != OMX_ErrorSameState) // OMX_ErrorSameState, not fatal
  {
    pthread_mutex_lock(&newDataAvailableMutex);

    flagQuit = 1;

    if(!flagNewDataAvailable)
      pthread_cond_broadcast(&newDataAvailableCond);

    pthread_mutex_unlock(&newDataAvailableMutex);

  }
}

static void port_settings_callback(void *userdata, COMPONENT_T *comp, OMX_U32 data)
{ 
  ERR("[%s][%s] Got port Settings event: 0x%.8X, %s",__FILE__,__FUNCTION__,data,OMX_getStateString(ilclient_get_handle(comp)));

  OMX_PARAM_PORTDEFINITIONTYPE portParam;
  memset(&portParam, 0, sizeof(portParam));
  // OMX_INIT_STRUCTURE(portParam);
  // 
  // 
  portParam.nSize = sizeof( OMX_PARAM_PORTDEFINITIONTYPE);
  portParam.nVersion.nVersion = OMX_VERSION;

  portParam.nPortIndex = 130; 
  // 
  OMX_ERRORTYPE r;

  r = OMX_GetParameter(ilclient_get_handle(comp),OMX_IndexParamPortDefinition, &portParam);

  if(r != OMX_ErrorNone)
  {
    ERR("Cannot get params");
  }
  else
  {
    LOG("Width:%d",portParam.format.video.nFrameWidth);
    LOG("Height:%d",portParam.format.video.nFrameHeight);
    LOG("xFramerate:%d",portParam.format.video.xFramerate);
  }
}

static void empty_buffer_done_callback(void *userdata, COMPONENT_T *comp)
{
  DBG("-");
  DBG("[%s] Got empty buffer done",__FUNCTION__);
}

static void my_fill_buffer_done(void* data, COMPONENT_T* comp)
{
  // Triple buffering
  eglWriteIndex = (eglWriteIndex +1)%3;
  openglReadIndex = (openglReadIndex + 1) %3; 

  printf("*");fflush(0);

  OMX_changeStateToIdle(comp);

  if (OMX_UseEGLImage(ilclient_get_handle(comp),&(eglBuffer[eglWriteIndex]),221,NULL,eglImage[eglWriteIndex]) != OMX_ErrorNone)
  {
    ERR("OMX_UseEGLImage failed.");
    exit(1);
  }
  printf("-");fflush(0);

  OMX_changeStateToExecuting(comp);

  printf("+");fflush(0);

  if(OMX_FillThisBuffer(ilclient_get_handle(comp), eglBuffer[eglWriteIndex]) != OMX_ErrorNone)
    ERR("[%s][%s][ERROR] OMX_FillThisBuffer failed in callback",__FILE__,__FUNCTION__);
}

int copy_into_buffer_and_empty(uint8_t *content,int size,int flagIsFirst,COMPONENT_T *component,OMX_BUFFERHEADERTYPE *buff_header)
{
  OMX_ERRORTYPE r;

  int isFull,isLast;

  int buff_size = buff_header->nAllocLen;

  // int size = pkt->size;
  // uint8_t *content = pkt->data;
  // 
      // ERR("AllocLen/nFilledLen: %d/%d",buff_header->nAllocLen,buff_header->nFilledLen);
  // 

  while (!flagFail && size > 0) 
  {
    // isFull = size >=buff_header->nFilledLen;

    // if(isFull)
    //   DBG( "[%s][%s][ERROR] Buffer full, force flush ",__FILE__,__FUNCTION__);

    buff_header->nFilledLen = (size > buff_size-1) ? buff_size-1 : size;
    buff_header->nOffset = 0;

    DBG( "[%s][%s] Copying a chunk of %d/%d bytes",__FILE__,__FUNCTION__,size,buff_size);

    memset(buff_header->pBuffer, 0x0, buff_size);
    memcpy(buff_header->pBuffer, content, buff_header->nFilledLen);

    DBG( "[%s][%s] Done copying a chunk of %d/%d bytes",__FILE__,__FUNCTION__,size,buff_size);      
    
    size -= buff_header->nFilledLen;
    content += buff_header->nFilledLen;

    /*
    if (size < buff_size) {
    memcpy((unsigned char *)buff_header->pBuffer, 
    pkt->data, size);
    } else {
    DBG("Buffer not big enough %d %d", buff_size, size);
    return -1;
    }

    buff_header->nFilledLen = size;
    */

    buff_header->nFlags = 0;

    isLast = (size == 0); // Last chunk
    
    if (isLast || (size <= 0 && sourceType != SOURCE_UDP))
    {
      // buff_header->nFlags |= OMX_BUFFERFLAG_ENDOFFRAME;
    }

    // DBG("  DTS is %s %ld", "str", pkt->dts);
    // DBG("  PTS is %s %ld", "str", pkt->pts);
    // 

    if (flagIsFirst/*pkt->dts == 0*/)
    {
        // DBG("START");
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
        // DBG("Time stamp %d", buff_header->nTimeStamp);
    }

    DBG( "[%s][%s] Empty buffer",__FILE__,__FUNCTION__,buff_size);      

    r = OMX_EmptyThisBuffer(ilclient_get_handle(component),buff_header);

    DBG( "[%s][%s] Done",__FILE__,__FUNCTION__,buff_size);      

    if (r != OMX_ErrorNone)
        ERR( "[%s][%s] Empty buffer error %s",__FILE__,__FUNCTION__,OMX_err2str(r));
    // else {
    //     // DBG("Emptying buffer %p", buff_header);
    // }

    DBG( "[%s][%s] Residual size %d",__FILE__,__FUNCTION__,size);      

    int waited = 0;

    if (size > 0)
    {
      while(!flagFail && ((buff_header = ilclient_get_input_buffer(component,130,0 /* 0 = noblock */))== NULL))
      {
        waited++;
        
        ERR("!");

      if(waited == FAIL_DELAY)
      {
        flagQuit = 1;
        flagFail = 1;
        return -1;
      }

        usleep(1000);        
      }
    }
  }
  
  DBG( "Returning");        
  
  return 0;
}

static void redraw_scene(QUAD_STATE_T *state)
{
  glBindTexture(GL_TEXTURE_2D, state->tex[openglReadIndex]);
  glVertexPointer(3, GL_FLOAT, 0, orthoVtx);
  glTexCoordPointer(4, GL_FLOAT, 0, orthoTex);
  glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_BYTE, orthoIndices);

  glBindTexture(GL_TEXTURE_2D, state->texOverlay);
  glVertexPointer(3, GL_FLOAT, 0, orthoVtx);
  glTexCoordPointer(4, GL_FLOAT, 0, orthoTexOverlay);
  glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_BYTE, orthoIndices);    

  if(flagDrawMire)
  {
    glBindTexture(GL_TEXTURE_2D, state->texMire);
    glVertexPointer(3, GL_FLOAT, 0, orthoVtx);
    glTexCoordPointer(4, GL_FLOAT, 0, orthoTexOverlay);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_BYTE, orthoIndices);     
  }

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
    ERR("[%s][%s][ERROR] Cannot eglGetDisplay",__FILE__,__FUNCTION__);

    return -1;
  }
  // assert(state->display!=EGL_NO_DISPLAY);

  // initialize the EGL display connection
  result = eglInitialize(state->display, NULL, NULL);
  // assert(EGL_FALSE != result);

  if(EGL_FALSE == result)
  {
    ERR("[%s][%s][ERROR] Cannot eglInitialize",__FILE__,__FUNCTION__);

    return -1;
  }

  // get an appropriate EGL frame buffer configuration
  // this uses a BRCM extension that gets the closest match, rather than standard which returns anything that matches
  result = eglSaneChooseConfigBRCM(state->display, attribute_list, &config, 1, &num_config);

  if(EGL_FALSE == result)
  {
  ERR("[%s][%s][ERROR] Cannot eglSaneChooseConfigBRCM",__FILE__,__FUNCTION__);

  return -1;
  }

  // create an EGL rendering context
  state->context = eglCreateContext(state->display, config, EGL_NO_CONTEXT, NULL);
  // assert(state->context!=EGL_NO_CONTEXT);

  if(state->context==EGL_NO_CONTEXT)
  {
  ERR("[%s][%s][ERROR] Cannot eglCreateContext",__FILE__,__FUNCTION__);

  return -1;
  }
  // create an EGL window surface
  success = graphics_get_display_size(0 /* LCD */, &state->screen_width, &state->screen_height);
  // assert( success >= 0 );

  if(success < 0)
  {
  ERR("[%s][%s][ERROR]Cannot graphics_get_display_size",__FILE__,__FUNCTION__);

  return -1;
  }   

  DBG("[%s][%s] Screen size %d x %d",__FILE__,__FUNCTION__,state->screen_width,state->screen_height);

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
  ERR("[%s][%s][ERROR] Cannot eglCreateWindowSurface",__FILE__,__FUNCTION__);

  return -1;
  }  

  // connect the context to the surface
  result = eglMakeCurrent(state->display, state->surface, state->surface, state->context);

  if(result == EGL_FALSE)
  {
  ERR("[%s][%s][ERROR] Cannot eglMakeCurrent",__FILE__,__FUNCTION__);

  return -1;
  }     

  glClearColor (0.0f, 0.0f, 0.0f, 0.0f);

  glDisable(GL_LIGHTING);
  glDisable (GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);

  glEnable(GL_TEXTURE_2D);
  glEnable(GL_BLEND);
  glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);

  glEnable(GL_POINT_SMOOTH);
  glEnable(GL_LINE_SMOOTH);
  glHint(GL_POINT_SMOOTH_HINT, GL_NICEST);
  glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);

  glMatrixMode(GL_MODELVIEW); 
}

float blendFunction(float offset)
{
  if(offset < 0.5)
    return pow(0.5*(2*offset)*(2*offset),1.0/1.8);
  else
    return pow(1.0-0.5*(2*(1-offset))*(2*(1-offset)),1.0/1.8);
}

void drawGradiant(unsigned char *pixels,unsigned int width,unsigned int height,float top,float left, float bottom, float right)
{
  memset(pixels,0,width*height*4);

  unsigned int leftLimit = left*width;
  unsigned int rightBegin = (1.0-right)*width;
  unsigned int topLimit = top*height;
  unsigned int bottomBegin = (1.0-bottom)*height;

  unsigned char *alpha;
  float offset;

  int i,j;

  ERR("Xaxis: Blending from %d to %d, and from %d to %d",0,leftLimit,rightBegin,width);
  ERR("Yaxis: Blending from %d to %d, and from %d to %d",0,topLimit,bottomBegin,height);

  for(i=0;i<height;i++)
  {
    for(j=0;j<leftLimit;j++)
    {
      offset = j/(float)leftLimit;

      alpha = pixels+(i*width+j)*4+3;
      
      *alpha = (unsigned char)(255.0-((255.0-*alpha)*blendFunction(offset)));

    }
    for(j=rightBegin;j<width;j++)
    {
      offset = 1.0-((j-rightBegin)/(float)(width-rightBegin));

      alpha = pixels+(i*width+j)*4+3;

      *alpha = (unsigned char)(255.0-((255.0-*alpha)*blendFunction(offset)));
    }
    if(i < topLimit)
    {
      offset = i/(float)topLimit;

      for(j=0;j<width;j++)
      {
        alpha = pixels+(i*width+j)*4+3;
        
        *alpha = (unsigned char)(255.0-((255.0-*alpha)*blendFunction(offset)));
      }
    }
    if(i > bottomBegin)   
    {
      offset = 1.0-((i-bottomBegin)/(float)(height-bottomBegin));

      for(j=0;j<width;j++)
      {
        alpha = pixels+(i*width+j)*4+3;
        
        *alpha = (unsigned char)(255.0-((255.0-*alpha)*blendFunction(offset)));
      }
    }
  }
}

static void init_textures(QUAD_STATE_T *state,unsigned int textureWidth,unsigned int textureHeight)
{
  int i;

  // glGenTextures(1, &state->tex);
  if(state->alreadyInit)
  {
    eglDestroyImageKHR(state->display,eglImage[0]);  
    eglDestroyImageKHR(state->display,eglImage[1]);  
    eglDestroyImageKHR(state->display,eglImage[2]);  

    glDeleteTextures(1,state->tex[0]); // triple buffering
    glDeleteTextures(1,state->tex[1]);
    glDeleteTextures(1,state->tex[2]);    
    glDeleteTextures(1,state->texOverlay);
    glDeleteTextures(1,state->texMire);    
  }

  glGenTextures(1, &state->tex[0]);
  glGenTextures(1, &state->tex[1]);
  glGenTextures(1, &state->tex[2]);

  glGenTextures(1, &state->texOverlay);
  glGenTextures(1, &state->texMire);

  glBindTexture(GL_TEXTURE_2D, state->texMire);
 
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1024, 768, 0, GL_RGBA, GL_UNSIGNED_BYTE, gimp_image.pixel_data);

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

  /*********************/  


  glBindTexture(GL_TEXTURE_2D, state->texOverlay);

  drawGradiant(blendPixels,1024,768,b[0],b[1],b[2],b[3]);
 
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1024, 768, 0, GL_RGBA, GL_UNSIGNED_BYTE, blendPixels);

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

   /***************************************************/

  for(i=0;i<3;i++)
  {
   glBindTexture(GL_TEXTURE_2D, state->tex[i]);   
   glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, textureWidth, textureHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

   // /* Create EGL Image */
   eglImage[i] = eglCreateImageKHR(state->display,state->context,EGL_GL_TEXTURE_2D_KHR,(EGLClientBuffer)state->tex[i],NULL);
    
   if (eglImage[i] == EGL_NO_IMAGE_KHR)
   {
      ERR("[init_textures][ERROR] eglCreateImageKHR failed.");
      exit(1);
   }

   // setup overall texture environment
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  }
   // glTexCoordPointer(2, GL_FLOAT, 0, texCoords);
   // glEnableClientState(GL_TEXTURE_COORD_ARRAY);
   // 


   glBindTexture(GL_TEXTURE_2D, state->texOverlay);


   glEnable(GL_TEXTURE_2D);

   // Bind texture surface to current vertices
   // glBindTexture(GL_TEXTURE_2D, state->tex);   
}

// Function to be passed to atexit().

void termHandler(int signo)
{
  LOG("Received TERM signal");
  pthread_mutex_lock(&newDataAvailableMutex);

  flagQuit = 1;

  pthread_cond_broadcast(&newDataAvailableCond);
  pthread_mutex_unlock(&newDataAvailableMutex);  
}

void updateBlending(QUAD_STATE_T *state)
{
  glBindTexture(GL_TEXTURE_2D, state->texOverlay);

  drawGradiant(blendPixels,1024,768,b[0],b[1],b[2],b[3]);

  //  {
  //     128,0,0,0,   0,0,182,255,
  //     0,128,182,255,   0,128,0,255,
  //     128,0,0,255,   0,0,182,0,
  //     0,128,182,0,   0,128,0,255,
  //     128,0,0,255,   0,0,182,0,
  //     0,128,182,0,   0,128,0,255
  // };
  
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 1024, 768, GL_RGBA, GL_UNSIGNED_BYTE, blendPixels); 
}

void updateMatrix(QUAD_STATE_T *state)
{
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


  // orthoVtx[12] = 
  // {
  //   (state->screen_width/2.0f)*p[0][0], (state->screen_height/2.0f)*p[0][1], 0.0f,
  //   (state->screen_width/2.0f)*p[1][0], (state->screen_height/2.0f)*p[1][1], 0.0f,
  //   (state->screen_width/2.0f)*p[2][0], (state->screen_height/2.0f)*p[2][1], 0.0f,
  //   (state->screen_width/2.0f)*p[3][0], (state->screen_height/2.0f)*p[3][1], 0.0f
  // };
  //
  //  
  orthoVtx[0] = (state->screen_width/2.0f)*p[0][0];
  orthoVtx[1] = (state->screen_height/2.0f)*p[0][1]; 
  orthoVtx[2] = 0.0f;
  orthoVtx[3] = (state->screen_width/2.0f)*p[1][0]; 
  orthoVtx[4] = (state->screen_height/2.0f)*p[1][1]; 
  orthoVtx[5] = 0.0f; 
  orthoVtx[6] = (state->screen_width/2.0f)*p[2][0]; 
  orthoVtx[7] = (state->screen_height/2.0f)*p[2][1]; 
  orthoVtx[8] = 0.0f; 
  orthoVtx[9] = (state->screen_width/2.0f)*p[3][0]; 
  orthoVtx[10] = (state->screen_height/2.0f)*p[3][1]; 
  orthoVtx[11] = 0.0f; 


  
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
  
  // orthoTex[16] =
  // {
  //   o[0][0]*q0, o[0][1]*q0, 0.0f*q0,1.0f*q0,
  //   o[1][0]*q1, o[1][1]*q1, 0.0f*q1,1.0f*q1,
  //   o[2][0]*q2, o[2][1]*q2, 0.0f*q2,1.0f*q2,
  //   o[3][0]*q3, o[3][1]*q3, 0.0f*q3,1.0f*q3
  // }; 
  // 
  // 
  // 


  orthoTexOverlay[0] = 0;
  orthoTexOverlay[1] = q0;
  orthoTexOverlay[2] = 0;    
  orthoTexOverlay[3] = q0;

  orthoTexOverlay[4] = q1;
  orthoTexOverlay[5] = q1;
  orthoTexOverlay[6] = 0;    
  orthoTexOverlay[7] = q1;

  orthoTexOverlay[8] = q2;
  orthoTexOverlay[9] = 0;
  orthoTexOverlay[10] = 0;  
  orthoTexOverlay[11] = q2;
  
  orthoTexOverlay[12] = 0;
  orthoTexOverlay[13] = 0;
  orthoTexOverlay[14] = 0;  
  orthoTexOverlay[15] = q3;  

  //   // O: 0.00,1.00,1.00,1.00,1.00,0.00,0.00,0.00

  // GLfloat orthoTex[] =
  //  +  {
  //  +    0.0f*q0, 1.0f*q0, 0.0f*q0,1.0f*q0,
  //  +    1.0f*q1, 1.0f*q1, 0.0f*q1,1.0f*q1,    
  //  +    1.0f*q2, 0.0f*q2, 0.0f*q2,1.0f*q2,
  //  +    0.0f*q3, 0.0f*q3, 0.0f*q3,1.0f*q3
  //  +  }; 

  orthoTex[0] = o[0][0]*q0;
  orthoTex[1] = o[0][1]*q0;
  orthoTex[2] = 0.0f*q0;    
  orthoTex[3] = 1.0f*q0;

  orthoTex[4] = o[1][0]*q1;
  orthoTex[5] = o[1][1]*q1;
  orthoTex[6] = 0.0f*q1;    
  orthoTex[7] = 1.0f*q1;

  orthoTex[8] = o[2][0]*q2;
  orthoTex[9] = o[2][1]*q2;
  orthoTex[10] = 0.0f*q2;  
  orthoTex[11] = 1.0f*q2;

  orthoTex[12] = o[3][0]*q3;
  orthoTex[13] = o[3][1]*q3;
  orthoTex[14] = 0.0f*q3;  
  orthoTex[15] = 1.0f*q3;

  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();

  glLoadMatrixf(orthoMat);

  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();  
}

int main(int argc, char** argv) 
{
  struct timeval last,now;
  int i;

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




  /*
  * Getting args
  */
  parseConfFile(&frame_height,&frame_width,&frameBufferCount,&udpWait,&sourceUDPport);
  
  bufferArray = circularBufferCollectionInit(FRAME_BUFFER_LENGTH,frameBufferCount);    

  LOG("Height/with/udpWait/buffersCount/port: %d,%d,%d,%d,%d ",frame_height,frame_width,udpWait,frameBufferCount,sourceUDPport);  

  flagSourceChanged = 1;

  sourceType = SOURCE_UDP;

  DBG("[%s][%s] Setting up demuxer with file %s...",__FILE__,__FUNCTION__,sourceFILEname);       

  DBG("[%s][%s] Init host...",__FILE__,__FUNCTION__);
  bcm_host_init();

  DBG("[%s][%s] Init ilclient...",__FILE__,__FUNCTION__);    
  handle = ilclient_init();

  vcos_log_set_level(VCOS_LOG_CATEGORY, VCOS_LOG_TRACE);

  if (handle == NULL) 
  {
    ERR( "[%s][%s][ERROR] IL client init failed",__FILE__,__FUNCTION__);
    exit(-1);
  }

  DBG("[%s][%s] Init host...",__FILE__,__FUNCTION__);    

  if (OMX_Init() != OMX_ErrorNone) 
  {
    ilclient_destroy(handle);
    ERR( "[%s][%s][ERROR] OMX init failed",__FILE__,__FUNCTION__);
    exit(-2);
  }

  DBG("[%s][%s] Setting callbacks...",__FILE__,__FUNCTION__); 

  ilclient_set_error_callback(handle,error_callback,NULL);
  ilclient_set_eos_callback(handle,eos_callback,NULL);
  ilclient_set_port_settings_callback(handle,port_settings_callback,NULL);
  ilclient_set_empty_buffer_done_callback(handle,empty_buffer_done_callback,NULL);
  ilclient_set_fill_buffer_done_callback(handle, my_fill_buffer_done, NULL);

  /* Now we have the video size, init opengl */

  // Clear application state
  memset( state, 0, sizeof( *state ) );

  DBG("[%s][%s] Init OpenGL...",__FILE__,__FUNCTION__);       

  // Start OGLES
  init_ogl(state);

  DBG("[%s][%s] Init textures...",__FILE__,__FUNCTION__);    

  // initialise the OGLES texture(s)
  init_textures(state,frame_width,frame_height);

  state->alreadyInit = 1;       

  DBG("[%s][%s] Create components...",__FILE__,__FUNCTION__);    

/* Create all OMX components*/
  OMX_createComponent(handle, "video_decode", &decodeComponent,ILCLIENT_DISABLE_ALL_PORTS | ILCLIENT_ENABLE_INPUT_BUFFERS);
  OMX_createComponent(handle, "egl_render", &renderComponent,ILCLIENT_DISABLE_ALL_PORTS|ILCLIENT_ENABLE_OUTPUT_BUFFERS); 
  // OMX_createComponent(handle, "clock", &clockComponent,ILCLIENT_DISABLE_ALL_PORTS);
  // OMX_createComponent(handle, "video_scheduler", &schedulerComponent, ILCLIENT_DISABLE_ALL_PORTS | ILCLIENT_ENABLE_INPUT_BUFFERS);
    
  // DBG("[%s][%s] Init clock...",__FILE__,__FUNCTION__);    
  // OMX_initClock(clockComponent);
  // 
  
  LOG("[%s][%s] Just after creation ",__FILE__,__FUNCTION__);
  LOG("[%s][%s] Decode status: %s",__FILE__,__FUNCTION__,OMX_getStateString(ilclient_get_handle(decodeComponent)));
  // DBG("[%s][%s] Clock status: %s",__FILE__,__FUNCTION__,OMX_getStateString(ilclient_get_handle(clockComponent)));
  // DBG("[%s][%s] Scheduler status: %s",__FILE__,__FUNCTION__,OMX_getStateString(ilclient_get_handle(schedulerComponent)));
  LOG("[%s][%s] Render status: %s",__FILE__,__FUNCTION__,OMX_getStateString(ilclient_get_handle(renderComponent)));

  DBG("[%s][%s] Create tunnel...",__FILE__,__FUNCTION__);      
  // set_tunnel(&decodeTunnel, decodeComponent, 131, schedulerComponent, 10);
  // set_tunnel(&schedulerTunnel, schedulerComponent, 11, renderComponent, 220);
  // 
  set_tunnel(&decodeTunnel, decodeComponent, 131, renderComponent, 220);

  // set_tunnel(&clockTunnel, clockComponent, 80, schedulerComponent, 12);

  /* Start the clock first */    
  // if ((err = ilclient_setup_tunnel(&clockTunnel, 0, 0)) < 0) 
  // {
  //   ERR( "[%s][%s][ERROR] setting up clock tunnel %X",__FILE__,__FUNCTION__, err);
  //   exit(1);
  // } 
  // else 
  // {
  //   DBG("[%s][%s] Clock tunnel set up ok",__FILE__,__FUNCTION__);
    
  // }

  // DBG("[%s][%s] OMX_printClockState before: ",__FILE__,__FUNCTION__);
  

  // OMX_printClockState(clockComponent);

  // OMX_changeStateToExecuting(clockComponent);

  // DBG("[%s][%s] OMX_printClockState after: ",__FILE__,__FUNCTION__);
  

  // OMX_printClockState(clockComponent);   

  // ilclient_change_component_state(decodeComponent, OMX_StateIdle);
   
  // No need to configure the framerate, as in this mode we don't use b frames

  OMX_setVideoDecoderInputFormat(decodeComponent,0,0,frame_width,frame_height);

  // OMX_setVideoDecoderInputFormat(decodeComponent,0,0,0,0);

  LOG("DecodeComponent to executing state...");          

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
//   //     DBG( "[%s][ERROR] Couldn't change state to Executing",__FUNCTION__);
//   //     exit(1);
//   // }

  if ((err = ilclient_setup_tunnel(&decodeTunnel, 0, 0)) < 0)
  {
    ERR( "[ERROR] Error setting up decode tunnel %X",err);
    exit(1);
  }
  else
  {
    LOG("Decode tunnel set up ok");
  }

  // DBG("[%s][%s] schedulerComponent to executing state...",__FILE__,__FUNCTION__);          

  // if ((err = ilclient_setup_tunnel(&schedulerTunnel, 0, 1000)) < 0) {
  //     ERR( "[%s][%s][ERROR] Error setting up scheduler tunnel %X",__FILE__,__FUNCTION__,err);
  //     exit(1);
  // } else {
  //     DBG("[%s][%s] Scheduler tunnel set up ok",__FILE__,__FUNCTION__);
  // }

  // OMX_changeStateToExecuting(schedulerComponent);

  LOG("Before Egl config ",__FILE__,__FUNCTION__);
  LOG("Decode status: %s",OMX_getStateString(ilclient_get_handle(decodeComponent)));
  // DBG("[%s][%s] Clock status: %s",__FILE__,__FUNCTION__,OMX_getStateString(ilclient_get_handle(clockComponent)));
  // DBG("[%s][%s] Scheduler status: %s",__FILE__,__FUNCTION__,OMX_getStateString(ilclient_get_handle(schedulerComponent)));
  LOG("Render status: %s",OMX_getStateString(ilclient_get_handle(renderComponent)));


  // Enable the output port and tell egl_render to use the texture as a buffer
  //ilclient_enable_port(egl_render, 221); THIS BLOCKS SO CANT BE USED
  if (OMX_SendCommand(ILC_GET_HANDLE(renderComponent), OMX_CommandPortEnable, 221, NULL) != OMX_ErrorNone)
  {
     ERR("OMX_CommandPortEnable failed.");
     exit(1);
  }
  LOG("OMX_UseEGLImage...");    



  OMX_PARAM_PORTDEFINITIONTYPE portParam;
  memset(&portParam, 0, sizeof(portParam));
 
  portParam.nSize = sizeof( OMX_PARAM_PORTDEFINITIONTYPE);
  portParam.nVersion.nVersion = OMX_VERSION;

  portParam.nPortIndex = 221; 
  OMX_ERRORTYPE r;

  r = OMX_GetParameter(ILC_GET_HANDLE(renderComponent),OMX_IndexParamPortDefinition, &portParam);

  if(r != OMX_ErrorNone)
  {
    ERR("Cannot get params");
  }
  else
  {
    LOG("nBufferAlignment:%d",portParam.nBufferAlignment);
    LOG("nBufferCountActual:%d",portParam.nBufferCountActual);
    LOG("nBufferSize:%d",portParam.nBufferSize);
  }



  // for(i=0;i<3;i++)
  // {
    if (OMX_UseEGLImage(ILC_GET_HANDLE(renderComponent),&(eglBuffer[eglWriteIndex]),221,NULL,eglImage[eglWriteIndex]) != OMX_ErrorNone)
    {
       ERR("OMX_UseEGLImage %d failed.");
       exit(1);
    }
  // }

  LOG("renderComponent to executing state...");          

  OMX_changeStateToExecuting(renderComponent);

  LOG("OMX_FillThisBuffer egl");          

  // Request egl_render to write data to the texture buffer


  if(OMX_FillThisBuffer(ilclient_get_handle(renderComponent), eglBuffer[eglWriteIndex]) != OMX_ErrorNone)
  {
     ERR("OMX_FillThisBuffer failed.");
     exit(1);
  }

  LOG("After Egl config ");
  LOG("Decode status: %s",OMX_getStateString(ilclient_get_handle(decodeComponent)));
  // DBG("[%s][%s] Clock status: %s",__FILE__,__FUNCTION__,OMX_getStateString(ilclient_get_handle(clockComponent)));
  // DBG("[%s][%s] Scheduler status: %s",__FILE__,__FUNCTION__,OMX_getStateString(ilclient_get_handle(schedulerComponent)));
  LOG("Render status: %s",OMX_getStateString(ilclient_get_handle(renderComponent)));

  // pthread_create(&threadMonitorFile,NULL,monitorFile,(void *)MAPDIR);

  // pthread_create(&threadCommandSocket,NULL,commandThread,NULL);
  pthread_create(&threadFrameInput,NULL,inputThread,NULL);
  pthread_create(&threadMonitorFile,NULL,monitorThread,NULL);

  cpu_set_t affinity;

  CPU_ZERO(&affinity);
  CPU_SET(1,&affinity);

  if(pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &affinity) != 0)
  {
    ERR("Cannot set affinity. errno:%d",errno);
  }

 // Startclock is not usefull when we dont need the services of video_scheduler 
  // DBG("[%s] Start clock...",__FUNCTION__);            

  // OMX_startClock(clockComponent);
  // 
  // 

  glViewport(0, 0, state->screen_width, state->screen_height);

  // glPushMatrix();

  updateMatrix(state);

  glEnableClientState(GL_VERTEX_ARRAY);
  glEnableClientState(GL_TEXTURE_COORD_ARRAY);

  struct sigaction sigIntHandler;

  sigIntHandler.sa_handler = termHandler;
  sigemptyset(&sigIntHandler.sa_mask);
  sigIntHandler.sa_flags = 0;

  sigaction(SIGINT, &sigIntHandler, NULL);  
  sigaction(SIGTERM, &sigIntHandler, NULL);  

  parseMapFile();

  LOG("Entering loop with flagquit %d",flagQuit);

  while(!flagQuit)
  {
    /*
     * Wait for the input thread
     */
    pthread_mutex_lock(&newDataAvailableMutex);

    // LOG("[%s][%s] Wait ",__FILE__,__FUNCTION__);

    while(!flagNewDataAvailable && !flagQuit && !flagConfChanged)
    {
      DBG("[%s][%s] Here am I, still waiting...",__FILE__,__FUNCTION__);      
      pthread_cond_wait(&newDataAvailableCond,&newDataAvailableMutex);
      DBG("[%s][%s] Wait loop ",__FILE__,__FUNCTION__);      
    }

    // LOG("[%s][%s] Wait finished! ",__FILE__,__FUNCTION__);

    unsigned int currentBufferIndex = bufferArray->readIndex;
    unsigned int *currentBufferCurrentSize = &(bufferArray->buffers[currentBufferIndex]->currentSize); 

    pthread_mutex_unlock(&newDataAvailableMutex);

    if(flagConfChanged || flagQuit)
    {
      flagQuit = 1;
      LOG("Exiting aftar critical section")
      break;
    }


    DBG("[%s][%s] Getting buffer, size %d ",__FILE__,__FUNCTION__,*currentBufferCurrentSize);    

    gettimeofday(&now,NULL);
    DBG("Time delta pre get buffer: %d",(now.tv_sec-last.tv_sec)*1000000+(now.tv_usec-last.tv_usec));

    // do we have a decode input buffer we can fill and empty?
    
    int waited = 0;

    while(!flagFail && ((buff_header = ilclient_get_input_buffer(decodeComponent,130,0 /* 0 = no-block */))==NULL))
    {
      ERR("!");      

      waited++;
      
      if(waited == FAIL_DELAY)
      {
        ERR("Cannot get input buffer");
        flagQuit = flagFail = 1;
        break;
      }

      usleep(1000);
    }

    gettimeofday(&now,NULL);
    DBG("Time delta pre copy: %d",(now.tv_sec-last.tv_sec)*1000000+(now.tv_usec-last.tv_usec));

    DBG("Copying %d bytes into buffer ",*currentBufferCurrentSize);        

    if(buff_header == NULL || copy_into_buffer_and_empty(
      bufferArray->buffers[currentBufferIndex]->data,
      *currentBufferCurrentSize,
      flagIsFirst,
      decodeComponent,buff_header) < 0)
    {
      ERR("No input buffer.");            
      flagQuit = flagFail = 1;
      break;
    }

    DBG("Copying done "); 

    /*
     * Now that the copy is done, release input thread
     */
    pthread_mutex_lock(&newDataAvailableMutex);

    flagNewDataAvailable = 0;

    *currentBufferCurrentSize  = 0;

    bufferArray->readIndex = (bufferArray->readIndex + 1) % bufferArray->count;
    bufferArray->currentCount--;

    pthread_cond_broadcast(&newDataAvailableCond);

    pthread_mutex_unlock(&newDataAvailableMutex);

    gettimeofday(&now,NULL);

    DBG("Time delta post copy: %d",(now.tv_sec-last.tv_sec)*1000000+(now.tv_usec-last.tv_usec));

    last.tv_sec = now.tv_sec;
    last.tv_usec = now.tv_usec;

    // memcpy(&last,&now,sizeof(struct timeval));
    // 
    
    // if(!flagFail && (err = ilclient_wait_for_event(decodeComponent,OMX_EventPortSettingsChanged, 131, 0, 0, 1, ILCLIENT_EVENT_ERROR | ILCLIENT_PARAMETER_CHANGED, 0)) == 0) // We got an event
    // {
    //   ERR("[%s][ERROR] %d Another port settings change",__FUNCTION__,err);
    // }
    

    /*
     * Notify input thread the data is consummed
     */
    redraw_scene(state);

    if(flagMappingChanged)
    {
      ERR("[%s] Update mapping from config file",__FUNCTION__);      
      updateMatrix(state);

      flagMappingChanged = 0;
    }

    if(flagBlendingChanged)
    {
      ERR("[%s] Update blending from config file",__FUNCTION__);      

      updateBlending(state);
      flagBlendingChanged = 0;
    }
  
    // pthread_mutex_lock(&playbackMutex);

    // /* Playback paused by command thread? */
    // while(flagPaused
    // {
    //   glClear( GL_COLOR_BUFFER_BIT );
    //   eglSwapBuffers(state->display, state->surface);              

    //   DBG("PAUSE");

    //   pthread_cond_wait(&condPaused,&playbackMutex);
    // }

    // /* File changed by command thread? */
    // if(flagSourceChanged)
    // {
    //   DBG("SOURCE CHANGED, loading new file");

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
      
    // 
    // pthread_mutex_unlock(&playbackMutex);  
    // 
    // if(flagQuit)
    //   break;
    //             
  }
  LOG("Exiting. quit:%d, fail:%d...",flagQuit,flagFail);

  LOG("Cleaning screen...");
  glClear( GL_COLOR_BUFFER_BIT );
  eglSwapBuffers(state->display, state->surface);

  LOG("Joining threads...");
  // pthread_join(&threadCommandSocket);
  
  pthread_cond_broadcast(&newDataAvailableCond); 
  pthread_join(threadFrameInput,NULL);
  LOG("Input thread finished");

  pthread_join(threadMonitorFile,NULL);
  LOG("Monitor thread finished");  

  if(!flagFail && !flagIsFirst)
  {
    LOG("Sending EOS...");

    flagQuit = 0;
    OMX_send_EOS_to_decoder(decodeComponent);

    unsigned int slept = 0;

    while(sleep < 1000 && flagQuit == 0)
    {
      usleep(5000);
      slept += 5;
    }

    // ilclient_wait_for_event(renderComponent, OMX_EventBufferFlag, 220, 0, OMX_BUFFERFLAG_EOS, 0,ILCLIENT_BUFFER_FLAG_EOS, -1);
  }


        // DBG("Timestamp offset was %lld",(long long)timestampOffset);

        /* Open next file for reading */
//         pthread_mutex_lock(&playbackMutex);

//         /* While file is paused, stay here but allow file change  */
//         while(flagPaused)
//         {

//           if(strcmp(currentFile,requestedFile) != 0)
//           {          
//             strcpy(currentFile,requestedFile);

//             DBG("Next is %s",currentFile);          

//             /* If file is empty */
//             if(strcmp(currentFile,"") != 0)
//             {
//               DBG("Next is %s",currentFile);          

//               timestampOffset = 0;      

//               gettimeofday(&timestampBegin,NULL);
//             }
//           }
//         }
//         /* Rewind this file */
//         else
//         {
//           // DBG("Loop");


//           inputRewind();
// // 
//           // timestampOffset += lastPts*time_base_num*1000/time_base_den;

//           // if(avformat_seek_file(&pFormatCtx,video_stream_idx,0,0,999999,AVSEEK_FLAG_BACKWARD | AVSEEK_FLAG_ANY)<0)
//           // {
//           //   DBG("Cannot seek");            
//             avformat_close_input(&pFormatCtx);
//             inputSetupSource(currentFile,&frame_width,&frame_height);
//           // }
//         }

//         pthread_mutex_unlock(&playbackMutex);

          // avformat_close_input(&pFormatCtx);
      
          // inputSetupSource(currentFile,&frame_width,&frame_height);

    
        // DBG("Timestamp offset is now %lld (+= %lld)",(long long)timestampOffset,(long long)lastPts);     

    //     OMX_SendCommand(ilclient_get_handle(decodeComponent),OMX_CommandFlush,130,NULL);
    //     OMX_SendCommand(ilclient_get_handle(decodeComponent),OMX_CommandFlush,131,NULL);

    //     DBG(".");

    //     ilclient_wait_for_event(decodeComponent, OMX_EventCmdComplete, OMX_CommandFlush, 0, 130, 0, ILCLIENT_PORT_FLUSH, -1);
    //     ilclient_wait_for_event(decodeComponent, OMX_EventCmdComplete, OMX_CommandFlush, 0, 131, 0, ILCLIENT_PORT_FLUSH, -1);

    //     DBG(".");

    //     OMX_send_EOS_to_decoder(decodeComponent);        

    //     OMX_stopClock(clockComponent);

    //     OMX_initClock(clockComponent);        

    //     DBG(".");

    // /* The following doesn't seem to work if a eos callback is already attached */
    //   ilclient_wait_for_event(renderComponent, OMX_EventBufferFlag, 220, 0, OMX_BUFFERFLAG_EOS, 0,ILCLIENT_BUFFER_FLAG_EOS, 10000);

    //     DBG(".");

// }


// ilclient_wait_for_event(renderComponent, OMX_EventBufferFlag, 220, 0, OMX_BUFFERFLAG_EOS, 0,ILCLIENT_BUFFER_FLAG_EOS, 100);
  
  // usleep(1000000);

  
  // need to flush the renderer to allow video_decode to disable its input port

  TUNNEL_T tunnels[2];

  memset(tunnels, 0, sizeof(tunnels));

  tunnels[0]=decodeTunnel;
  // tunnels[1]=schedulerTunnel;
  // // tunnels[2]=clockTunnel;
  
  if(!flagFail)
  {
    LOG("Flush the render pipe....");
    ilclient_flush_tunnels(tunnels, 0);
  
      // ilclient_flush_tunnels(tunnels, 0);
  // LOG("[%s][%s] Before OMX_changeStateToIdle ",__FILE__,__FUNCTION__);

  // LOG("[%s][%s] Decode status: %s",__FILE__,__FUNCTION__,OMX_getStateString(ilclient_get_handle(decodeComponent)));
  // LOG("[%s][%s] Render status: %s",__FILE__,__FUNCTION__,OMX_getStateString(ilclient_get_handle(renderComponent)));

  // OMX_changeStateToIdle(decodeComponent); 
  // OMX_changeStateToIdle(renderComponent); 

  // LOG("[%s][%s] After OMX_changeStateToIdle ",__FILE__,__FUNCTION__);

  // LOG("[%s][%s] Decode status: %s",__FILE__,__FUNCTION__,OMX_getStateString(ilclient_get_handle(decodeComponent)));
  // LOG("[%s][%s] Render status: %s",__FILE__,__FUNCTION__,OMX_getStateString(ilclient_get_handle(renderComponent)));

  //   // ERR("[%s][%s] Flushed.",__FILE__,__FUNCTION__);
  // OMX_changeStateToIdle(decodeComponent); 
  // OMX_changeStateToIdle(renderComponent); 
    // LOG("Disabling port buffer");

    //     // OMX_changeStateToIdle(decodeComponent); 

    // OMX_SendCommand(ilclient_get_handle(decodeComponent),  OMX_CommandPortDisable, 130, NULL);        
    // OMX_SendCommand(ilclient_get_handle(decodeComponent),  OMX_CommandPortDisable, 131, NULL);

    // LOG("...");


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
    // DBG("EOS on render");        



    LOG("Disabling tunnels");
    ilclient_disable_tunnel(&decodeTunnel);
    // ilclient_disable_tunnel(&schedulerTunnel);

    LOG("Disabling port buffer");

    // OMX_SendCommand(ilclient_get_handle(decodeComponent),  OMX_CommandPortDisable, 130, NULL);        
    // OMX_SendCommand(ilclient_get_handle(decodeComponent),  OMX_CommandPortDisable, 131, NULL);
    // LOG("...");
    // ilclient_wait_for_event(decodeComponent, OMX_EventCmdComplete,  OMX_CommandPortDisable, 0, 130, 0, ILCLIENT_PORT_FLUSH, -1);
    // ilclient_wait_for_event(decodeComponent, OMX_EventCmdComplete,  OMX_CommandPortDisable, 0, 131, 0, ILCLIENT_PORT_FLUSH, -1);

    ilclient_disable_port_buffers(decodeComponent, 130, NULL, NULL, NULL);    

    LOG("Destroying tunnels");
    ilclient_teardown_tunnels(tunnels);
  }

  LOG("Idling components");

  COMPONENT_T *list[4];   
  memset(list, 0, sizeof(list));

  // list[0]=schedulerComponent;
  list[0]=decodeComponent;
  list[1]=renderComponent;
  // list[3]=clockComponent;   

  ilclient_state_transition(list, OMX_StateIdle);

  // ERR("[%s][%s] Unloading components",__FILE__,__FUNCTION__);

  // ilclient_state_transition(list, OMX_StateLoaded);
  // 
  usleep(1000000);

  LOG("Cleaning components");
  ilclient_cleanup_components(list);

  LOG("OMX deinit");
  OMX_Deinit();

  LOG("Release OpenGL resources");
  eglMakeCurrent(state->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
  eglDestroySurface(state->display, state->surface);
  eglDestroyContext(state->display, state->context);

  LOG("EGL deinit");
  eglTerminate( state->display );

  LOG("Il deinit");
  ilclient_destroy(handle);

  LOG("Host deinit");
  bcm_host_deinit();      

   // DBG("+");   


//         OMX_send_EOS_to_decoder(decodeComponent);        
        
//         pFormatCtx = NULL;

//         DBG("1");

//         inputSetupSource(argv[fileIndex++],&frame_width,&frame_height);

//         DBG("2");

//         OMX_changeStateToIdle(decodeComponent); 


//   ilclient_disable_port_buffers(decodeComponent, 130, NULL, NULL, NULL);
//   ilclient_disable_port(decodeComponent, 130);

//   ilclient_disable_port_buffers(decodeComponent, 131, NULL, NULL, NULL);
//   ilclient_disable_port(decodeComponent, 131);  

// // enable the decode output ports
//   OMX_SendCommand(ilclient_get_handle(decodeComponent), OMX_CommandPortDisable, 130, NULL);
//   OMX_SendCommand(ilclient_get_handle(decodeComponent), OMX_CommandPortDisable, 131, NULL);



        

//   DBG("[%s] Decode status: %s",__FUNCTION__,OMX_getStateString(ilclient_get_handle(decodeComponent)));
//   DBG("[%s] Clock status: %s",__FUNCTION__,OMX_getStateString(ilclient_get_handle(clockComponent)));
//   DBG("[%s] Scheduler status: %s",__FUNCTION__,OMX_getStateString(ilclient_get_handle(schedulerComponent)));
//   DBG("[%s] Render status: %s",__FUNCTION__,OMX_getStateString(ilclient_get_handle(renderComponent)));

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
//      DBG("[%s] OMX_UseEGLImage failed.",__FUNCTION__);
//      exit(1);
//   }


//         OMX_changeStateToExecuting(decodeComponent);

//   DBG("[%s] Decode status: %s",__FUNCTION__,OMX_getStateString(ilclient_get_handle(decodeComponent)));
//   DBG("[%s] Clock status: %s",__FUNCTION__,OMX_getStateString(ilclient_get_handle(clockComponent)));
//   DBG("[%s] Scheduler status: %s",__FUNCTION__,OMX_getStateString(ilclient_get_handle(schedulerComponent)));
//   DBG("[%s] Render status: %s",__FUNCTION__,OMX_getStateString(ilclient_get_handle(renderComponent)));


//         DBG("3");


        // avformat_seek_file(pFormatCtx,0,0,0,10,AVSEEK_FLAG_BACKWARD);

    
  if(flagFail)
    exit(-1);
  else
    exit(0);
}
