#include "includes.h"
#include "sockethost.h"

void _addTree(socketHost* root, socketHost* newHost){
	newHost->U = root;
	if (root->pid > newHost->pid) {
		if (root->L == NULL) { root->L = newHost; return; }
		_addTree(root->L, newHost);
	}
	else {
		if (root->R == NULL) { root->R = newHost; return; }
		_addTree(root->R, newHost);
	}
	return;
};

void deleteHost(socketHost** hosts, HANDLE lock, socketHost* host) {
	WaitForSingleObject(lock, INFINITE);
	CloseHandle(host->writeLock);
	closesocket(host->s);
	if (host == *hosts) { /*Special case tree top*/
		if (host->L != NULL) {
			*hosts = host->L;
			(*hosts)->U = NULL;
			if (host->R != NULL) { _addTree(*hosts, host->R); }
		}
		else { *hosts = host->R; }
	}
	else {
		if ((host->U)->L == host) { (host->U)->L = NULL; }
		if ((host->U)->R == host) { (host->U)->R = NULL; }
		if (host->L != NULL) { _addTree(*hosts, host->L); }
		if (host->R != NULL) { _addTree(*hosts, host->R); }
	}
	free(host);
	ReleaseMutex(lock);
};

socketHost* _findHost(socketHost* hosts, UINT32 pid, UINT32 fd){
	if (hosts == NULL) { return NULL; }
	if (hosts->pid == pid && hosts->fd == fd) { return hosts; }
	if (hosts->pid > pid) { return _findHost(hosts->L, pid, fd); }
	return _findHost(hosts->R, pid, fd);
};

socketHost* findHost(socketHost* hosts, HANDLE lock, UINT32 pid, UINT32 fd) {
	socketHost* rtn = NULL;
	WaitForSingleObject(lock, INFINITE);
	rtn = _findHost(hosts, pid, fd);
	ReleaseMutex(lock);
	return rtn;
};

/*Broker process thread will create new hosts, while the host process thread will destroy
hosts to avoid a race where the socket could get closed before a write*/


void _eachHost(socketHost* top, HANDLE lock, socketHost** hosts, writeQnode** wqueue, HANDLE wlock, fn_hostTask fn, void* v){
	socketHost* l;
	socketHost* r;
	if (top == NULL) { return; }
	l = top->L;
	r = top->R;
	(*fn)(hosts, top, lock, wqueue, wlock, v);
	_eachHost(l, lock, hosts, wqueue, wlock, fn, v);
	_eachHost(r, lock, hosts, wqueue, wlock, fn, v);
	return;
};

void eachHost(socketHost** hosts, HANDLE lock, writeQnode** wq, HANDLE wlock, fn_hostTask fn, void* v){
	socketHost* top = NULL;
	WaitForSingleObject(lock, INFINITE);
	top = *hosts;
	_eachHost(top, lock, hosts, wq, wlock, fn, v); 
	ReleaseMutex(lock);
	return;
};

int newHost(socketHost** hosts, HANDLE lock, UINT32 pid, UINT32 fd, int status, SOCKET sock) {
	socketHost* newHost;

	newHost = (socketHost*)malloc(sizeof(socketHost));
	if (NULL == newHost) { return FALSE; }
	memset(newHost, 0, sizeof(socketHost));
	newHost->fd = fd;
	newHost->pid = pid;
	newHost->s = sock;
	newHost->status = status;
	newHost->writeLock = CreateMutex(NULL, FALSE, NULL);
	newHost->writeQueue = NULL;
	if (newHost->writeLock == NULL) { return FALSE; }

	WaitForSingleObject(lock, INFINITE);
	if (*hosts == NULL) { *hosts = newHost; }
	else { _addTree(*hosts, newHost); }
	ReleaseMutex(lock);
	return TRUE;
};