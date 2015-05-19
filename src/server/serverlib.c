#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <pthread.h>

#include "serverlib.h"

//Handler per le uscite dal programma, libero le risorse e avverto i client che non sono più disponibile
void handler ()
{  
	broadcastServerClosed();
	close(serverAnswerFIFO);
	close(serverAuthFIFO);
	unlink(SERVER_ANSWER_FIFO);
	unlink(SERVER_AUTHORIZATION_FIFO);
	printf("\n");
	exit(0);
}

//Thread che gestisce le richieste di autorizzazione dei client
void* authorizationThread(void* arg)
{
	//Avvio il monitoraggio delle autorizzazioni
	serverAuthFIFO = open(SERVER_AUTHORIZATION_FIFO,O_RDWR);
	char* clientMessage=(char*)malloc((MAX_MESSAGE_SIZE*clientsMaxNumber)*sizeof(char));
	char serverMessage[MAX_MESSAGE_SIZE];
	char fifoPath [MAX_FIFO_NAME_SIZE];
	
	char* answer;
	
	//Ciclo per la lettura dei messaggi nella coda serverAuthFIFO
	while(1)
	{
		int size = read(serverAuthFIFO,clientMessage,MAX_MESSAGE_SIZE*clientsMaxNumber);
		Message **messages=parseMessages(clientMessage,size);
		int i=0;
		while(messages[i]!=NULL)
		{
			Message *message = messages[i];
			i++;
			
			if(strchr(message->parameters[0],'R')!=NULL) //richiesta di autentificazione
			{	
				strcpy(fifoPath,CLIENT_MESSAGE_FIFO);
				strcat(fifoPath,message->parameters[1]);
				
				printf("[AUTH] %s ha effettuato una richiesta di connessione\n",message->parameters[2]);
				
				//apro la fifo del client
				int clientMessageFIFO = open(fifoPath,O_RDWR);
				
				int id=checkClientAuthRequest(message);
				
				if(id>=0)
				{
					connectNewClient(id,message->parameters[2],clientMessageFIFO);
					answer=authAcceptMessage(id);
					broadcastConnection(id,message->parameters[2]);
				}
				else
				{
					answer=authRejectMessage(id);
				}
				write(clientMessageFIFO,answer, strlen(answer)+1);
			}
			if(strchr(message->parameters[0],'Q')!=NULL) //notifica di disconnessione
			{	
				int id=atoi(message->parameters[1]);
				printf("[AUTH] %s si e' disconnesso\n",clientData[id]->name);
				broadcastDisonnection(id,clientData[id]->name);		
				disconnectClient(id);		
			}
			free(message);
			free(answer);
		}
		free(messages);
	}
}

//Thread per il bash utente, necessario per la lettura dei comandi
void* bashThread(void*arg)
{
	char* rawCommand = (char*)malloc(MAX_COMMAND_SIZE*sizeof(char));
	while(1)
	{
		size_t size = MAX_COMMAND_SIZE;
		getline(&rawCommand,&size,stdin);
		fflush(stdin);
		strchr(rawCommand,'\n')[0]='\0';
		Command* command=parseCommand(rawCommand);
		if (strcmp(command->operation, "help")==0) //Comando help
		{
			printf("Lista comandi utente:\n");
			printf("help: Richiama questo messaggio di aiuto\n");
			printf("kick <utente>: Kick di <utente> dalla partita\n");
			printf("question \"question\" \"answer\": Invia una nuova domanda a tutti gli utenti\n");
			printf("list: Stampa la lista degli utenti connessi\n");
			printf("clear: Pulisce la schermata corrente\n");
		}
		else if (strcmp(command->operation, "clear")==0) //Comando clear
		{
			printf("\e[1;1H\e[2J");
		}
		else if (strstr(command->operation, "kick")!=NULL) //Comando kick
		{
			if(command->parameterCount==1)
			{
				kick(command->parameters[0]);
			}
			else
			{
				printf("[ERROR] numero di parametri errato in %s: 1 parametro necessario\n",rawCommand);
			}
		}
		else if (strstr(command->operation, "list")!=NULL) //Comando list
		{
			if(command->parameterCount==0)
			{
				listCommand();
			}
			else
			{
				printf("[ERROR] numero di parametri errato in %s: 0 parametri necessari\n",rawCommand);
			}
		}
		else if (strstr(command->operation, "question")!=NULL) //Comando question
		{
			if(command->parameterCount==2)
			{
				sendCustomizedQuestion(command->parameters[0],command->parameters[1]);
			}
			else
			{
				printf("[ERROR] numero di parametri errato in %s: 2 parametri necessari\n",rawCommand);
			}
		}
		else //Comandi sconosciuti
		{
			printf("[ERROR] %s:Comando non riconosciuto\n",command->operation);
		}
	}
}

