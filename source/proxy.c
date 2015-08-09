#include "includes.h"
#include "proxy.h"

int WSAGetLastErrorMapped(void){
	UINT32 err;
	err = WSAGetLastError();
	switch (err) {
	case WSAECONNREFUSED:
		return 111;
	case WSAETIMEDOUT:
		return 110;
	case WSAEHOSTDOWN:
		return 112;
	case WSAEHOSTUNREACH:
		return 113;
	case WSAENETUNREACH:
		return 101;
	default:
		return err;
	}
};

int connectBroker(char* ip, char* port, SOCKET* broker) {
	struct addrinfo *result = NULL;
	struct addrinfo	hints;
	UINT16 cmd = RELAY_OPEN;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;

	if (getaddrinfo(ip, port, &hints, &result)) {
		WSACleanup();
		printf("Invalid address port info\n");
		return 0;
	}

	*broker = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
	if (SOCKET_ERROR == connect(*broker, result->ai_addr, (int)result->ai_addrlen)) {
		printf("Connect failed\n");
		WSACleanup();
		return 0;
	}
	freeaddrinfo(result);

	/*Tell the borker what we are*/
	if (SOCKET_ERROR == send(*broker, (char*)&cmd, sizeof(cmd), 0)) { return 0; }
	return 1;
};

int disconnectBroker(SOCKET* broker){
	closesocket(*broker);
	WSACleanup();
	return 0;
};

__inline void clearFDs(fd_set* readfds, fd_set* writefds, fd_set* exfds, SOCKET* single) {
	FD_ZERO(readfds);
	FD_ZERO(writefds);
	FD_ZERO(exfds);
	FD_SET(*single, readfds);
	FD_SET(*single, writefds);
	FD_SET(*single, exfds);
};

void proxyConnect(writeQnode* message, SOCKET* sock) {
	struct sockaddr_in remote;
	UINT32 r;

	message->cmd = RELAY_CONNECT;
	remote.sin_family = AF_INET;
	memcpy(&remote.sin_addr, message->buffer, sizeof(UINT32));
	memcpy(&remote.sin_port, message->buffer + 4, sizeof(UINT16));
	*sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	message->bufflen = sizeof(UINT32);
	if (r = connect(*sock, (struct sockaddr *)&remote, sizeof(remote))) { r = WSAGetLastErrorMapped(); }
	memcpy(message->buffer, &r, sizeof(UINT32));
	return;
};

void hostByname(writeQnode* message) {
	char name[255];
	UINT32* addresses;
	struct hostent* result;
	int i = 0;

	addresses = (UINT32*)malloc(sizeof(UINT32) * 8);
	memset(addresses, 0, sizeof(UINT32) * 8);
	memset(name, 0, 255);
	strncpy(name, message->buffer, 254);
	free(message->buffer);
	message->buffer = (char*)addresses;
	message->bufflen = sizeof(UINT32) * 8;
	result = gethostbyname(name);
	if (result != NULL) {
		while (i < 8 && result->h_addr_list[i] != 0) {
			addresses[i] = *(UINT32*)result->h_addr_list[i];
			i++;
		}
	}
};

__inline int readMessage(SOCKET* sock, writeQnode* message) {
	if (!recv(*sock, (char*)&message->cmd, sizeof(message->cmd), MSG_WAITALL)) { return FALSE; } /*broker closed connection?*/
	if (!recv(*sock, (char*)&message->fd, sizeof(message->fd), MSG_WAITALL)) { return FALSE; }
	if (!recv(*sock, (char*)&message->pid, sizeof(message->pid), MSG_WAITALL)) { return FALSE; }
	if (!recv(*sock, (char*)&message->bufflen, sizeof(message->bufflen), MSG_WAITALL)) { return FALSE; }
	if (0 < message->bufflen) {
		message->buffer = (char*)malloc(message->bufflen);
		if (NULL == message->buffer) { return FALSE; } /*out of memory?*/
		if (!recv(*sock, message->buffer, message->bufflen, MSG_WAITALL)) { return FALSE; }
	}
	return TRUE;
};

