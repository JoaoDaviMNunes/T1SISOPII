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
#include <dirent.h>
#include <experimental/filesystem>
#include <map>
#include <set> 

using namespace std;

#define PORT 4000

map<int,int > mSockToUserId;
map<int,set<int> > mUserIdToSocks;
map<int,int> mUserPropSock;

pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;

bool hasNewFile = false;

char buffer[10000];
void openSocket()
{

}

void initClient(int userId, int clientSocket)
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
void sendMessage(std::string message, int clientSocket){
			char buffer[10000];
			strcpy(buffer,message.c_str());

			int bytes = write(clientSocket , buffer , 10000*sizeof(char));
		}

void closeSocket(int userId){
	//close(sync_sock);
	for(auto x:mUserIdToSocks[userId]){
		cout << x << endl;
		sendMessage("exit",x);
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
	ofstream file;
	string dir = to_string(userId) + "/" + fileName;
	file.open(dir);
	int bytes;
	char buffer[10000];
	 cout << dir << " " << fileName << endl << fileSize << endl;
	if (fileSize > 0)
	{

		while (fileSize > 0)
		{
			bytes = read(clientSocket, buffer, 10000);
			cout << buffer << endl;
			//file.write(buffer, min(fileSize, 10000)); Certo
			file.write(buffer, min(10000,fileSize));


			fileSize -= 10000;
		}
	}
	file.close();
	hasNewFile = true;
	pthread_mutex_unlock(&m);
}

void sendFile(int userId,string filepath, int clientSocket)
{
	FILE *file;
	int fileSize;
	int bytes;
	// string filename;
	// char ch = '/';

	string dir = to_string(userId)+"/"+filepath;
	if ((file = fopen(dir.c_str(), "rb")))
	{
		// envia requisição de envio para o servidor
		fileSize = getFileSize(to_string(userId)+"/"+filepath);


		// envia o nome do arquivo para o servidor
		// filename = filepath.substr(filepath.find_last_of(ch) + 1 ,filepath.length()- filepath.find(ch));
		// this->sendMessage(filename);

		// waitConfirm();
		sendMessage(to_string(fileSize),clientSocket);
		cout << filepath << endl;
		cout << fileSize << endl;

		char data[10000];
		while (!feof(file))
		{
			fread(data, sizeof(data), 1, file);

			bytes = write(clientSocket, data, 10000);
			if (bytes < 0)
				cout << "erro ao enviar arquivo" << endl;
			fileSize -= 10000;
		}
		fclose(file);
	}else{

		sendMessage("erro",clientSocket);
	}
}

void listenClient(int userId, int clientSocket)
{
	cout << "listen client" << endl;
	int bytes;
	char buffer[10000];
	char confirm[10] = "ok";

	bytes = read(clientSocket, buffer, 10000);
	cout << "ListenClient: " << buffer << endl;

	//cout << buffer << endl;
	if (bytes < 0)
		cout << "erro ao ler requisicao do cliente" << endl;

	while (strcmp(buffer, "exit") != 0)
	{
		if (strcmp(buffer, "upload") == 0)
		{
			char fileName[10000];
			char fileSize[10000];
			int ifileSize;
			cout << "waiting " << endl;
			bytes = read(clientSocket, buffer, 10000);
			strcpy(fileName, buffer);
			cout << "Filename upload: " << buffer << endl;
			// send(*clientSocket,confirm,sizeof(confirm),0);
			bytes = read(clientSocket, buffer, 10000);
			// cout << buffer << endl;
			strcpy(fileSize, buffer);
			ifileSize = atoi(fileSize);
			cout << "Filesize upload: " << buffer << endl;
			receiveFile(userId, fileName, ifileSize, clientSocket);

			
		}
		if (strcmp(buffer, "download") == 0)
		{
			cout << "here" << endl;
			char fileName[10000];
			// cout << buffer << endl;
			bytes = read(clientSocket, buffer, 10000);
			strcpy(fileName, buffer);
			cout << buffer << endl;
			sendFile(userId, fileName, clientSocket);
		}
		if(strcmp(buffer, "delete") == 0){
			char fileName[10000];
			int ifileSize;
			bytes = read(clientSocket, buffer, 10000);
			strcpy(fileName, buffer);
			sendMessage("delete",mUserPropSock[userId]);
			deleteFile(userId, clientSocket, fileName);
		}
		memset(buffer,0,10000);
		bytes = read(clientSocket, buffer, 10000);
	}
	closeSocket(userId);
}

void *startClientThread(void *socketAd)
{
    int *socket = (int *)socketAd;
	int bytes;
	int userId;
	char buffer[10000];
	// Le userId
	bytes = read(*socket, buffer, 10000);
	userId = atoi(buffer);
	if(bytes < 0)
		cout << "erro ao ler userID" << endl;
	char isConnected = 'Y';
	// Informa ao usuário que conseguiu conectar ao server

	mSockToUserId[*socket] = userId;
	mUserIdToSocks[userId].insert(*socket);

	sendMessage("Y",*socket);
	initClient(userId, *socket);
	listenClient(userId, *socket);
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

            // O uso dessas strings aqui é uma gambiarra pra pegar o caminho de cada arquivo
            filename = dent->d_name;
            path = dirName + (string) "/" + filename;

            struct stat info;
            stat(path.c_str(), &info); // O primeiro argumento aqui é o caminho do arquivo
            if(dent->d_name[0] != '.'){
                count++;

            }
        }
        //fflush(stdout);
        closedir(dir);
    }
    else{
        std::cout << "Erro na abertura do diretório\n" << std::endl;
    }
    return count;
}