//Controllo della richiesta di autentificazione ricevuta dal client
int checkClientAuthRequest(Message *message)
{
	if(message->parameterCount!=3)
	{
		return -2; //BAD REQUEST
	}
	else
	{
		if (connectedClientsNumber<clientsMaxNumber)
		{
			int i;
			for(i=0;i<clientsMaxNumber;i++)
			{
				if(clientData[i]!=NULL)
				{
					if(strcmp(clientData[i]->name,message->parameters[2])==0)
					{
						return -4; //NAME ALREADY IN USE
					}
				}
			}
			for(i=0;i<clientsMaxNumber;i++)
			{
				if(clientData[i]==NULL)
				{
					return i; //GIVEN ID
				}
			}
			
		}
		else
		{
			return -3; //SERVER FULL
		}
	}
}

//Funzione di inizializzazione della struttura contenente i dati associati ai client
void initializeClientData()
{
	clientData = (ClientData**)malloc(sizeof(ClientData*)*clientsMaxNumber);
	int i;
	for(i=0;i<clientsMaxNumber;i++)
	{
		clientData[i]=NULL;
	}
}

//Aggiunta di un nuovo client a clientData, con il set di tutti i parametri
void connectNewClient(int id,char* name,int fifoID)
{
	clientData[id]=(ClientData*)malloc(sizeof(ClientData));
	clientData[id]->name=(char*)malloc(sizeof(char)*(strlen(name)+1));
	strcpy(clientData[id]->name,name);
	clientData[id]->fifoID=fifoID;
	clientData[id]->points=clientsMaxNumber-connectedClientsNumber;
	connectedClientsNumber++;
	printf("[AUTH] Ho accettato la richiesta di %s, e gli ho assegnato %d punti\n[INFO] Rimangono %d posti liberi\n",clientData[id]->name,clientData[id]->points,clientsMaxNumber-connectedClientsNumber);
}

//Funzione di creazione del messaggio da reinviare al client che ha richiesto di partecipare
//{A|idClient|question|idQuestion|points}
char* authAcceptMessage(int id)
{
	char idc[3];
	char points[10];
	sprintf(idc,"%d",id);
	sprintf(points,"%d",clientData[id]->points);
	char *answer = (char*)malloc(sizeof(char)*MAX_MESSAGE_SIZE);
	strcpy(answer,"A|");
	strcat(answer,idc);
	strcat(answer,"|");
	strcat(answer,questions[currentQuestion].question->text);
	strcat(answer,"|");
	strcat(answer,questions[currentQuestion].question->id);
	strcat(answer,"|");
	strcat(answer,points);
	return answer;
}

//Creazione del messaggio di reject della richiesta di autentificazione
//{A|errors}
char* authRejectMessage(int error)
{
	char errors[4];
	sprintf(errors,"%d",error);
	char *answer = (char*)malloc(sizeof(char)*MAX_MESSAGE_SIZE);
	strcpy(answer,"A|");
	strcat(answer,errors);
	return answer;
}

//Routine di controllo delle risposte
int checkAnswer(Message* message)
{
	int questionIndex = atoi(message->parameters[1]);
	if (strcmp(message->parameters[2],questions[questionIndex].answer)==0)
	{
		if (questionIndex==currentQuestion)
		{
			return 1; //Risposta giusta domanda corrente
		}
		else
		{
			return 3; //Risposta giusta domanda vecchia
		}
	}
	else
	{
		return 2; //Risposta sbagliata
	}
}

//Costruzione del risultato della risposta da reinviare al client
//{C/W/T|idQuestion|points}
char* buildResult(Message* message, ClientData* player, int cwt)
{
	char castedPoints[POINT_SIZE];
	sprintf(castedPoints, "%d", player->points);
	char* idQuestion = message->parameters[1];
	char* answer = malloc((6+strlen(idQuestion)+strlen(castedPoints))*sizeof(char));
	if (cwt==1)
	{
		strcpy(answer, "C|"); //Risposta giusta domanda corrente
	}
	else if(cwt==2)
	{
		strcpy(answer, "W|"); //Risposta sbagliata
	}
	else
	{
		strcpy(answer, "T|"); //Risposta giusta domanda vecchia
	}
	strcat(answer, idQuestion);
	strcat(answer, "|");
	strcat(answer, castedPoints);
	
	return answer;
}

