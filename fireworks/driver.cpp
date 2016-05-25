/*
 * Copyright (C) 2002 Tugrul Galatali <tugrul@galatali.com>
 *
 * driver.c is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * driver.c is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/param.h>
#include <assert.h>

#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

#include "cJSON.h"

#include "skyrocket_particle.h"

#include <termios.h>            //termios, TCSANOW, ECHO, ICANON
#include <unistd.h>     //STDIN_FILENO



#include "bcm_host.h"

#include "GLES/gl.h"
#include "EGL/egl.h"
#include "EGL/eglext.h"

#define LISTEN_PORT 1338

#define SOUND_CLIENT_PORT 7862
#define SOUND_CLIENT_IP "192.168.1.254"

#define CONF_FILE "/etc/pmw.d/mapping.conf"

uint32_t screen_width;
uint32_t screen_height;
   
EGLDisplay display;
EGLSurface surface;
EGLContext context;

char buffer[1024];

extern float p[4][2];

unsigned int s[2] = {1,2};
unsigned int o[2] = {0,0};

int soundSocket;
struct sockaddr_in soundClientAddr;

extern  int mire;

extern particle *addParticle (unsigned long seed);

pthread_t               threadKeyboardInput;
pthread_t               threadSocketInput;

pthread_mutex_t         listMutex;

int hack_draw (float, float);
void hack_init();

#define constapmc 16807    

int shouldQuit = 0;

float rsRandf2(float x,unsigned long seed31pmc,unsigned long *newSeed31pmc)
{                             
    long unsigned int hi, lo;
                                     
    lo = constapmc * (seed31pmc & 0xFFFF);                               
    hi = constapmc * (seed31pmc >> 16);
                                
    lo += (hi & 0x7FFF) << 16;                                
    lo += hi >> 15;                  
                             
    if (lo > 0x7FFFFFFF) lo -= 0x7FFFFFFF;
    
    *newSeed31pmc = (long)lo;

//     printf("%f\n",((long)lo )/ 2147483647.0);
    
    return x*((long)lo)/2147483647.0;

}

int rsRandi2(int x,unsigned long seed31pmc,unsigned long *newSeed31pmc)
{                             
    long unsigned int hi, lo;
                                     
    lo = constapmc * (seed31pmc & 0xFFFF);                               
    hi = constapmc * (seed31pmc >> 16);
                                
    lo += (hi & 0x7FFF) << 16;                                
    lo += hi >> 15;                  
                             
    if (lo > 0x7FFFFFFF) lo -= 0x7FFFFFFF;
    
    *newSeed31pmc = (long)lo;

//     printf("%f\n",((long)lo )/ 2147483647.0);
    
    return (long)lo%x;

}

int deltaus(const struct timeval now, const struct timeval then)
{
  return (now.tv_sec - then.tv_sec) * 1000000 + now.tv_usec - then.tv_usec;
}

static void init_ogl()
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
   display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
   assert(display!=EGL_NO_DISPLAY);

   // initialize the EGL display connection
   result = eglInitialize(display, NULL, NULL);
   assert(EGL_FALSE != result);

   // get an appropriate EGL frame buffer configuration
   // this uses a BRCM extension that gets the closest match, rather than standard which returns anything that matches
   result = eglSaneChooseConfigBRCM(display, attribute_list, &config, 1, &num_config);
   assert(EGL_FALSE != result);

   // create an EGL rendering context
   context = eglCreateContext(display, config, EGL_NO_CONTEXT, NULL);
   assert(context!=EGL_NO_CONTEXT);

   // create an EGL window surface
   success = graphics_get_display_size(0 /* LCD */, &screen_width, &screen_height);
   assert( success >= 0 );

   dst_rect.x = 0;
   dst_rect.y = 0;
   dst_rect.width = screen_width;
   dst_rect.height = screen_height;
      
   src_rect.x = 0;
   src_rect.y = 0;
   src_rect.width = screen_width << 16;
   src_rect.height = screen_height << 16;        

   dispman_display = vc_dispmanx_display_open( 0 /* LCD */);
   dispman_update = vc_dispmanx_update_start( 0 );
         
   dispman_element = vc_dispmanx_element_add(
     dispman_update,
     dispman_display,
     0/*layer*/, 
     &dst_rect,
     0/*src*/,
     &src_rect,
     DISPMANX_PROTECTION_NONE,
     0 /*alpha*/,
     0/*clamp*/,
     0/*transform*/);
      
   nativewindow.element         = dispman_element;
   nativewindow.width           = screen_width;
   nativewindow.height          = screen_height;
   
   printf("[init_ogl] Screens size is: %d x %d\n",screen_width,screen_height);
   
   vc_dispmanx_update_submit_sync( dispman_update );
      
   surface = eglCreateWindowSurface( display, config, &nativewindow, NULL );

   assert(surface != EGL_NO_SURFACE);

   // connect the context to the surface
   result = eglMakeCurrent(display, surface, surface, context);
   assert(EGL_FALSE != result);
}

