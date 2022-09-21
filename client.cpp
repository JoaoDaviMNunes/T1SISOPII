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

//variáveis para seção crítica
pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t m2 = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t m3 = PTHREAD_MUTEX_INITIALIZER;

//variáveis globais
#define PROPAGATESERVICE 3
#define SYNCSERVICE 2
#define REQUESTSERVICE 1
#define PORT 4000
#define PORT_OFFSET 100
#define ALOC_SIZE 512

using namespace std;

string dirName;
int sockfd;
int gsynckSock;
int gpropSock;
int waitSocket;
struct sockaddr_in serv_addr;
struct hostent *server;
string userId;
char *hostname;
int server_port = PORT;

string filenameToBeIgnored = "";

bool deletedAllFiles = false;

pthread_t sync_thread_prop;
pthread_t sync_thread_thread;


void ClientSocket()
{

    server = gethostbyname(hostname);
    if((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
        std::cout << "ERROR while opening socket" << std::endl;

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(server_port);
    serv_addr.sin_addr = *((struct in_addr *)server->h_addr);
    bzero(&(serv_addr.sin_zero), 8);
}

int exists(const char *fname)
{
    //https://stackoverflow.com/questions/230062/whats-the-best-way-to-check-if-a-file-exists-in-c
    FILE *file;
    if ((file = fopen(fname, "rb")))
    {
        fclose(file);
        return 1;
    }
    return 0;
}
int sendMessage(std::string message)
{
    char buffer[ALOC_SIZE];
    strcpy(buffer,message.c_str());

    int bytes = write(sockfd, buffer, ALOC_SIZE);

    return bytes;
}

int connectSocket()
{
    if (connect(sockfd,(struct sockaddr *) &serv_addr,sizeof(serv_addr)) < 0)
    {
        printf("ERROR connecting\n");
        return -1;
    }
    int service = 1;
    cout << "Connected on socket" << endl;
    sendMessage(to_string(service));

    sendMessage(userId);

    char buffer[ALOC_SIZE];
    int bytes = read(sockfd, buffer,ALOC_SIZE);


    return 1;
}

int sendMessageSync(std::string message, int sync_sock)
{
    char buffer[ALOC_SIZE];
    strcpy(buffer,message.c_str());

    int bytes = write(sync_sock, buffer, ALOC_SIZE);

    return bytes;
}

void listClient()
{

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
                std::cout <<
                          "> Arquivo: " << dent->d_name << std::endl <<
                          "  Modification Time: " << 4+ctime(&info.st_mtime) <<
                          "  Access Time: " << 4+ctime(&info.st_atime) <<
                          "  Creation Time: " << 4+ctime(&info.st_ctime) << std::endl;
            }
        }
        closedir(dir);
    }
    else
    {
        std::cout << "Erro na abertura do diretório\n" << std::endl;
    }
}


void closeSocket()
{
    sendMessage("exit");
    sendMessageSync("exit",gsynckSock);
    sendMessageSync("exit",gpropSock);
    close(sockfd);
    close(gsynckSock);
    close(gpropSock);

}
void closeSyncSocket(int sync_sock)
{
    cout << "sync sock closed" << endl;
    close(sync_sock);
}

void waitConfirm()
{
    int bytes;
    char buff[ALOC_SIZE];
    bytes = read(sockfd,buff,ALOC_SIZE);
    cout << buff << endl;
}

int getFileSize(string filepath)
{
    ifstream in(filepath, std::ifstream::ate | std::ifstream::binary);
    return in.tellg();
}

