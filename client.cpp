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
		void sendFile(fstream &file){
			std::string fileContent((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());	

			int bytes = send(sockfd, fileContent.c_str(), fileContent.length(),0);
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
		void sync_client(){
			cout << "here" << endl;
			string dirName = "sync_dir_" + this->userId;

			if(mkdir(dirName.c_str(),0777) < 0){
				cout << "Erro ao criar diretorio ou diretorio ja existente" << endl;
			}else{
				cout << "sync_dir_"+this->userId << " created" << endl;

			}	
		}
		void sync_thread(){
			download_all_files();
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

	//Envia uma mensagem
	std::cout << "Digite uma mensagem:" << std::endl;
	std::string msg;
	getline(std::cin, msg);
	cli.sendMessage(msg);

	//Envia um arquivo
	fstream file;
	file.open("arquivoCliente.txt");
	cli.sendFile(file);

	cli.closeSocket();

	return 0;
}
