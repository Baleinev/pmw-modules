/**
 * The role of this thread is to make a single raw H264 frame available to other threads, through
 * the inputGetNextFrame function, at the correct rate.
 */
#define _GNU_SOURCE             /* See feature_test_macros(7) */
#include <sched.h>
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
#include "circularBufferCollection.h"

/*
 * The buffers collection
 */
CircularBufferCollection *bufferArray;

// char iframeBuffer[IFRAME_BUFFER_COUNT][FRAME_BUFFER_LENGTH];
// unsigned int iframeWriteBufferPos[IFRAME_BUFFER_COUNT];
// unsigned int iframeReadBufferPos[IFRAME_BUFFER_COUNT];
// unsigned int iframeBufferStatus[IFRAME_BUFFER_COUNT];
// unsigned int currentBuffer = 0;


extern unsigned int udpWait;
unsigned int frameBufferCount = DEFAULT_FRAME_BUFFER_COUNT;

float roundMean = -1;
float sizeMean = -1;

unsigned int nbMerge = 0;
unsigned int nbSplit = 0;

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
// AVBitStreamFilterContext *AVbsfc = NULL;
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


// static AVPacket *filter(AVBitStreamFilterContext *AVbsfc,AVStream *in, AVPacket *rp)
// {
//   AVPacket *p;
//   AVPacket *fp;
  
//   int rc;

//   if(AVbsfc)
//   {
//     fp = calloc(sizeof(AVPacket), 1);

//     rc = av_bitstream_filter_filter(AVbsfc,in->codec,NULL, &(fp->data), &(fp->size),rp->data, rp->size,rp->flags);
    
//     if (rc > 0) 
//     {
//       av_free_packet(rp);
//       fp->destruct = av_destruct_packet;
//       p = fp;
//     }
//     else
//     {
//       DBG( "[ERROR] Failed to filter packet");
//       p = rp;
//     }
//   }
//   else
//   {
//     ERR("No filter context ");
//     p = rp;
//   }
  
//   return p;
// }

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
  // printf("[setup_receiveSocket] Sock Status %d", sock);

  sock_in.sin_family = AF_INET;
  sock_in.sin_port = htons(sourceUDPport);
  sock_in.sin_addr.s_addr =  htonl(INADDR_ANY);

  int status = bind(sourceUDPsocket, (struct sockaddr *)&sock_in, sinlen);
  // printf("[setup_receiveSocket] Bind Status %d, errno: %d", status, errno);

  struct timeval tv;
  tv.tv_sec = 0;
  tv.tv_usec = udpWait;

  if (setsockopt(sourceUDPsocket, SOL_SOCKET, SO_RCVTIMEO,&tv,sizeof(tv)) < 0) {
    ERR("[ERROR] Failed to set socket to non-blocking");    
  }  

  int n = 0;

  // setsockopt(sourceUDPsocket, SOL_SOCKET, SO_RCVBUF,&n,sizeof(n));

  // int UdpBufSize = 0;
  socklen_t optlen = sizeof(n);
  // setsockopt(socket, SOL_SOCKET, SO_RCVBUF, &UdpBufSize, optlen);  
  
  // DBG(" Current UDP size %d",n);    

  if (setsockopt(sourceUDPsocket, SOL_SOCKET, SO_RCVBUF, &n, optlen) < 0) {
    ERR("[ERROR] Failed to discard UDP packets. ERRNO:%d",errno);    
  }  

  n = UDP_BUFFER_LENGTH;

  if (setsockopt(sourceUDPsocket, SOL_SOCKET, SO_RCVBUF, &n, optlen) < 0) {
    ERR("[ERROR] Failed to expand UDP buffer. ERRNO:%d",errno);    
  }  

  if(status < 0)
  {
    inputFreeUDPsource();
    ERR("[ERROR] Failed open UDP listening socket");    
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
      ERR( "[ERROR] Can't get format");
      return -1; // Couldn't open file
  }

  // Retrieve stream information
  if (avformat_find_stream_info(AVpFormatCtx, NULL) < 0) {
      ERR( "[ERROR] Couldn't find stream information");  
      inputFreeFileSource();    
      return -2; // Couldn't find stream information
  }

  // printf("Format:");
  // av_dump_format(AVpFormatCtx, 0, sourceFILEname, 0);

  int ret = av_find_best_stream(AVpFormatCtx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);

  if (ret < 0)
  {
    ERR( "[%s][ERROR] Couldn't find best stream",__FUNCTION__); 
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

  DBG(" Rate %d scale %d time base %d %d",
      __FILE__,
      __FUNCTION__,
      AVvideoStream->r_frame_rate.num,
      AVvideoStream->r_frame_rate.den,
      AVvideoStream->time_base.num,
      AVvideoStream->time_base.den);

  AVCodec *codec = avcodec_find_decoder(AVvideoStream->codec->codec_id);

  if (codec)
  {
    DBG(" Codec name %s",codec->name);
  }
  
  flagIsFirst = 1;
  
  return 0;
}

