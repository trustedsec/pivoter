#ifndef PROXY_H_INCLUDED
#define PROXY_H_INCLUDED
#include "writeQueue.h"
#include "sockethost.h"

#define RELAY_OPEN 16384 

#define PROXY_WRITE 3
#define RELAY_WRITE 3
#define RELAY_CLOSE 5
#define PROXY_CLOSE 5
#define RELAY_DEAD 6
#define PROXY_CONNECT 1
#define RELAY_CONNECT 2
#define PROXY_HOSTBYNAME 20
#define RELAY_HOSTBYNAME 20

typedef struct _in {
	socketHost* hosts;
	HANDLE hostLock;
	writeQnode* writeQueue;
	HANDLE writeQlock;
	int mtu;
} sharedData;

typedef struct _embryo {
	sharedData* shared;
	writeQnode* message;
} embryo;

int connectBroker(char* ip, char* port, SOCKET* broker);
int disconnectBroker(SOCKET* broker);
__inline void clearFDs(fd_set* readfds, fd_set* writefds, fd_set* exfds, SOCKET* single);
void proxyConnect(writeQnode* message, SOCKET* sock);
void hostByname(writeQnode* message);
__inline int readMessage(SOCKET* sock, writeQnode* message);
__inline int writeMessage(SOCKET* sock, writeQnode *message);
void readWriteHost(socketHost** hosts, socketHost* client, HANDLE lock, writeQnode** writeQueue, HANDLE writeLock, void* mtu);
void cleanHosts(socketHost** hosts, socketHost* client, HANDLE lock, writeQnode** writeQueue, HANDLE writeLock, void* nul);
DWORD WINAPI clientProcess(LPVOID in);
int brokerProcess(SOCKET* broker, sharedData* shared);
int newHost(socketHost** hosts, HANDLE lock, UINT32 pid, UINT32 fd, int status, SOCKET sock);
DWORD WINAPI AsyncConnect(LPVOID in);

#endif