void sendAllFiles(int userId, int syncSocket)
{
	cout << to_string(userId) + "/" << endl;

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
            // O uso dessas strings aqui é uma gambiarra pra pegar o caminho de cada arquivo
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
        // fflush(stdout);
        closedir(dir);
    }
    else
    {
        std::cout << "Erro na abertura do diretório\n"
                  << std::endl;
    }
    cout << "finished send all files" << endl;
}



void listenSync(int userId, int clientSocket)
{
	int bytes;
	char buffer[10000];

	bytes = read(clientSocket, buffer, 10000);
	cout << "ListenSync: " << buffer << endl;
	if (bytes < 0)
		cout << "erro ao ler requisicao do cliente" << endl;

	while (strcmp(buffer, "exit") != 0)
	{
		if (strcmp(buffer, "upload") == 0)
		{
			char fileName[10000];
			char fileSize[10000];
			int ifileSize;
			bytes = read(clientSocket, buffer, 10000);
			strcpy(fileName, buffer);
			// send(*clientSocket,confirm,sizeof(confirm),0);
			bytes = read(clientSocket, buffer, 10000);
			strcpy(fileSize, buffer);
			ifileSize = atoi(fileSize);
			receiveFile(userId, fileName, ifileSize, clientSocket);
			
		}
		if (strcmp(buffer, "DOWNLOADALLFILES") == 0)
        {
            cout << "DOWNLOAD ALL FILES" << endl;
			cout << "user Id = " << userId << endl;
            sendAllFiles(userId, clientSocket);
        }
		
        memset(buffer,0,10000);
		bytes = read(clientSocket, buffer, 10000);
	}
	//sendMessage("exit",clientSocket);
}
void *startSyncThread(void *socket)
{
	int *socketAdress = (int *)socket;
	int bytes;
	int userId;
	char buffer[10000];
	// Le userId
	bytes = read(*socketAdress, buffer, 10000);
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
	char buffer[10000];
	// Le userId
	bytes = read(*socketAdress, buffer, 10000);
	userId = atoi(buffer);
	if (userId < 0)
		cout << "Erro ao ler do socket" << endl;

	mUserIdToSocks[userId].insert(*socketAdress);
	mUserPropSock[userId] = *socketAdress;

	char isConnected = 'Y';
	// Informa ao usuário que conseguiu conectar ao server

	sendMessage("Y",*socketAdress);
	// Le userId
	while(true){
		if(hasNewFile){
			cout << "propagate" << endl;
			hasNewFile = false;
			sendMessage("propagate",*socketAdress);
		}
	}
}


int main(int argc, char *argv[])
{
int sockfd, clientSockfd, n;
socklen_t clilen;
struct sockaddr_in serv_addr, cli_addr;
if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
		cout << "ERROR opening socket" << endl;

	serv_addr.sin_family = AF_INET;
	serv_addr.sin_port = htons(PORT);
	serv_addr.sin_addr.s_addr = INADDR_ANY;
	bzero(&(serv_addr.sin_zero), 8);

	if (bind(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
		cout << "ERROR on binding" << endl;
	listen(sockfd, 5);

	clilen = sizeof(struct sockaddr_in);
	pthread_t clientThread, syncThread, propThread;
	cout << "Server incializado..." << endl;
	while (true)
	{
		if ((clientSockfd = accept(sockfd, (struct sockaddr *)&cli_addr, &clilen)) == -1)
		{
			cout << "Erro ao aceitar" << endl;
		}
		else
		{
			bzero(buffer, 10000);

			int typeOfService;
			/* read from the socket */
			char buffer[10000];
			read(clientSockfd, buffer, 10000);
			typeOfService = atoi(buffer);
			if (typeOfService < 0)
				cout << "Erro ao ler do socket" << endl;

			if (typeOfService == 1) // Atende request do cliente
			{
				cout << "Starting client thread" << endl;
				if (pthread_create(&clientThread, NULL, startClientThread, &clientSockfd))
				{
					cout << "Erro ao abrir a thread do cliente" << endl;
				}
			}
			else if (typeOfService == 2)
			{ // Sincronização com cliente
				cout << "Start Sync Thread" << endl;
				if (pthread_create(&syncThread, NULL, startSyncThread, &clientSockfd))
				{
					cout << "Erro ao abrir a thread do cliente" << endl;
				}
			}
			else if (typeOfService == 3)
			{ // Sincronização com cliente
				cout << "Start Propagate Thread" << endl;
			
				if (pthread_create(&propThread, NULL, startPropagateThread, &clientSockfd))
				{
					cout << "Erro ao abrir a thread do cliente" << endl;
				}
			}
		}
	}

	return 0;
}