void mainLoop (void)
{
  struct timeval cycleStart,now;
  int frameTimeSoFar = 0;

  glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT  | GL_STENCIL_BUFFER_BIT);
  eglSwapBuffers(display, surface);
	  
  gettimeofday (&cycleStart, NULL);

  int i;
  
  hack_init();
  
  while (1) 
  {
    if(hack_draw ((float)now.tv_sec + now.tv_usec / 1000000.0f,(float) frameTimeSoFar / 1000000.0f))
    {
         eglSwapBuffers(display, surface); 
    }

    gettimeofday (&now, NULL);
  }
}

void *keyboardInput(void *param)
{
  unsigned int i = 0;
  
    static struct termios oldt, newt;

    /*tcgetattr gets the parameters of the current terminal
    STDIN_FILENO will tell tcgetattr that it should write the settings
    of stdin to oldt*/
    tcgetattr( STDIN_FILENO, &oldt);
    /*now the settings will be copied*/
    newt = oldt;

    /*ICANON normally takes care that one line at a time will be processed
    that means it will return if it sees a "\n" or an EOF or an EOL*/
    newt.c_lflag &= ~(ICANON);          

    /*Those new settings will be set to STDIN
    TCSANOW tells tcsetattr to change attributes immediately. */
    tcsetattr( STDIN_FILENO, TCSANOW, &newt);  
  
            FILE *f;
    
  while(1)
  {
    char c = getchar();
    
    switch(c)
    {
      case ' ':
        i=(i+1)%4;
        printf("%d %f:%f\n",i,p[i][0],p[i][0]);
      break;
      case 'w':
        p[i][1] += 0.001;
      break;
      case 's':
        p[i][1] -= 0.001;        
      break;
      case 'd':
        p[i][0] += 0.001;        
      break;
      case 'a':
        p[i][0] -= 0.001;                
      break;
      case 'm':
        mire *=-1;                
      break;
      case 'i':
        p[0][1] *= -1;                
        p[1][1] *= -1;                
        p[2][1] *= -1;                
        p[3][1] *= -1;                        
      break;    
      case '0':
        o[1] = 0;
      break;
      case '1':
        o[1] = 1;
      break;
      case 'p':
        f = fopen(CONF_FILE, "w");
        sprintf(buffer,"%f,%f,%f,%f,%f,%f,%f,%f\n%d,%d\n%d,%d",
                p[0][0],
                p[0][1],
                p[1][0],
                p[1][1],
                p[2][0],
                p[2][1],
                p[3][0],
                p[3][1],
                s[0],
                s[1],
                o[0],
                o[1]
               );
        fwrite(buffer,sizeof(char),strlen(buffer),f);
        fclose(f);                     
      break;
      default:
      break;
    }
    
  }
    tcsetattr( STDIN_FILENO, TCSANOW, &oldt);
  return NULL;
}

