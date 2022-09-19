#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <signal.h>
#include <errno.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <pthread.h>
#include <sys/stat.h>
#include <iostream>
#include <fstream>
#include <dirent.h>
#include <experimental/filesystem>
#include <map>
#include <set>
#include <chrono>

using namespace std;

#define PORT 4000
#define PORT_OFFSET 100
#define ALOC_SIZE 512

map<int,int > mSockToUserId;
map<int,set<int> > mUserIdToSocks;
map<int,int> mUserPropSock;
map<int,set<int> > mSocketPropagate;

map<int,int> socketBackup; //Chave: Id , Valor: socket do servidor backup //Server para o primário repassar as infos para os backups
map<int,string> backupToIp; //Chave: socketOfBkacup , idOfBackup;

pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
bool hasDeleteFile = false;
bool hasNewFile = false;
string fileNameToPropagate;

set<string> devicesAddress; // Salva endereços ips dos devices para reconexão

set<string> listOfBackupsIp;

bool doingElection = false;

int id;

struct CliInfo {
	int sockfd;
	struct sockaddr_in* cli_addr;
};

char buffer[ALOC_SIZE];

struct hostent *primaryServer;

struct sockaddr_in serv_addr, cli_addr;

pthread_t clientThread, syncThread, propThread, backupThread, aliveThread, electionThread;

bool isPrimary = false;

int sockfd, clientSockfd, n, primarySockfd;

void sendNewPrimary() {
    char hostbuffer[256];
    char IPbuffer[256];
    struct hostent *host_entry;
    int hostname;

    // To retrieve hostname
    hostname = gethostname(hostbuffer, sizeof(hostbuffer));
    // To retrieve host information
    host_entry = gethostbyname(hostbuffer);
    // To convert an Internet network
    // address into ASCII string
    inet_ntop( AF_INET, &serv_addr.sin_addr, IPbuffer, INET_ADDRSTRLEN );
    //IPbuffer = inet_ntoa(*((struct in_addr*)
    //                       host_entry->h_addr_list[0]));
    cout << "fazendo novas conexões" << endl;
	int ip_size = strlen(IPbuffer);
	cout << "IPbuffer " << IPbuffer << endl;

	struct hostent *device_frontend;


	for(auto device_ip : devicesAddress) {
		int frontend_sock;
		struct sockaddr_in frontend_addr;
		device_frontend = gethostbyname(device_ip.c_str());
		if (device_frontend == NULL) {
			fprintf(stderr,"ERROR, no such host\n");
			continue;
		}

		if ((frontend_sock = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
			printf("ERROR opening socket with frontend\n");
			continue;
		}

		frontend_addr.sin_family = AF_INET;
		frontend_addr.sin_port = htons(PORT+PORT_OFFSET);
		frontend_addr.sin_addr = *((struct in_addr *)device_frontend->h_addr);
		bzero(&(frontend_addr.sin_zero), 8);

		cout << "conectando-se com " << device_ip << endl;

		if (connect(frontend_sock,(struct sockaddr *) &frontend_addr,sizeof(frontend_addr)) < 0)
        	printf("ERROR connecting\n");

		cout << "conectado a " << device_ip << endl;
        cout << "IP BUFFER " << IPbuffer << endl;
		write(frontend_sock, IPbuffer, ip_size);

		close(frontend_sock);
	}
}

int sendMessage(std::string message, int clientSocket){
    char buffer[ALOC_SIZE];

    strcpy(buffer,message.c_str());
    int bytes = write(clientSocket , buffer , ALOC_SIZE);
	if(bytes < 0){
		cout << "err" << endl;
	}

    return bytes;
}


void sendInitClientToBackup(int userId){

    for(auto const& backupServer : socketBackup){
        sendMessage("INIT",backupServer.second);
        sendMessage(to_string(userId),backupServer.second);
    }
}

void initClient(int userId)
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
	sendInitClientToBackup(userId);

}


