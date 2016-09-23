#ifndef INPUTTHREAD_H
#define INPUTTHREAD_H

#define UDP_BURST_TIMEOUT 3000
#define IFRAME_BUFFER_LENGTH (1024*1024)

void *inputThread(void *param);


#endif