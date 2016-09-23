#ifndef COMMANDTHREAD_H
#define COMMANDTHREAD_H

#define FILENAME_ATTRIBUTE "fileName"
#define COMMAND_ATTRIBUTE "command"

#define CMD_PLAY "play"
#define CMD_PAUSE "pause"

#define LISTEN_PORT 1337

#define BUFFER_SIZE 1024
#define FILENAME_SIZE 128


typedef enum 
{ 
  SOURCE_FILE,
  SOURCE_UDP 
} sourceType_t;

void *commandThread(void *param);

#endif