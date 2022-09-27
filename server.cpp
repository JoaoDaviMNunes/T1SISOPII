#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <ifaddrs.h>
#include <signal.h>
#include <errno.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <pthread.h>
#include <semaphore.h>
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

sem_t* initElectionSemaphore; // Semaforo para iniciar eleição
sem_t* endElectionSemaphore; // Semaforo para finalizar eleição


map<int,int > mSockToUserId;
map<int,set<int> > mUserIdToSocks;
map<int,int> mUserPropSock;
map<int,set<int> > mSocketPropagate;

map<int,int> socketBackup; //Chave: Id , Valor: socket do servidor backup //Server para o primário repassar as infos para os backups
map<int,string> backupToIp; //Chave: socketOfBkacup , idOfBackup;

pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;

bool hasDeleteFile = false;
bool hasNewFile = false;
bool openedBackupThread = false;

string fileNameToPropagate;

set<string> devicesAddress; // Salva endereços ips dos devices para reconexão

set<string> listOfBackupsIp;

struct DeviceSockets {
  int sockfd;
  int gsyncSock;
  int gpropSock;
};

map<int, map <int, DeviceSockets>> userDevices;

// para acessar
// userDevices[userId][sockfd];

int id;


struct CliInfo {
	int sockfd;
	struct sockaddr_in* cli_addr;
};

char buffer[ALOC_SIZE];

struct hostent *primaryServer;

struct sockaddr_in serv_addr, cli_addr;
struct sockaddr_in serv_addr_backup;

pthread_t clientThread, syncThread, propThread, backupThread, aliveThread, electionThread, backupWaitThread;

bool isPrimary = false;

bool doingElection = false;

int sockfd, clientSockfd, n, primarySockfd, prevBackupSockfd;


void backupConnectToNewPrimary(int electedId);
void doElection(int nextBackupSockfd);
void doConnectionForElection();
void *startBackupWaitThread(void *client_info);
void *startBackupThread(void *socket);


string getethname () {
    struct ifaddrs * ifAddrStruct=NULL;
    struct ifaddrs * ifa=NULL;
    void * tmpAddrPtr=NULL;

    getifaddrs(&ifAddrStruct);
    char addressBuffer[INET_ADDRSTRLEN];
    for (ifa = ifAddrStruct; ifa != NULL; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr) {
            continue;
        }
        if (ifa->ifa_addr->sa_family == AF_INET && strcmp(ifa->ifa_name, "eth0") == 0)  { // check it is IP4
            // is a valid IP4 Address
            tmpAddrPtr=&((struct sockaddr_in *)ifa->ifa_addr)->sin_addr;
            inet_ntop(AF_INET, tmpAddrPtr, addressBuffer, INET_ADDRSTRLEN);
            printf("%s", addressBuffer);
        }
    }
    if (ifAddrStruct!=NULL) freeifaddrs(ifAddrStruct);
    return addressBuffer;
}

void sendNewPrimary() { // para o cliente
    string IPbuffer;


    // To retrieve hostname

    //IPbuffer = inet_ntoa(*((struct in_addr*)
    //                       host_entry->h_addr_list[0]));
    IPbuffer = getethname();
    cout << "fazendo novas conexões" << endl;
	int ip_size = IPbuffer.size();
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
		write(frontend_sock, IPbuffer.c_str(), ALOC_SIZE);

		int primaryId = PORT + id;

		cout << "Enviou primaryId : " << primaryId << endl;
		write(frontend_sock, to_string(primaryId).c_str(), ALOC_SIZE);

		close(frontend_sock);
	}
	cout << "fim dos avisos" << endl;
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

