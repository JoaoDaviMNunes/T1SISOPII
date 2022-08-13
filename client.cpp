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
//#include <sys/inotify.h>

//variáveis inotify
#define EVENT_SIZE ( sizeof (struct inotify_event) )
#define EVENT_BUF_LEN ( 1024 * ( EVENT_SIZE + 16 ) )
int fd;
int wd;


#define SYNCSERVICE 2
#define REQUESTSERVICE 1
#define PORT 4000

using namespace std;
string dirName;

namespace fs = std::__fs::filesystem;

string directory;
class ClientSocket{
	private:
		int sockfd,sync_sock;
		struct sockaddr_in serv_addr;
		struct hostent *server;
		string userId;
	public:
		ClientSocket(char * hostname, string userId){
			this->userId = userId;
			this->server = gethostbyname(hostname);
			if((this->sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
				std::cout << "ERROR while opening socket" << std::endl;

			this->serv_addr.sin_family = AF_INET;     
			this->serv_addr.sin_port = htons(PORT);    
			this->serv_addr.sin_addr = *((struct in_addr *)this->server->h_addr);
			bzero(&(this->serv_addr.sin_zero), 8);     



		}
		// Lista os arquivos do diretório do cliente
		// Tratamento do comando "list client"
		void listClient()
		{
			std::string path = dirName;
   			for (const auto & entry : fs::directory_iterator(path))
        			std::cout << entry.path() << std::endl;
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
		void sendMessage(std::string message){
			char buffer[10000];
			strcpy(buffer,message.c_str());
		
			int bytes = write(this->sockfd , buffer , 10000*sizeof(char));
			cout << buffer << endl;
			cout << "Bytes enviados: " << bytes << endl;
		}
		
		int connectSocket(){
			if (connect(this->sockfd,(struct sockaddr *) &this->serv_addr,sizeof(this->serv_addr)) < 0){ 
				printf("ERROR connecting\n");
				return -1;
			}
			int service = 1;
			
			sendMessage(to_string(service));
			
			sendMessage(this->userId);
		
			char buffer[10000];
			int bytes = read(this->sockfd, buffer,10000);
/*
			int devicesConnected = 0;
			bytes = read(this->sockfd, &devicesConnected, sizeof(int));
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
			close(this->sockfd);
		}
		void download_all_files(){
			string command = "DOWNLOADALLFILES";
			int bytes = send(this->sync_sock, command.c_str(), command.length(),0);
			if(bytes < 0){
				cout << "error while downloading all files" << endl;
				return;
			}
			int qtFiles;
			bytes = read(this->sync_sock, &qtFiles, sizeof(qtFiles));
			string fileName;
			for(int i=0;i<qtFiles;i++){
				//Le nome do arquivo
				bytes = read(this->sync_sock,&fileName,sizeof(fileName));

				if(bytes < 0){
					cout << "error downloading files" << endl;
				}

				FILE *file;
				string dirFile = "sync_dir_"+this->userId+"/"+fileName;
				file = fopen(dirFile.c_str(),"wb");
				
				int fileSize;
				bytes = read(this->sync_sock, &fileSize, sizeof(fileSize));

				char buffer[10000];
				if(fileSize > 0){
					
					while(fileSize > 0){
						bytes = read(this->sync_sock,buffer,10000);
						fwrite(buffer,min(fileSize,10000),1,file);

						fileSize -= 10000;
					}

				}
				fclose(file);
			}
		}
		void waitConfirm(){
			int bytes;
			char buff[10000];
			bytes = read(this->sockfd,buff,10000);
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
			//string filename;
			//char ch = '/';
			
			if((file = fopen(filepath.c_str(),"rb"))){
				//envia requisição de envio para o servidor
				fileSize = getFileSize(filepath);
				this->sendMessage("upload");
				
				//envia o nome do arquivo para o servidor
				//filename = filepath.substr(filepath.find_last_of(ch) + 1 ,filepath.length()- filepath.find(ch));
				//this->sendMessage(filename);
				
				this->sendMessage(filepath);
				//waitConfirm();
				this->sendMessage(to_string(fileSize));
				char data[10000];
				while(!feof(file)){
					fread(data, sizeof(data), 1, file);

					bytes = write(this->sockfd, data, min(fileSize,10000));
					if(bytes < 0)
						cout << "erro ao enviar arquivo" << endl; 
					fileSize -= 10000;
				}
				fclose(file);
			}
		}
		void receiveFile(){
			//Gabriel*


		}

		void downloadFile(string fileName){ //TODO
			//envia requisição de download para o server
			this->sendMessage("download");
			this->sendMessage(fileName);

			ofstream file;
			string dir = fileName;
			file.open(dir);
			int bytes;
			char buffer[10000];
			int fileSize;
			bytes = read(this->sockfd,buffer,10000);
			if(strcmp(buffer,"erro") == 0){
				cout << "erro ao baixar arquivo do servidor";
				return;
			}
			fileSize = atoi(buffer);
			cout << fileName << endl << fileSize << endl;
			if (fileSize > 0){
				while (fileSize > 0)
				{
					bytes = read(this->sockfd, buffer, min(fileSize,10000));
					cout << buffer << endl;
					file.write(buffer, min(fileSize, 10000));
					

					fileSize -= 10000;
				}
			}
			file.close();
		}

		void deleteFile(){ //TODO
			//envia requisição de delete para o servidor
			this->sendMessage("delete");
			//envia o nome do arquivo para ser deletado pelo servidor
			//this->sendMessage(filename);
		}

		// //Função para tratamento de comandos de interface do cliente
		// void interface(ClientSocket cli){

		// 	std::string request, command, file;

		// 	cout << "\nComandos:\nupload <path/filename.ext>\ndownload <filename.ext>\ndelete <filename.ext>\nlist_server\nlist_client\nget_sync_dir\nexit\n";
			
		// 	do
		// 	{
		// 		cout << "Digite o comando: \n";
		// 		getline(std::cin, request);

		// 		if (request.find(" ") != -1){
		// 			command = request.substr(0,request.find(" "));
		// 			filepath = request.substr(request.find(" "), request.length()- request.find(" "));
		// 		}
		// 		if(request == "exit"){
		// 			//Fecha a sessão com o servidor.
		// 			cout << "encerrar conexão\n";
		// 			this->cli.sendMessage(request);
		// 			this->cli.closeSocket();
		// 			break;
		// 		}
		// 		else if(request == "list_server"){
		// 			//Lista os arquivos salvos no servidor associados ao usuário.
		// 			this->cli.sendMessage(request);
		// 			// read socket
		// 			cout << "listar arquivos do servidor\n";
		// 		}
		// 		else if(request == "list_client"){
		// 			//Lista os arquivos salvos no diretório “sync_dir”				
		// 			cout << "listar arquivos do cliente: \n";

		// 		}
		// 		else if(request == "get_sync_dir"){
		// 			//Cria o diretório “sync_dir” e inicia as atividades de sincronização
		// 			//getSyncDir();
		// 			this->cli.sendFile(request);
		// 			cout << "sincronizar diretórios\n";
		// 		}		
		// 		else if(aux == "upload"){
		// 			/*Envia o arquivo filename.ext para o servidor, colocando-o no “sync_dir” do
		// 			servidor e propagando-o para todos os dispositivos daquele usuário.
		// 			e.g. upload /home/user/MyFolder/filename.ext*/
		// 			this->sendFile(filepath);
		// 			cout << "subir arquivo: " + file + "\n";
		// 		}
		// 		else if(aux == "download"){
		// 			/*Faz uma cópia não sincronizada do arquivo filename.ext do servidor para
		// 			o diretório local (de onde o servidor foi chamado). e.g. download
		// 			mySpreadsheet.xlsx*/
		// 			this->sendMessage(aux);
		// 			cout << "baixar arquivo" + file + "\n";
		//			downloadFile(file);
		// 		}
		// 		else if(aux == "delete"){
		// 			//Exclui o arquivo <filename.ext> de “sync_dir”
		// 			//deleteFile();
		// 			cout << "deletar arquivo" + file + "\n";
		// 		}
		// 		else{
		// 			cout << "ERRO, comando inválido\nPor favor, digite novamente: \n";
		// 		}
		// 	}while(request != "exit");
		// }
		int sync_socket(){
			int service = SYNCSERVICE;
			int bytes;
			if(this->server == NULL){
				cout << "erro ao criar sync socket" << endl;
				return -1;
			}
			if((sync_sock = socket(AF_INET, SOCK_STREAM, 0)) == -1){
				cout << "erro ao criar sync socket" << endl;
				return -1;
			}

			this->serv_addr.sin_family = AF_INET;     
			this->serv_addr.sin_port = htons(PORT);    
			this->serv_addr.sin_addr = *((struct in_addr *)this->server->h_addr);

			bzero(&(this->serv_addr.sin_zero), 8);     

			if (connect(this->sync_sock,(struct sockaddr *) &this->serv_addr,sizeof(this->serv_addr)) < 0){ 
				cout << "erro ao conectar sync_sock" << endl;
				return -1;
			}

			bytes = send(this->sync_sock, &service, sizeof(SYNCSERVICE),0);

			bytes = send(this->sync_sock, this->userId.c_str(), sizeof(this->userId),0);

			char buffer[256];
			bytes = read(this->sockfd, buffer,256); //Socket confirm
		}
		void sync_client(){

			pthread_t sync_thread_thread;
			cout << "here" << endl;
			string dirName = "sync_dir_" + this->userId;

			if(mkdir(dirName.c_str(),0777) < 0){
				cout << "Erro ao criar diretorio ou diretorio ja existente" << endl;
			}else{
				cout << "sync_dir_"+this->userId << " created" << endl;

			}	

			if(pthread_create(&sync_thread_thread, NULL, sync_thread_helper, NULL)){
				cout << "erro ao criar sync thread" << endl;
			}
		}
		void inotifyInit(){
	
			//fd = inotify_init();
			//wd = inotify_add_watch( fd, sync_dir, IN_CREATE | IN_CLOSE_WRITE | IN_MOVED_TO | IN_DELETE | IN_MOVED_FROM );
		}

		static void *sync_thread_helper(void *context){
			return ((ClientSocket *)context)->sync_thread();
		}

		void *sync_thread(){
			//https://www.thegeekstuff.com/2010/04/inotify-c-program-example/
			
			sync_socket();
		// 	download_all_files();
		// 	int length, i = 0;
		// 	char buffer[EVENT_BUF_LEN];
		// 	char path[200];



		// 	while(1){
		// 	  //cout << "Monitorando" << endl;
		// 	  /*read to determine the event change happens on “/sync_dir” dirName.c_str(). Actually this read blocks until the change event occurs*/
		// 	  length = read( fd, buffer, EVENT_BUF_LEN ); 

		// 	  /*checking for error*/
		// 	  if ( length < 0 ) {
		// 	    perror( "read" );
		// 	  }

		// 	  /*actually read return the list of change events happens. Here, read the change event one by one and process it accordingly.*/
		// 	  while ( i < length ) { 
		// 		struct inotify_event *event = ( struct inotify_event * ) &buffer[ i ];     
		// 		if ( event->len ) {
		// 	      if ( event->mask & IN_CREATE || event->mask & IN_CLOSE_WRITE || event->mask & IN_MOVED_TO) {
		// 		strcpy(path, dirName.c_str());
		// 		strcat(path, "/");
		// 		strcat(path, event->name);
		// 		if(exists(path) && (event->name[0] != '.')){
		// 				//sendFile(path);
		// 				cout << "Send File" << endl;
		// 			}
		// 	      }
		// 	      else if ( event->mask & IN_DELETE || event->mask & IN_MOVED_FROM ) {
		// 			if(event->name[0] != '.')
		// 			{
		// 				//deleteFile(event->name);
		// 				cout << "Delete File" << endl;
		// 			}
		// 	      }
		// 	    }
		// 	    i += EVENT_SIZE + event->len;
		// 	  }
		// 		i = 0;
		// 		sleep(5);
		// 	}

		// 	inotify_rm_watch( fd, wd );
		// 	close( fd );
			
		}


};

int main(int argc, char *argv[])
{

	//Estabelece conexão
	//argv1 : Host , argv2 = UserId 
	ClientSocket cli(argv[1],argv[2]);
	if(cli.connectSocket() == -1){
		cout << "Erro ao conectar" << endl;
		return 0;
	}
	
	// cli.sync_client();
	//cout << "try to send file" << endl;
	//cli.sendFile("arquivoCliente.txt");
	cout << "try to download" << endl;
	cli.downloadFile("arquivoCliente.txt");

	// //Envia um arquivo
	// fstream file;
	// file.open("arquivoCliente.txt");
	// cli.sendFile(file);

	cli.closeSocket();

	return 0;
}
