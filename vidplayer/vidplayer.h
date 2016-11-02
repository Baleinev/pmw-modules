#ifndef VIDPLAYER_H
#define VIDPLAYER_H

#define FAIL_DELAY 100

#define DBG(fmt,...) do{/*fprintf(stdout,"[%s][%s][%d]",__FILE__,__FUNCTION__,__LINE__);fprintf(stdout,fmt,##__VA_ARGS__);fprintf(stdout,"\n");fflush(stdout);*/}while(0);
#define ERR(fmt,...) do{fprintf(stderr,"[%s][%s][%d]",__FILE__,__FUNCTION__,__LINE__);fprintf(stderr,fmt,##__VA_ARGS__);fprintf(stderr,"\n");fflush(stderr);}while(0);
#define LOG(fmt,...) do{fprintf(stdout,"[%s][%s][%d]",__FILE__,__FUNCTION__,__LINE__);fprintf(stdout,fmt,##__VA_ARGS__);fprintf(stdout,"\n");fflush(stdout);}while(0);


#endif