#ifndef VIDPLAYER_H
#define VIDPLAYER_H

#define DBG(fmt,...) //fprintf(stderr,fmt, ##__VA_ARGS__);fflush(stderr);
#define ERR(fmt,...) fprintf(stderr,fmt, ##__VA_ARGS__);fflush(stderr);
#define LOG(fmt,...) fprintf(stdout,fmt, ##__VA_ARGS__);fflush(stdout);

enum Security_Levels 
{ 
  black_ops, 
  top_secret, 
  secret, 
  non_secret 
};

#endif