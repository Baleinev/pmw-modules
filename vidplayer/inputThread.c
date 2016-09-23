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

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/mathematics.h>
#include <libavutil/samplefmt.h>

#include <pthread.h>

#include "vidplayer.h"
#include "commandThread.h"
#include "inputThread.h"


/*
 * The buffer and its write pointer, used for storing the single raw H264 frame
 */
char iframeBuffer[IFRAME_BUFFER_LENGTH];
unsigned int iframeBufferPos = 0;

struct timeval sourceTimestampBegin; // Time at which we started reading from the source
int64_t lastPts = 0; // Last frame timestamp
int64_t timestampOffset = 0; // Timestamp offset, incremented by the duration of the source when we loop
int64_t lastDuration = 0;

/*
 * libAV related stuff, for reading from file, demuxing and filtering the frames
 */
AVFormatContext *AVpFormatCtx = NULL;
static AVCodecContext *AVvideoDecCtx  = NULL;
static AVStream *AVvideoStream = NULL;
AVBitStreamFilterContext *AVbsfc = NULL;
unsigned int AVframeWidth;
unsigned int AVframeHeight;

unsigned int AVtimeBaseNum;
unsigned int AVtimeBaseDen;
static int AVvideoStreamIdx;

unsigned int fpsscale;
unsigned int fpsrate;

/*
 * Data available monitor. Used by player thread
 */
pthread_mutex_t newDataAvailableMutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t newDataAvailableCond = PTHREAD_COND_INITIALIZER;
unsigned int flagNewDataAvailable = 0;

unsigned int flagIsFirst = 0;

/*
 * Source configuration variables, declared in the command Thread
 *
 * Will be modified by control thread, so always use them protected by their sync variables
 */
extern int sourceUDPsocket;
extern int sourceUDPport;
extern char sourceFILEname[FILENAME_SIZE];
extern sourceType_t sourceType;

extern unsigned int optionLoop;

extern unsigned int flagQuit;

extern pthread_mutex_t sourceMutex;
extern pthread_cond_t sourceCond;
extern unsigned int flagSourceChanged;
extern unsigned int flagSourceLoaded;


static AVPacket *filter(AVBitStreamFilterContext *AVbsfc,AVStream *in, AVPacket *rp)
{
  AVPacket *p;
  AVPacket *fp;
  
  int rc;

  if(AVbsfc)
  {
    fp = calloc(sizeof(AVPacket), 1);

    rc = av_bitstream_filter_filter(AVbsfc,in->codec,NULL, &(fp->data), &(fp->size),rp->data, rp->size,rp->flags);
    
    if (rc > 0) 
    {
      av_free_packet(rp);
      fp->destruct = av_destruct_packet;
      p = fp;
    }
    else
    {
      fprintf(stderr, "[%s][%s][ERROR] Failed to filter packet\n",__FILE__,__FUNCTION__);
      p = rp;
    }
  }
  else
  {
    printf("No filter context \n");
    p = rp;
  }
  
  return p;
}

static int inputFreeUDPsource()
{
  close(sourceUDPsocket);
  return 0;
}

static int inputSetupUDPsource()
{  
  struct sockaddr_in sock_in;

  int sinlen = sizeof(struct sockaddr_in);

  memset(&sock_in, 0, sinlen);

  sourceUDPsocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  // printf("[setup_receiveSocket] Sock Status %d\n", sock);

  sock_in.sin_family = AF_INET;
  sock_in.sin_port = htons(sourceUDPport);
  sock_in.sin_addr.s_addr =  htonl(INADDR_ANY);

  int status = bind(sourceUDPsocket, (struct sockaddr *)&sock_in, sinlen);
  // printf("[setup_receiveSocket] Bind Status %d, errno: %d\n", status, errno);

  struct timeval tv;
  tv.tv_sec = 0;
  tv.tv_usec = UDP_BURST_TIMEOUT;

  setsockopt(sourceUDPsocket, SOL_SOCKET, SO_RCVTIMEO,&tv,sizeof(tv));

  if(status < 0)
  {
    inputFreeUDPsource();
    fprintf(stderr, "[%s][%s][ERROR] Failed open UDP listening socket\n",__FILE__,__FUNCTION__);    
  }

  flagIsFirst = 1;

  return status;
}

