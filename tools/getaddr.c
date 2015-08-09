#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <errno.h>
#include <arpa/inet.h>

int
main(int argc,char *argv[]){
    int sock;
    struct addrinfo hints,*res,*list;
    int n;
    int err;
    if(argc != 2){
        fprintf(stderr,"Usage : %s name \n",argv[0]);
        return 1;
    } 
    memset(&hints,0,sizeof(hints));
    hints.ai_family = AF_INET; 
    hints.ai_socktype = SOCK_STREAM;
//    hints.ai_socktype = 0;
    err = getaddrinfo(argv[1],"12345",&hints,&res);
    if(err != 0){
        perror("getaddrinfo");
        printf("getaddrinfo %s\n",strerror(errno));
        printf("getaddrinfo : %s \n",gai_strerror(err));
        return 1;
    }

    struct sockaddr_in *addr;
    list = res;
    do {
        addr = (struct sockaddr_in *)list->ai_addr; 
        printf("inet_ntoa(in_addr)sin = %s\n",inet_ntoa((struct in_addr)addr->sin_addr));
        list = list->ai_next;
    }while( list != NULL);    

    printf("############ finish !! #######\n");
    freeaddrinfo(res);
    return 0;
}

