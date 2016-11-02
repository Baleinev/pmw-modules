#ifndef MONITORTHREAD_H
#define MONITORTHREAD_H

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/inotify.h>

#define EVENT_SIZE  ( sizeof (struct inotify_event) )
#define BUF_LEN     ( 1024 * ( EVENT_SIZE + 16 ) )

#define CONF_FILE_LENGTH (4*1024)
#define CONF_FILE_PATH_LENGTH 1024

#define DIRNAME "/etc/pmw.d/"
#define MAP_FILENAME "mapping.conf.json"
#define CONF_FILENAME "vidplayer.conf.json"

void * monitorThread(void *param);
int parseConfFile(unsigned int *height,unsigned int *width,unsigned int *bufferLength,unsigned int *udpWait,unsigned int *port);

#endif