static int inputFreeFileSource()
{
  avformat_close_input(&AVpFormatCtx);
  return 0;
}

static int inputSetupFileSource(unsigned int loop)
{
  /*
   * Only used for debug purposes
   */


  uint8_t extradatasize;
  void *extradata;

  /* Reset timestamp offset */
  if(!loop)
    timestampOffset = 0;
  
  lastPts = 0;
  lastDuration = 0;

  if(avformat_open_input(&AVpFormatCtx, sourceFILEname, NULL, NULL)!=0) {
      fprintf(stderr, "[%s][%s][ERROR] Can't get format\n",__FILE__,__FUNCTION__);
      return -1; // Couldn't open file
  }

  // Retrieve stream information
  if (avformat_find_stream_info(AVpFormatCtx, NULL) < 0) {
      fprintf(stderr, "[%s][%s][ERROR] Couldn't find stream information\n",__FILE__,__FUNCTION__);  
      inputFreeFileSource();    
      return -2; // Couldn't find stream information
  }

  // printf("Format:\n");
  // av_dump_format(AVpFormatCtx, 0, sourceFILEname, 0);

  int ret = av_find_best_stream(AVpFormatCtx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);

  if (ret < 0)
  {
    fprintf(stderr, "[%s][ERROR] Couldn't find best stream\n",__FUNCTION__); 
    inputFreeFileSource();           
    return ret;
  }

  AVvideoStreamIdx = ret;

  AVvideoStream    = AVpFormatCtx->streams[AVvideoStreamIdx];
  AVvideoDecCtx   = AVvideoStream->codec;

  AVframeWidth    = AVvideoStream->codec->width;
  AVframeHeight   = AVvideoStream->codec->height;

  AVtimeBaseNum   = AVvideoStream->time_base.num;
  AVtimeBaseDen   = AVvideoStream->time_base.den;

  extradata       = AVvideoStream->codec->extradata;
  extradatasize   = AVvideoStream->codec->extradata_size;
  fpsscale        = AVvideoStream->r_frame_rate.den;
  fpsrate         = AVvideoStream->r_frame_rate.num;

  fprintf(stderr,"[%s][%s] Rate %d scale %d time base %d %d\n",
      __FILE__,
      __FUNCTION__,
      AVvideoStream->r_frame_rate.num,
      AVvideoStream->r_frame_rate.den,
      AVvideoStream->time_base.num,
      AVvideoStream->time_base.den);fflush(stderr);

  AVCodec *codec = avcodec_find_decoder(AVvideoStream->codec->codec_id);

  if (codec)
  {
    fprintf(stderr,"[%s][%s] Codec name %s\n",__FILE__,__FUNCTION__,codec->name);fflush(stderr);
  }
  
  flagIsFirst = 1;
  
  return 0;
}

static int inputRewindFileSource()
{
  fprintf(stderr, "[%s][%s] Input needs rewinding\n",__FILE__,__FUNCTION__); fflush(stderr);                    

  timestampOffset += lastPts*AVtimeBaseNum*1000/AVtimeBaseDen+1000*fpsscale/fpsrate;
  inputFreeFileSource();
  inputSetupFileSource(1);
  return 0;
}

