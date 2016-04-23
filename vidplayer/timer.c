#include <stdio.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/mathematics.h>
#include <libavutil/samplefmt.h>

static signal_recv_count;
unsigned long now;
unsigned long last;

static AVCodecContext *video_dec_ctx = NULL;
static AVStream *video_stream = NULL;
static AVPacket pkt;
AVFormatContext *pFormatCtx = NULL;

uint8_t extradatasize;
void *extradata;

unsigned int fpsscale;
unsigned int fpsrate;
unsigned int time_base_num;
unsigned int time_base_den;

int img_width, img_height;

static int video_stream_idx = -1;

int sock;
int sinlen;
  struct sockaddr_in sock_in;
  int yes = 1;



unsigned long getMicroTime()
{
    struct timeval tv;
    gettimeofday(&tv,NULL);    
    return 1000000 * tv.tv_sec + tv.tv_usec;
}

void sigalrm_handler(int signum)
{
  now = getMicroTime();

  printf("%d\n", now-last);
  signal_recv_count++;

  last = now;

    if(av_read_frame(pFormatCtx, &pkt) >= 0)
    {
        printf("Read pkt %d\n", pkt.size);

        AVPacket orig_pkt = pkt;

        int status = sendto(sock, pkt.data, pkt.size, 0, (struct sockaddr *)&sock_in, sinlen);
        printf("sendto Status = %d\n", status);        

        av_free_packet(&orig_pkt);
    }
}

int setup_broadcastSocket(unsigned int port)
{
  int status, buflen;
  int yes = 1;

  sinlen = sizeof(struct sockaddr_in);
  memset(&sock_in, 0, sinlen);

  sock = socket (PF_INET, SOCK_DGRAM, IPPROTO_UDP);

  sock_in.sin_addr.s_addr = htonl(INADDR_ANY);
  sock_in.sin_port = htons(port);
  sock_in.sin_family = PF_INET;

  status = bind(sock, (struct sockaddr *)&sock_in, sinlen);
  printf("Bind Status = %d\n", status);

  status = setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &yes, sizeof(int) );
  printf("Setsockopt Status = %d\n", status);

  /* -1 = 255.255.255.255 this is a BROADCAST address,
     a local broadcast address could also be used.
     you can comput the local broadcat using NIC address and its NETMASK 
  */ 

  sock_in.sin_addr.s_addr=htonl(-1); /* send message to 255.255.255.255 */
  sock_in.sin_port = htons(port); /* port number */
  sock_in.sin_family = PF_INET;



  // shutdown(sock, 2);
  // close(sock);
}

int setup_demuxer(const char *filename)
{
    // Register all formats and codecs
    av_register_all();

    if(avformat_open_input(&pFormatCtx, filename, NULL, NULL)!=0) {
        fprintf(stderr, "Can't get format\n");
        return -1; // Couldn't open file
    }

    // Retrieve stream information
    if (avformat_find_stream_info(pFormatCtx, NULL) < 0) {
        return -1; // Couldn't find stream information
    }

    printf("Format:\n");
    av_dump_format(pFormatCtx, 0, filename, 0);

    int ret;
    ret = av_find_best_stream(pFormatCtx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);

    if (ret >= 0) {
        video_stream_idx = ret;

        video_stream = pFormatCtx->streams[video_stream_idx];
        video_dec_ctx = video_stream->codec;

        img_width         = video_stream->codec->width;
        img_height        = video_stream->codec->height;
        extradata         = video_stream->codec->extradata;
        extradatasize     = video_stream->codec->extradata_size;
        fpsscale          = video_stream->r_frame_rate.den;
        fpsrate           = video_stream->r_frame_rate.num;
        time_base_num         = video_stream->time_base.num;
        time_base_den         = video_stream->time_base.den;

        printf("Rate %d scale %d time base %d %d extra data size %d\n",
            video_stream->r_frame_rate.num,
            video_stream->r_frame_rate.den,
            video_stream->time_base.num,
            video_stream->time_base.den,
            extradatasize);

        AVCodec *codec = avcodec_find_decoder(video_stream->codec->codec_id);

        if (codec) {
            printf("Codec name %s\n", codec->name);
        }
    }
    return 0;
}

int main(int argc,const char * args[])
{
   now = getMicroTime();

   printf("Open file %s\n",args[1]);

   fflush(0);

   setup_demuxer(args[1]);

   setup_broadcastSocket(atoi(args[2]));

  fflush(0);

  struct itimerval timer={0};
  char a[200];
  /* Initial timeout value */
  timer.it_value.tv_sec = 0;
  timer.it_value.tv_usec = 1;

  /* We want a repetitive timer */
  timer.it_interval.tv_sec = 0;
  timer.it_interval.tv_usec = (unsigned int)((float)video_stream->time_base.den / (float)video_stream->time_base.num);

  /* Register Signal handler
   * And register for periodic timer with Kernel*/
  signal(SIGALRM, &sigalrm_handler);
  setitimer(ITIMER_REAL, &timer, NULL);

  read(2, &a, 199);
}