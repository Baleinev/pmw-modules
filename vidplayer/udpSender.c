/*
*/
#include <stdio.h> //printf
#include <string.h> //memset
#include <stdlib.h> //exit(0);
#include <arpa/inet.h>
#include <sys/socket.h>
#include <linux/random.h>
 
#define SERVER "192.168.1.123"
#define BUFLEN 512
#define PORT 4243
#define USLEEP 1000000
 
void die(char *s)
{
    perror(s);
    exit(1);
}
 
int main(int charc,char *argv[])
{
    struct sockaddr_in si_other;
    int s, i, slen=sizeof(si_other);
    char message[BUFLEN];
 
    if ( (s=socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1)
    {
        die("socket");
    }
 
    memset((char *) &si_other, 0, sizeof(si_other));
    si_other.sin_family = AF_INET;
    si_other.sin_port = htons(PORT);
     
    if (inet_aton(SERVER , &si_other.sin_addr) == 0) 
    {
        fprintf(stderr, "inet_aton() failed\n");
        exit(1);
    }
 
    while(1)
    {

FILE *fp;
fp = fopen("/dev/urandom", "r");
fread(&message, 1, BUFLEN, fp);
fclose(fp);        
         
        //send the message
        if (sendto(s, message, strlen(message) , 0 , (struct sockaddr *) &si_other, slen)==-1)
        {
            die("sendto()");
        }
         
        usleep(USLEEP);
    }
    close(s);
    return 0;
}