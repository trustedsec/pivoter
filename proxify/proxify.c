#define _GNU_SOURCE
#include <stdio.h> 
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <sys/stat.h>
#include <netdb.h>
#include <dlfcn.h>
#include <linux/limits.h>
#include <unistd.h> 
#include <fcntl.h>  
#include <netinet/in.h>
#include <arpa/inet.h>
#include "proxify.h"

typedef struct {
  uint16_t command;
  uint32_t pid;
  uint32_t fd;
  uint32_t length;
  uint8_t* message;
} message;

/*TLS*/
static __thread struct hostent DNSinfo;
static __thread char DNS_name[255];
static __thread uint32_t DNS_addrs[8];
static __thread uint32_t* DNS_addr_x[9];

/* helpers */
static void debug(const char* buffer) {
  if (strncmp(getenv("PROXIFY_DEBUG"), "YES", 3) == 0) { fprintf(stderr, "%s\n", buffer); }
};

int aToPort(const char *service, const char *proto) {
  int port;
  long int lport;
  struct servent *serv;
  char *errpos;
  
  if ( service == NULL ) { return 0; }      
  serv = getservbyname(service, proto);
  if (serv != NULL) { port = serv->s_port; } else { /* numeric? */
    lport = strtol(service,&errpos,0);
    if ( (errpos[0] != 0) || (lport < 1) || (lport > 65535) ) { return -1;} /* Invalid port address */
    port = htons(lport);
  }  
  return port;
};

int AddrInfoAlloc(struct addrinfo** out) {
  struct sockaddr* addr;
  struct addrinfo* info;
  char* canonname;
  info = (struct addrinfo*)malloc(sizeof(struct addrinfo));
  addr = (struct sockaddr*)malloc(sizeof(struct sockaddr));
  canonname = malloc(255);
  if ((addr == NULL) || (info == NULL) || ( canonname == NULL)) { free(addr); free(info); free(canonname); return 0; }
  memset(info, 0, sizeof(struct addrinfo));
  memset(addr, 0, sizeof(struct sockaddr));
  memset(canonname, 0, 255);
  info->ai_canonname = canonname;
  info->ai_addr = addr;
  info->ai_family = AF_INET;
  info->ai_addrlen = 4;
  info->ai_socktype = SOCK_STREAM;
  info->ai_protocol = IPPROTO_TCP;
  *out = info;
  return 1;
};

static void getBrokerAddr(struct sockaddr_in *addr) {
  char* address;
  char* port;
  if (NULL == getenv("PROXIFY_ADDR")) { 
    debug("Must set environment PROXIFY_ADDR");
    abort();
    } else { address = getenv("PROXIFY_ADDR"); }
  if (NULL == getenv("PROXIFY_PORT")) { 
    debug("Must set environment PROXIFY_PORT");
    abort();
  } else { port = getenv("PROXIFY_PORT"); }
  addr->sin_port = htons(atoi(port));
  addr->sin_family = AF_INET;
  if ( 0 == inet_aton(address, &addr->sin_addr)) { debug("bad PROXIFY_ADDRESS value"); abort();}
};


static int sendMessage(uint16_t cmd, uint32_t fd, uint32_t len, const void* ptr){
  int sock = fd, bytes = 0;
  uint32_t pid = getpid();
  bytes += write(sock, &cmd, sizeof(cmd));
  bytes += write(sock, &fd, sizeof(fd));
  bytes += write(sock, &pid, sizeof(pid));
  bytes += write(sock, &len, sizeof(len));
  if (len > 0) { bytes += write(sock, ptr, len); }
  bytes = bytes - 14;
  return bytes;  
}; 

static uint32_t proxyConnect(uint32_t fd, uint32_t pid, struct in_addr addr, in_port_t port) {
  fd_set read_fd;
  int cnt = 0; 
  uint32_t err = 6;
  uint16_t cmd = PROXY_CONNECT;
  struct timeval t;
  
  t.tv_sec = 60;
  t.tv_usec = 5;
  write(fd, &cmd, sizeof(cmd));
  write(fd, &fd, sizeof(fd));
  write(fd, &pid, sizeof(pid));
  write(fd, &err, sizeof(err));
  write(fd, &addr, sizeof(addr));
  write(fd, &port, sizeof(port));
  FD_ZERO(&read_fd);
  err = 0;
  while (!(FD_ISSET(fd, &read_fd))) {
    cnt += 1;
    FD_ZERO(&read_fd);
    FD_SET(fd, &read_fd);
    select(fd + 1, &read_fd, NULL, NULL, &t);
    if ( cnt == 60) { err = ETIMEDOUT; break;}
  }
  if ( err == 0 ) { read(fd, &err, sizeof(err)); }
  debug("PROXYCONNECT: broker response read");
  if ( err == 0 ) { return 0; }
  errno = err;
  return -1;  
};  