void sendFile(string filepath)
{
    pthread_mutex_lock(&m2);

    FILE *file;
    int fileSize;
    int bytes;
    string filename;
    char ch = '/';


    if((file = fopen(filepath.c_str(),"rb")))
    {
        //envia requisição de envio para o servidor
        fileSize = getFileSize(filepath);
        if(sendMessage("upload") < 0)
        {
            cout << "Erro ao enviar mensagem upload" << endl ;
        }

        //envia o nome do arquivo para o servidor
        filename = filepath.substr(filepath.find_last_of(ch) + 1,filepath.length()- filepath.find(ch));
        sendMessage(filename);
        cout << "sending: " << filename << endl;

        sendMessage(to_string(fileSize));

        char data[ALOC_SIZE];
        bzero(data, ALOC_SIZE);

        int length = 0;
        //Each time a piece of data is read, it is sent to the client and looped until the file is finished
        while((length = fread(data, sizeof(char), ALOC_SIZE, file)) > 0)
        {
            if(send(sockfd, data, length, 0) < 0)
            {
                cout << "Send File: " << filename << " Failed." << endl;
                break;
            }
            bzero(data, ALOC_SIZE);
        }

        fclose(file);
    }
    else
    {
        cout << "erro ao abrir o arquivo" << endl;
    }

    pthread_mutex_unlock(&m2);
}

void downloadFileSync(string fileName)  //TODO
{
    //envia requisição de download para o server
    sendMessage("download");
    sendMessage(fileName);

    ofstream file;
    string filepath = "./sync_dir_" + userId + "/" + fileName;


    file.open(filepath);
    int bytes;
    char buffer[ALOC_SIZE];
    cout << "baixando arquivo" << endl;

    int fileSize;
    bytes = read(sockfd,buffer,ALOC_SIZE);
    if(strcmp(buffer,"erro") == 0)
    {
        cout << "erro ao baixar arquivo do servidor";
        return;
    }

    fileSize = atoi(buffer);
    cout << fileName << endl << fileSize << endl;

    bzero(buffer, ALOC_SIZE);
    if (fileSize > 0)
    {
        while (fileSize > 0)
        {
            bytes = recv(sockfd, buffer, min(fileSize, ALOC_SIZE), MSG_WAITALL);
            file.write(buffer, bytes);

            fileSize -= bytes;
            bzero(buffer, ALOC_SIZE);
        }
    }
    file.close();

}

void downloadFile(string fileName)  //TODO
{
    //envia requisição de download para o server
    sendMessage("download");
    sendMessage(fileName);

    ofstream file;
    file.open(fileName);
    int bytes;
    char buffer[ALOC_SIZE];


    int fileSize;
    bytes = read(sockfd,buffer,ALOC_SIZE);
    if(strcmp(buffer,"erro") == 0)
    {
        cout << "erro ao baixar arquivo do servidor";
        return;
    }

    fileSize = atoi(buffer);
    cout << fileName << endl << fileSize << endl;

    bzero(buffer, ALOC_SIZE);
    if (fileSize > 0)
    {
        while (fileSize > 0)
        {
            bytes = recv(sockfd, buffer, min(fileSize, ALOC_SIZE), MSG_WAITALL);
            file.write(buffer, bytes);

            fileSize -= bytes;
            bzero(buffer, ALOC_SIZE);
        }
    }
    file.close();
}

void download_all_files(int sync_sock)
{
    //seção crítica entre downloadall e sendfile
    pthread_mutex_lock(&m2);

    string command = "DOWNLOADALLFILES";
    char buffer[ALOC_SIZE];
    bzero(buffer,ALOC_SIZE);

    int bytes = sendMessageSync(command, sync_sock);

    if(bytes < 0)
    {
        cout << "error while downloading all files" << endl;
        return;
    }

    int qtFiles;
    bytes = recv(sync_sock, buffer, ALOC_SIZE, MSG_WAITALL);
    qtFiles = atoi(buffer);

    string fileName;
    bzero(buffer, ALOC_SIZE);
    for(int i=0; i<qtFiles; i++)
    {

        //Le nome do arquivo
        bytes = read(sync_sock,buffer,ALOC_SIZE);
        fileName = buffer;
        if(bytes < 0)
        {
            cout << "error downloading files" << endl;
        }

        FILE *file;
        string dirFile = "sync_dir_" + userId + "/" + fileName;
        file = fopen(dirFile.c_str(),"wb");
        int fileSize;

        bytes = read(sync_sock, buffer, ALOC_SIZE);
        fileSize = atoi(buffer);
        bzero(buffer, ALOC_SIZE);

        if(fileSize > 0)
        {
            while(fileSize > 0)
            {

                bytes = recv(sync_sock, buffer, min(fileSize, ALOC_SIZE), MSG_WAITALL); //conteudo do arquivo

                fwrite(buffer, 1, bytes,file);
                fileSize -= bytes;
                bzero(buffer, ALOC_SIZE);
            }

        }
        fclose(file);
    }
    pthread_mutex_unlock(&m2);
}

