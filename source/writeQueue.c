#include "includes.h"
#include "writeQueue.h"

int writeQpop(writeQnode** head, HANDLE lock, writeQnode* out) {
	int rtn;
	WaitForSingleObject(lock, INFINITE);
	writeQnode* top = *head;
	writeQnode* last = top;
	if (NULL == top) { ReleaseMutex(lock); rtn = FALSE; } /*Nothing*/
	else {
		rtn = TRUE; /*True we poped something*/
		while (NULL != (top->next)) { last = top;  top = top->next; } /*to end of queue*/
		memcpy(out, top, sizeof(writeQnode));
		last->next = NULL;
		free(top);
		if ( top == *head) { *head = NULL; }
	}
	ReleaseMutex(lock);
	return rtn;
};

void writeQUnPop(writeQnode** head, HANDLE lock, writeQnode* in) {
	WaitForSingleObject(lock, INFINITE);
	writeQnode* last = *head;
	writeQnode* up = (writeQnode*)malloc(sizeof(writeQnode));
	up->next = NULL; 
	memcpy(up, in, sizeof(writeQnode));
	if (NULL == last) { *head = up; }
	else {
		while (NULL != last->next) { last = last->next; }
		last->next = up;
	}
	ReleaseMutex(lock);
};

void writeQpush(writeQnode** head, HANDLE lock, writeQnode* in) {
	writeQnode* top;
	WaitForSingleObject(lock, INFINITE);
	top = *head;
	*head = (writeQnode*)malloc(sizeof(writeQnode));
	if (NULL == *head) { exit(2); } /*out of memory*/
	memcpy(*head, in, sizeof(writeQnode));
	(*head)->next = top;
	ReleaseMutex(lock);
};

int writeQpeek(writeQnode** head, HANDLE lock) {
	if (NULL == *head) { return FALSE; }
	return TRUE;
};