//Get del mittente del messaggio
ClientData* getSender(Message* message)
{
	int id = atoi(message->parameters[0]);
	return clientData[id];
}

//Invio della risposta alla FIFO del client
void sendResponse(int fifoID, char* response)
{
	write(fifoID,response, strlen(response)+1);
	free(response);
}

//Inizializzazione delle domande
void InitializeQuestions()
{
	currentQuestion=QUESTION_ID;
	srand(time(NULL));
}

//Funzione di generazione di domande casuali e relative risposte
void GenerateNewQuestion(){
	currentQuestion=(currentQuestion+1)%QUESTION_ID;
	
	if(questions[currentQuestion].question!=NULL)
	{
		free(questions[currentQuestion].question);
	}
	if(questions[currentQuestion].answer!=NULL)
	{
		free(questions[currentQuestion].answer);
	}
	
	char newText[MAX_QUESTION_SIZE];
	char newId[MAX_QID_SIZE];
	char *answs=(char*)malloc(11*sizeof(char));
	
	if(testRun==0)
	{
		char nums1[10];
		char nums2[10];
		
		int num1,num2,answ;
		char op[2];
		
		num1=rand()%MAX_QUESTION_NUM;
		num2=rand()%MAX_QUESTION_NUM;
		
		//TODO OPTION WITH OTHER OPERATIONS
		op[0]='+';
		op[1]='\0';
		
		sprintf(nums1,"%d",num1);
		sprintf(nums2,"%d",num2);
		
		
		strcpy(newText,nums1);
		strcat(newText," ");
		strcat(newText,op);
		strcat(newText," ");
		strcat(newText,nums2);
		strcat(newText," = ?");
		
		if(strcmp(op,"+")==0) //TODO add other operations
		{
			answ=num1+num2;
		}
		
		sprintf(answs,"%d",answ);	
	}
	else
	{
		
		fgets(newText, MAX_MESSAGE_SIZE, testFile);
		strchr(newText,'\n')[0]='\0';
		if(strcmp(newText,"end")==0)
		{
			printf("[INFO] Sessione di test terminata\n\n");
			{
				handler();
			}
		}
		fgets(answs, MAX_MESSAGE_SIZE, testFile);
		
		strchr(answs,'\n')[0]='\0';
		
	}
	sprintf(newId,"%d",currentQuestion);
	
	Question* newQuestion = (Question*) malloc(sizeof(Question));
	strcpy(newQuestion->id,newId);
	strcpy(newQuestion->text,newText);
	
	questions[currentQuestion].question=newQuestion;
	questions[currentQuestion].answer=answs;
	printf("[GAME] La nuova domanda e' %s, ha risposta %s\n",newText,answs);
}

//Broadcast del messaggio contenente la nuova domanda a tutti i client
void BroadcastQuestion(){
	int i;
	char newQuestionMessage [MAX_MESSAGE_SIZE];
	strcpy(newQuestionMessage,"Q|");
	strcat(newQuestionMessage,questions[currentQuestion].question->id);
	strcat(newQuestionMessage,"|");
	strcat(newQuestionMessage,questions[currentQuestion].question->text);
	for(i=0;i<clientsMaxNumber;i++){
		if(clientData[i]!=NULL)
		{
			write(clientData[i]->fifoID,newQuestionMessage,strlen(newQuestionMessage)+1);
		}	
	}
}

//Chiamata quando si vuole far disconnettere forzatamente un client dal gioco (usando il comando kick)
//Non invia il messaggio, per quello va usata la funzione void kick(char* name)
void disconnectClient(int id)
{
	if(clientData[id]!=NULL)
	{
		if(clientData[id]->name!=NULL)
		{
			free(clientData[id]->name);
		}
		free(clientData[id]);
		clientData[id]=NULL;
		connectedClientsNumber--;
	}
}

//Funzione che invia il messaggio di server down a tutti i client
void broadcastServerClosed()
{
	int i;
	char serverCloseMessage [MAX_MESSAGE_SIZE];
	strcpy(serverCloseMessage,"D");
	for(i=0;i<clientsMaxNumber;i++){
		if(clientData[i]!=NULL)
		{
			write(clientData[i]->fifoID,serverCloseMessage,strlen(serverCloseMessage)+1);
		}	
	}
}