/* Original functions definitions */
static int o_connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
  int (*orig_connect)(int fd, const struct sockaddr *addr, socklen_t addrlen);
  orig_connect = dlsym(RTLD_NEXT, "connect");
  return (*orig_connect)(sockfd, addr, addrlen); 
};

static int o_getaddrinfo(const char *node, const char *service, const struct addrinfo *hints, struct addrinfo **res) {
 int (*orig_getaddrinfo)(const char* node, const char* service, const struct addrinfo* hints, struct addrinfo** res);
 orig_getaddrinfo = dlsym(RTLD_NEXT, "getaddrinfo");
 return (*orig_getaddrinfo)(node, service, hints, res);
};

static int o_getnameinfo (__const struct sockaddr *__restrict sa, socklen_t salen, char *__restrict host,
                          socklen_t hostlen, char *__restrict serv, socklen_t servlen, unsigned int flags) {
 int (*orig_getnameinfo)(__const struct sockaddr *__restrict sa, socklen_t salen, char *__restrict host,   
                           socklen_t hostlen, char *__restrict serv, socklen_t servlen, unsigned int flags);
 orig_getnameinfo = dlsym(RTLD_NEXT, "getnameinfo");
 return (*orig_getnameinfo)(sa, salen, host, hostlen, serv, servlen, flags);                                                   
};                          

/* helpers needing originals calls */
static int connectBroker(int* fd) {
  struct sockaddr_in broker_addr;
  getBrokerAddr(&broker_addr);
  *fd = socket(AF_INET, SOCK_STREAM, 0);
  if (*fd < 0) { debug("connectBroker failed no socket"); return 0; }
  if( o_connect(*fd, (const struct sockaddr *)&broker_addr, sizeof(broker_addr)) != 0) { debug("connectBroker failed."); return 0; }
  return -1; 
};

/* sockets API */
int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
  struct sockaddr_in *conaddr = (struct sockaddr_in *) addr;
  struct sockaddr_in broker;
  int sock_type = 0;
  if (conaddr->sin_family != AF_INET) { return o_connect(sockfd, addr, addrlen); } /* do nothing */
  getsockopt(sockfd, SOL_SOCKET, SO_TYPE, (void *) &sock_type, &addrlen);
  if (sock_type != SOCK_STREAM) { return o_connect(sockfd, addr, addrlen); } /* do nothing */
  debug("CONNECT: atempting proxy connection");
  getBrokerAddr(&broker);
  sock_type = o_connect(sockfd, (const struct sockaddr *) &broker, sizeof(broker));
  if ( 0 != sock_type) { return sock_type; }
  return proxyConnect(sockfd, getpid(), conaddr->sin_addr, conaddr->sin_port);  
};