void *inputThread(void *param)
{
  av_register_all();

  AVPacket *pkt = calloc(sizeof(AVPacket), 1);

  AVbsfc = av_bitstream_filter_init("h264_mp4toannexb");

  fprintf(stderr, "[%s][%s] Entering loop\n",__FILE__,__FUNCTION__); fflush(stderr);

  while(!flagQuit)
  {
    int nbRounds = 0;    

    /*
     * Wait for a source to be available
     */
    pthread_mutex_lock(&sourceMutex);

      /*
       * If new source available, load new stream
       */
      if(flagSourceChanged)
      {
        fprintf(stderr, "[%s][%s] Source changed! \n",__FILE__,__FUNCTION__); fflush(stderr);

        flagSourceChanged = 0;        

        int newSourceSuccess;

        switch(sourceType)
        {
          case SOURCE_FILE:
            newSourceSuccess = inputSetupFileSource(0);
          break;
          case SOURCE_UDP:
            newSourceSuccess = inputSetupUDPsource(sourceUDPport);
          break;
        }

        fprintf(stderr, "[%s][%s] Input set up \n",__FILE__,__FUNCTION__); fflush(stderr);        

        if(newSourceSuccess < 0)
        {
          fprintf(stderr, "[%s][%s][ERROR] Can't set new source\n",__FILE__,__FUNCTION__);
          flagSourceLoaded = 0;          
        }
        else
        {
          flagSourceLoaded = 1;
          iframeBufferPos = 0;
          gettimeofday(&sourceTimestampBegin,NULL);
        }
      }

      /*
       * If nothing available, wait for source change, trigged by the control stream.
       */
      if(!flagSourceLoaded)
      {
        while(!flagSourceChanged && !flagQuit)
        {
          fprintf(stdout, "[%s][%s] Here am I, still waiting...\n",__FILE__,__FUNCTION__);          
          pthread_cond_wait(&sourceCond,&sourceMutex);
        }
      }

    pthread_mutex_unlock(&sourceMutex);

    if(flagQuit)
      break;

    fprintf(stderr, "[%s][%s] Reading frame \n",__FILE__,__FUNCTION__); fflush(stderr);            

    /*
     * At this point, the source must be loaded
     */
    if(!flagSourceLoaded)
    {
      fprintf(stderr, "[%s][%s][ERROR] Source not loaded\n",__FILE__,__FUNCTION__);
      continue;
    }

    if(sourceType == SOURCE_FILE)
    {
        /*
         * If file source is EOF, rewind or pause playback
         */
        if(av_read_frame(AVpFormatCtx, pkt) < 0)
        {
          if(optionLoop)
          {
            inputRewindFileSource();
            av_read_frame(AVpFormatCtx, pkt);
          }
          else
          {
            avformat_close_input(&AVpFormatCtx);
            flagSourceLoaded = 0;
            continue;
          }
        }


        fprintf(stderr, "[%s][%s] Read from file %d,%d,%d\n",__FILE__,__FUNCTION__,pkt->size,pkt->stream_index,pkt->pts); fflush(stderr);                    

        unsigned int origStampDTS = pkt->dts;
        unsigned int origStampPTS = pkt->pts;

        if (pkt->stream_index != AVvideoStreamIdx)
          continue;        

        if (!(
          pkt->data[0] == 0x00 && 
          pkt->data[1] == 0x00 && 
          pkt->data[2] == 0x00 && 
          pkt->data[3] == 0x01))
        {
          pkt = filter(AVbsfc,AVvideoStream, pkt);
          fprintf(stderr, "[%s][%s] Filtered %d,%d,%d\n",__FILE__,__FUNCTION__,pkt->size,pkt->stream_index,pkt->pts); fflush(stderr);                              
        }

        pkt->pts = origStampPTS;
        pkt->dts = origStampDTS;

        lastDuration = pkt->pts-lastPts;
        lastPts = pkt->pts;

        unsigned int packetTimestampMs = pkt->pts*AVtimeBaseNum*1000/AVtimeBaseDen+timestampOffset;

        fprintf(stderr, "[%s][%s] Got time %d, duration \n",__FILE__,__FUNCTION__,packetTimestampMs,lastDuration); fflush(stderr);                                    

        struct timeval timestampNow;
        gettimeofday(&timestampNow,NULL);                                 

        unsigned int timeElapsedSinceBeginningMs = (timestampNow.tv_sec-sourceTimestampBegin.tv_sec)*1000+(timestampNow.tv_usec-sourceTimestampBegin.tv_usec)/1000;                                


        fprintf(stderr,"[%s][%s] Time since begin,packet: %d,%d\n",__FILE__,__FUNCTION__,timeElapsedSinceBeginningMs,packetTimestampMs);fflush(stderr);    

        if(packetTimestampMs > timeElapsedSinceBeginningMs)
        {
          fprintf(stderr,"[%s][%s] Sleeping %d\n",__FILE__,__FUNCTION__,(packetTimestampMs-timeElapsedSinceBeginningMs)*1000);fflush(stderr);            
          usleep((packetTimestampMs-timeElapsedSinceBeginningMs)*1000);
        }

        /* Copy packet data into buffer */
        if(pkt->size > IFRAME_BUFFER_LENGTH)
        {
          fprintf(stderr, "[%s][%s][ERROR] packet size is too big: %d \n",__FILE__,__FUNCTION__,pkt->size);
          continue;  
        }

        fprintf(stderr, "[%s][%s] FILE source copying a frame of %d bytes \n",__FILE__,__FUNCTION__,pkt->size);fflush(stderr);

        memcpy(iframeBuffer,pkt->data,pkt->size);
        iframeBufferPos = pkt->size;

    }
    else if( sourceType == SOURCE_UDP)
    {
      /*
       * In UDP source mode, we get raw H264 frames directly from UDP. If the buffer is empty (it should) we get every pending frame
       */
      int readBytes;

      struct sockaddr_in recv_sock;
      socklen_t receiveSockaddrLen = sizeof(recv_sock);          

      /* 
       * Get all pending data in temporary buffer
       */
      do
      {
        readBytes = recvfrom(
          sourceUDPsocket,
          iframeBuffer+iframeBufferPos,
          IFRAME_BUFFER_LENGTH-iframeBufferPos,
          0,
          (struct sockaddr *)&recv_sock,
          &receiveSockaddrLen);

        if(readBytes != -1) // Nothing to read and nothing already read
        {
          nbRounds++;
          iframeBufferPos += readBytes;
        }

      }
      // Loop while there is no error or something in the buffer
      while(readBytes != -1 || errno != EAGAIN);
        
    }

    fprintf(stderr, "[%s][%s] %d \n",__FILE__,__FUNCTION__,iframeBufferPos);fflush(stderr);    

    /*
     * If we have data...
     */
    if(iframeBufferPos > 0)
    {
      fprintf(stderr, "[%s][%s] UDP source: Now we have %d bytes in %d rounds \n",__FILE__,__FUNCTION__,iframeBufferPos,nbRounds);fflush(stderr);

      pthread_mutex_lock(&newDataAvailableMutex);

      /*
       * .. notify the player thread, which is potentially waiting...
       */
      fprintf(stderr, "[%s][%s] Data available, signaling \n",__FILE__,__FUNCTION__);fflush(stderr);
      
      flagNewDataAvailable = 1;
      pthread_cond_broadcast(&newDataAvailableCond);

      fprintf(stderr, "[%s][%s] Done \n",__FILE__,__FUNCTION__);fflush(stderr);

      /*
       * ... and wait for it to consume the data, setting the flag flagNewDataAvailable to false
       */
      while(flagNewDataAvailable != 0)
      {
        fprintf(stderr, "[%s][%s] Here am I, still waiting...\n",__FILE__,__FUNCTION__);fflush(stderr);

        pthread_cond_wait(&newDataAvailableCond,&newDataAvailableMutex);

        fprintf(stderr, "[%s][%s] Wait loop\n",__FILE__,__FUNCTION__);fflush(stderr);              
      }

      fprintf(stderr, "[%s][%s] Done waiting...\n",__FILE__,__FUNCTION__);fflush(stderr);      

      iframeBufferPos = 0;
      flagIsFirst = 0;

      av_free_packet(pkt);

      pthread_mutex_unlock(&newDataAvailableMutex);
    }
    /*
     * Else loop again, and try to get some from file or 
     */
  }

  /*
   * Exit routine
   */
  if(flagSourceLoaded)
  {
    switch(sourceType)
    {
      case SOURCE_UDP:
        inputFreeUDPsource();
      break;
      case SOURCE_FILE:
        inputFreeFileSource();
      break;
    }
  }
  return NULL;
}