//Parsing del comando inserito nella bash, contenente un opzionale argomento
//L'argomento se è una frase va inserito tra " " in modo da riconoscerlo come unico
//Argomenti a singola parola possono essere tra " "
Command* parseCommand(char* rawCommand){
	Command *command=(Command*)malloc(sizeof(Command));
	int size = strlen(rawCommand);
	char *start = rawCommand;
	char* space=strchr(rawCommand,' ');
	if(space==NULL) //Comando senza argomenti
	{
		command->operation=(char*)malloc(sizeof(char)*(strlen(rawCommand)+1));
		strcpy(command->operation,rawCommand);
		command->parameterCount=0;
		command->parameters=NULL;
	}
	else //Il comando contiene 1 o più argomenti
	{
		space[0]='\0';
		space++;
		command->operation=(char*)malloc(sizeof(char)*(strlen(rawCommand)+1));
		strcpy(command->operation,rawCommand);
		command->parameterCount=0;
		command->parameters=(char**)malloc(sizeof(char*)*MAX_PARAMETERS_NUMBER);
		while(space[0]!='\0')//Parso l'argomento e riduco il numero di spazi a 1
		{
			if(space[0]==' ')
			{
				space++;
			}
			if(space[0]=='"')
			{
				space++;
				char* nextQuotes=strchr(space,'"');
				if(nextQuotes==NULL)
				{
					break;
				}
				else
				{
					nextQuotes[0]='\0';
					nextQuotes++;
					command->parameters[command->parameterCount]=malloc(sizeof(char)*(strlen(space)+1));
					strcpy(command->parameters[command->parameterCount],space);
					command->parameterCount++;
					space=nextQuotes;
				}
			}
			else
			{
				char* nextSpace=strchr(space,' ');
				if(nextSpace==NULL)
				{
					command->parameters[command->parameterCount]=malloc(sizeof(char)*(strlen(space)+1));
					strcpy(command->parameters[command->parameterCount],space);
					command->parameterCount++;
					break;
				}
				else
				{
					nextSpace[0]='\0';
					nextSpace++;
					command->parameters[command->parameterCount]=malloc(sizeof(char)*(strlen(space)+1));
					strcpy(command->parameters[command->parameterCount],space);
					command->parameterCount++;
				}
				if(command->parameterCount==MAX_PARAMETERS_NUMBER)
				{
					break;
				}
				space=nextSpace;
			}
		}
	}
	return command;
}

//Invio del messaggio di kick al client
void kick(char* name)
{
	
	int i;
	int kicked=-1;
	char kickMessage [MAX_MESSAGE_SIZE];
	strcpy(kickMessage ,"K");
	for(i=0;i<clientsMaxNumber;i++){
		if(clientData[i]!=NULL)
		{
			if(strstr(name,clientData[i]->name)!=NULL)
			{
				write(clientData[i]->fifoID,kickMessage ,strlen(kickMessage)+1);
				kicked=i;
				break;
			}
		}	
	}
	if(kicked>=0)
	{
		printf("[AUTH] Il giocatore %s e' stato disconnesso\n",clientData[kicked]->name);
		broadcastDisonnection(kicked,clientData[kicked]->name);
		disconnectClient(kicked);
	}
	else
	{
		printf("[ERROR] Nessun giocatore ha il nome specificato\n");
	}
}

//Stampa dei giocatori in partita (comando list)
void listCommand()
{
	
	int i;
	printf("[INFO] Ci sono %d giocatori connessi su  un massimo di %d\n",connectedClientsNumber,clientsMaxNumber);
	if(connectedClientsNumber>0)
	{
		printf("\nGiocatore\tPunteggio\n");
		for(i=0;i<clientsMaxNumber;i++){
			if(clientData[i]!=NULL)
			{
				printf("%s\t\t%d\n",clientData[i]->name,clientData[i]->points);
			}	
		}
	}
}

