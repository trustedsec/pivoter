#ifndef WRITEQUEUE_H_INCLUDED
#define WRITEQUEUE_H_INCLUDED

typedef struct _wq {
	struct _wq* next;
	UINT16 cmd;
	UINT32 fd;
	UINT32 pid;
	char* buffer;
	int bufflen;
} writeQnode;

int writeQpop(writeQnode** head, HANDLE lock, writeQnode* out);
void writeQUnPop(writeQnode** head, HANDLE lock, writeQnode* in);
void writeQpush(writeQnode** head, HANDLE lock, writeQnode* in);
int writeQpeek(writeQnode** head, HANDLE lock);

#endif