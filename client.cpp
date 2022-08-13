#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <netdb.h> 
#include <iostream>
#include <string>
#include <fstream>
#include <errno.h>
#include <sys/inotify.h>

//variáveis inotify
#define EVENT_SIZE ( sizeof (struct inotify_event) )
#define EVENT_BUF_LEN ( 1024 * ( EVENT_SIZE + 16 ) )
int fd;
int wd;
string directory;


#define PORT 4000

using namespace std;
class ClientSocket{
	private:
		int sockfd;
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
		int connectSocket(){
			if (connect(this->sockfd,(struct sockaddr *) &this->serv_addr,sizeof(this->serv_addr)) < 0){ 
				printf("ERROR connecting\n");
				return -1;
			}

			int bytes = send(this->sockfd, this->userId.c_str(), this->userId.length(),0);
			if(bytes < 0){
				cout << "error connecting" << endl;
			}
			bytes = send(this->sockfd, this->userId.c_str(), this->userId.length(),0);
			if(bytes < 0){
				cout << "error connecting" << endl;
			}
			char buffer[256];
			bytes = read(this->sockfd, buffer,256);
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
		void sendMessage(std::string message){
			int bytes = send(this->sockfd , message.c_str() , message.length() , 0 );
			cout << "Bytes enviados: " << bytes << endl;
		}
		
		void sync_dir_onConnect(){
			string dirName = "sync_dir_";

		}
		void closeSocket(){
			close(this->sockfd);
		}
		void download_all_files(){
			string command = "DOWNLOADALLFILES";
			int bytes = send(this->sockfd, command.c_str(), command.length(),0);
			if(bytes < 0){
				cout << "error while downloading all files" << endl;
				return;
			}
			int qtFiles;
			bytes = read(this->sockfd, &qtFiles, sizeof(qtFiles));
			string fileName;
			for(int i=0;i<qtFiles;i++){
				//Le nome do arquivo
				bytes = read(this->sockfd,&fileName,sizeof(fileName));

				if(bytes < 0){
					cout << "error downloading files" << endl;
				}

				FILE *file;
				string dirFile = "sync_dir_"+this->userId+"/"+fileName;
				file = fopen(dirFile.c_str(),"wb");
				
				int fileSize;
				bytes = read(this->sockfd, &fileSize, sizeof(fileSize));

				char buffer[10000];
				if(fileSize > 0){
					
					while(fileSize > 0){
						bytes = read(this->sockfd,buffer,10000);
						fwrite(buffer,min(fileSize,10000),1,file);

						fileSize -= 10000;
					}

				}
				fclose(file);
			}
		}
		void waitConfirm(){
			int bytes;
			char buff[256];
			bytes = read(this->sockfd,buff,256);
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

					bytes = send(this->sockfd, data, min(fileSize,10000),0);
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

		void downloadFile(){ //TODO
			//envia requisição de download para o server
			this->sendMessage("download");

			//aqui deve ter uma espera para o arquivo lido do servidor

			//envia o nome do arquivo para o servidor
			this->sendMessage("filename");
		}

		void deleteFile(){ //TODO
			//envia requisição de delete para o servidor
			this->sendMessage("delete");
			//envia o nome do arquivo para ser deletado pelo servidor
			this->sendMessage(filename);
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
		
		void sync_client(){
			cout << "here" << endl;
			string dirName = "sync_dir_" + this->userId;

			if(mkdir(dirName.c_str(),0777) < 0){
				cout << "Erro ao criar diretorio ou diretorio ja existente" << endl;
			}else{
				cout << "sync_dir_"+this->userId << " created" << endl;

			}	
		}
		void inotifyInit(){
	
			fd = inotify_init();
			wd = inotify_add_watch( fd, sync_dir, IN_CREATE | IN_CLOSE_WRITE | IN_MOVED_TO | IN_DELETE | IN_MOVED_FROM );
		}

		void *sync_thread( ){

			download_all_files();
			int length, i = 0;
			char buffer[EVENT_BUF_LEN];
			string path;

			while(1){
			  /*read to determine the event change happens on “/sync_dir” directory. Actually this read blocks until the change event occurs*/
			  length = read( fd, buffer, EVENT_BUF_LEN ); 

			  /*checking for error*/
			  if ( length < 0 ) {
			    perror( "read" );
			  }

			  /*actually read return the list of change events happens. Here, read the change event one by one and process it accordingly.*/
			  while ( i < length ) {    
				struct inotify_event *event = ( struct inotify_event * ) &buffer[ i ];     
				if ( event->len ) {
			      if ( event->mask & IN_CREATE || event->mask & IN_CLOSE_WRITE || event->mask & IN_MOVED_TO) {
				strcpy(path, directory);
				strcat(path, "/");
				strcat(path, event->name);
				if(exists(path) && (event->name[0] != '.')){
						sendFile(path);
					}
			      }
			      else if ( event->mask & IN_DELETE || event->mask & IN_MOVED_FROM ) {
					if(event->name[0] != '.')
					{
						deleteFile(event->name);
					}
			      }
			    }
			    i += EVENT_SIZE + event->len;
			  }
				i = 0;
				sleep(10);
			}
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
	
	cli.sync_client();

	cli.sendFile("arquivoCliente.txt");

	// //Envia um arquivo
	// fstream file;
	// file.open("arquivoCliente.txt");
	// cli.sendFile(file);

	cli.closeSocket();

	return 0;
}
