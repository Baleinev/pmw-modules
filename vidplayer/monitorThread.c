#define _GNU_SOURCE             /* See feature_test_macros(7) */
#include <sched.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/inotify.h>
#include <unistd.h>
#include <fcntl.h>

#include "cJSON/cJSON.h"
#include "monitorThread.h"

#include "vidplayer.h"

extern unsigned int flagQuit;
extern unsigned int flagMappingChanged;
extern unsigned int flagConfChanged; 
extern unsigned int flagBlendingChanged;
extern unsigned int flagDrawMire;

extern pthread_cond_t newDataAvailableCond;
extern unsigned int flagNewDataAvailable;
extern pthread_mutex_t newDataAvailableMutex;

/*
 * Shared variable to set
 */

extern float p[4][2];
extern float o[4][2];
extern float b[4];

void printValues()
{
  DBG("[%s][%s] P: %.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f ",__FILE__,__FUNCTION__,p[0][0],p[0][1],p[1][0],p[1][1],p[2][0],p[2][1],p[3][0],p[3][1]);
  DBG("[%s][%s] O: %.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f ",__FILE__,__FUNCTION__,o[0][0],o[0][1],o[1][0],o[1][1],o[2][0],o[2][1],o[3][0],o[3][1]);  
  DBG("[%s][%s] B: %.2f,%.2f,%.2f,%.2f ",__FILE__,__FUNCTION__,b[0],b[1],b[2],b[3]);  
}

int parseConfFile(unsigned int *height,unsigned int *width,unsigned int *bufferLength,unsigned int *udpWait,unsigned int *port)
{
  char buffer[CONF_FILE_LENGTH];
  char filename[CONF_FILE_PATH_LENGTH];

  memset(filename,0,sizeof(filename));

  strcat(filename,DIRNAME);
  strcat(filename,"/");
  strcat(filename,CONF_FILENAME);

  FILE *f;
    
  f=fopen(filename,"rb");

  fread(buffer,1, CONF_FILE_LENGTH, f);

  LOG("[%s][%s] Parsing config file. ",__FILE__,__FUNCTION__);

  fclose(f);
    
  cJSON *root;
  cJSON *item;
    
  root=cJSON_Parse(buffer);

  if (!root)
  {
    ERR("[%s][%s] Error before: [%s] parsing file %s",__FILE__,__FUNCTION__,cJSON_GetErrorPtr(),filename);
    return -1;
  }  

  if(height != NULL && (item = cJSON_GetObjectItem(root,"height")) != NULL)
    *height = item->valueint;

  if(width != NULL && (item = cJSON_GetObjectItem(root,"width")) != NULL)
    *width = item->valueint;

  if(bufferLength != NULL && (item = cJSON_GetObjectItem(root,"bufferLength")) != NULL)
    *bufferLength = item->valueint;

  if(udpWait != NULL && (item = cJSON_GetObjectItem(root,"udpWait")) != NULL)
    *udpWait = item->valueint;

  if(port != NULL && (item = cJSON_GetObjectItem(root,"port")) != NULL)
    *port = item->valueint;

  cJSON_Delete(root);

  return 0;
}

