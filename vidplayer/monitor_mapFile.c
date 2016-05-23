#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/inotify.h>

#include <GLES/gl.h>

#include "cJSON/cJSON.h"
#include "monitor_mapFile.h"

#define MAPFILE "mapping.conf"

GLbyte quadx[4*3];

/** Texture coordinates for the quad. */
GLfloat texCoords[4 * 2];

void parseFile(char *file)
{
    printf("[%s] File has changed\n",__FUNCTION__);

    FILE *f;
    long len;
    char *data;
    
    f=fopen(file,"rb");
    fseek(f,0,SEEK_END);
    len=ftell(f);
    fseek(f,0,SEEK_SET);

    data=(char*)malloc(len+1);
    fread(data,1,len,f);
    data[len]='\0';
    fclose(f);
    
    cJSON *root;
    
    root=cJSON_Parse(data);

    if (!root)
      printf("Error before: [%s]\n",cJSON_GetErrorPtr());
    else
    {
      cJSON * edges = cJSON_GetObjectItem(root,"edges");
      cJSON * crop = cJSON_GetObjectItem(root,"crop");
       
      cJSON_Delete(root);
    }
    free(data);
  
}

void * monitorFile(void * filename) 
{
  int length, i = 0;
  int fd;
  int wd;
  char buffer[BUF_LEN];

  fd = inotify_init();

  if ( fd < 0 ) {
    printf( "[%s][ERROR] inotify_init\n",__FUNCTION__);
    return;
  }

  wd = inotify_add_watch( fd, (char *)filename, IN_MODIFY | IN_CREATE | IN_DELETE );

  length = read( fd, buffer, BUF_LEN );  

  if ( length < 0 ) {
    printf( "[%s][ERROR] read\n",__FUNCTION__);
    return;
  }  

  while ( i < length ) {
    struct inotify_event *event = ( struct inotify_event * ) &buffer[ i ];
    if ( event->len ) {
      if ( event->mask & IN_CREATE ) {
        if ( event->mask & IN_ISDIR ) {
          printf( "[%s][ERROR] The directory %s was created.\n",__FUNCTION__, event->name );       
        }
        else {
          printf( "[%s][ERROR] The file %s was created.\n",__FUNCTION__, event->name );

          if(strcmp(event->name,MAPFILE) == 0)
            parseFile(MAPFILE);
          
        }
      }
      else if ( event->mask & IN_DELETE ) {
        if ( event->mask & IN_ISDIR ) {
          printf( "[%s][ERROR] The directory %s was deleted.\n",__FUNCTION__, event->name );       
        }
        else {
          printf( "[%s][ERROR] The file %s was deleted.\n",__FUNCTION__, event->name );
        }
      }
      else if ( event->mask & IN_MODIFY ) {
        if ( event->mask & IN_ISDIR ) {
          printf( "[%s][ERROR] The directory %s was modified.\n",__FUNCTION__, event->name );
        }
        else {
          printf( "[%s][ERROR] The file %s was modified.\n",__FUNCTION__, event->name );

          if(strcmp(event->name,MAPFILE) == 0)
          {
            texCoords[0] = 0.1f;  
            texCoords[1] = 0.1f;
            texCoords[2] = 0.1f;
            texCoords[3] = 0.5f;
            texCoords[4] = 0.5f;
            texCoords[5] = 0.1f;
            texCoords[6] = 0.5f;
            texCoords[7] = 0.5f;

            quadx[0] = -6;
            quadx[0] = -10;
            quadx[0] = 5;
            quadx[0] = 6;
            quadx[0] = -10;
            quadx[0] = 5;
            quadx[0] = -6;
            quadx[0] = 10;
            quadx[0] = 5;
            quadx[0] = 6;
            quadx[0] = 10;
            quadx[0] = 5;

            parseFile(MAPFILE);
          }
      
        }
      }
    }
    i += EVENT_SIZE + event->len;
  }

  inotify_rm_watch( fd, wd );
  close( fd );
}