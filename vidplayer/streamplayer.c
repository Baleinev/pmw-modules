/*
Copyright (c) 2012, Broadcom Europe Ltd
All rights reserved.
Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the copyright holder nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.
THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

// Video deocode demo using OpenMAX IL though the ilcient helper library

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include "bcm_host.h"
#include "ilclient.h"

#define IFRAME_BUFFER_LENGTH (1024*1024)
 
static int open_listening_socket(unsigned int udp_port)
{
	int sock;

	struct sockaddr_in sock_in;

  int sinlen = sizeof(struct sockaddr_in);

  memset(&sock_in, 0, sinlen);

  sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  printf("[setup_receiveSocket] Sock Status %d\n", sock);

  sock_in.sin_family = AF_INET;
  sock_in.sin_port = htons(udp_port);
  sock_in.sin_addr.s_addr =  htonl(INADDR_ANY);

  int status = bind(sock, (struct sockaddr *)&sock_in, sinlen);
  printf("[setup_receiveSocket] Bind Status %d, errno: %d\n", status, errno);


	struct timeval tv;
	tv.tv_sec = 0;
	tv.tv_usec = 3000;

  setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO,&tv,sizeof(tv));


  if(status < 0)
  	return status;
  else
  	return sock;
}
 

static int video_decode_test(unsigned int udp_port)
{
   OMX_VIDEO_PARAM_PORTFORMATTYPE format;
   OMX_TIME_CONFIG_CLOCKSTATETYPE cstate;
   COMPONENT_T *video_decode = NULL, *video_scheduler = NULL, *video_render = NULL, *clock = NULL;
   COMPONENT_T *list[5];
   TUNNEL_T tunnel[4];
   ILCLIENT_T *client;
   int status = 0;
   unsigned int data_len = 0;
   unsigned int already_read =0;
   unsigned int buflen = 0;
   unsigned int bufptr = 0;

   char iframeBuffer[IFRAME_BUFFER_LENGTH];

	int sock;
	struct sockaddr_in recv_sock;
    socklen_t receiveSockaddrLen = sizeof(recv_sock);  

   FILE *in;

   memset(list, 0, sizeof(list));
   memset(tunnel, 0, sizeof(tunnel));

   if((sock = open_listening_socket(udp_port)) < 0)
      return -2;

   // if((in = fopen("test.h264", "rb")) == NULL)
   //    return -2;  

   if((client = ilclient_init()) == NULL)
   {
      close(sock);
      return -3;
   }

   if(OMX_Init() != OMX_ErrorNone)
   {
      ilclient_destroy(client);
      close(sock);
      return -4;
   }

   // create video_decode
   if(ilclient_create_component(client, &video_decode, "video_decode", ILCLIENT_DISABLE_ALL_PORTS | ILCLIENT_ENABLE_INPUT_BUFFERS) != 0)
      status = -14;
   list[0] = video_decode;

   // create video_render
   if(status == 0 && ilclient_create_component(client, &video_render, "video_render", ILCLIENT_DISABLE_ALL_PORTS) != 0)
      status = -14;
   list[1] = video_render;

   // create clock
   if(status == 0 && ilclient_create_component(client, &clock, "clock", ILCLIENT_DISABLE_ALL_PORTS) != 0)
      status = -14;
   list[2] = clock;

   memset(&cstate, 0, sizeof(cstate));
   cstate.nSize = sizeof(cstate);
   cstate.nVersion.nVersion = OMX_VERSION;
   cstate.eState = OMX_TIME_ClockStateWaitingForStartTime;
   cstate.nWaitMask = 1;
   if(clock != NULL && OMX_SetParameter(ILC_GET_HANDLE(clock), OMX_IndexConfigTimeClockState, &cstate) != OMX_ErrorNone)
      status = -13;

   // create video_scheduler
   if(status == 0 && ilclient_create_component(client, &video_scheduler, "video_scheduler", ILCLIENT_DISABLE_ALL_PORTS) != 0)
      status = -14;
   list[3] = video_scheduler;

   set_tunnel(tunnel, video_decode, 131, video_scheduler, 10);
   set_tunnel(tunnel+1, video_scheduler, 11, video_render, 90);
   set_tunnel(tunnel+2, clock, 80, video_scheduler, 12);

   // setup clock tunnel first
   if(status == 0 && ilclient_setup_tunnel(tunnel+2, 0, 0) != 0)
      status = -15;
   else
      ilclient_change_component_state(clock, OMX_StateExecuting);

   if(status == 0)
      ilclient_change_component_state(video_decode, OMX_StateIdle);

   memset(&format, 0, sizeof(OMX_VIDEO_PARAM_PORTFORMATTYPE));

   format.nSize = sizeof(OMX_VIDEO_PARAM_PORTFORMATTYPE);
   format.nVersion.nVersion = OMX_VERSION;
   format.nPortIndex = 130;
   format.eCompressionFormat = OMX_VIDEO_CodingAVC;

   if(status == 0 &&
      OMX_SetParameter(ILC_GET_HANDLE(video_decode), OMX_IndexParamVideoPortFormat, &format) == OMX_ErrorNone &&
      ilclient_enable_port_buffers(video_decode, 130, NULL, NULL, NULL) == 0)
   {
      OMX_BUFFERHEADERTYPE *buf;
      int port_settings_changed = 0;
      int first_packet = 1;

      ilclient_change_component_state(video_decode, OMX_StateExecuting);

      while((buf = ilclient_get_input_buffer(video_decode, 130, 1)) != NULL)
      {
         // feed data and wait until we get port settings changed

         // printf("Waiting... buffer length:%d",buf->nAllocLen);fflush(0);

         unsigned char *dest = buf->pBuffer;

         // data_len += fread(dest, 1, 10000 > buf->nAllocLen-data_len ? buf->nAllocLen-data_len : 10000, in);         

		int retVal;

         // printf("Now we had %d bytes\n",data_len);		

		int i=0;


		/* If buffer is empty, get new data */
		if(buflen == 0)
		{
         	bufptr = 0;

	        /* Get all pending data in temporary buffer */
	        do
	        {
	         	retVal = recvfrom(sock, iframeBuffer+buflen, IFRAME_BUFFER_LENGTH-buflen, 0, (struct sockaddr *)&recv_sock, &receiveSockaddrLen);

	         	if(retVal != -1) // Nothing to read and nothing already read
	         	{
	         		i++;
	         		// printf("+= %d\n",retVal);         		
		        	buflen += retVal;
	         	}

	         }
	         while(buflen == 0 || !(retVal == -1 && errno == EAGAIN)); // Loop while we havent read everything or last wouldn't have blocked
	
	         printf("Now we have %d bytes in %d rounds\n",buflen,i);
     	}

         if(buflen < buf->nAllocLen)
         {
         	data_len = buflen;
         	buflen = 0;
         }
         else
         {
         	data_len = buf->nAllocLen;
         	buflen-= buf->nAllocLen;
         	bufptr += buf->nAllocLen;
         }

	     printf("Cousume %d bytes\n",data_len);         


         memcpy(buf->pBuffer,iframeBuffer+bufptr,data_len);

         buf->nFilledLen = data_len;
         buf->nOffset = 0;         



		// if(data_len==buf->nAllocLen)
			// printf("GLITCH");

         // fflush(0);

         //fread(dest, 1, buf->nAllocLen-data_len, in);

         if(port_settings_changed == 0 &&
            ((data_len > 0 && ilclient_remove_event(video_decode, OMX_EventPortSettingsChanged, 131, 0, 0, 1) == 0) ||
             (data_len == 0 && ilclient_wait_for_event(video_decode, OMX_EventPortSettingsChanged, 131, 0, 0, 1,
                                                       ILCLIENT_EVENT_ERROR | ILCLIENT_PARAMETER_CHANGED, 10000) == 0)))
         {
            port_settings_changed = 1;

            if(ilclient_setup_tunnel(tunnel, 0, 0) != 0)
            {
               status = -7;
               break;
            }

            ilclient_change_component_state(video_scheduler, OMX_StateExecuting);

            // now setup tunnel to video_render
            if(ilclient_setup_tunnel(tunnel+1, 0, 1000) != 0)
            {
               status = -12;
               break;
            }

            ilclient_change_component_state(video_render, OMX_StateExecuting);
         }
         if(!data_len)
            break;

         data_len = 0;

         if(first_packet)
         {
            buf->nFlags = OMX_BUFFERFLAG_STARTTIME;
            first_packet = 0;
         }
         else
            buf->nFlags = OMX_BUFFERFLAG_TIME_UNKNOWN;

         if(OMX_EmptyThisBuffer(ILC_GET_HANDLE(video_decode), buf) != OMX_ErrorNone)
         {
            status = -6;
            break;
         }
      }

      buf->nFilledLen = 0;
      buf->nFlags = OMX_BUFFERFLAG_TIME_UNKNOWN | OMX_BUFFERFLAG_EOS;

      if(OMX_EmptyThisBuffer(ILC_GET_HANDLE(video_decode), buf) != OMX_ErrorNone)
         status = -20;

      // wait for EOS from render
      ilclient_wait_for_event(video_render, OMX_EventBufferFlag, 90, 0, OMX_BUFFERFLAG_EOS, 0,
                              ILCLIENT_BUFFER_FLAG_EOS, -1);

      // need to flush the renderer to allow video_decode to disable its input port
      ilclient_flush_tunnels(tunnel, 0);

   }

   close(sock);

   ilclient_disable_tunnel(tunnel);
   ilclient_disable_tunnel(tunnel+1);
   ilclient_disable_tunnel(tunnel+2);
   ilclient_disable_port_buffers(video_decode, 130, NULL, NULL, NULL);
   ilclient_teardown_tunnels(tunnel);

   ilclient_state_transition(list, OMX_StateIdle);
   ilclient_state_transition(list, OMX_StateLoaded);

   ilclient_cleanup_components(list);

   OMX_Deinit();

   ilclient_destroy(client);
   return status;
}

int main (int argc, char **argv)
{
   if (argc < 2) {
      printf("Usage: %s <port>\n", argv[0]);
      exit(1);
   }
   bcm_host_init();
   return video_decode_test(atoi(argv[1]));
} 
