#ifndef INPUTTHREAD_H
#define INPUTTHREAD_H

#define DEFAULT_UDP_WAIT 2000
#define DEFAULT_FRAME_BUFFER_COUNT 1

#define FRAME_BUFFER_LENGTH (1024*1024*4)
#define UDP_BUFFER_LENGTH (1024*1024)
#define UDP_MAX_SIZE 548


void *inputThread(void *param);


#endif