static int inputRewindFileSource()
{
  DBG( " Input needs rewinding");                     

  timestampOffset += lastPts*AVtimeBaseNum*1000/AVtimeBaseDen+1000*fpsscale/fpsrate;
  inputFreeFileSource();
  inputSetupFileSource(1);
  return 0;
}

void *inputThread(void *param)
{
  cpu_set_t affinity;

  CPU_ZERO(&affinity);
  CPU_SET(0,&affinity);

  if(pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &affinity) != 0)
  {
    ERR("Cannot set affinity. errno:%d",errno);
  }

  struct timeval last,now,lastDecodedFrameTime;

  av_register_all();

  AVPacket *pkt = calloc(sizeof(AVPacket), 1);

  // AVbsfc = av_bitstream_filter_init("h264_mp4toannexb");

  // memset(0,iframeWriteBufferPos,sizeof(iframeWriteBufferPos));
  // memset(0,iframeReadBufferPos,sizeof(iframeReadBufferPos));
  // memset(0,iframeBufferStatus,sizeof(iframeBufferStatus));

  LOG("Entering loop"); 

  while(!flagQuit)
  {
    int nbRounds = 0;    

    /*
     * Wait for a source to be available
     */
    pthread_mutex_lock(&sourceMutex);

      DBG(" Loop marker"); 

      unsigned int currentBufferIndex = bufferArray->writeIndex;
      unsigned int *currentBufferCurrentSize = &(bufferArray->buffers[currentBufferIndex]->currentSize);  

      *currentBufferCurrentSize = 0;    

      DBG(" Loop marker"); 

      /*
       * If new source available, load new stream
       */
      if(flagSourceChanged)
      {
        DBG(" Source changed! "); 

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

        DBG(" Input set up ");         

        if(newSourceSuccess < 0)
        {
          DBG("[ERROR] Can't set new source");
          flagSourceLoaded = 0;          
        }
        else
        {
          flagSourceLoaded = 1;
          gettimeofday(&sourceTimestampBegin,NULL);
        }
      }

      /*
       * If nothing available, wait for source change, trigged by the control stream.
       */
      if(!flagQuit && !flagSourceLoaded)
      {
        while(!flagSourceChanged && !flagQuit)
        {
          DBG(stdout, " Here am I, still waiting...");          
          pthread_cond_wait(&sourceCond,&sourceMutex);
        }
      }

    pthread_mutex_unlock(&sourceMutex);

    if(flagQuit)
      break;

    DBG(" Reading frame ");             

    /*
     * At this point, the source must be loaded
     */
    if(!flagSourceLoaded)
    {
      DBG("[ERROR] Source not loaded");
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

      DBG(" Read from file %d,%d,%d",pkt->size,pkt->stream_index,pkt->pts);                     

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
        // pkt = filter(AVbsfc,AVvideoStream, pkt);
        DBG(" Filtered %d,%d,%d",pkt->size,pkt->stream_index,pkt->pts);                               
      }

      pkt->pts = origStampPTS;
      pkt->dts = origStampDTS;

      lastDuration = pkt->pts-lastPts;
      lastPts = pkt->pts;

      unsigned int packetTimestampMs = pkt->pts*AVtimeBaseNum*1000/AVtimeBaseDen+timestampOffset;

      DBG(" Got time %d, duration ",packetTimestampMs,lastDuration);                                     

      struct timeval timestampNow;
      gettimeofday(&timestampNow,NULL);                                 

      unsigned int timeElapsedSinceBeginningMs = (timestampNow.tv_sec-sourceTimestampBegin.tv_sec)*1000+(timestampNow.tv_usec-sourceTimestampBegin.tv_usec)/1000;                                


      DBG(" Time since begin,packet: %d,%d",timeElapsedSinceBeginningMs,packetTimestampMs);    

      if(packetTimestampMs > timeElapsedSinceBeginningMs)
      {
        DBG(" Sleeping %d",(packetTimestampMs-timeElapsedSinceBeginningMs)*1000);            
        usleep((packetTimestampMs-timeElapsedSinceBeginningMs)*1000);
      }

      /* Copy packet data into buffer */
      if(pkt->size > FRAME_BUFFER_LENGTH)
      {
        DBG("[ERROR] packet size is too big: %d ",pkt->size);
        continue;  
      }

      DBG(" FILE source copying a frame of %d bytes ",pkt->size);

      memcpy(bufferArray->buffers[currentBufferIndex]->data,pkt->data,pkt->size);
      *currentBufferCurrentSize = pkt->size;

      // iframeWriteBufferPos[currentBuffer] = pkt->size;
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
      gettimeofday(&now,NULL);

      DBG("\nTime delta pre recv: %d",(now.tv_sec-last.tv_sec)*1000000+(now.tv_usec-last.tv_usec));

      do
      {
        if(FRAME_BUFFER_LENGTH - *currentBufferCurrentSize < 2000)
        {
          ERR("[ERROR] Error no space left on buffer");
          break;
        }

        readBytes = recvfrom(
          sourceUDPsocket,
          bufferArray->buffers[currentBufferIndex]->data + *currentBufferCurrentSize,
          FRAME_BUFFER_LENGTH - *currentBufferCurrentSize,
          0,
          (struct sockaddr *)&recv_sock,
          &receiveSockaddrLen);

        if(readBytes != -1) // Nothing to read and nothing already read
        {
          nbRounds++;
          DBG(" Written %d bytes at index %d ",readBytes,*currentBufferCurrentSize);
          *currentBufferCurrentSize = *currentBufferCurrentSize + readBytes;
        }
        // if(readBytes != UDP_MAX_SIZE)
        // {
        //   DBG(" Last one %d ",readBytes);          
        //   break;
        // }
      }
      // Loop while there is no error or something in the buffer
      while(readBytes > 0 || errno != EAGAIN);

      DBG(" errno %d ",errno == EAGAIN);          

      gettimeofday(&now,NULL);

      if(*currentBufferCurrentSize > 0)
      {
        if(roundMean < 0)
          roundMean = nbRounds;

        if(sizeMean < 0)
          sizeMean = *currentBufferCurrentSize;

        roundMean = roundMean*0.95+nbRounds*0.05;
        sizeMean = sizeMean*0.95+*currentBufferCurrentSize*0.05;

        if(fabs(roundMean - nbRounds) > roundMean*0.7)
        {
          if(nbRounds>roundMean)
            nbMerge++;
          else
            nbSplit++;
        }

        // LOG("\r UDP source: Now we have %d bytes in %d rounds ",*currentBufferCurrentSize,nbRounds);

        // LOG("\rUDP log: roundMean: %.2f sizeMean: %.2f nbMerge: %d nbSplit: %d This nbRounds:%d, size:%d bufferCount:%d",
        //   roundMean,
        //   sizeMean,
        //   nbMerge,
        //   nbSplit,
        //   nbRounds,
        //   *currentBufferCurrentSize,
        //   bufferArray->currentCount+1);
      }

      DBG("Time delta post recv: %d (rounds:%d,bytes:%d)",(now.tv_sec-last.tv_sec)*1000000+(now.tv_usec-last.tv_usec),nbRounds,*currentBufferCurrentSize);

      last.tv_sec = now.tv_sec;
      last.tv_usec = now.tv_usec;
    }

    DBG(" %d ",*currentBufferCurrentSize);    

    /*
     * If we have read any data in this round...
     */
    if(*currentBufferCurrentSize > 0)
    {
      pthread_mutex_lock(&newDataAvailableMutex);

      bufferArray->writeIndex = (bufferArray->writeIndex + 1) % bufferArray->count;
      bufferArray->currentCount++;

      /*
       * and the buffers are all full... notify the player thread, which is potentially waiting...
       */
      if(isBufferCircularCollectionFull(bufferArray)/* && flagNewDataAvailable == 0*/)
      {
        DBG(" Data available, signaling ");
      
        flagNewDataAvailable = 1;
        pthread_cond_broadcast(&newDataAvailableCond);

        DBG(" Done ");

        /*
         * ... and wait for it to consume the data, setting the flag flagNewDataAvailable to false
         */
        while(!flagQuit && flagNewDataAvailable != 0)
        {
          DBG(" Here am I, still waiting...");

          pthread_cond_wait(&newDataAvailableCond,&newDataAvailableMutex);

          DBG(" Wait loop");              
        }

        gettimeofday(&lastDecodedFrameTime,NULL);        

        DBG(" Done waiting...");      

        flagIsFirst = 0;

        // LOG("direct");      

        av_free_packet(pkt);

        
      }
      else
      {
        DBG(" Data available, not signaling ");
      }

      pthread_mutex_unlock(&newDataAvailableMutex);
    }
    /*
     * else if the packet is late, and we have a buffer, allow it to be consummed
     */
    // else if((now.tv_sec - lastDecodedFrameTime.tv_sec)*1000000+(now.tv_usec - lastDecodedFrameTime.tv_usec) > 30000)
    // {
    //   LOG("late");

    //   pthread_mutex_lock(&newDataAvailableMutex);

    //   if(!isBufferCircularCollectionEmpty(bufferArray))
    //   {
    //     DBG(" Data available, signaling ");
      
    //     flagNewDataAvailable = 1;
    //     pthread_cond_broadcast(&newDataAvailableCond);

    //     DBG(" Done ");

        
    //      * ... and wait for it to consume the data, setting the flag flagNewDataAvailable to false
         
    //     while(flagNewDataAvailable != 0)
    //     {
    //       DBG(" Here am I, still waiting...");

    //       pthread_cond_wait(&newDataAvailableCond,&newDataAvailableMutex);

    //       DBG(" Wait loop");              
    //     }

    //     gettimeofday(&lastDecodedFrameTime,NULL);        
    //   }
      
    //   pthread_mutex_unlock(&newDataAvailableMutex);
    // }
    // else
    // {
    //   LOG("nope");      
    // }
      // 
    /*
     * Else loop again, and try to get some data from file or UDP
     */
  }

  LOG("Exiting loop"); 

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
