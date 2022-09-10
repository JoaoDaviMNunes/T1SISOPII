#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <pthread.h>
#include <sys/stat.h>
#include <iostream>
#include <fstream>
#include <dirent.h>
#include <experimental/filesystem>
#include <map>
#include <set>

using namespace std;

#define PORT 4000
#define ALOC_SIZE 512

map<int,int > mSockToUserId;
map<int,set<int> > mUserIdToSocks;
map<int,int> mUserPropSock;
map<int,set<int> > mSocketPropagate;

pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;

bool hasNewFile = false;

char buffer[ALOC_SIZE];

struct hostent *primaryServer;

void initClient(int userId, int clientSocket)
{
	if (mkdir(to_string(userId).c_str(), 0777) < 0)
	{
		cout << "Erro ao criar diretorio : " << userId << endl;
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

int sendMessage(std::string message, int clientSocket){
			char buffer[ALOC_SIZE];

			strcpy(buffer,message.c_str());

			int bytes = write(clientSocket , buffer , ALOC_SIZE);

			return bytes;
}

void closeSocket(int userId){

	for(auto x:mUserIdToSocks[userId]){
		cout << x << endl;
		close(x);
	}
	cout << "All sockets closed" << endl;
}

void deleteFile(int userId, int clientSocket, string filename){
	DIR *dir;
    dir = opendir((const char *) to_string(userId).c_str()); // Tenta abrir o diretório.

    string path; // String auxiliar pra pegar o caminho completo do arquivo.

    if(dir!=NULL){ // Verifica se deu certo abrir o diretório.
        path = to_string(userId) + (string) "/" + filename;

        if(remove(path.c_str()) == 0){ // remove() precisa receber o caminho do arquivo.
            cout << "Arquivo \"" << filename << "\" deletado." << endl;
            return;
        }
        else{ // Cai aqui se o remove() não funfou
            cout << "Erro ao apagar o arquivo!" << endl;
            return;
        }
    }
    else{
        cout << "Erro na abertura do diretório" << endl;
        return;
    }
    closedir(dir);
}

void receiveFile(int userId, string fileName, int fileSize, int clientSocket)
{
	pthread_mutex_lock(&m);

	string dir = to_string(userId) + "/" + fileName;
	FILE *fp = fopen(dir.c_str(),"w");
	int bytes;
	char buffer[ALOC_SIZE];

	bzero(buffer,ALOC_SIZE);

	if (fileSize > 0)
	{

		while (fileSize > 0)
		{
			bytes = recv(clientSocket, buffer,min(ALOC_SIZE,fileSize),MSG_WAITALL);
			
            fwrite(buffer, 1, bytes, fp);

			fileSize -= bytes;

			bzero(buffer,ALOC_SIZE);
		}
	}

	fclose(fp);
	hasNewFile = true;
	pthread_mutex_unlock(&m);
}

void sendFile(int userId,string filepath, int clientSocket)
{
	FILE *file;
	int fileSize;
	int bytes;

	string dir = to_string(userId)+"/"+filepath;
	if ((file = fopen(dir.c_str(), "rb")))
	{
		// envia requisição de envio para o servidor
		fileSize = getFileSize(to_string(userId)+"/"+filepath);

		// waitConfirm();
		sendMessage(to_string(fileSize),clientSocket);

		char data[ALOC_SIZE];
		bzero(data,ALOC_SIZE);

		int length = 0;

		while((length = fread(data, sizeof(char), ALOC_SIZE, file)) > 0){
            if(send(clientSocket, data, length, 0 ) < 0){

                cout << "ERRO " << filepath << endl;
                break;
            }
            bzero(data, ALOC_SIZE);
		}
        fclose(file);
	}else{
        cout << "Erro ao abrir o arquivo" << endl;
	}
}

void listenClient(int userId, int clientSocket)
{
	int bytes;
	char buffer[ALOC_SIZE];
	char confirm[10] = "ok";
	bzero(buffer,ALOC_SIZE);
	bytes = read(clientSocket, buffer, ALOC_SIZE);

	if (bytes < 0)
		cout << "erro ao ler requisicao do cliente" << endl;

	while (strcmp(buffer, "exit") != 0)
	{
		if (strcmp(buffer, "upload") == 0)
		{
			char fileName[ALOC_SIZE];
			char fileSize[ALOC_SIZE];
			int ifileSize;

			bytes = read(clientSocket, buffer, ALOC_SIZE);
			strcpy(fileName, buffer);
			bytes = read(clientSocket, buffer, ALOC_SIZE);
			strcpy(fileSize, buffer);
			ifileSize = atoi(fileSize);
			receiveFile(userId, fileName, ifileSize, clientSocket);
		}
		if (strcmp(buffer, "download") == 0)
		{
			char fileName[ALOC_SIZE];
			bytes = read(clientSocket, buffer, ALOC_SIZE);
			strcpy(fileName, buffer);
			sendFile(userId, fileName, clientSocket);
		}
		if(strcmp(buffer, "delete") == 0){
			char fileName[ALOC_SIZE];
			int ifileSize;
			bytes = read(clientSocket, buffer, ALOC_SIZE);
			strcpy(fileName, buffer);
			deleteFile(userId, clientSocket, fileName);
			for(auto x: mSocketPropagate[userId]){
				sendMessage("delete",x);
			}
		}
		bzero(buffer,ALOC_SIZE);
		bytes = read(clientSocket, buffer, ALOC_SIZE);
	}
	closeSocket(userId);
}

int countFiles(string dirName)
{
    DIR *dir;
    struct dirent *dent;
    dir = opendir((const char *) dirName.c_str());

    string path, filename;
    int count= 0;
    if(dir!=NULL)
    {
        while((dent=readdir(dir))!=NULL){
            
            filename = dent->d_name;
            path = dirName + (string) "/" + filename;
            struct stat info;
            stat(path.c_str(), &info); // O primeiro argumento aqui é o caminho do arquivo
            if(dent->d_name[0] != '.'){
                count++;
            }
        }
        closedir(dir);
    }
    else{
        std::cout << "Erro na abertura do diretório\n" << std::endl;
    }
    return count;
}

void sendAllFiles(int userId, int syncSocket)
{
	pthread_mutex_lock(&m);

    sendMessage(to_string(countFiles(to_string(userId) + "/")),syncSocket);
    DIR *dir;
    struct dirent *dent;
    string dirName = to_string(userId);
    dir = opendir((const char *)dirName.c_str());

    string path, filename;

    if (dir != NULL)
    {
        while ((dent = readdir(dir)) != NULL)
        {
            filename = dent->d_name;
            path = dirName + (string) "/" + filename;
            if(filename[0] == '.')
                continue;

            sendMessage(filename, syncSocket);
            struct stat info;
            stat(path.c_str(), &info); // O primeiro argumento aqui é o caminho do arquivo
            if (dent->d_name[0] != '.')
            {
                sendFile(userId, filename, syncSocket);
            }
        }
        closedir(dir);
    }
    else
    {
        std::cout << "Erro na abertura do diretório\n"
                  << std::endl;
    }

	pthread_mutex_unlock(&m);
}



void listenSync(int userId, int clientSocket)
{
	int bytes;
	char buffer[ALOC_SIZE];
    bzero(buffer,ALOC_SIZE);
	bytes = read(clientSocket, buffer, ALOC_SIZE);
	if (bytes < 0)
		cout << "erro ao ler requisicao do cliente" << endl;

	while (strcmp(buffer, "exit") != 0)
	{
		if (strcmp(buffer, "upload") == 0)
		{
			char fileName[ALOC_SIZE];
			char fileSize[ALOC_SIZE];
			int ifileSize;
			bytes = read(clientSocket, buffer, ALOC_SIZE);
			strcpy(fileName, buffer);			
			bytes = read(clientSocket, buffer, ALOC_SIZE);
			strcpy(fileSize, buffer);
			ifileSize = atoi(fileSize);
			receiveFile(userId, fileName, ifileSize, clientSocket);
		}
		if (strcmp(buffer, "DOWNLOADALLFILES") == 0)
        {
            sendAllFiles(userId, clientSocket);
        }
        bzero(buffer,ALOC_SIZE);
		bytes = read(clientSocket, buffer, ALOC_SIZE);
	}
}

void *startClientThread(void *socketAd)
{
    int *socket = (int *)socketAd;
	int bytes;
	int userId;
	char buffer[ALOC_SIZE];
	bytes = read(*socket, buffer, ALOC_SIZE);
	userId = atoi(buffer);
	if(bytes < 0)
		cout << "erro ao ler userID" << endl;
	char isConnected = 'Y';

	mSockToUserId[*socket] = userId;
	mUserIdToSocks[userId].insert(*socket);

	sendMessage("Y",*socket);
	initClient(userId, *socket);
	listenClient(userId, *socket);
}

void *startSyncThread(void *socket)
{
	int *socketAdress = (int *)socket;
	int bytes;
	int userId;
	char buffer[ALOC_SIZE];
	bytes = read(*socketAdress, buffer, ALOC_SIZE);
	userId = atoi(buffer);

	if (userId < 0)
		cout << "Erro ao ler do socket" << endl;

	mUserIdToSocks[userId].insert(*socketAdress);

	char isConnected = 'Y';
	// Informa ao usuário que conseguiu conectar ao server

	sendMessage("Y",*socketAdress);
	if (bytes < 1)
	{
		cout << "Erro ao informar usuario" << endl;
	}

	listenSync(userId, *socketAdress);
}

void *startPropagateThread(void *socket)
{
	int *socketAdress = (int *)socket;

	int bytes;
	int userId;
	char buffer[ALOC_SIZE];

	bytes = read(*socketAdress, buffer, ALOC_SIZE);
	userId = atoi(buffer);
	if (userId < 0)
		cout << "Erro ao ler do socket" << endl;

	mUserIdToSocks[userId].insert(*socketAdress);
	mUserPropSock[userId] = *socketAdress;
	mSocketPropagate[userId].insert(*socketAdress);
	
	// Informa ao usuário que conseguiu conectar ao server
	char isConnected = 'Y';

	while(true){
        pthread_mutex_lock(&m);

		if(hasNewFile){
			for(auto x:mSocketPropagate[userId]){
				hasNewFile = false;
				bytes = sendMessage("propagate",x);
                if(bytes < 0){
                    cout << "ERRO AO ENVIAR PROPAGATE" << endl;
                }
			}
		}
        pthread_mutex_unlock(&m);
	}
}

void *startBackupThread(void *socket)
{
	int *socketAdress = (int *)socket;

	int bytes;
	int userId;
	char buffer[ALOC_SIZE];

	bytes = read(*socketAdress, buffer, ALOC_SIZE);
	
	while(true){
       if(strcmp(buffer,"DELETE") == 0){ //Primário mandando backup deletar arquivo
			bytes = read(*socketAdress, buffer, ALOC_SIZE);
			userId = atoi(buffer); //Le o userId
			char fileName[ALOC_SIZE];
			bytes = read(*socketAdress, buffer, ALOC_SIZE); //Le o nome do arquivo
			strcpy(fileName, buffer);
			deleteFile(userId,*socketAdress,fileName);
	   }else if(strcmp(buffer,"RECEIVE") == 0){//Primário enviando arquivo ao backup

	   }
	}
}

int main(int argc, char *argv[])
{

	bool isPrimary = false;
	int id;
	if(strcmp(argv[1],"0") == 0)
		isPrimary = true;
	id  = atoi(argv[1]);




	int sockfd, clientSockfd, n, primarySockfd;
	socklen_t clilen;
	struct sockaddr_in serv_addr, cli_addr;
	if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
		cout << "ERROR opening socket" << endl;

	serv_addr.sin_family = AF_INET;

	if(isPrimary)
		serv_addr.sin_port = htons(PORT);
	else
		serv_addr.sin_port = htons(PORT + id);
	
	serv_addr.sin_addr.s_addr = INADDR_ANY;

	bzero(&(serv_addr.sin_zero), 8);

	if (bind(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
		cout << "ERROR on binding" << endl;
	listen(sockfd, 5);

	clilen = sizeof(struct sockaddr_in);
	pthread_t clientThread, syncThread, propThread, backupThread;
	cout << "Server incializado..." << endl;

	if(!isPrimary){
		//Se conecta ao servidor primário
		primaryServer = gethostbyname(argv[2]);

		if((primarySockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
			std::cout << "ERROR while opening socket" << std::endl;

		serv_addr.sin_family = AF_INET;
		serv_addr.sin_port = htons(PORT);
		serv_addr.sin_addr = *((struct in_addr *)primaryServer->h_addr);
		bzero(&(serv_addr.sin_zero), 8);		
	}

	while (true)
	{
		if(isPrimary){ //Se é primário
			if ((clientSockfd = accept(sockfd, (struct sockaddr *)&cli_addr, &clilen)) == -1)
			{
				cout << "Erro ao aceitar" << endl;
			}
			else
			{
				bzero(buffer, ALOC_SIZE);

				int typeOfService;

				char buffer[ALOC_SIZE];
				read(clientSockfd, buffer, ALOC_SIZE);
				typeOfService = atoi(buffer);

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
				{ 	// Sincronização com cliente
					if (pthread_create(&syncThread, NULL, startSyncThread, &clientSockfd))
					{
						cout << "Erro ao abrir a thread do cliente" << endl;
					}
				}
				else if (typeOfService == 3)
				{ 	// Sincronização com cliente
					if (pthread_create(&propThread, NULL, startPropagateThread, &clientSockfd))
					{
						cout << "Erro ao abrir a thread do cliente" << endl;
					}
				}
			}
		}else{ //Se é backup
			if (pthread_create(&backupThread, NULL, startBackupThread, &primaryServer))
			{
				cout << "Erro ao abrir a thread do cliente" << endl;
			}

		}
	}
	return 0;
}