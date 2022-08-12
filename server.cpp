#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/stat.h>
#include <iostream>
#include <fstream>

using namespace std;

#define PORT 4000

int sockfd, clientSockfd, n;
socklen_t clilen;
char buffer[256];
struct sockaddr_in serv_addr, cli_addr;

void openSocket()
{
	if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
		cout << "ERROR opening socket" << endl;

	serv_addr.sin_family = AF_INET;
	serv_addr.sin_port = htons(PORT);
	serv_addr.sin_addr.s_addr = INADDR_ANY;
	bzero(&(serv_addr.sin_zero), 8);

	if (bind(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
		cout << "ERROR on binding" << endl;
}

void initClient(int userId, int *clientSocket)
{
	if (mkdir(to_string(userId).c_str(), 0777) < 0)
	{
	}
	else
	{
		// Cria o diretorio do cliente no server
		cout << "Criando diretorio do cliente" << " " << userId << endl;
	}
}

void receiveFile(int userId, string fileName, int fileSize, int *clientSocket)
{
	ofstream file;
	string dir = to_string(userId) + "/" + fileName;
	file.open(dir);
	int bytes;
	char buffer[10000];
	cout << fileName << endl << fileSize << endl;
	if (fileSize > 0)
	{

		while (fileSize > 0)
		{
			bytes = read(*clientSocket, buffer, min(fileSize,10000));
			cout << buffer << endl;
			file.write(buffer, min(fileSize, 10000));

			fileSize -= 10000;
		}
	}
	file.close();
}

void listenClient(int userId, int *clientSocket)
{
	cout << "listen client" << endl;
	int bytes;
	char buffer[256];
	char confirm[10] = "ok";

	bytes = read(*clientSocket,buffer,256);

	cout << buffer << endl;
	if(bytes < 0)
		cout << "erro ao ler requisicao do cliente" << endl;

	while(strcmp(buffer,"exit") != 0){
		if(strcmp(buffer,"upload") == 0){
			char fileName[256];
			char fileSize[256];
			int ifileSize;
			bytes = read(*clientSocket, buffer, 256);
			strcpy(fileName, buffer);
			//send(*clientSocket,confirm,sizeof(confirm),0);
			bytes = read(*clientSocket, buffer,256);
			strcpy(fileSize, buffer);
			ifileSize = atoi(fileSize);
			receiveFile(userId,fileName,ifileSize,clientSocket);
		}
		bytes = read(*clientSocket,buffer,256);
	}
	
}

void *startClientThread(void *socket)
{
	int *socketAdress = (int *)socket;
	int bytes;
	int userId;

	// Le userId
	userId = read(clientSockfd, buffer, 256);
	if (userId < 0)
		cout << "Erro ao ler do socket" << endl;

	char isConnected = 'Y';
	// Informa ao usuário que conseguiu conectar ao server

	bytes = send(*socketAdress, &isConnected, sizeof(char),0);
	if (bytes < 1)
	{
		cout << "Erro ao informar usuario" << endl;
	}
	initClient(userId, socketAdress);
	listenClient(userId, socketAdress);
}

int main(int argc, char *argv[])
{

	openSocket();
	listen(sockfd, 5);

	clilen = sizeof(struct sockaddr_in);
	pthread_t clientThread;
	cout << "Server incializado..." << endl;
	while (true)
	{
		if ((clientSockfd = accept(sockfd, (struct sockaddr *)&cli_addr, &clilen)) == -1)
		{
			cout << "Erro ao aceitar" << endl;
		}
		else
		{
			bzero(buffer, 256);

			int userIdRead;
			/* read from the socket */
			userIdRead = read(clientSockfd, buffer, 256);
			if (userIdRead < 0)
				cout << "Erro ao ler do socket" << endl;

			if (userIdRead > 0)
			{
				if (pthread_create(&clientThread, NULL, startClientThread, &clientSockfd))
				{
					cout << "Erro ao abrir a thread do cliente" << endl;
				}
			}
		}
	}
	close(clientSockfd);
	close(sockfd);
	return 0;
}
