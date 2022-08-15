#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <netdb.h>
#include <pthread.h>
#include <iostream>
#include <string>
#include <fstream>
#include <errno.h>
#include <sys/inotify.h>
#include <cstdio>
#include <dirent.h>
#include <ctime>
#include <pwd.h>
#include <experimental/filesystem>
#include <unistd.h>

//variáveis inotify
#define EVENT_SIZE ( sizeof (struct inotify_event) )
#define EVENT_BUF_LEN ( 1024 * ( EVENT_SIZE + 16 ) )
int fd;
int wd;


pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;


#define SYNCSERVICE 2
#define REQUESTSERVICE 1
#define PORT 4000
#define BUFSIZE 10000

using namespace std;

string dirName;
int sockfd;
struct sockaddr_in serv_addr;
struct hostent *server;
string userId;
char *hostname;

void ClientSocket(){

	server = gethostbyname(hostname);
	if((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
		std::cout << "ERROR while opening socket" << std::endl;

	serv_addr.sin_family = AF_INET;
	serv_addr.sin_port = htons(PORT);
	serv_addr.sin_addr = *((struct in_addr *)server->h_addr);
	bzero(&(serv_addr.sin_zero), 8);
}

int exists(const char *fname){
		    //https://stackoverflow.com/questions/230062/whats-the-best-way-to-check-if-a-file-exists-in-c
	FILE *file;
	if ((file = fopen(fname, "rb")))
	{
		fclose(file);
		return 1;
	}
	return 0;
}
int sendMessage(std::string message){
	char buffer[10000];
	strcpy(buffer,message.c_str());

	int bytes = write(sockfd , buffer , 10000);
			// cout << buffer << endl;
			// cout << "Bytes enviados: " << bytes << endl;
	return bytes;
}

int sendMessageSync(std::string message, int sync_sock){
	char buffer[10000];
	strcpy(buffer,message.c_str());

	int bytes = write(sync_sock , buffer , 10000);
			// cout << buffer << endl;
			// cout << "Bytes enviados: " << bytes << endl;
	return bytes;
}

void listClient(){

	DIR *dir;
	struct dirent *dent;
	dir = opendir((const char *) dirName.c_str());

	string path, filename;

	if(dir!=NULL)
	{
		while((dent=readdir(dir))!=NULL){

			// O uso dessas strings aqui é uma gambiarra pra pegar o caminho de cada arquivo
			filename = dent->d_name;
			path = dirName + (string) "/" + filename;

			struct stat info;
			stat(path.c_str(), &info); // O primeiro argumento aqui é o caminho do arquivo
			if(dent->d_name[0] != '.'){
				std::cout <<
				"> Arquivo: " << dent->d_name << std::endl <<
				"  Modification Time: " << 4+ctime(&info.st_mtime) <<
				"  Access Time: " << 4+ctime(&info.st_atime) <<
				"  Creation Time: " << 4+ctime(&info.st_ctime) << std::endl;
			}
		}
		//fflush(stdout);
		closedir(dir);
	}
	else{
		std::cout << "Erro na abertura do diretório\n" << std::endl;
	}
}
int connectSocket(){
	if (connect(sockfd,(struct sockaddr *) &serv_addr,sizeof(serv_addr)) < 0){
		printf("ERROR connecting\n");
		return -1;
	}
	int service = 1;

	sendMessage(to_string(service));

	sendMessage(userId);

	char buffer[10000];
	int bytes = read(sockfd, buffer,10000);
/*
			int devicesConnected = 0;
			bytes = read(sockfd, &devicesConnected, sizeof(int));
			if(bytes < 0){
				cout << "error on connecting" << endl;
			}
			if(devicesConnected == 2){
				cout << "Max devices conneted" << endl;
				return -1;

			}*/
	return 1;
}

void sync_dir_onConnect(){
	string dirName = "sync_dir_";

}
void closeSocket(){

	close(sockfd);
	sendMessage("exit");
}
void closeSyncSocket(int sync_sock){
    close(sync_sock);
}

void waitConfirm(){
	int bytes;
	char buff[10000];
	bytes = read(sockfd,buff,10000);
	cout << buff << endl;
}

int getFileSize(string filepath){
	ifstream in(filepath, std::ifstream::ate | std::ifstream::binary);
	return in.tellg();
}

void sendFile(string filepath){
	FILE *file;
	int fileSize;
	int bytes;
	string filename;
	char ch = '/';

	if((file = fopen(filepath.c_str(),"rb"))){
				//envia requisição de envio para o servidor
		fileSize = getFileSize(filepath);
		if(sendMessage("upload") < 0){
			cout << "Erro ao enviar mensagem upload" << endl ;
		}

				//envia o nome do arquivo para o servidor
		filename = filepath.substr(filepath.find_last_of(ch) + 1 ,filepath.length()- filepath.find(ch));
		sendMessage(filename);
		cout << "sending: " << filename << endl;

				//waitConfirm();
		sendMessage(to_string(fileSize));
		char data[10000];
		while(!feof(file)){
			fread(data, 10000, 1, file);
			cout << data << endl;

			bytes = write(sockfd, data,10000);
			if(bytes < 0){
				cout << "erro ao enviar arquivo" << endl;
                break;
            }
			fileSize -= 10000;
		}

		fclose(file);
	}
	else{
		cout << "erro ao abrir o arquivo" << endl;
	}
}

void receiveFile(){
			//Gabriel*
}

void downloadFile(string fileName){ //TODO
	//envia requisição de download para o server
	sendMessage("download");
	sendMessage(fileName);

	ofstream file;
	//string dir = "sync_dir_" + userId + "/" + fileName;
	file.open(fileName);
	int bytes;
	char buffer[10000];
	int fileSize;
	bytes = read(sockfd,buffer,10000);
	if(strcmp(buffer,"erro") == 0){
		cout << "erro ao baixar arquivo do servidor";
		return;
	}
	fileSize = atoi(buffer);
	cout << fileName << endl << fileSize << endl;
	if (fileSize > 0){
		while (fileSize > 0)
		{
			bytes = read(sockfd, buffer, min(fileSize,10000));
			cout << buffer << endl;
			file.write(buffer, min(fileSize, 10000));


			fileSize -= 10000;
		}
	}
	file.close();
}

void download_all_files(int sync_sock){
    cout << "try to download all files" << endl;
    string command = "DOWNLOADALLFILES";
    char buffer[BUFSIZE];
    int bytes = sendMessageSync(command,sync_sock);
    if(bytes < 0){
        cout << "error while downloading all files" << endl;
        return;
    }
    int qtFiles;
    bytes = read(sync_sock, buffer, BUFSIZE);

    qtFiles = atoi(buffer);
    cout << "qtFiles: " << qtFiles << endl;
    string fileName;
    for(int i=0;i<qtFiles;i++){

                //Le nome do arquivo
        bytes = read(sync_sock,buffer,BUFSIZE);
        fileName = buffer;
        cout << "Filename: " << fileName << endl;
        if(bytes < 0){
            cout << "error downloading files" << endl;
        }

        FILE *file;
        string dirFile = "sync_dir_" + userId + "/" + fileName;
        file = fopen(dirFile.c_str(),"wb");
        int fileSize;
        bytes = read(sync_sock, buffer, BUFSIZE);

        fileSize = atoi(buffer);
        cout << "Filesize: " << fileSize << endl;
        //char buffer[BUFSIZE];
        if(fileSize > 0){

            while(fileSize > 0){
                bytes = read(sync_sock,buffer,BUFSIZE); //conteudo do arquivo
				cout << "conteudo: " << buffer << endl;
                fwrite(buffer,min(BUFSIZE,fileSize),1,file);
                fileSize -= BUFSIZE;
            }

        }
        memset(buffer,0,BUFSIZE);
        fclose(file);
    }
    cout << "finished download all files" << endl;
}

void deleteFile(const char *name){ // name é o nome do arquivo que vai deletar.

    DIR *dir;
    dir = opendir((const char *) dirName.c_str()); // Tenta abrir o diretório.

    string path; // String auxiliar pra pegar o caminho completo do arquivo.

    if(dir!=NULL){ // Verifica se deu certo abrir o diretório.
        path = dirName + (string) "/" + name;

        if(remove(path.c_str()) == 0){ // remove() precisa receber o caminho do arquivo.
            cout << "Arquivo \"" << name << "\" deletado." << endl;
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

//deleteAllFiles
void deleteAllFiles(){
    DIR *dir;
    struct dirent *dent;
    dir = opendir((const char *) dirName.c_str());
    // dir = opendir((const char *) dirName);

    string path, filename;
    int count = 0; // Contador de arquivos apagados, pra bonito

    if(dir!=NULL){ // Verifica se deu certo abrir o diret贸rio.
        while((dent=readdir(dir))!=NULL){// Iterador no diret贸rio

            if(dent->d_name[0] != '.'){ // Condicional pra nao pegar o diret贸rio em si
                filename = dent->d_name; // Pega o nome do arquivo
                path = dirName + (string) "/" + filename; // Caminho completo do arquivo

                if(remove(path.c_str()) == 0){ // Tenta remover cada arquivo.
                    count++;
                    cout << "Arquivo \"" << filename << "\" deletado." << endl;
                }
                else{
                    cout << "Erro ao apagar o arquivo \"" << filename << "\"" << endl;
                }
            }
        }
        cout << "> Operacao finalizada: " << count << " arquivos deletados." << endl;
        return;
    }
    else{
        cout << "Erro na abertura do diret贸rio!" << endl;
        return;
    }
    //fflush(stdout);
    closedir(dir);
}

// //Função para tratamento de comandos de interface do cliente
void interface(){

	std::string request;

	cout << "\nComandos:\nupload <path/filename.ext>\ndownload <filename.ext>\ndelete <filename.ext>\nlist_server\nlist_client\nget_sync_dir\nexit\n";

	do
	{
		std::string command, file;
		cout << "Digite o comando: \n";
		getline(std::cin, request);

		if (request.find(" ") != -1){
			command = request.substr(0,request.find(" "));
			file = request.substr(request.find(" ") + 1, request.length()- request.find(" "));
		}
		if(request == "exit"){
			//Fecha a sessão com o servidor.
			cout << "encerrar conexão\n";
			sendMessage(request);
			closeSocket();
			break;
		}
		else if(request == "list_server"){
			//Lista os arquivos salvos no servidor associados ao usuário.
			//sendMessage(request);
			// read socket
			cout << "listar arquivos do servidor\n";
		}
		else if(request == "list_client"){
			//Lista os arquivos salvos no diretório “sync_dir”
			cout << "listar arquivos do cliente: \n";
			listClient();
		}
		else if(request == "get_sync_dir"){
			//Cria o diretório “sync_dir” e inicia as atividades de sincronização
			//getSyncDir();
			//sendFile(request);
			cout << "sincronizar diretórios\n";
		}
		else if(command == "upload"){
			/*Envia o arquivo filename.ext para o servidor, colocando-o no “sync_dir” do
			servidor e propagando-o para todos os dispositivos daquele usuário.
			e.g. upload /home/user/MyFolder/filename.ext*/
			sendFile(file);
			cout << "subir arquivo: " + file + "\n";
		}
		else if(command == "download"){
			/*Faz uma cópia não sincronizada do arquivo filename.ext do servidor para
			o diretório local (de onde o servidor foi chamado). e.g. download
			mySpreadsheet.xlsx*/
			//sendMessage(aux);
			cout << "baixar arquivo" + file + "\n";
			downloadFile(file);
		}
		else if(command == "delete"){
			//Exclui o arquivo <filename.ext> de “sync_dir”
			deleteFile(file.c_str());
			cout << "deletar arquivo" + file + "\n";
		}
		else{
			cout << "ERRO, comando inválido\nPor favor, digite novamente: \n";
		}
	}while(request != "exit");
}

int sync_socket(){
/*
	struct sockaddr_in serv_addr_sync;
	struct hostent *server_sync;

	server_sync = gethostbyname(hostname);

    int service = SYNCSERVICE;
    int bytes;

    if(server_sync == NULL){
        cout << "erro ao criar sync socket 1 " << endl;
        return -1;
    }
    if((sync_sock = socket(AF_INET, SOCK_STREAM, 0)) == -1){
        cout << "erro ao criar sync socket 2 " << endl;
        return -1;
    }

    serv_addr_sync.sin_family = AF_INET;
    serv_addr_sync.sin_port = htons(PORT);
    serv_addr_sync.sin_addr = *((struct in_addr *)server_sync->h_addr);

    bzero(&(serv_addr_sync.sin_zero), 8);

    if (connect(sync_sock,(struct sockaddr *) &serv_addr_sync,sizeof(serv_addr_sync)) < 0){
        cout << "erro ao conectar sync_sock" << endl;
        return -1;
    }

    bytes = sendMessageSync(to_string(SYNCSERVICE));
    if(bytes < 0){
        cout << "erro ao conectar sync thread" << endl;
    }
    bytes = sendMessageSync(userId);

    char buffer[10000];
    bytes = read(sync_sock, buffer,10000); //Socket confirm
    */
}

void inotifyInit(){

	fd = inotify_init();
	wd = inotify_add_watch( fd, dirName.c_str(), IN_CREATE | IN_CLOSE_WRITE | IN_MOVED_TO | IN_DELETE | IN_MOVED_FROM );

	cout << dirName << endl;
}

void listenServer(int sync_sock){

    char buffer[10000];
    int bytes;

    bytes = read(sync_sock,buffer,10000);
    if(strcmp("exit",buffer) == 0){
        closeSyncSocket(sync_sock);
    }
}

void *sync_thread(void *socket){
	//https://www.thegeekstuff.com/2010/04/inotify-c-program-example/

    int *socketAdress = (int *)socket;
	//sync_socket(*socket);

	deleteAllFiles();
	cout << "Arquivos deletados" << endl;

	download_all_files(*socketAdress);
    cout << "Downloaded all files" << endl;

    //listenServer(*socketAdress);

	int length, i = 0;
	char buffer[EVENT_BUF_LEN];
	char path[200];

	while(1){


		length = read( fd, buffer, EVENT_BUF_LEN );


		if ( length < 0 ) {
			perror( "read" );
		}


		while ( i < length ) {
			struct inotify_event *event = ( struct inotify_event * ) &buffer[ i ];
			if ( event->len ) {
				if ( event->mask & IN_CREATE || event->mask & IN_CLOSE_WRITE || event->mask & IN_MOVED_TO) {

					strcpy(path, dirName.c_str());
					strcat(path, "/");
					strcat(path, event->name);


					if(exists(path) && (event->name[0] != '.')){
						char fpath[256] = "sync_dir_";
						strcat(fpath, userId.c_str());
						strcat(fpath, "/");
						strcat(fpath, event->name);
						sendFile(fpath);
						cout << "Send File" << endl;
					}
				}
				else if ( event->mask & IN_DELETE || event->mask & IN_MOVED_FROM ) {
					if(event->name[0] != '.')
					{
						//sendMessage("deleteFile")
						cout << "Delete File" << endl;
					}
				}
			}
			i += EVENT_SIZE + event->len;
		}
		i = 0;
		sleep(5);
	}

	inotify_rm_watch( fd, wd );
	close( fd );


}

void sync_client(){

	pthread_t sync_thread_thread;
	char directory[256];
	getcwd(directory, 256);

	dirName = string(directory) + "/" + "sync_dir_" + userId;

	if(mkdir(dirName.c_str(),0777) < 0){
		cout << "Erro ao criar diretorio ou diretorio ja existente" << endl;
	}else{
		cout << "sync_dir_"+userId << " created" << endl;
	}



    int sync_sock;
	//AQUI TA CABREIRO
	struct sockaddr_in serv_addr_sync;
	struct hostent *server_sync;

	server_sync = gethostbyname(hostname);

    int service = SYNCSERVICE;
    int bytes;

    if(server_sync == NULL){
        cout << "erro ao criar sync socket 1 " << endl;

    }
    if((sync_sock = socket(AF_INET, SOCK_STREAM, 0)) == -1){
        cout << "erro ao criar sync socket 2 " << endl;

    }

    serv_addr_sync.sin_family = AF_INET;
    serv_addr_sync.sin_port = htons(PORT);
    serv_addr_sync.sin_addr = *((struct in_addr *)server_sync->h_addr);

    bzero(&(serv_addr_sync.sin_zero), 8);

    if (connect(sync_sock,(struct sockaddr *) &serv_addr_sync,sizeof(serv_addr_sync)) < 0){
        cout << "erro ao conectar sync_sock" << endl;

    }

    bytes = sendMessageSync(to_string(SYNCSERVICE),sync_sock);
    if(bytes < 0){
        cout << "erro ao conectar sync thread" << endl;
    }
    bytes = sendMessageSync(userId,sync_sock);

    char buffer[10000];
    bytes = read(sync_sock, buffer,10000); //Socket confirm






	if(pthread_create(&sync_thread_thread, NULL, sync_thread, &sync_sock)){
        cout << "erro ao criar sync thread" << endl;
    }
	inotifyInit();
}



int main(int argc, char *argv[])
{

//Estabelece conexão
//argv1 : Host , argv2 = UserId
	userId = argv[2];
	hostname = (char *)malloc(sizeof(argv[1]));
	strcpy(hostname, argv[1]);
	ClientSocket();
	if(connectSocket() == -1){
		cout << "Erro ao conectar" << endl;
		return 0;
	}

	sync_client();
	interface();
	closeSocket();

	return 0;
}
