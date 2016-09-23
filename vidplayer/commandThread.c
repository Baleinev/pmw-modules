#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <errno.h> 

#include <pthread.h>

#include "cJSON/cJSON.h"

#include "commandThread.h"

int sourceUDPsocket;
int sourceUDPport;
char sourceFILEname[FILENAME_SIZE];
sourceType_t sourceType;

unsigned int optionLoop = 1;

unsigned int flagQuit = 0;

pthread_mutex_t sourceMutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t sourceCond = PTHREAD_COND_INITIALIZER;
unsigned int flagSourceChanged = 0;
unsigned int flagSourceLoaded = 0;

static void enable_keepalive(int sock)
{
  int yes = 1;
  setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &yes, sizeof(int));

  int idle = 1;
  setsockopt(sock, IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof(int));

  int interval = 1;
  setsockopt(sock, IPPROTO_TCP, TCP_KEEPINTVL, &interval, sizeof(int));

  int maxpkt = 10;
  setsockopt(sock, IPPROTO_TCP, TCP_KEEPCNT, &maxpkt, sizeof(int));
}

void *commandThread(void *param)
{
  /* master file descriptor list */
  fd_set master;
  /* temp file descriptor list for select() */
  fd_set read_fds;

  /* buffer for client data */
  char buffer[BUFFER_SIZE];
  unsigned int totalcnt = 0;

  char *boundary;

  /* server address */
  struct sockaddr_in serveraddr;
  /* client address */
  struct sockaddr_in clientaddr;
  /* maximum file descriptor number */
  int fdmax;
  /* listening socket descriptor */
  int listener;
  /* newly accept()ed socket descriptor */
  int newfd;

  int nbytes;
  /* for setsockopt() SO_REUSEADDR, below */
  int yes = 1;
  int addrlen;
  int i, j;
  /* clear the master and temp sets */
  FD_ZERO(&master);
  FD_ZERO(&read_fds);

  int connfd = 0;
  struct sockaddr_in serv_addr,cliaddr;

  listener = socket(AF_INET, SOCK_STREAM, 0);  

  /*"address already in use" error message */
  if(setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1)
    printf("[%s][%s][%d][ERROR] setsockopt failed\n",__FILE__,__FUNCTION__,__LINE__);fflush(0);       

  memset(buffer, '0', BUFFER_SIZE);
  memset(&serv_addr, '0', sizeof(serv_addr));

  serv_addr.sin_family = AF_INET;
  serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  serv_addr.sin_port = htons(LISTEN_PORT); 

  bind(listener, (struct sockaddr*)&serv_addr, sizeof(serv_addr)); 

  listen(listener, 10);

  /* add the listener to the master set */
  FD_SET(listener, &master);

  /* keep track of the biggest file descriptor */
  fdmax = listener; /* so far, it's this one*/  

  printf("[%s][%s][%d] Waiting for connection\n",__FILE__,__FUNCTION__,__LINE__);fflush(0);              

  while(!flagQuit)
  {    
    read_fds = master;

    if(select(fdmax+1, &read_fds, NULL, NULL, NULL) == -1)
    {
      printf("[%s][%s][%d][ERROR] select failed\n",__FILE__,__FUNCTION__,__LINE__);fflush(0);              
      return 1;
    }

    printf("[%s][%s][%d] Got one\n",__FILE__,__FUNCTION__,__LINE__);fflush(0);                   

    for(i = 0; i <= fdmax; i++)
    {
      if(FD_ISSET(i, &read_fds))
      { 
        /* we got one... */
        if(i == listener)
        {
          /* handle new connections */
          addrlen = sizeof(clientaddr);

          if((newfd = accept(listener, (struct sockaddr *)&clientaddr, &addrlen)) == -1)
          {
            printf("[%s][%s][%d][ERROR] accept failed\n",__FILE__,__FUNCTION__,__LINE__);fflush(0);              
          }
          else
          {
            enable_keepalive(newfd);

            FD_SET(newfd, &master); /* add to master set */

            if(newfd > fdmax)
            { /* keep track of the maximum */
              fdmax = newfd;
            }
            printf("[%s][%s][%d] New connection from %s on socket %d\n",__FILE__,__FUNCTION__,__LINE__,inet_ntoa(clientaddr.sin_addr), newfd);
          }
        }
        else
        {
        /* handle data from a client */
            // printf("[%s][%s][%d] Writing %d bytes in buffer %d on offset %d\n",__FILE__,__FUNCTION__,__LINE__,BUFFER_SIZE-totalcnt,i,totalcnt);fflush(0);

          nbytes = recv(i, &(buffer[totalcnt]), BUFFER_SIZE-totalcnt, 0);

          if(nbytes <= 0)
          {
            /* got error or connection closed by client */
            if(nbytes == 0)
            /* connection closed */
              printf("[%s][%s][%d] Socket %d hung up. errno:%d\n",__FILE__,__FUNCTION__,__LINE__, i,errno);
            else
              printf("[%s][%s][%d] recv()error on socket %d return: %d error %d, closing",__FILE__,__FUNCTION__,__LINE__,i,nbytes,errno);

            /* close it... */
            close(i);
            /* remove from master set */
            FD_CLR(i, &master);
          }
          else
          {
            totalcnt += nbytes;

            // printf("[%s][%s][%d] Buffer before: %s, cound:%d\n",__FILE__,__FUNCTION__,__LINE__,buffer,totalcnt);fflush(0);              

            while((boundary = strchr(buffer,'\n'))!=NULL)
            {
              *boundary = '\0';

              printf("[%s][%s][%d] Buffer %s\n",__FILE__,__FUNCTION__,__LINE__,buffer);fflush(0);                            

              cJSON * root = cJSON_Parse(buffer);

              totalcnt -= (boundary-buffer)+1;    

              memmove(buffer,boundary+1,totalcnt);

              // printf("[%s][%s][%d] Buffer after memove:%s (count:%d)\n",__FILE__,__FUNCTION__,__LINE__,buffer,totalcnt);fflush(0);
              // printf("[%s][%s][%d] Recv:%s\n",__FILE__,__FUNCTION__,__LINE__,buffer);fflush(0);

              if(root)
              {
                cJSON * command  = cJSON_GetObjectItem(root,COMMAND_ATTRIBUTE);

                pthread_mutex_lock(&sourceMutex);

                if(strcmp(command->valuestring,CMD_PLAY)==0)
                {
                  cJSON * fileName  = cJSON_GetObjectItem(root,FILENAME_ATTRIBUTE);

                  strcpy(sourceFILEname,fileName->valuestring);

                  optionLoop = 1;

                  pthread_cond_signal(&sourceCond);
                  flagSourceChanged = 1;

                  // printf("[%s][%s][%d] Play %s\n",__FILE__,__FUNCTION__,__LINE__,sourceFILEname);fflush(0);                                

                  // if(isPaused)
                  // {
                  //   isPaused = 0;
                  //   pthread_cond_signal(&condPaused);
                  // }
                }
                else if(strcmp(command->valuestring,CMD_PAUSE)==0)
                {
                  // printf("[%s][%s][%d] Pause\n",__FILE__,__FUNCTION__,__LINE__);fflush(0);                                

                  // isPaused = 1;
                }

                pthread_mutex_unlock(&sourceMutex);                

                
                cJSON_Delete(root);

                
                // printf("eol\n");fflush(0);                
              }
            }
          }
        }     
      }
      if(flagQuit)
      {
        /* close it... */
        close(i);
        /* remove from master set */
        FD_CLR(i, &master);              
      }   
    }
  }
}