void deleteFile(const char *name)  // name é o nome do arquivo que vai deletar.
{

    DIR *dir;
    dir = opendir((const char *) dirName.c_str()); // Tenta abrir o diretório.

    string path; // String auxiliar pra pegar o caminho completo do arquivo.

    if(dir!=NULL)  // Verifica se deu certo abrir o diretório.
    {
        path = dirName + (string) "/" + name;

        if(remove(path.c_str()) == 0)  // remove() precisa receber o caminho do arquivo.
        {
            cout << "Arquivo \"" << name << "\" deletado." << endl;
            return;
        }
        else  // Cai aqui se o remove() não funfou
        {
            cout << "Erro ao apagar o arquivo!" << endl;
            return;
        }
    }
    else
    {
        cout << "Erro na abertura do diretório" << endl;
        return;
    }
    closedir(dir);
}

void deleteAllFiles()
{
    DIR *dir;
    struct dirent *dent;
    dir = opendir((const char *) dirName.c_str());

    string path, filename;
    int count = 0; // Contador de arquivos apagados, pra bonito

    if(dir!=NULL)  // Verifica se deu certo abrir o diret贸rio.
    {
        while((dent=readdir(dir))!=NULL) // Iterador no diret贸rio
        {

            if(dent->d_name[0] != '.')  // Condicional pra nao pegar o diret贸rio em si
            {
                filename = dent->d_name; // Pega o nome do arquivo
                path = dirName + (string) "/" + filename; // Caminho completo do arquivo

                if(remove(path.c_str()) == 0)  // Tenta remover cada arquivo.
                {
                    count++;
                    cout << "Arquivo \"" << filename << "\" deletado." << endl;
                }
                else
                {
                    cout << "Erro ao apagar o arquivo \"" << filename << "\"" << endl;
                }
            }
        }
        return;
    }
    else
    {
        cout << "Erro na abertura do diret贸rio!" << endl;
        return;
    }
    closedir(dir);
}

void sendDeleteFile(string filename)
{
    sendMessage("delete");
    sendMessage(filename);
}

// //Função para tratamento de comandos de interface do cliente
void interface()
{

    std::string request;

    cout << "\nComandos:\nupload <path/filename.ext>\ndownload <filename.ext>\ndelete <filename.ext>\nlist_server\nlist_client\nget_sync_dir\nexit\n";

    do
    {
        std::string command, file;
        cout << "Digite o comando: \n";
        getline(std::cin, request);

        if (request.find(" ") != -1)
        {
            command = request.substr(0,request.find(" "));
            file = request.substr(request.find(" ") + 1, request.length()- request.find(" "));
        }
        if(request == "exit")
        {
            //Fecha a sessão com o servidor.
            cout << "encerrar conexão\n";
            sendMessage(request);
            closeSocket();
            break;
        }
        else if(request == "list_server")
        {
            //Lista os arquivos salvos no servidor associados ao usuário.
            //sendMessage(request);
            // read socket
            cout << "listar arquivos do servidor\n";
        }
        else if(request == "list_client")
        {
            //Lista os arquivos salvos no diretório “sync_dir”
            cout << "listar arquivos do cliente: \n";
            listClient();
        }
        else if(request == "get_sync_dir")
        {
            //Cria o diretório “sync_dir” e inicia as atividades de sincronização
            //getSyncDir();
            //sendFile(request);
            cout << "sincronizar diretórios\n";
        }
        else if(command == "upload")
        {
            /*Envia o arquivo filename.ext para o servidor, colocando-o no “sync_dir” do
            servidor e propagando-o para todos os dispositivos daquele usuário.
            e.g. upload /home/user/MyFolder/filename.ext*/
            sendFile(file);
            cout << "subir arquivo: " + file + "\n";
        }
        else if(command == "download")
        {
            /*Faz uma cópia não sincronizada do arquivo filename.ext do servidor para
            o diretório local (de onde o servidor foi chamado). e.g. download
            mySpreadsheet.xlsx*/
            //sendMessage(aux);
            cout << "baixar arquivo" + file + "\n";
            downloadFile(file);
        }
        else if(command == "delete")
        {
            //Exclui o arquivo <filename.ext> de “sync_dir”
            deleteFile(file.c_str());
            sendDeleteFile(file);
            cout << "deletar arquivo " + file + "\n";
        }
        else
        {
            cout << "ERRO, comando inválido\nPor favor, digite novamente: \n";
        }
    }
    while(request != "exit");
}

