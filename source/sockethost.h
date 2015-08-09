#ifndef SOCKETHOST_H_INCLUDED
#define SOCKETHOST_H_INCLUDED
#include "writeQueue.h"

typedef struct _sh {
	struct _sh* L;
	struct _sh* R;
	struct _sh* U;
	int status;
	SOCKET s;
	UINT32 fd;
	UINT32 pid;
	writeQnode* writeQueue;
	HANDLE writeLock;
} socketHost;

typedef void(*fn_hostTask)(socketHost**, socketHost* host, HANDLE, writeQnode**, HANDLE, void*);

void _addTree(socketHost* root, socketHost* newHost);
void deleteHost(socketHost** hosts, HANDLE lock, socketHost* host);
socketHost* _findHost(socketHost* hosts, UINT32 pid, UINT32 fd);
socketHost* findHost(socketHost* hosts, HANDLE lock, UINT32 pid, UINT32 fd);
void _eachHost(socketHost* top, HANDLE lock, socketHost** hosts, writeQnode** wqueue, HANDLE wlock, fn_hostTask fn, void* v);
void eachHost(socketHost** hosts, HANDLE lock, writeQnode** wq, HANDLE wlock, fn_hostTask fn, void* v);
int newHost(socketHost** hosts, HANDLE lock, UINT32 pid, UINT32 fd, int status, SOCKET sock);
#endif