int getFileSize(string filepath){
        ifstream in(filepath, std::ifstream::ate | std::ifstream::binary);
    		return in.tellg();
}

void closeSocket(int userId){

	for(auto x:mUserIdToSocks[userId]){
		cout << x << endl;
		close(x);
	}
	cout << "All sockets closed" << endl;
}
void deleteFileToBackup(int userId, string filename){

	for(auto const& backupServer : socketBackup){
        cout << "SEND DELETE" << endl;
		sendMessage("DELETE",backupServer.second);
		sendMessage(to_string(userId),backupServer.second);
		sendMessage(filename,backupServer.second);
	}
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

//Quando servidor primário recebe um arquivo ele envia esse arquivo para os backups
void sendFileToBackup(int userId, string filename){

	for(auto const& backupServer : socketBackup){
		sendMessage("RECEIVE",backupServer.second);
		sendMessage(to_string(userId),backupServer.second);
		sendMessage(filename,backupServer.second);
		sendFile(userId,filename,backupServer.second);
	}
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
	fileNameToPropagate = fileName;
	sendFileToBackup(userId, fileName);
	pthread_mutex_unlock(&m);
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
			fileNameToPropagate = fileName;
            hasDeleteFile = true;

            deleteFileToBackup(userId,fileName);
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

void sendCliAddrToBackups(struct sockaddr_in* cli_addr) {
	// Pega o endereço ip do cliente
	char str[INET_ADDRSTRLEN];
	inet_ntop( AF_INET, &cli_addr->sin_addr, str, INET_ADDRSTRLEN );
	cout << "novo device: " << str << endl;
	// envia o endereço para os backups (o primário não precisa salvar)
	for (auto const& backup_pair : socketBackup) {
		if (write(backup_pair.second, str, INET_ADDRSTRLEN) < 0)
			cout << "Erro ao enviar endereço do device para o backup" << endl;
	}
}

void *startClientThread(void *client_info)
{
	struct CliInfo* cli_info = (struct CliInfo*) client_info;
    int socket = cli_info->sockfd;
	int bytes;
	int userId;
	char buffer[ALOC_SIZE];
	bytes = read(socket, buffer, ALOC_SIZE);
	userId = atoi(buffer);
	if(bytes < 0)
		cout << "erro ao ler userID" << endl;
	char isConnected = 'Y';

	mSockToUserId[socket] = userId;
	mUserIdToSocks[userId].insert(socket);

	sendMessage("Y",socket);
	initClient(userId);
	sendCliAddrToBackups(cli_info->cli_addr);
	listenClient(userId, socket);
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

		if(hasNewFile || hasDeleteFile){
			for(auto x:mSocketPropagate[userId]){
                cout << "PROPAGATE" << endl;
				bytes = sendMessage("propagate",x);

                if(bytes < 0){
                    cout << "ERRO AO ENVIAR PROPAGATE" << endl;
                }
                if(hasNewFile){
                    cout << "Enviou propagate upload" << endl;
                    bytes = sendMessage("upload",x);
                    bytes = sendMessage(fileNameToPropagate,x);
                }
                if(hasDeleteFile){
                    cout << "Enviou propagate delete" << endl;
                    bytes = sendMessage("delete",x);
                    cout << "Deletar: " << fileNameToPropagate << endl;
                    bytes = sendMessage(fileNameToPropagate,x);
                }
			}
			hasNewFile = false;
            hasDeleteFile = false;
		}
        pthread_mutex_unlock(&m);
	}
}

void printSet(set<string> s){
	cout << "Set: ";
	for(auto x : s){
		cout << x << " ";
	}
	cout << endl;
}

void printMap(map<int,int> m){
	for(auto const& backupServer : socketBackup){
		cout << backupServer.first << " - " << backupServer.second << endl;
	}
}

void *startAliveThread(void *socket)
{
	int *socketAdress = (int *)socket;

	int bytes;
	char buffer[ALOC_SIZE];
	while(true){

		set<string> listOfBackupsAlive;

		printMap(socketBackup);
		printSet(listOfBackupsAlive);
		for(auto const& backupServer : socketBackup){

			if(sendMessage("ALIVE",backupServer.second) >=  0){
				listOfBackupsAlive.insert(backupToIp[backupServer.second]);
			}else{
				cout << "Erro ao enviar alive ao backup: " << backupServer.first << endl;
				listOfBackupsAlive.erase(listOfBackupsAlive.find(backupToIp[backupServer.second]));

				socketBackup.erase(backupServer.first);
				break;

			}
			struct timeval timeout;
			timeout.tv_sec = 2;
			timeout.tv_usec = 0;
			setsockopt(backupServer.second, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

			bytes = recv(backupServer.second, buffer, ALOC_SIZE, 0);
			if (bytes == -1)
			{
				if ((errno != EAGAIN) && (errno != EWOULDBLOCK)){
					listOfBackupsAlive.erase(listOfBackupsAlive.find(backupToIp[backupServer.second]));

					socketBackup.erase(backupServer.first);
					cout << "TIMEOUT" << endl;
					break;
				}
			}

		}
		printMap(socketBackup);
		printSet(listOfBackupsAlive);
		//Passando para cada backup uma "lista" de ips:porta de todos backups vivos conectados ao server primario
		for(auto const& backupServer : socketBackup){
			bool failed = false;
			if(sendMessage("LISTBACKUP",backupServer.second) < 0){
				cout << "Erro ao enviar LISTBACKUP" << endl;
				listOfBackupsAlive.erase(listOfBackupsAlive.find(backupToIp[backupServer.second]));
				socketBackup.erase(backupServer.first);

				break;
			}

			int qtOfBackupIps = listOfBackupsAlive.size();
			if(sendMessage(to_string(qtOfBackupIps),backupServer.second) < 0){
				cout << "Erro ao enviar qtOfBackupIps" << endl;
				listOfBackupsAlive.erase(listOfBackupsAlive.find(backupToIp[backupServer.second]));
				socketBackup.erase(backupServer.first);

				break;
			}
			for(auto backupIp : listOfBackupsAlive){
				cout << backupIp << endl;
				if(sendMessage(backupIp, backupServer.second) < 0){
					cout << "erro ao enviar backup ip" << endl;
					listOfBackupsAlive.erase(listOfBackupsAlive.find(backupToIp[backupServer.second]));
					socketBackup.erase(backupServer.first);
					failed = true;

					break;
				}
			}
			printSet(listOfBackupsAlive);
			if(failed)
				break;
		}
		sleep(2);
	}
}
void *startElectionThread(void *socketRec)
{
	int *socketAdress = (int *)socketRec;
	int bytes;
	int idRead;
	int qtOfDynamicList;
	int idOfFirst;
	set<string> dynamicList = listOfBackupsIp;


	while(true){
		bytes = read(*socketAdress, buffer, ALOC_SIZE);
		idRead = stoi(buffer);

		string ipOfNext ="-";
		int minPort = 1e9;

		if(idRead == id){//Elected
			cout << idRead <<" : eleito" << endl;
            sendNewPrimary();
			break;
		}

		for(auto ip: dynamicList){
			string portStr = ip.substr(ip.find(':')+1,4);
			int port = stoi(portStr);
			if(port > PORT + id){
				minPort = min(minPort,port);
				ipOfNext = ip.substr(0,ip.find(":"));
			}
		}

		//There is no port greater than mine

		if(ipOfNext == "-"){
			for(auto ip:dynamicList){
				string portStr = ip.substr(ip.find(':')+1,4);
				int port = stoi(portStr);
				if(port < minPort){
					minPort = min(minPort,port);
					ipOfNext = ip.substr(0,ip.find(":"));
				}
			}
		}


		int sockOfNextBackup;
		struct sockaddr_in serv_addr_backup;
		struct hostent *server_backup;

		server_backup = gethostbyname(ipOfNext.c_str());
		int bytes;

		if(server_backup == NULL){
			cout << "erro ao criar sync socket 1 " << endl;

		}
		if((sockOfNextBackup = socket(AF_INET, SOCK_STREAM, 0)) == -1){
			cout << "erro ao criar sync socket 2 " << endl;

		}

		serv_addr_backup.sin_family = AF_INET;
		serv_addr_backup.sin_port = htons(minPort);
		serv_addr_backup.sin_addr = *((struct in_addr *)server_backup->h_addr);

		bzero(&(serv_addr_backup.sin_zero), 8);

		if (connect(sockOfNextBackup,(struct sockaddr *) &serv_addr_backup,sizeof(serv_addr_backup)) < 0){
			cout << "erro ao conectar sync_sock" << endl;
		}
		//Connected on the next of the ring

		//Send message
		bytes = sendMessage("5",sockOfNextBackup); //5 = typeOfService ELECTION
		if(idRead > id){
			//Send my id
			bytes = sendMessage(to_string(id),sockOfNextBackup);

		}else{
			bytes = sendMessage(to_string(idRead),sockOfNextBackup);
		}



	}
}

void doElection(){

	string ipOfNext ="-";
	int minPort = 1e9;


	if(listOfBackupsIp.size() == 1){//Only alive
		cout << id <<" Eleito" << endl;
		isPrimary = true;
		sendNewPrimary();
		return;

	}else{ // Send message to the next of the ring
		for(auto ip : listOfBackupsIp){
			string portStr = ip.substr(ip.find(':')+1,4);
			int port = stoi(portStr);
			if(port > PORT + id){
				minPort = min(minPort,port);
				ipOfNext = ip.substr(0,ip.find(":"));
			}

			//if(port != PORT + id){
			//	dynamicList.insert(ip);
			//}
		}
		if(ipOfNext == "-"){
			for(auto ip:listOfBackupsIp){
				string portStr = ip.substr(ip.find(':')+1,4);
				int port = stoi(portStr);
				if(port < minPort){
					minPort = min(minPort,port);
					ipOfNext = ip.substr(0,ip.find(":"));
				}
			}
		}
	}



	int sockOfNextBackup;
	struct sockaddr_in serv_addr_backup;
	struct hostent *server_backup;

	cout << "IPOFNEXT: " << ipOfNext << ":" << minPort << endl;

	server_backup = gethostbyname(ipOfNext.c_str());
    int bytes;

    if(server_backup == NULL){
        cout << "erro ao achar server backup " << endl;

    }
    if((sockOfNextBackup = socket(AF_INET, SOCK_STREAM, 0)) == -1){
        cout << "erro ao criar socket backup " << endl;

    }

    serv_addr_backup.sin_family = AF_INET;
    serv_addr_backup.sin_port = htons(minPort);
    serv_addr_backup.sin_addr = *((struct in_addr *)server_backup->h_addr);

    bzero(&(serv_addr_backup.sin_zero), 8);

    if (connect(sockOfNextBackup,(struct sockaddr *) &serv_addr_backup,sizeof(serv_addr_backup)) < 0){
        cout << "erro ao conectar com backup " << endl;
    }else{
		cout << "Conectado ao proximo backup com sucesso " << endl;

	}
	//Connected on the next of the ring

	//Send message
	bytes = sendMessage("5",sockOfNextBackup); //5 = typeOfService ELECTION
	if(bytes < 0){
		cout << "erro ao enviar typeOfService para backup" << endl;
	}
	//Send my id
	bytes = sendMessage(to_string(id),sockOfNextBackup);
	if(bytes < 0){
		cout << "erro ao enviar id para backup" << endl;
	}




}

void *startBackupThread(void *socket)
{
	int *socketAdress = (int *)socket;

	int bytes;
	int userId;
	int serverId;
	char buffer[ALOC_SIZE];



	bytes = read(*socketAdress, buffer, ALOC_SIZE);
	serverId = atoi(buffer);
	std::chrono::steady_clock::time_point begin = std::chrono::steady_clock::now();
	std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
	while(true){



       if(strcmp(buffer,"DELETE") == 0){ //Primário mandando backup deletar arquivo
			bytes = read(*socketAdress, buffer, ALOC_SIZE);
			userId = atoi(buffer); //Le o userId
			char fileName[ALOC_SIZE];
			bytes = read(*socketAdress, buffer, ALOC_SIZE); //Le o nome do arquivo
			strcpy(fileName, buffer);
			deleteFile(userId,*socketAdress,fileName);
			begin = std::chrono::steady_clock::now();
	   }else if(strcmp(buffer,"RECEIVE") == 0){//Primário enviando arquivo ao backup
			bytes = read(*socketAdress, buffer, ALOC_SIZE);
			userId = atoi(buffer); //Le o userId
			char fileName[ALOC_SIZE];
			bytes = read(*socketAdress, buffer, ALOC_SIZE); //Le o nome do arquivo
			strcpy(fileName, buffer);
			bytes = read(*socketAdress,buffer,ALOC_SIZE);
			int fileSize = atoi(buffer);
			receiveFile(userId,fileName,fileSize,*socketAdress);
			begin = std::chrono::steady_clock::now();

	   }else if(strcmp(buffer,"ALIVE") == 0){
			begin = std::chrono::steady_clock::now();
			sendMessage("OK",*socketAdress);
             for(auto ip:devicesAddress){
                cout << "Device: " << ip << endl;
             }

	   }else if(strcmp(buffer,"LISTBACKUP") == 0){

			begin = std::chrono::steady_clock::now();
			bytes = read(*socketAdress,buffer,ALOC_SIZE);
			int qtOfBackupIps = atoi(buffer);
			cout << "qtBackups: " << qtOfBackupIps << endl;
			for(int i = 0; i < qtOfBackupIps; i++){
				bytes = read(*socketAdress,buffer,ALOC_SIZE);
				listOfBackupsIp.insert(buffer);
				cout << "Ip: " << buffer << endl;
			}
       }

	   else if(strcmp(buffer,"INIT") == 0){
            begin = std::chrono::steady_clock::now();
            bytes = read(*socketAdress, buffer, ALOC_SIZE);
			userId = atoi(buffer); //Le o userId
            initClient(userId);
			char addr[INET_ADDRSTRLEN] = "";
			bytes = read(*socketAdress,addr,INET_ADDRSTRLEN);
			if (bytes<0) {
				cout << "cannot read device address" << endl;
			}
			devicesAddress.insert(string(addr));
			cout << "novo device: " << addr << endl;
	   }
	   end = std::chrono::steady_clock::now();
	   if(std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count() > 2000){
			//do election
			if(doingElection == false){
				cout << "Primario caiu" << endl;

				cout << "Doing election..." << endl;
				doElection();
				if(isPrimary){
                    pthread_cancel(backupThread);
				}
				doingElection = true;
			}
	   }
	   //cout << std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count() << endl;
		bzero(buffer, ALOC_SIZE);
		bytes = read(*socketAdress, buffer, ALOC_SIZE);
        //cout << "COMANDO: " << buffer << endl;
	}
}

int main(int argc, char *argv[])
{
	doingElection = false;

	if(strcmp(argv[1],"0") == 0)
		isPrimary = true;
	id  = atoi(argv[1]);



	socklen_t clilen;

	if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
		cout << "ERROR opening socket" << endl;

	serv_addr.sin_family = AF_INET;

	if(isPrimary)
		serv_addr.sin_port = htons(PORT);
	else
		serv_addr.sin_port = htons(PORT + id);

	serv_addr.sin_addr.s_addr = INADDR_ANY;

	bzero(&(serv_addr.sin_zero), 8);

	if (bind(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0){
		cout << "ERROR on binding" << endl;
		return 0;
	}
	listen(sockfd, 5);

	clilen = sizeof(struct sockaddr_in);

	if(isPrimary)
		cout << "Server primary inicializado..." << endl;
	else
		cout << "Server backup inicializado..." << endl;

	if(!isPrimary){
		cout << "Tentando se conectar ao primario" << endl;
		//Se conecta ao servidor primário
		primaryServer = gethostbyname(argv[2]);

		if((primarySockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
			std::cout << "ERROR while connecting to primary server" << std::endl;

		serv_addr.sin_family = AF_INET;
		serv_addr.sin_port = htons(PORT);
		serv_addr.sin_addr = *((struct in_addr *)primaryServer->h_addr);
		bzero(&(serv_addr.sin_zero), 8);

    	if (connect(primarySockfd,(struct sockaddr *) &serv_addr,sizeof(serv_addr)) < 0){
      	  cout << "erro ao conectar sync_sock" << endl;
   		 }
		cout << "conectado" << endl;
		sendMessage("4",primarySockfd); //Type of service = 4 : Backup
		sendMessage(to_string(id),primarySockfd); //Envia id do servidor backup para o primario
	}
	if(!isPrimary){
		if (pthread_create(&backupThread, NULL, startBackupThread, &primarySockfd))
		{
			cout << "Erro ao abrir a thread do cliente" << endl;
		}
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

			    cout << "Novo device conectado" << endl;
				bzero(buffer, ALOC_SIZE);

				int typeOfService;

				char buffer[ALOC_SIZE];
				read(clientSockfd, buffer, ALOC_SIZE);
				typeOfService = atoi(buffer);

				if (typeOfService < 0)
					cout << "Erro ao ler do socket" << endl;

				if (typeOfService == 1) // Atende request do cliente
				{
					struct CliInfo cli_info = {clientSockfd, &cli_addr};
					if (pthread_create(&clientThread, NULL, startClientThread, &cli_info))
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
				else if(typeOfService == 4){
                    //Le o id do servidor backup e armazena o socket no map socketBackup
                    int bytes;
                    int serverId;
                    char buffer[ALOC_SIZE];

                    bytes = read(clientSockfd, buffer, ALOC_SIZE);
                    serverId = atoi(buffer);
                    socketBackup[serverId] = clientSockfd;
                    char str[INET_ADDRSTRLEN];
                    string port = to_string(ntohs(cli_addr.sin_port));
                    inet_ntop( AF_INET, &cli_addr.sin_addr, str, INET_ADDRSTRLEN );
                    backupToIp[clientSockfd] = str;
                    backupToIp[clientSockfd] += ":" + to_string(4000 + serverId);

                    cout << backupToIp[clientSockfd] << endl;

                    cout << "Backup " << serverId << " conectado" << endl;

                    if (pthread_create(&aliveThread, NULL, startAliveThread, &primarySockfd))
                    {
                        cout << "Erro ao abrir a thread do cliente" << endl;
                    }
                }
			}
		}else{ //Se é backup
                if ((clientSockfd = accept(sockfd, (struct sockaddr *)&cli_addr, &clilen)) == -1)
                {
                    cout << "Erro ao aceitar" << endl;
                }
                bzero(buffer, ALOC_SIZE);

				int typeOfService;

				char buffer[ALOC_SIZE];
				read(clientSockfd, buffer, ALOC_SIZE);
				typeOfService = atoi(buffer);
                if(typeOfService == 5){//ELECTION
					cout << "Conexao de eleicao recebida" << endl;
					int bytes;
                    int serverId;
                    char buffer[ALOC_SIZE];

					if (pthread_create(&electionThread, NULL, startElectionThread, &clientSockfd))
                    {
                        cout << "Erro ao abrir a thread do cliente" << endl;
                    }
				}
		}

	}
	return 0;
}