void inotifyInit()
{

    fd = inotify_init();
    wd = inotify_add_watch( fd, dirName.c_str(), IN_CREATE | IN_CLOSE_WRITE | IN_MOVED_TO | IN_DELETE | IN_MOVED_FROM );
}

void listenServer(int sync_sock)
{

    char buffer[ALOC_SIZE];
    int bytes;

    bytes = read(sync_sock,buffer,ALOC_SIZE);
    if(strcmp("exit",buffer) == 0)
    {
        closeSyncSocket(sync_sock);
    }
}

void *sync_thread(void *socket)
{
    //https://www.thegeekstuff.com/2010/04/inotify-c-program-example/

    int *socketAdress = (int *)socket;

    int length, i = 0;
    char buffer[EVENT_BUF_LEN];
    char path[200];
    while(1)
    {

        length = read( fd, buffer, EVENT_BUF_LEN );


        if ( length < 0 )
        {
            perror( "read" );
        }

        if(deletedAllFiles)
        {
            pthread_mutex_lock(&m3);

            deletedAllFiles = false;
            i = length;

            pthread_mutex_unlock(&m3);
        }

        while ( i < length )
        {


            struct inotify_event *event = ( struct inotify_event * ) &buffer[ i ];

            if ( event->len)
            {

                if ( event->mask & IN_CREATE || event->mask & IN_CLOSE_WRITE || event->mask & IN_MOVED_TO)
                {

                    strcpy(path, dirName.c_str());
                    strcat(path, "/");
                    strcat(path, event->name);


                    if(strcmp(event->name,filenameToBeIgnored.c_str()) != 0 && exists(path) && (event->name[0] != '.' && event->name[0] != '*'))
                    {
                        char fpath[256] = "sync_dir_";
                        strcat(fpath, userId.c_str());
                        strcat(fpath, "/");
                        strcat(fpath, event->name);
                        cout << "SENDING: " << event->name << endl;
                        sendFile(fpath);
                    }

                }
                else if ( event->mask & IN_DELETE || event->mask & IN_MOVED_FROM )
                {
                    if(event->name[0] != '.')
                    {
                        string filename = event->name;
                        char ch = '/';

                        cout << "Inotify Delete : " << filename << endl;

                        filename = filename.substr(filename.find_last_of(ch) + 1,filename.length()- filename.find(ch));
                        sendDeleteFile(filename);
                    }
                }
            }

            //i += EVENT_SIZE + event->len;
            i = length;
        }

        i = 0;
        sleep(1);
    }

    inotify_rm_watch( fd, wd );
    close( fd );
}