//Invio di una domanda personalizzata ai client (con il comando question)
void sendCustomizedQuestion(char* question,char* answer)
{
	currentQuestion=(currentQuestion+1)%QUESTION_ID;
	if(questions[currentQuestion].question!=NULL)
	{
		free(questions[currentQuestion].question);
	}
	if(questions[currentQuestion].answer!=NULL)
	{
		free(questions[currentQuestion].answer);
	}
	char newId[MAX_QID_SIZE];
	sprintf(newId,"%d",currentQuestion);
	
	char*answ=(char*)malloc(sizeof(char)*(strlen(answer)+1));
	strcpy(answ,answer);
	
	Question* newQuestion = (Question*) malloc(sizeof(Question));
	strcpy(newQuestion->id,newId);
	strcpy(newQuestion->text,question);
	questions[currentQuestion].question=newQuestion;
	questions[currentQuestion].answer=answ;
	
	BroadcastQuestion();
}

//Funzione per avvertire i client che un nuovo giocatore si è connesso
void broadcastConnection(int id,char* name)
{
	char notification[MAX_MESSAGE_SIZE];
	char message[MAX_MESSAGE_SIZE];
	sprintf(message,"[AUTH] %s si e' connesso\n",name);
	strcpy(notification,"N|");
	strcat(notification,message);
	int i;
	for(i=0;i<clientsMaxNumber;i++){
		if(clientData[i]!=NULL && id!=i)
		{
			write(clientData[i]->fifoID,notification,strlen(notification)+1);
		}	
	}
}

//Funzione per avvertire i client che un nuovo giocatore si è disconnesso
void broadcastDisonnection(int id,char* name)
{
	char notification[MAX_MESSAGE_SIZE];
	char message[MAX_MESSAGE_SIZE];
	sprintf(message,"[AUTH] %s si e' disconnesso\n",name);
	strcpy(notification,"N|");
	strcat(notification,message);
	int i;
	for(i=0;i<clientsMaxNumber;i++){
		if(clientData[i]!=NULL && id!=i)
		{
			write(clientData[i]->fifoID,notification,strlen(notification)+1);
		}	
	}
}

//Terminazione del gioco, unlink delle FIFO e liberazione delle risorse
void endGame(ClientData* winner)
{	
	ClientData** ranking=(ClientData**)malloc(sizeof(ClientData*)*clientsMaxNumber);
	int* done=(int*)malloc(sizeof(int)*clientsMaxNumber);
	int i;
	for(i=0;i<clientsMaxNumber;i++){
		done[i]=0;
	}
	int max;
	int j=0;
	ClientData* best;
	int maxIndex;
	while(1)
	{
		best=NULL;
		max=-1000;
		for(i=0;i<clientsMaxNumber;i++)
		{
			if(clientData[i]!=NULL && done[i]!=1)
			{
				if((clientData[i]->points)>max)
				{
					best=clientData[i];
					max=clientData[i]->points;
					maxIndex=i;
				}
			}
		}
		if(best==NULL)
		{
			break;
		}
		else
		{
			ranking[j]=best;
			done[maxIndex]=1;
			j++;
		}
	}
	char message[MAX_MESSAGE_SIZE];
	strcpy(message,"R|");
	for(i=0;i<j;i++)
	{
		strcat(message,ranking[i]->name);
		strcat(message," ");
		char points[18];
		sprintf(points,"%d",ranking[i]->points);
		strcat(message,points);
		strcat(message,"\n");
	}
	for(i=0;i<clientsMaxNumber;i++){
		if(clientData[i]!=NULL)
		{
			write(clientData[i]->fifoID,message,strlen(message)+1);
		}	
	}
	
	
	printf("[GAME] La partita e' terminata\n\n");
	close(serverAnswerFIFO);
	close(serverAuthFIFO);
	unlink(SERVER_ANSWER_FIFO);
	unlink(SERVER_AUTHORIZATION_FIFO);
	exit(0);
}

//Invio della classifica ai client a fine gioco
void broadcastRank(ClientData* best)
{
	char notification[MAX_MESSAGE_SIZE];
	char message[MAX_MESSAGE_SIZE];
	sprintf(message,"%d",(best->points));
	strcpy(notification,"N|");
	strcat(notification,(best->name));
	strcat(notification," ");
	strcat(notification,message);
	int i;
	for(i=0;i<clientsMaxNumber;i++){
		if(clientData[i]!=NULL)
		{
			write(clientData[i]->fifoID,notification,strlen(notification)+1);
		}	
	}
}

//Notifica ai client della fine del gioco
void broadcastEndGame()
{
	int i;
	for(i=0;i<clientsMaxNumber;i++){
		if(clientData[i]!=NULL)
		{
			write(clientData[i]->fifoID,"S",2);
		}	
	}
}
