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
#include <filesystem>

using namespace std;
namespace fs = std::filesystem;

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
		cout << "Criando diretorio do cliente"
			 << " " << userId << endl;
	}
}

int getFileSize(string filepath){
			ifstream in(filepath, std::ifstream::ate | std::ifstream::binary);
    		return in.tellg(); 
		}
void sendMessage(std::string message, int *clientSocket){
			char buffer[10000];
			strcpy(buffer,message.c_str());
		
			int bytes = write(*clientSocket , buffer , 10000*sizeof(char));
		}
void receiveFile(int userId, string fileName, int fileSize, int *clientSocket)
{
	ofstream file;
	string dir = to_string(userId) + "/" + fileName;
	file.open(dir);
	int bytes;
	char buffer[10000];
	// cout << fileName << endl << fileSize << endl;
	if (fileSize > 0)
	{

		while (fileSize > 0)
		{
			bytes = read(*clientSocket, buffer, min(fileSize, 10000));
			cout << buffer << endl;
			file.write(buffer, min(fileSize, 10000));

			fileSize -= 10000;
		}
	}
	file.close();
}

void sendFile(int userId,string filepath, int *clientSocket)
{
	FILE *file;
	int fileSize;
	int bytes;
	// string filename;
	// char ch = '/';

	
	if ((file = fopen(filepath.c_str(), "rb")))
	{
		// envia requisição de envio para o servidor
		fileSize = getFileSize(to_string(userId)+"/"+filepath);
		

		// envia o nome do arquivo para o servidor
		// filename = filepath.substr(filepath.find_last_of(ch) + 1 ,filepath.length()- filepath.find(ch));
		// this->sendMessage(filename);

		// waitConfirm();
		sendMessage(to_string(fileSize),clientSocket);
		
		char data[10000];
		while (!feof(file))
		{
			fread(data, sizeof(data), 1, file);

			bytes = write(*clientSocket, data, min(fileSize, 10000));
			if (bytes < 0)
				cout << "erro ao enviar arquivo" << endl;
			fileSize -= 10000;
		}
		fclose(file);
	}else{

		sendMessage("erro",clientSocket);
	}
}

// Lista os arquivos do diretório do cliente
// Tratamento do comando "list client"
void listClient(int userId, int *clientSocket)
{
	std::string path = fs::current_path();
   	for (const auto & entry : fs::directory_iterator(path))
        	std::cout << entry.path() << std::endl;
}

void listenClient(int userId, int *clientSocket)
{
	cout << "listen client" << endl;
	int bytes;
	char buffer[10000];
	char confirm[10] = "ok";

	bytes = read(*clientSocket, buffer, 10000);

	cout << buffer << endl;
	if (bytes < 0)
		cout << "erro ao ler requisicao do cliente" << endl;

	while (strcmp(buffer, "exit") != 0)
	{
		if (strcmp(buffer, "upload") == 0)
		{
			char fileName[10000];
			char fileSize[10000];
			int ifileSize;
			// cout << buffer << endl;
			bytes = read(*clientSocket, buffer, 10000);
			strcpy(fileName, buffer);

			// send(*clientSocket,confirm,sizeof(confirm),0);
			bytes = read(*clientSocket, buffer, 10000);
			// cout << buffer << endl;
			strcpy(fileSize, buffer);
			ifileSize = atoi(fileSize);
			receiveFile(userId, fileName, ifileSize, clientSocket);
		}
		if (strcmp(buffer, "download") == 0)
		{
			char fileName[10000];
			// cout << buffer << endl;
			bytes = read(*clientSocket, buffer, 10000);
			strcpy(fileName, buffer);
			cout << buffer << endl;	
			sendFile(userId, fileName, clientSocket);
		}
		if (strcmp(buffer, "list server") == 0))
		{
			listServer(userId, clientSocket);
		}
		bytes = read(*clientSocket, buffer, 1000);
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

	bytes = send(*socketAdress, &isConnected, sizeof(char), 0);
	if (bytes < 1)
	{
		cout << "Erro ao informar usuario" << endl;
	}
	initClient(userId, socketAdress);
	listenClient(userId, socketAdress);
}


void listenSync(int userId, int *clientSocket)
{
	int bytes;
	char buffer[256];

	bytes = read(*clientSocket, buffer, 256);
	cout << buffer << endl;
	if (bytes < 0)
		cout << "erro ao ler requisicao do cliente" << endl;

	while (strcmp(buffer, "exit") != 0)
	{
		if (strcmp(buffer, "upload") == 0)
		{
			char fileName[256];
			char fileSize[256];
			int ifileSize;
			bytes = read(*clientSocket, buffer, 256);
			strcpy(fileName, buffer);
			// send(*clientSocket,confirm,sizeof(confirm),0);
			bytes = read(*clientSocket, buffer, 256);
			strcpy(fileSize, buffer);
			ifileSize = atoi(fileSize);
			receiveFile(userId, fileName, ifileSize, clientSocket);
		}
		bytes = read(*clientSocket, buffer, 256);
	}
}
void *startSyncThread(void *socket)
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

	bytes = send(*socketAdress, &isConnected, sizeof(char), 0);
	if (bytes < 1)
	{
		cout << "Erro ao informar usuario" << endl;
	}

	listenSync(userId, socketAdress);
}

int main(int argc, char *argv[])
{

	openSocket();
	listen(sockfd, 5);

	clilen = sizeof(struct sockaddr_in);
	pthread_t clientThread, syncThread;
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

			int typeOfService;
			/* read from the socket */
			typeOfService = read(clientSockfd, buffer, 256);
			if (typeOfService < 0)
				cout << "Erro ao ler do socket" << endl;

			if (typeOfService == 1) // Atende request do cliente
			{
				if (pthread_create(&clientThread, NULL, startClientThread, &clientSockfd))
				{
					cout << "Erro ao abrir a thread do cliente" << endl;
				}
			}
			else if (typeOfService == 2)
			{ // Sincronização com cliente
				if (pthread_create(&syncThread, NULL, startSyncThread, &clientSockfd))
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