void sync_client()
{

    char directory[256];
    getcwd(directory, 256);

    dirName = string(directory) + "/" + "sync_dir_" + userId;

    if(mkdir(dirName.c_str(),0777) < 0)
    {
        cout << "Erro ao criar diretorio ou diretorio ja existente" << endl;
    }
    else
    {
        cout << "sync_dir_"+userId << " created" << endl;
    }

    int sync_sock;

    struct sockaddr_in serv_addr_sync;
    struct hostent *server_sync;

    server_sync = gethostbyname(hostname);

    int service = SYNCSERVICE;
    int bytes;

    if(server_sync == NULL)
    {
        cout << "erro ao criar sync socket 1 " << endl;

    }
    if((sync_sock = socket(AF_INET, SOCK_STREAM, 0)) == -1)
    {
        cout << "erro ao criar sync socket 2 " << endl;

    }

    serv_addr_sync.sin_family = AF_INET;
    serv_addr_sync.sin_port = htons(server_port);
    serv_addr_sync.sin_addr = *((struct in_addr *)server_sync->h_addr);

    bzero(&(serv_addr_sync.sin_zero), 8);

    if (connect(sync_sock,(struct sockaddr *) &serv_addr_sync,sizeof(serv_addr_sync)) < 0)
    {
        cout << "erro ao conectar sync_sock" << endl;

    }

    bytes = sendMessageSync(to_string(SYNCSERVICE),sync_sock);
    if(bytes < 0)
    {
        cout << "erro ao conectar sync thread" << endl;
    }

    bytes = sendMessageSync(userId,sync_sock);
    char buffer[ALOC_SIZE];
    bytes = read(sync_sock, buffer,ALOC_SIZE); //Socket confirm
    gsynckSock = sync_sock;

    deleteAllFiles();
    download_all_files(sync_sock);

    if(pthread_create(&sync_thread_thread, NULL, sync_thread, &sync_sock))
    {
        cout << "erro ao criar sync thread" << endl;
    }

    inotifyInit();
}

void *sync_thread_propagate(void *socket)
{
    //https://www.thegeekstuff.com/2010/04/inotify-c-program-example/

    int *socketAdress = (int *)socket;
    int bytes;
    char buffer[ALOC_SIZE];
    bzero(buffer,ALOC_SIZE);


    bytes = read(gpropSock, buffer, ALOC_SIZE);

    while (strcmp(buffer, "exit") != 0)
    {
        /*if(strcmp(buffer,"propagate") == 0){
        	pthread_mutex_lock(&m3);
        	pthread_mutex_lock(&m2);
        	deletedAllFiles = true;
        	deleteAllFiles();
            pthread_mutex_unlock(&m2);
        	download_all_files(gsynckSock);
        	pthread_mutex_unlock(&m3);
        }
        if(strcmp(buffer,"delete") == 0){
        	pthread_mutex_lock(&m3);
        	deletedAllFiles = true;
        	deleteAllFiles();
        	download_all_files(gsynckSock);
        	pthread_mutex_unlock(&m3);
        }*/

        if(strcmp(buffer, "propagate")== 0)
        {
            cout << "PROPAGATE RECEBIDO" << endl;
            bzero(buffer, ALOC_SIZE);
            bytes = read(gpropSock, buffer, ALOC_SIZE);
            cout << bytes << " - " << buffer << endl;
            if(strcmp(buffer, "upload")== 0)
            {
                cout << "UPLOAD PROPAGATE" << endl;
                bzero(buffer, ALOC_SIZE);
                bytes = read(gpropSock, buffer, ALOC_SIZE);

                pthread_mutex_lock(&m3);
                inotify_rm_watch(fd,wd);
                deletedAllFiles = true;
                downloadFileSync(buffer);
                inotifyInit();

                pthread_mutex_unlock(&m3);

            }
            else if(strcmp(buffer, "delete")== 0)
            {
                cout << "DELETE PROPAGATE" << endl;
                bzero(buffer, ALOC_SIZE);
                bytes = read(gpropSock, buffer, ALOC_SIZE);
                cout << "FILENAME: " << buffer <<endl;
                deleteFile(buffer);
            }
            else
            {
                cout << "Erro ao propagar: requisição não reconhecida" << endl;
            }
        }
        bzero(buffer,ALOC_SIZE);
        bytes = recv(gpropSock, buffer, ALOC_SIZE, MSG_WAITALL);
    }
}