void *socketInput(void *param)
{
    int listenfd = 0, connfd = 0;
    struct sockaddr_in serv_addr,cliaddr; 
    socklen_t len;
     
    listenfd = socket(AF_INET, SOCK_STREAM, 0);
    memset(&serv_addr, '0', sizeof(serv_addr));

    serv_addr.sin_family = AF_INET;
    
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    serv_addr.sin_port = htons(LISTEN_PORT); 

    bind(listenfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)); 

    listen(listenfd, 1); 
    
    printf("[fireworks] Waiting for connection\n");fflush(0);              
    
    connfd = accept(listenfd, NULL, NULL);
    
    printf("[fireworks] Connected\n");fflush(0);          

    while(!shouldQuit)
    {
      
      unsigned char buffer[1024];
      unsigned char *boundary;
      
      unsigned int totalcnt = 0;
      int socketRead;
      
      while(!shouldQuit)
      {
        while((boundary = strchr(buffer,'\n'))==NULL)
        {
          socketRead= read(connfd, &buffer[totalcnt], 1024-totalcnt);
          
          if(!socketRead)
            break;
          
          totalcnt += socketRead;
          
          printf("[soundplayer] buffer before:%s (count:%d)\n",buffer,totalcnt);fflush(0);        
          
          if(shouldQuit)
            break;      
        }
        if(shouldQuit)
          break;
        
        *boundary = '\0';
        
        cJSON * root = cJSON_Parse(buffer);
        
        totalcnt -= (boundary-buffer)+1;    
        
        memmove(buffer,boundary+1,totalcnt);
        
        printf("[soundplayer] buffer after memove:%s (count:%d)\n",buffer,totalcnt);fflush(0);

        printf("[soundplayer] Recv:%s\n",buffer);fflush(0);

        
        if(root)
        {
          printf("[soundplayer] Valid json\n");fflush(0);
          
          cJSON * priR = cJSON_GetObjectItem(root,"primaryR");
          cJSON * priG = cJSON_GetObjectItem(root,"primaryG");
          cJSON * priB = cJSON_GetObjectItem(root,"primaryB");
          
          cJSON * secR = cJSON_GetObjectItem(root,"secondaryR");
          cJSON * secG = cJSON_GetObjectItem(root,"secondaryG");
          cJSON * secB = cJSON_GetObjectItem(root,"secondaryB");
          
          cJSON * power = cJSON_GetObjectItem(root,"power");         
          cJSON * angle = cJSON_GetObjectItem(root,"angle");          
          cJSON * type  = cJSON_GetObjectItem(root,"type");          
          cJSON * seed  = cJSON_GetObjectItem(root,"seed");        
          
          if(priR && priG && priB && secR && secG && secB && power && angle && type && seed)
          {           
            
            printf("[Launching rocket] %d,%f,%f,%f,%f,%f,%f,%f,%f\n",seed->valueint,            (float)priR->valuedouble,
            (float)priG->valuedouble,
            (float)priB->valuedouble,
            (float)secR->valuedouble,
            (float)secG->valuedouble,
            (float)secB->valuedouble,
            ((float)atof(power->valuestring))/100.0f*7.0f,
            ((float)atof(angle->valuestring)));            
            
            pthread_mutex_lock(&listMutex);
            
            printf("cltogo\n",fflush(0));
            
//             981997,0.898039,0.156863,0.482353,0.988235,0.913725,0.309804,5.175040,13.974669
            
            particle *rock = addParticle(seed->valueint);
            
            rock->initRocket(
            (float)priR->valuedouble,
            (float)priG->valuedouble,
            (float)priB->valuedouble,
            (float)secR->valuedouble,
            (float)secG->valuedouble,
            (float)secB->valuedouble,
            ((float)atof(power->valuestring))/100.0f*7.0f,
            ((float)atof(angle->valuestring))
            );

            rock->explosiontype = type->valueint;
            
            pthread_mutex_unlock(&listMutex);
          }
        }
        cJSON_Delete(root);
      }
    }
}

void playSound(unsigned int soundId)
{
  char buf[512];
  
  printf("[playSound] %d\n",soundId);
    
  sprintf(buf,"{\"fireworkId\":0,\"soundId\":%d}\n",soundId);
  
  sendto(soundSocket, buf, strlen(buf), 0, (struct sockaddr *)&soundClientAddr, sizeof(soundClientAddr));
}

int main (int argc, char *argv[])
{
  bcm_host_init();
  
  init_ogl();
  
  /* Load mapping file */
  FILE *f = fopen(CONF_FILE, "r");
  unsigned int len;
          
  if(f!=NULL)
  { 
    fgets(buffer, 1024, f);    
    sscanf(buffer,"%f,%f,%f,%f,%f,%f,%f,%f\n",
                &p[0][0],
                &p[0][1],
                &p[1][0],
                &p[1][1],
                &p[2][0],
                &p[2][1],
                &p[3][0],
                &p[3][1]         
    );
    fgets(buffer, 1024, f);    
    sscanf(buffer,"%d,%d\n",
                &s[0],
                &s[1]           
    ); 
    fgets(buffer, 1024, f);    
    sscanf(buffer,"%d,%d",
                &o[0],
                &o[1]           
    );     

    
    printf("Loaded conf: %f,%f,%f,%f,%f,%f,%f,%f\n%d,%d\n%d,%d",
                p[0][0],
                p[0][1],
                p[1][0],
                p[1][1],
                p[2][0],
                p[2][1],
                p[3][0],
                p[3][1],
                s[0],
                s[1],
                o[0],
                o[1]);
    
   fclose(f);  
  }
	
  pthread_create(&threadKeyboardInput,NULL,keyboardInput,NULL);
  pthread_create(&threadSocketInput,NULL,socketInput,NULL);
  
  /* Create UDP socket for sound events */
  soundClientAddr.sin_family    = AF_INET;
  soundClientAddr.sin_port      = htons(SOUND_CLIENT_PORT);

  if(argc > 1)
    inet_aton(argv[1], &soundClientAddr.sin_addr);
  else
    inet_aton("127.0.0.1", &soundClientAddr.sin_addr);
    
  soundSocket = socket(PF_INET, SOCK_DGRAM, 0);
  
  if (soundSocket == -1)
  {
    printf("[main] sound socket error\n");
  }

  mainLoop();

  return 0;
}
