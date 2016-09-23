#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/inotify.h>

#include <GLES/gl.h>

#include "cJSON/cJSON.h"
#include "monitorThread.h"

#define MAPFILE "mapping.conf.json"

extern unsigned int flagQuit;

/*
 * Shared variable to set
 */

extern float p[4][2];
extern float o[4][2];

void printValues()
{
  fprintf(stderr,"[%s][%s] P: %.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f \n",__FILE__,__FUNCTION__,p[0][0],p[0][1],p[1][0],p[1][1],p[2][0],p[2][1],p[3][0],p[3][1]);fflush(stderr);
  fprintf(stderr,"[%s][%s] O: %.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f \n",__FILE__,__FUNCTION__,o[0][0],o[0][1],o[1][0],o[1][1],o[2][0],o[2][1],o[3][0],o[3][1]);fflush(stderr);  
}

void parseFile()
{

  char buffer[CONF_FILE_LENGTH];
  char filename[CONF_FILE_PATH_LENGTH];

  memset(filename,0,sizeof(filename));

  strcat(filename,MAP_DIRNAME);
  strcat(filename,"/");
  strcat(filename,MAP_FILENAME);

  FILE *f;
    
  f=fopen(filename,"rb");

  fread(buffer,1, CONF_FILE_LENGTH, f);

  fprintf(stderr,"[%s][%s] File has changed. \n",__FILE__,__FUNCTION__);fflush(stderr);

  fclose(f);
    
  cJSON *root;
    
  root=cJSON_Parse(buffer);

  if (!root)
  {
    fprintf(stderr,"[%s][%s] Error before: [%s] parsing file %s\n",__FILE__,__FUNCTION__,cJSON_GetErrorPtr(),filename);fflush(stderr);
  }
  else
  {
    printValues();    
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

    cJSON_Delete(root);

    printValues();        
  }
  // free(data);
}

void * monitorThread(void *param) 
{
  int length, i;
  int fd;
  int wd;
  char buffer[BUF_LEN];

  fd = inotify_init();

  if ( fd < 0 ) {
    fprintf(stderr,"[%s][%s] inotify_init\n",__FILE__,__FUNCTION__);fflush(stderr);
    return;
  }

  wd = inotify_add_watch( fd, (char *)MAP_DIRNAME, IN_MODIFY | IN_CREATE | IN_DELETE );

  fprintf(stderr,"[%s][%s] Thread started\n",__FILE__,__FUNCTION__);fflush(stderr);

  parseFile();

  while(!flagQuit)
  {
    i = 0;

    length = read( fd, buffer, BUF_LEN ); 

    fprintf(stderr,"[%s][%s] read %d\n",__FILE__,__FUNCTION__,length);fflush(stderr);

    if ( length < 0 ) {
      continue;
    }  

    while ( i < length ) {
      
      struct inotify_event *event = ( struct inotify_event * ) &buffer[ i ];

      fprintf(stderr,"[%s][%s] Event mask:%d name:%s\n",__FILE__,__FUNCTION__, event->mask, event->name );fflush(stderr);


      if ( event->len ) {
        if ( event->mask & IN_CREATE ) {
          if ( event->mask & IN_ISDIR ) {
            fprintf(stderr,"[%s][%s] The directory %s was created.\n",__FILE__,__FUNCTION__, event->name );fflush(stderr);
          }
          else {
            fprintf(stderr,"[%s][%s] The file %s was created.\n",__FILE__,__FUNCTION__, event->name );fflush(stderr);

            if(strcmp(event->name,MAP_FILENAME) == 0)
              parseFile();
            
          }
        }
        else if ( event->mask & IN_DELETE ) {
          if ( event->mask & IN_ISDIR ) {
            fprintf(stderr,"[%s][%s] The directory %s was deleted.\n",__FILE__,__FUNCTION__, event->name );fflush(stderr);       
          }
          else {
            fprintf(stderr,"[%s][%s] The file %s was deleted.\n",__FILE__,__FUNCTION__, event->name );fflush(stderr);
          }
        }
        else if ( event->mask & IN_MODIFY ) {
          if ( event->mask & IN_ISDIR ) {
            fprintf(stderr,"[%s][%s] The directory %s was modified.\n",__FILE__,__FUNCTION__, event->name );fflush(stderr);
          }
          else {
            fprintf(stderr,"[%s][%s] The file %s was modified.\n",__FILE__,__FUNCTION__, event->name );fflush(stderr);

            if(strcmp(event->name,MAP_FILENAME) == 0)
            {
              parseFile();
            }
        
          }
        }
      }
      i += EVENT_SIZE + event->len;
    }

  }
  inotify_rm_watch( fd, wd );
  close( fd );
}