void sync_propagate()
{


    int sync_sock;
    struct sockaddr_in serv_addr_sync;
    struct hostent *server_sync;

    server_sync = gethostbyname(hostname);
    int service = PROPAGATESERVICE;
    int bytes;

    if(server_sync == NULL)
    {
        cout << "erro ao criar sync socket 1 " << endl;

    }
    if((sync_sock = socket(AF_INET, SOCK_STREAM, 0)) == -1)
    {
        cout << "erro ao criar sync socket 2 " << endl;

    }

    serv_addr_sync.sin_family = AF_INET;
    serv_addr_sync.sin_port = htons(server_port);
    serv_addr_sync.sin_addr = *((struct in_addr *)server_sync->h_addr);

    bzero(&(serv_addr_sync.sin_zero), 8);

    if (connect(sync_sock,(struct sockaddr *) &serv_addr_sync,sizeof(serv_addr_sync)) < 0)
    {
        cout << "erro ao conectar sync_sock" << endl;
    }

    bytes = sendMessageSync(to_string(PROPAGATESERVICE),sync_sock);
    if(bytes < 0)
    {
        cout << "erro ao conectar sync thread" << endl;
    }

    bytes = sendMessageSync(userId,sync_sock);
    char buffer[ALOC_SIZE];
    bzero(buffer,ALOC_SIZE);

    gpropSock = sync_sock;

    if(pthread_create(&sync_thread_prop, NULL, sync_thread_propagate, &sync_sock))
    {
        cout << "erro ao criar sync thread" << endl;
    }

}

void *waitForNewServer(void* param)
{
    waitSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (waitSocket < 0)
        cout << "ERROR creating waitSocket" << endl;

    struct sockaddr_in self_addr, server_addr;
    self_addr.sin_family = AF_INET;
    self_addr.sin_port = htons(PORT+PORT_OFFSET);
    self_addr.sin_addr.s_addr = INADDR_ANY;
    bzero(&(self_addr.sin_zero), 8);

    if (bind(waitSocket, (struct sockaddr*) &self_addr, sizeof(self_addr)) < 0)
        cout << "ERROR on waitSocket binding" << endl;

    // Se dois servidores tentarem se conectar ao mesmo tempo, é erro na eleição
    listen(waitSocket, 1);
    cout << "ready to receive new primary" << endl;

    while(true)
    {
        int newsockfd;
        socklen_t servLen = sizeof(struct sockaddr_in);
        if ((newsockfd = accept(waitSocket, (struct sockaddr *) &server_addr, &servLen)) == -1)
        {
            cout << "ERROR accepting new server" << endl;
            continue;
        }
        char buff[ALOC_SIZE] = {};
        read(newsockfd, buff, ALOC_SIZE);
        cout << "novo primario detectado: " << buff << endl;
        hostname = (char *) malloc(sizeof(buff));
        strcpy(hostname, buff);

        //Nova porta de conexão
        bzero(buff, ALOC_SIZE);        
        read(newsockfd, buff, ALOC_SIZE);
        cout << "Id do servidor :" << buff << endl;
        server_port = atoi(buff); 
        cout << "Porta do novo servidor :" << server_port << endl;   

        close(newsockfd);

        pthread_cancel(sync_thread_prop);
        pthread_cancel(sync_thread_thread);

        // A partir daqui, envia
        close(sockfd);
        close(gsynckSock);
        close(gpropSock);

        inotify_rm_watch( fd, wd );
        sleep(3);

        ClientSocket();
        cout << "conectado" << endl;

        if(connectSocket() == -1)
        {
            cout << "Erro ao conectar" << endl;
            return 0;
        }
        sync_client();

        sync_propagate();

    }
}

int main(int argc, char *argv[])
{
    pthread_t wait_server_thread;
    //Estabelece conexão
    //argv1 : Host , argv2 = UserId, argv3 = server_port(padrão 4000)
    userId = argv[2];
    hostname = (char *)malloc(sizeof(argv[1]));
    if (argc == 4)
        server_port = atoi(argv[3]);

    strcpy(hostname, argv[1]);

    cout << "porta: " << server_port << endl;
    ClientSocket();

    if(connectSocket() == -1)
    {
        cout << "Erro ao conectar" << endl;
        return 0;
    }

    sync_client();
    sync_propagate();
    pthread_create(&wait_server_thread, NULL, waitForNewServer, NULL);
    interface();
    closeSocket();

    return 0;
}