void parseMapFile()
{

  char buffer[CONF_FILE_LENGTH];
  char filename[CONF_FILE_PATH_LENGTH];

  memset(filename,0,sizeof(filename));

  strcat(filename,DIRNAME);
  strcat(filename,"/");
  strcat(filename,MAP_FILENAME);

  FILE *f;
    
  f=fopen(filename,"rb");

  fread(buffer,1, CONF_FILE_LENGTH, f);


  fclose(f);
    
  cJSON *root;
    
  root=cJSON_Parse(buffer);

  if (!root)
  {
    DBG("[%s][%s] Error before: [%s] parsing file %s",__FILE__,__FUNCTION__,cJSON_GetErrorPtr(),filename);
  }
  else
  {
    printValues();

    flagDrawMire = cJSON_GetObjectItem(root,"mire")->valueint;

    /*
     * Get edges
     */
    cJSON *edges = cJSON_GetObjectItem(root,"edges");

    cJSON *edgesTop = cJSON_GetObjectItem(edges,"top");
    cJSON *edgesBottom = cJSON_GetObjectItem(edges,"bottom");

    cJSON *edgesTopLeft = cJSON_GetObjectItem(edgesTop,"left");
    cJSON *edgesTopRight = cJSON_GetObjectItem(edgesTop,"right");
    cJSON *edgesBottomLeft = cJSON_GetObjectItem(edgesBottom,"left");
    cJSON *edgesBottomRight = cJSON_GetObjectItem(edgesBottom,"right");

    p[0][0] = cJSON_GetObjectItem(edgesBottomLeft,"x")->valuedouble*2.0-1.0;
    p[0][1] = cJSON_GetObjectItem(edgesBottomLeft,"y")->valuedouble*2.0-1.0;

    p[1][0] = cJSON_GetObjectItem(edgesBottomRight,"x")->valuedouble*2.0-1.0;
    p[1][1] = cJSON_GetObjectItem(edgesBottomRight,"y")->valuedouble*2.0-1.0;

    p[2][0] = cJSON_GetObjectItem(edgesTopRight,"x")->valuedouble*2.0-1.0;
    p[2][1] = cJSON_GetObjectItem(edgesTopRight,"y")->valuedouble*2.0-1.0;

    p[3][0] = cJSON_GetObjectItem(edgesTopLeft,"x")->valuedouble*2.0-1.0;
    p[3][1] = cJSON_GetObjectItem(edgesTopLeft,"y")->valuedouble*2.0-1.0;

    /*
     * Get cropping
     */
    cJSON *crop = cJSON_GetObjectItem(root,"crop");

    cJSON *cropTop = cJSON_GetObjectItem(crop,"top");
    cJSON *cropBottom = cJSON_GetObjectItem(crop,"bottom");

    cJSON *cropTopLeft = cJSON_GetObjectItem(cropTop,"left");
    cJSON *cropTopRight = cJSON_GetObjectItem(cropTop,"right");
    cJSON *cropBottomLeft = cJSON_GetObjectItem(cropBottom,"left");
    cJSON *cropBottomRight = cJSON_GetObjectItem(cropBottom,"right");

    o[0][0] = cJSON_GetObjectItem(cropTopLeft,"x")->valuedouble;
    o[0][1] = cJSON_GetObjectItem(cropTopLeft,"y")->valuedouble;

    o[1][0] = cJSON_GetObjectItem(cropTopRight,"x")->valuedouble;
    o[1][1] = cJSON_GetObjectItem(cropTopRight,"y")->valuedouble;

    o[2][0] = cJSON_GetObjectItem(cropBottomRight,"x")->valuedouble;
    o[2][1] = cJSON_GetObjectItem(cropBottomRight,"y")->valuedouble;

    o[3][0] = cJSON_GetObjectItem(cropBottomLeft,"x")->valuedouble;
    o[3][1] = cJSON_GetObjectItem(cropBottomLeft,"y")->valuedouble;


    /*
     * Get blending 
     */
    cJSON *blending = cJSON_GetObjectItem(root,"blending");

    float newBlending[4];

    newBlending[0] = cJSON_GetObjectItem(blending,"top")->valuedouble;
    newBlending[1] = cJSON_GetObjectItem(blending,"right")->valuedouble;
    newBlending[2] = cJSON_GetObjectItem(blending,"bottom")->valuedouble;
    newBlending[3] = cJSON_GetObjectItem(blending,"left")->valuedouble;

    if(newBlending[0] != b[0] || newBlending[1] != b[1] || newBlending[2] != b[2] || newBlending[3] != b[3])
    {
      b[0] = newBlending[0];
      b[1] = newBlending[1];
      b[2] = newBlending[2];
      b[3] = newBlending[3];

      flagBlendingChanged = 1;
    }

    flagMappingChanged = 1;

    cJSON_Delete(root);

    printValues();        
  }
  // free(data);
}