int getaddrinfo(const char *host, const char* service, const struct addrinfo* hints, struct addrinfo** res) {
  struct addrinfo* result=NULL;
  struct sockaddr_in* addr;
  int broker, flags = 0;
  int len = strnlen(host, 254) + 1;
  uint32_t dns[8];
  /* stub this call with gethostbyname and getservicebyname */
  /* will use the local services as there isn't usually useful informantion on the far end */
  debug("GETADDRINFO: called");
  *res = NULL;
  if (hints != NULL) {
    if (!(( hints->ai_family == AF_INET) || (hints->ai_family == AF_UNSPEC))) { return o_getaddrinfo(host, service, hints, res); }
    if (!(( hints->ai_socktype == 0) || (hints->ai_socktype == SOCK_STREAM))) { o_getaddrinfo(host, service, hints, res); }
    if ((host == NULL) && (AI_PASSIVE == (AI_PASSIVE & hints->ai_flags))) { o_getaddrinfo(host, service, hints, res); }
    flags = hints->ai_flags; 
  }
  if (inet_aton(host, (struct in_addr*)dns)) { flags = AI_NUMERICHOST; } /*minimize calls to proxy*/
  if ((AI_NUMERICHOST == (AI_NUMERICHOST & flags)) || (host == NULL)) {   
    if (AddrInfoAlloc(&result)) {
      if ( host != NULL ) { strncpy(result->ai_canonname, host, 254); } else { strcpy(result->ai_canonname, "127.0.0.1"); }
      addr = (struct sockaddr_in*)result->ai_addr;
      if (inet_aton(result->ai_canonname, &addr->sin_addr) == 0) { return EAI_NONAME; }
      addr->sin_family = AF_INET;
      if (service != NULL) { addr->sin_port = aToPort(service, "tcp");}    
    } else {return EAI_MEMORY; }
    *res = result;
    return 0;
  }
  debug("GETADDRINFO: connecting broker");
  if ( -1 != connectBroker(&broker)) { return EAI_FAIL; }
  if (len != sendMessage(PROXY_HOSTBYNAME, broker, len, host)) { return EAI_FAIL; }
  read(broker, dns, sizeof(uint32_t) * 8);
  debug("GETADDRINFO: broker response read");
  close(broker);
  if (dns[0] == 0) { return EAI_NONAME; }
  len = 0;
  while ((dns[len] != 0) && (len < 8)) {
   if (AddrInfoAlloc(&result)) {
     addr = (struct sockaddr_in*)result->ai_addr;
     addr->sin_family = AF_INET;
     if (service != NULL) { addr->sin_port = aToPort(service, "tcp");}
     memcpy(&addr->sin_addr, &dns[len], sizeof(struct in_addr));
     strncpy(result->ai_canonname, host, 254);
     if (len == 0) { *res = result; } else {
       result->ai_next = (*res)->ai_next;
       (*res)->ai_next = result; 
     } 
   }
   len++;
  }
  return 0;
}

struct hostent* gethostbyname(const char *name) {
  int broker;
  int len = strnlen(name, 254) + 1;
  debug("GETHOSTBYNAME: connecting broker");
  if (-1 != connectBroker(&broker)) { errno = NO_RECOVERY; return NULL; }
  if (len != sendMessage(PROXY_HOSTBYNAME, broker, len, name)) { errno = NO_RECOVERY; return NULL; }
  memset(&DNSinfo, 0, sizeof(struct hostent));
  memset(DNS_name, 0, 255);
  memset(DNS_addrs, 0, sizeof(uint32_t) * 8);
  memset(DNS_addr_x, 0, sizeof(uint32_t) * 9); /*ensure the list always terminates*/
  strncpy(DNS_name, name, 254);
  DNSinfo.h_addrtype = AF_INET;
  DNSinfo.h_name = DNS_name;
  DNSinfo.h_length = 4;
  DNSinfo.h_addr_list = DNS_addr_x;
  read(broker, DNS_addrs, sizeof(uint32_t) * 8);
  debug("GETHOSTBYNAME: broker response read");
  close(broker);
  if (DNS_addrs[0] == 0) { errno = HOST_NOT_FOUND; return NULL; }
  len = 0;
  while ((DNS_addrs[len] != 0) && ( len < 8)) { DNS_addr_x[len] = &DNS_addrs[len]; len++; }
  errno = 0;
  return &DNSinfo;
};


int getnameinfo (__const struct sockaddr *__restrict sa, socklen_t salen, char *__restrict host,
                 socklen_t hostlen, char *__restrict serv, socklen_t servlen, unsigned int flags) {
  struct servent* service = NULL;
  struct sockaddr_in* inaddr = (struct sockaddr_in*)sa;
  if (salen != 4) { return o_getnameinfo(sa, salen, host, hostlen, serv, servlen, flags); }
  if (NI_NAMEREQD == (flags & NI_NAMEREQD)) { return EAI_NONAME; }
  if ((host != NULL) && (hostlen != 0)) {
    if (hostlen < 16 ) { host = NULL; } else {
    memset(host, 0, hostlen);
    strncpy(host, inet_ntoa(inaddr->sin_addr), hostlen - 1);
    }
  }
  if ((serv != NULL) && (servlen != 0)) {
    if ( NI_DGRAM == ( flags & NI_DGRAM )) {
      service = getservbyport(inaddr->sin_port,"udp"); } else {
      service = getservbyport(inaddr->sin_port,"tcp");
    }
    memset(serv, 0, servlen);
    if ( service != NULL) { strncpy(serv, service->s_name, servlen - 1); }
  } 
  return 0; 
};