__inline int writeMessage(SOCKET* sock, writeQnode *message) {
	send(*sock, (char*)&message->cmd, sizeof(message->cmd), 0);
	send(*sock, (char*)&message->fd, sizeof(message->fd), 0);
	send(*sock, (char*)&message->pid, sizeof(message->pid), 0);
	send(*sock, (char*)&message->bufflen, sizeof(message->bufflen), 0);
	send(*sock, message->buffer, message->bufflen, 0);
	free(message->buffer); /* will have been dynamically allocated*/
	message->buffer = NULL;
	return TRUE;
};

void readWriteHost(socketHost** hosts, socketHost* client, HANDLE lock, writeQnode** writeQueue, HANDLE writeLock, void* mtu){
	int r;
	char* t;
	struct timeval timeout;
	writeQnode message;
	fd_set readfds, writefds, exfds;

	clearFDs(&readfds, &writefds, &exfds, &client->s);
	memset(&message, 0, sizeof(writeQnode));
	message.cmd = RELAY_WRITE;
	message.fd = client->fd;
	message.pid = client->pid;

	timeout.tv_sec = 0;
	timeout.tv_usec = 0;
	
	select(0, &readfds, NULL, NULL, &timeout);
	if (FD_ISSET(client->s, &readfds)) {
		message.buffer = (char*)malloc(((int*)mtu)[0]);
		if (message.buffer != NULL) { 
			message.bufflen = recv(client->s, message.buffer, ((int*)mtu)[0], 0);
			if (message.bufflen > 0) {
				((int*)mtu)[1] += 1; /*might be reply packets skip the wait*/
				writeQpush(writeQueue, writeLock, &message);
			}
			else { client->status = RELAY_DEAD; }
		}
	}

	select(0, NULL, &writefds, NULL, &timeout);
	if (FD_ISSET(client->s, &writefds)) { 
		if (writeQpop(&client->writeQueue, client->writeLock, &message)) {
			((int*)mtu)[0] += 1; /*might be reply packets skip the wait*/
			r = send(client->s, message.buffer, message.bufflen, 0);
			if (r == SOCKET_ERROR) { client->status = RELAY_DEAD; }
			else if (r < message.bufflen) {
				t = message.buffer;
				message.bufflen = message.bufflen - r;
				message.buffer = (char*)malloc(message.bufflen);
				memcpy(message.buffer, t + r, message.bufflen);
				writeQUnPop(&client->writeQueue, client->writeLock, &message);
				message.buffer = t;
			}
			free(message.buffer);
		}
	}

};

void cleanHosts(socketHost** hosts, socketHost* client, HANDLE lock, writeQnode** writeQueue, HANDLE writeLock, void* nul) {
	writeQnode message;
	if (client->status == RELAY_DEAD || 
	   (!writeQpeek(&client->writeQueue, client->writeLock) && client->status == RELAY_CLOSE)) {
		while (writeQpop(&client->writeQueue, client->writeLock, &message)) { 
			free(message.buffer); /*relase any memory holding messages that can't be sent*/
		} 
		message.cmd = RELAY_CLOSE;
		message.bufflen = 0;
		message.buffer = NULL;
		message.fd = client->fd;
		message.pid = client->pid;
		writeQpush(writeQueue, writeLock, &message);
		deleteHost(hosts, lock, client);
	}
};

DWORD WINAPI AsyncConnect(LPVOID in) {
	int tmp_sock;
	sharedData* shared = ((embryo*)in)->shared;
	writeQnode* message = ((embryo*)in)->message;

	proxyConnect(message, &tmp_sock);
	if (!(*(UINT32*)message->buffer)) {
		if (!newHost(&shared->hosts, shared->hostLock, message->pid, message->fd, RELAY_WRITE, tmp_sock)) {
			memset(&message->buffer, WSATRY_AGAIN, sizeof(UINT32)); /*could not create host so..*/
			closesocket(tmp_sock);
		}
	}
	writeQpush(&shared->writeQueue, shared->writeQlock, message); /* broker needs a reply connected or not */

	free(message);
	free(in);
	return 0;
}

DWORD WINAPI clientProcess(LPVOID in) {
	sharedData* shared = (sharedData*)in;
	int runvars[2];
	runvars[0] = shared->mtu;
	runvars[1] = 0;
	while (TRUE){
		eachHost(&shared->hosts, shared->hostLock, &shared->writeQueue, shared->writeQlock, cleanHosts, NULL);
		eachHost(&shared->hosts, shared->hostLock, &shared->writeQueue, shared->writeQlock, readWriteHost, runvars);
		if (runvars[1] == 0) { Sleep(2); } else { runvars[1] = 0; }
	}
};

