#ifndef CLIENTLIB_H
#define CLIENTLIB_H

#include "../common/commonlib.h"

#define MAX_USERNAME_LENGHT 20
#define MIN_USERNAME_LENGHT 1

typedef struct {
	char* name;
	char* id;
	char* points;
} ClientData;

int serverAuthFIFO;
Question currentQuestion;
ClientData* clientData;

void* userInput(void* arg);
int validateUsername(char* username);
char* authRequestMessage(char* pid,char *name);
int checkServerAuthResponse(Message* message);
void initializeClientData(Message *message);

#endif