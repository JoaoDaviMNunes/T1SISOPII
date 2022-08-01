#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
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
	public:
		ClientSocket(char * hostname){
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
			return 1;
		}
		void sendMessage(std::string message){
			int bytes = send(sockfd , message.c_str() , message.length() , 0 );
			cout << "Bytes enviados: " << bytes << endl;
		}
		void sendFile(fstream &file){
			std::string fileContent((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());	

			int bytes = send(sockfd, fileContent.c_str(), fileContent.length(),0);
			cout << "Bytes enviados: " << bytes << endl;
		}
		void closeSocket(){
			close(this->sockfd);
		}
};

int main(int argc, char *argv[])
{

	//Estabelece conexão
	ClientSocket cli(argv[1]);
	if(cli.connectSocket() == -1){
		cout << "Erro ao conectar" << endl;
		return 0;
	}
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