int brokerProcess(SOCKET* broker, sharedData* shared) {
	writeQnode message, *messagetmp;
	embryo* newconnection;
	HANDLE* threadhnd;
	socketHost* client;
	int err = 0;
	struct timeval timeout;
	fd_set readfds, writefds, exfds;

	timeout.tv_sec = 0;
	timeout.tv_usec = 20;
	
	clearFDs(&readfds, &writefds, &exfds, broker);
	while (SOCKET_ERROR != err) {
	err = select(0, &readfds, NULL, NULL, &timeout);
		memset(&message, 0, sizeof(message));
		if (FD_ISSET(*broker, &readfds)) {
			if (!readMessage(broker, &message)) { return -2; } /*would not known how to recover broker is gone*/
			/* having obtained a message we now need to forward it or handle it somehow*/
			switch (message.cmd) {
			case PROXY_CONNECT: /*connects may block for a long time so thread them*/
				newconnection = (embryo*)malloc(sizeof(embryo));
				messagetmp = (writeQnode*)malloc(sizeof(writeQnode));
				if ((newconnection == NULL) || (messagetmp == NULL)) { return -3; } /*out of memory*/
				memcpy(messagetmp, &message, sizeof(writeQnode)); /*don't touch buffers allocation*/
				newconnection->message = messagetmp;
				newconnection->shared = shared;
				threadhnd = CreateThread(NULL, 0, AsyncConnect, newconnection, 0, NULL);
				if (threadhnd != NULL) { CloseHandle(threadhnd); }
				break;
		    case PROXY_WRITE:
				client = findHost(shared->hosts, shared->hostLock, message.pid, message.fd); /*send a close if we don't have a connection*/
				if (client == NULL) { message.cmd = RELAY_CLOSE; message.bufflen = 0; writeQpush(&shared->writeQueue, shared->writeQlock, &message); }
					else { writeQpush(&client->writeQueue, client->writeLock, &message);}
				break;
			case PROXY_CLOSE:
				client = findHost(shared->hosts, shared->hostLock, message.pid, message.fd); 
				if (client != NULL) { client->status = RELAY_CLOSE; }
				break;
			case PROXY_HOSTBYNAME:
				hostByname(&message);
				writeQpush(&shared->writeQueue, shared->writeQlock, &message);
				break;
			default:
				/*discard, unknown command*/
				memset(&message, 0, sizeof(message));
			}
		}
	err = select(0, NULL, &writefds, NULL, &timeout);
		if (FD_ISSET(*broker, &writefds)){
			if (writeQpop(&shared->writeQueue, shared->writeQlock, &message)) {
				writeMessage(broker, &message);
			}
		}
		clearFDs(&readfds, &writefds, &exfds, broker);
	}
	return 0;
};

int main(int argc, char *argv[]) {
	WSADATA wsaData;
	SOCKET broker;
	sharedData shared;
	HANDLE client_thread;
	int rtn;

	shared.writeQueue = NULL; /*broker write queue*/
	shared.mtu = 500; /*default in broker*/
	shared.hosts = NULL; /*root of hosts tree*/

	shared.hostLock = CreateMutex(NULL, FALSE, NULL);
	if (NULL == shared.hostLock) { return 1; }

	shared.writeQlock = CreateMutex(NULL, FALSE, NULL);
	if (NULL == shared.writeQlock) { return 1; }

	if (WSAStartup(MAKEWORD(2, 2), &wsaData)) { printf("WSAStartup failed\n"); return 1; }

	if ((argc == 0) || (argc == 1)) {
		if (!connectBroker("001.003.003.007", "31337", &broker)) { return 1; }
	}
	else if (argc == 4) {
		shared.mtu = atoi(argv[3]);
		if (!connectBroker(argv[1], argv[2], &broker)) { return 1; }
	} 
	else { printf("Need ip, port, mtu, [optioanal debug]\n"); exit(1); }

	client_thread = CreateThread(NULL, 0, clientProcess, &shared, 0, NULL);
	rtn = brokerProcess(&broker, &shared);
	TerminateThread(client_thread, 0);

	CloseHandle(shared.hostLock);
	CloseHandle(shared.writeQlock);
	CloseHandle(client_thread);
	disconnectBroker(&broker);
	return rtn;
};
