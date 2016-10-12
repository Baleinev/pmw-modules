 /**
 * The role of this thread is to make a single raw H264 frame available to other threads, through
 * the inputGetNextFrame function, at the correct rate.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <errno.h> 

#include <pthread.h>

#define DBG(fmt,...) fprintf(stderr,fmt, ##__VA_ARGS__);fflush(stderr);
#define ERR(fmt,...) fprintf(stderr,fmt, ##__VA_ARGS__);fflush(stderr);
#define LOG(fmt,...) fprintf(stdout,fmt, ##__VA_ARGS__);

#define UDP_BUFFER_LENGTH (1024*1024)
#define IFRAME_BUFFER_LENGTH (1024*1024*4)

char iframeBuffer[IFRAME_BUFFER_LENGTH];

/*
 * Source configuration variables, declared in the command Thread
 *
 * Will be modified by control thread, so always use them protected by their sync variables
 */
int sourceUDPsocket;
int sourceUDPport;

static int inputSetupUDPsource()
{
  struct sockaddr_in sock_in;

  int sinlen = sizeof(struct sockaddr_in);

  memset(&sock_in, 0, sinlen);

  sourceUDPsocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  DBG("[setup_receiveSocket] Sock Status %d\n", sourceUDPsocket);

  sock_in.sin_family = AF_INET;
  sock_in.sin_port = htons(sourceUDPport);
  sock_in.sin_addr.s_addr =  htonl(INADDR_ANY);

  int status = bind(sourceUDPsocket, (struct sockaddr *)&sock_in, sinlen);

  int n;

  setsockopt(sourceUDPsocket, SOL_SOCKET, SO_RCVBUF,&n,sizeof(n));
  
  DBG("[%s][%s] Current UDP size %d\n",__FILE__,__FUNCTION__,n);    

  n = UDP_BUFFER_LENGTH;

  if (setsockopt(socket, SOL_SOCKET, SO_RCVBUF, &n, sizeof(n)) == -1) {
    ERR("[%s][%s][ERROR] Failed to expand UDP buffer size\n",__FILE__,__FUNCTION__);    
  }  

  return status;
}

int main(int argc, char *argv[])
{
  int readBytes;
      struct sockaddr_in recv_sock;
      socklen_t receiveSockaddrLen = sizeof(recv_sock);    

  sourceUDPport = atoi(argv[1]);

  inputSetupUDPsource();

  struct timeval now,first;

  gettimeofday(&first,NULL);  

  while(1)
  {
    readBytes = recvfrom(
      sourceUDPsocket,
      iframeBuffer,
      IFRAME_BUFFER_LENGTH,
      0,
      (struct sockaddr *)&recv_sock,
      &receiveSockaddrLen);

    gettimeofday(&now,NULL);

    printf("%d,%d\n",((now.tv_sec-first.tv_sec)*1000000+(now.tv_usec-first.tv_usec)),readBytes);

  }
}