void listServer(int userId, int clientSocket)
{

	string buffer = "";
	int bytes;

	char directory[256];
    getcwd(directory, 256);

	string dirName = string(directory);
	dirName += "/";
	dirName += to_string(userId);

	//cout << dirName << endl;

    DIR *dir;
    struct dirent *dent;
    dir = opendir((const char *) dirName.c_str());

    string path, filename;

    if(dir!=NULL)
    {
        while((dent=readdir(dir))!=NULL)
        {

            // O uso dessas strings aqui é uma gambiarra pra pegar o caminho de cada arquivo
            filename = dent->d_name;
            path = dirName + (string) "/" + filename;

            struct stat info;
            stat(path.c_str(), &info); // O primeiro argumento aqui é o caminho do arquivo
            if(dent->d_name[0] != '.')
            {
				buffer += "> Arquivo: ";
				buffer += dent->d_name ;
				buffer += "\n";
				bytes = write(clientSocket, buffer.c_str(),ALOC_SIZE);
				buffer = "";

				buffer += "  Modification Time: ";
				buffer +=  (4+ctime(&info.st_mtime));
				bytes = write(clientSocket, buffer.c_str(),ALOC_SIZE);
				buffer = "";

				buffer += "  Access Time: ";
				buffer +=  (4+ctime(&info.st_atime));
				bytes = write(clientSocket, buffer.c_str(),ALOC_SIZE);
				buffer = "";

				buffer += "  Creation Time: ";
				buffer += (4+ctime(&info.st_ctime));
				buffer += "\n";
				bytes = write(clientSocket, buffer.c_str(),ALOC_SIZE);
				buffer = "";
            }
        }
		buffer = "ENDOFFILESINSERVER";
		bytes = write(clientSocket, buffer.c_str(),ALOC_SIZE);
		buffer = "";
        closedir(dir);
    }
    else
    {
        std::cout << "Erro na abertura do diretório\n" << std::endl;
    }
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
	bytes = recv(clientSocket, buffer, ALOC_SIZE,MSG_WAITALL);

	if (bytes < 0)
		cout << "erro ao ler requisicao do cliente" << endl;

	//cout << "User Id Cliente em execução : " << userId << endl;


    //cout << "REQUISICAO CLIENTE: " << buffer << endl;
	while (strcmp(buffer, "exit") != 0)
	{
		if (strcmp(buffer, "upload") == 0)
		{
			char fileName[ALOC_SIZE];
			char fileSize[ALOC_SIZE];
			int ifileSize;

			bytes = recv(clientSocket, buffer, ALOC_SIZE,MSG_WAITALL);
			strcpy(fileName, buffer);
			bytes = recv(clientSocket, buffer, ALOC_SIZE,MSG_WAITALL);
			strcpy(fileSize, buffer);
			ifileSize = atoi(fileSize);
			receiveFile(userId, fileName, ifileSize, clientSocket);
		}
		if (strcmp(buffer, "download") == 0)
		{
			char fileName[ALOC_SIZE];
			bytes = recv(clientSocket, buffer, ALOC_SIZE, MSG_WAITALL);
			strcpy(fileName, buffer);
			sendFile(userId, fileName, clientSocket);
		}
		if(strcmp(buffer, "delete") == 0){
			char fileName[ALOC_SIZE];
			int ifileSize;
			bytes = recv(clientSocket, buffer, ALOC_SIZE,MSG_WAITALL);
			strcpy(fileName, buffer);
			deleteFile(userId, clientSocket, fileName);
			fileNameToPropagate = fileName;
            hasDeleteFile = true;

            deleteFileToBackup(userId,fileName);
		}
		if(strcmp(buffer, "list_server") == 0){
			listServer(userId, clientSocket);
		}
		bzero(buffer,ALOC_SIZE);
		bytes = recv(clientSocket, buffer, ALOC_SIZE, MSG_WAITALL);
        //cout << "REQUISICAO CLIENTE: " << buffer << endl;
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
	bytes = recv(clientSocket, buffer, ALOC_SIZE,MSG_WAITALL);
	if (bytes < 0)
		cout << "erro ao ler requisicao do cliente" << endl;

	while (strcmp(buffer, "exit") != 0)
	{
		if (strcmp(buffer, "upload") == 0)
		{
			char fileName[ALOC_SIZE];
			char fileSize[ALOC_SIZE];
			int ifileSize;
			bytes = recv(clientSocket, buffer, ALOC_SIZE,MSG_WAITALL);
			strcpy(fileName, buffer);
			bytes = recv(clientSocket, buffer, ALOC_SIZE,MSG_WAITALL);
			strcpy(fileSize, buffer);
			ifileSize = atoi(fileSize);
			receiveFile(userId, fileName, ifileSize, clientSocket);
		}
		if (strcmp(buffer, "DOWNLOADALLFILES") == 0)
        {
            sendAllFiles(userId, clientSocket);
        }
        bzero(buffer,ALOC_SIZE);
		bytes = recv(clientSocket, buffer, ALOC_SIZE,MSG_WAITALL);
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
	bytes = recv(socket, buffer, ALOC_SIZE,MSG_WAITALL);
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
	bytes = recv(*socketAdress, buffer, ALOC_SIZE,MSG_WAITALL);
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

	bytes = recv(*socketAdress, buffer, ALOC_SIZE,MSG_WAITALL);
	userId = atoi(buffer);
	if (userId < 0)
		cout << "Erro ao ler do socket" << endl;

	userDevices[userId][*socketAdress].gpropSock = *socketAdress;

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

string findElectedIp(int electedId){
	for(auto ip : listOfBackupsIp){
    if(ip.substr(ip.find(":")+1,4) == to_string(4000+electedId)){
        return ip.substr(0,ip.find(":"));
    }
}
}

void backupConnectToNewPrimary(int electedId){
	if(!isPrimary){
			cout << "Tentando se conectar ao novo primario" << endl;
			//Se conecta ao servidor primário

			//primaryServer = gethostbyname(backupToIp[socketBackup[electedId]].c_str());
			string electedIp = findElectedIp(electedId);
			cout << "Novo ip Primario para conectar: " << electedIp << endl;

			primaryServer = gethostbyname(electedIp.c_str());
			//cout << "Novo ip Primario para conectar: " << backupToIp[socketBackup[electedId]].c_str() << endl;

			if((primarySockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
				std::cout << "ERROR while connecting to primary server" << std::endl;

			serv_addr_backup.sin_family = AF_INET;
			serv_addr_backup.sin_port = htons(PORT + electedId);
			serv_addr_backup.sin_addr = *((struct in_addr *)primaryServer->h_addr);
			bzero(&(serv_addr_backup.sin_zero), 8);

			if (connect(primarySockfd,(struct sockaddr *) &serv_addr_backup,sizeof(serv_addr_backup)) < 0){
			cout << "erro ao conectar sync_sock" << endl;
			}
			cout << "conectado" << endl;
			sendMessage("4",primarySockfd); //Type of service = 4 : Backup
			sendMessage(to_string(id),primarySockfd); //Envia id do servidor backup para o primario

		}
	else{
		cout << "Erro na configuração de conexão do backup ao novo primário" << endl;
	}
}

void doElection(int nextBackupSockfd)
{
	const int SIZE_BUFF = 5;

	bool inElection = true;
	string vote = to_string(id);
	int bestVote = id;
	int ans;
	int electedId;
	while(inElection) {
		char buff[SIZE_BUFF] = "";
		ans = write(nextBackupSockfd, vote.c_str(), vote.size());
		if (ans == -1) break;
		recv(prevBackupSockfd, buff, SIZE_BUFF,MSG_WAITALL);
		int receivedVote = atoi(buff);
		if (receivedVote < bestVote) // Recebeu um voto melhor, salva
		{
			bestVote = receivedVote;
			vote = to_string(receivedVote);
		}
		else if (receivedVote == bestVote) // eleição acabou, voto é o id do vencedor
		{
			inElection = false;
			electedId = bestVote;
			ans = write(nextBackupSockfd, vote.c_str(), vote.size());
		}
	}
	close(nextBackupSockfd);
	close(prevBackupSockfd);
	close(primarySockfd);
	sem_post(endElectionSemaphore);

	if (electedId == id) // Foi eleito primário
	{

		pthread_cancel(backupWaitThread);
		isPrimary = true;
		doingElection = false;
		sendNewPrimary();
		listOfBackupsIp.clear();

		cout << "Fui Eleito" << endl;
	}
	 else // Configura backups
	{
		//fechar threads de backup anteriores e abrir nova, acontece naturalmente na main
		//se conecta com o novo primario

		pthread_cancel(backupWaitThread);
		doingElection = false;
		backupConnectToNewPrimary(electedId);
		openedBackupThread = false;
		listOfBackupsIp.clear();

		cout << "Fim da eleição: " << electedId << " foi eleito"<< endl;
	}

}

void doConnectionForElection(){

	string ipOfNext ="-";
	int minPort = 1e9;

	if(listOfBackupsIp.size() == 1){//Only alive
		cout << id <<" Eleitoopoo" << endl;
		pthread_cancel(backupWaitThread);
		isPrimary = true;
		sendNewPrimary();
		doingElection = false;
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
	sem_wait(initElectionSemaphore);
	// Aqui o anel ta feito, pode começar eleicão
	doElection(sockOfNextBackup);

}

void *startBackupThread(void *socket)
{
	int *socketAdress = (int *)socket;

	int bytes;
	int userId;
	int serverId;
	char buffer[ALOC_SIZE];



	bytes = recv(*socketAdress, buffer, ALOC_SIZE,MSG_WAITALL);
	serverId = atoi(buffer);
	std::chrono::steady_clock::time_point begin = std::chrono::steady_clock::now();
	std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
	doingElection = false;
	while(!isPrimary){

       if(strcmp(buffer,"DELETE") == 0){ //Primário mandando backup deletar arquivo
			bytes = recv(*socketAdress, buffer, ALOC_SIZE,MSG_WAITALL);
			userId = atoi(buffer); //Le o userId
			char fileName[ALOC_SIZE];
			bytes = recv(*socketAdress, buffer, ALOC_SIZE,MSG_WAITALL); //Le o nome do arquivo
			strcpy(fileName, buffer);
			deleteFile(userId,*socketAdress,fileName);
			begin = std::chrono::steady_clock::now();
	   }else if(strcmp(buffer,"RECEIVE") == 0){//Primário enviando arquivo ao backup
			bytes = recv(*socketAdress, buffer, ALOC_SIZE,MSG_WAITALL);
			userId = atoi(buffer); //Le o userId
			char fileName[ALOC_SIZE];
			bytes = recv(*socketAdress, buffer, ALOC_SIZE,MSG_WAITALL); //Le o nome do arquivo
			strcpy(fileName, buffer);
			bytes = recv(*socketAdress,buffer,ALOC_SIZE,MSG_WAITALL);
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
			bytes = recv(*socketAdress,buffer,ALOC_SIZE,MSG_WAITALL);
			int qtOfBackupIps = atoi(buffer);
			cout << "qtBackups: " << qtOfBackupIps << endl;
			for(int i = 0; i < qtOfBackupIps; i++){
				bytes = recv(*socketAdress,buffer,ALOC_SIZE,MSG_WAITALL);
				listOfBackupsIp.insert(buffer);
				cout << "Ip: " << buffer << endl;
			}
       }

	   else if(strcmp(buffer,"INIT") == 0){
            begin = std::chrono::steady_clock::now();
            bytes = recv(*socketAdress, buffer, ALOC_SIZE,MSG_WAITALL);
			userId = atoi(buffer); //Le o userId
            initClient(userId);
			char addr[INET_ADDRSTRLEN] = "";
			bytes = recv(*socketAdress,addr,INET_ADDRSTRLEN,MSG_WAITALL);
			if (bytes<0) {
				cout << "cannot read device address" << endl;
			}
			devicesAddress.insert(string(addr));
			cout << "novo device: " << addr << endl;
	   }
	   end = std::chrono::steady_clock::now();


	   if(std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count() > 2000){
			//do election

			cout << "Fazendo eleição " << endl;
			if(!doingElection){
				cout << "Primario caiu" << endl;

				cout << "Doing election..." << endl;
				doConnectionForElection();

				doingElection = true;

				break;
			}
	   }
	   //cout << std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count() << endl;
		bzero(buffer, ALOC_SIZE);
		bytes = recv(*socketAdress, buffer, ALOC_SIZE,MSG_WAITALL);
        //cout << "COMANDO: " << buffer << endl;
	}
}

void *startBackupWaitThread(void *client_info)
{
	socklen_t clilen = sizeof(struct sockaddr_in); //PODE NAO ESTAR CORRETO <- ?
	struct CliInfo* cli_info = (struct CliInfo*) client_info;
	//Espera backup se conectar para fazer eleição
	if(isPrimary){
		cout << "Mudou para primario" << endl;
	}
	if ((prevBackupSockfd = accept(sockfd, (struct sockaddr *)&cli_info->cli_addr, &clilen)) == -1)
	//if ((clientSockfd = accept4(sockfd, (struct sockaddr *)&cli_info->cli_addr, &clilen,SOCK_NONBLOCK)) == -1)
	{
		cout << "Erro ao aceitar" << endl;
	}
	sem_post(initElectionSemaphore);
	// TODO: sincronização
	sem_wait(endElectionSemaphore);
}

int main(int argc, char *argv[])
{
	initElectionSemaphore = new sem_t();
	endElectionSemaphore = new sem_t();

	sem_init(initElectionSemaphore, 0, 0); // Inicializa semaforo de sync em 0
	sem_init(endElectionSemaphore, 0, 0); // Inicializa semaforo de sync em 0


	if(strcmp(argv[1],"0") == 0)
		isPrimary = true;
	id  = atoi(argv[1]);

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

	socklen_t clilen = sizeof(struct sockaddr_in);

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

		serv_addr_backup.sin_family = AF_INET;
		serv_addr_backup.sin_port = htons(PORT);
		serv_addr_backup.sin_addr = *((struct in_addr *)primaryServer->h_addr);
		bzero(&(serv_addr_backup.sin_zero), 8);

    	if (connect(primarySockfd,(struct sockaddr *) &serv_addr_backup,sizeof(serv_addr_backup)) < 0){
      	  cout << "erro ao conectar sync_sock" << endl;
   		 }
		cout << "conectado" << endl;
		sendMessage("4",primarySockfd); //Type of service = 4 : Backup
		sendMessage(to_string(id),primarySockfd); //Envia id do servidor backup para o primario


	}

	while (true)
	{
	    //cout << "Primary: " << isPrimary << endl;
		if(isPrimary){ //Se é primário
            cout << "here" << endl;
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
				recv(clientSockfd, buffer, ALOC_SIZE,MSG_WAITALL);
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

                    bytes = recv(clientSockfd, buffer, ALOC_SIZE, MSG_WAITALL);
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
                if(!openedBackupThread){
					doingElection = false;
					struct CliInfo cli_info = {clientSockfd, &cli_addr};
					if (pthread_create(&backupThread, NULL, startBackupThread, &primarySockfd))
					{
						cout << "Erro ao abrir a thread do cliente" << endl;
					}
                    if (pthread_create(&backupWaitThread, NULL, startBackupWaitThread, &cli_info))
                    {
                        cout << "Erro ao abrir a thread do cliente" << endl;
                    }
                    openedBackupThread = true;
                }
		}

	}
	return 0;
}