void * monitorThread(void *param) 
{
  cpu_set_t affinity;

  CPU_ZERO(&affinity);
  CPU_SET(2,&affinity);

  if(pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &affinity) != 0)
  {
    ERR("Cannot set affinity. errno:%d",errno);
  }

  int length, i;
  int fd;
  int wd;
  char buffer[BUF_LEN];

  fd = inotify_init();

  int flags = fcntl(fd, F_GETFL, 0);
  
  if(fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1)
  {
    ERR("Cannot set non blocking. errno:%d",errno);
  }

  if ( fd < 0 ) {
    DBG("[%s][%s] inotify_init",__FILE__,__FUNCTION__);
    return NULL;
  }

  wd = inotify_add_watch( fd, (char *)DIRNAME, IN_MODIFY | IN_CREATE | IN_DELETE );

  DBG("[%s][%s] Thread started",__FILE__,__FUNCTION__);
  
  LOG("Entering loop"); 

  while(!flagQuit)
  {
    i = 0;

    length = read( fd, buffer, BUF_LEN ); 

    DBG("[%s][%s] read %d",__FILE__,__FUNCTION__,length);

    if ( length < 0 ) {
      // printf("*");fflush(0);
      usleep(10000);
      continue;
    }  

    while ( i < length ) {
      
      struct inotify_event *event = ( struct inotify_event * ) &buffer[ i ];

      DBG("[%s][%s] Event mask:%d name:%s",__FILE__,__FUNCTION__, event->mask, event->name );


      if ( event->len ) {
        if ( event->mask & IN_CREATE ) {
          if ( event->mask & IN_ISDIR ) {
            DBG("[%s][%s] The directory %s was created.",__FILE__,__FUNCTION__, event->name );
          }
          else {
            DBG("[%s][%s] The file %s was created.",__FILE__,__FUNCTION__, event->name );

            if(strcmp(event->name,MAP_FILENAME) == 0)
            {
              LOG("[%s][%s] Mapping file has changed. ",__FILE__,__FUNCTION__);
              parseMapFile();
              flagMappingChanged = 1;
            }
            else if(strcmp(event->name,CONF_FILENAME) == 0)
            {
              LOG("[%s][%s] Config file has changed. ",__FILE__,__FUNCTION__);              
              pthread_mutex_lock(&newDataAvailableMutex);

              flagConfChanged = 1;    

              pthread_cond_broadcast(&newDataAvailableCond);

              pthread_mutex_unlock(&newDataAvailableMutex);

              break;                        
            }          
            
          }
        }
        else if ( event->mask & IN_DELETE ) {
          if ( event->mask & IN_ISDIR ) {
            DBG("[%s][%s] The directory %s was deleted.",__FILE__,__FUNCTION__, event->name );       
          }
          else {
            DBG("[%s][%s] The file %s was deleted.",__FILE__,__FUNCTION__, event->name );
          }
        }
        else if ( event->mask & IN_MODIFY ) {
          if ( event->mask & IN_ISDIR ) {
            DBG("[%s][%s] The directory %s was modified.",__FILE__,__FUNCTION__, event->name );
          }
          else {
            DBG("[%s][%s] The file %s was modified.",__FILE__,__FUNCTION__, event->name );

            if(strcmp(event->name,MAP_FILENAME) == 0)
            { 
              LOG("[%s][%s] Mapping file has changed. ",__FILE__,__FUNCTION__);                    
              parseMapFile();
              flagMappingChanged = 1;
            }
            else if(strcmp(event->name,CONF_FILENAME) == 0)
            {
              LOG("[%s][%s] Config file has changed. ",__FILE__,__FUNCTION__);                            
              pthread_mutex_lock(&newDataAvailableMutex);

              flagConfChanged = 1;    

              pthread_cond_broadcast(&newDataAvailableCond);
              pthread_mutex_unlock(&newDataAvailableMutex);

              break;                        
            }
          }
        }
      }
      i += EVENT_SIZE + event->len;
    }
  }

  LOG("Exiting loop"); 

  inotify_rm_watch( fd, wd );
  close( fd );
  
  LOG("Quitting"); 

  return NULL;
}