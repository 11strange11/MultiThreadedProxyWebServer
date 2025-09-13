#include "proxy_parse.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/wait.h>
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <time.h>

// Number of clients that can connect to the proxy server
#define MAX_CLIENTS 10
#define MAX_BYTES 4096
#define MAX_SIZE 200 * 1024
#define MAX_ELEMENT_SIZE 10 * 1024

typedef struct cache_element cache_element;

// list of all elements in cache
struct cache_element{
    char* data;
    int len;
    char* url;
    time_t lru_time_track;
    cache_element* next; 
};


// function to find an element in the cache
cache_element* find(char* url);

// function to add an element to the cache
int add_cache_element(char* data, int size, char* url);

// function to remove an element from the cache
void remove_cache_element();



int port_number = 8080;
int proxy_socketId; 

/*
the number of clients that will connect will each 
have individual thread, and each thread will have indiviual 
socket
*/
pthread_t tid[MAX_CLIENTS]; // array of thread Ids for each client

/*
Semaphore and mutex locks are used when there is a shared resource (cache)
*/

// semaphore has multiple states: available and unavailable (eg. 10)
sem_t semaphore;

// lock has only two states: locked and unlocked
pthread_mutex_t lock; 

cache_element* head;
int cache_size;

// Connceting to end server (eg. host of google.com)
int connectRemoteServer(char *host_addr, int port_num){
    int remoteSocket = socket(AF_INET, SOCK_STREAM, 0);
    if(remoteSocket<0){
        printf("Error in creating your socket\n");
        return -1;
    }
    struct hostent* host = gethostbyname(host_addr); //gets the host address
    if(host == NULL){
        fprintf(stderr, "No such host exists\n");
        return -1;
    }
    struct sockaddr_in server_addr;
    
    bzero((char *)&server_addr, sizeof(server_addr)); 
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port_num);
    
    // copying the host address to the server address
    bcopy((char *)host->h_addr,(char *)&server_addr.sin_addr.s_addr,host->h_length);

    // Connect to the remote server
    if( connect(remoteSocket, (struct sockaddr*)&server_addr, (socklen_t)sizeof(server_addr)) < 0 )
	{
		fprintf(stderr, "Error in connecting !\n"); 
		return -1;
	}
    // free host_addr
    return remoteSocket;

}

int handle_request(int clientSocket, ParsedRequest *request, char *tempReq){
    // Creating a buffer to store the request
    char *buf = (char *)malloc(sizeof(char)*MAX_BYTES);
    strcpy(buf, "GET");
    strcat(buf, request->path);
    strcat(buf, " ");
    strcat(buf, request->version);
    strcat(buf, "\r\n");

    int len = strlen(buf);

    // Setting the Connection header
    if (ParsedHeader_set(request, "Connection", "close") < 0){
		printf("set header key is not working\n");
	}
    // Setting the Host header
    if(ParsedHeader_get(request, "Host") == NULL){
		if(ParsedHeader_set(request, "Host", request->host) < 0){
			printf("Set \"Host\" header key not working\n");
		}
	}

    if(ParsedRequest_unparse_headers(request, buf+len, (size_t)MAX_BYTES - len) < 0){
        printf("unparsing failed\n");
    }

    int server_port = 80; // End server (not proxy server) port
    if(request->port != NULL ){
        server_port = atoi(request->port);
    }

    int remoteSocketID = connectRemoteServer(request->host, server_port);

    if(remoteSocketID < 0){
        return -1;
    }

    int bytes_send = send(remoteSocketID, buf, strlen(buf), 0);

}


void* thread_fn(void* socketNew){
    //checking if the semaphore is available (meaning the cache is not full)
    sem_wait(&semaphore);
    int p;
    sem_getvalue(&semaphore, &p);
    printf("Semaphore value: %d\n", p);

    // Obtaining the socket id from the thread function
    int* t = (int*)(socketNew);
    int socket = *t;
    int bytes_send_client, len; //bytes sent to the client

    // Allocating memory for the buffer (4KB) for each client
    char *buffer = (char*)calloc(MAX_BYTES, sizeof(char));


    bzero(buffer, MAX_BYTES);
    // Receiving the request of client by proxy server
    bytes_send_client = recv(socket, buffer, MAX_BYTES, 0);


    // while the client is sending the request to the proxy server
    while(bytes_send_client > 0){
        len = strlen(buffer);
        if(strstr(buffer, "\r\n\r\n") == NULL){
            bytes_send_client = recv(socket, buffer + len, MAX_BYTES - len, 0);
        }
        else{
            break;
        }
    }
    // Creating a temporary request buffer
    char *tempReq = (char*)malloc(strlen(buffer)*sizeof(char) + 1);

    for(int i=0; i<strlen(buffer); i++){
        tempReq[i] = buffer[i];
    }
    struct cache_element* temp = find(tempReq);
    if(temp != NULL){  // if the request is found in the cache, then send the response to the client
        int size = temp->len/sizeof(char);
        int pos = 0;
        char response[MAX_BYTES];
        while(pos < size){
            bzero(response, MAX_BYTES);
            for(int i=0; i<MAX_BYTES; i++){
                response[i] = temp->data[pos];
                pos++;
            }
            send(socket, response, MAX_BYTES, 0);
        }
        printf("Data retrieved from cache\n\n");
        printf("%s\n\n", response);
    }
    else if(bytes_send_client > 0){
        len = strlen(buffer);
        ParsedRequest* request = ParsedRequest_create();

        if(ParsedRequest_parse(request, buffer, len) < 0){
            printf("Parsing failed\n");
        }
        else{
            bzero(buffer, MAX_BYTES);
            if(!strcmp(request->method, "GET")){
                if(request->host && request->path && (checkHTTPversion(request->version) == 1) ){
                    bytes_send_client = handle_request(socket, request, tempReq);
                    if(bytes_send_client == -1){
                        sendErrorMessage(socket, 500);
                    }
                }
                else{
                    sendErrorMessage(socket, 500);
                }
            }
            else{
                printf("Method not supported\n");
            }
        }
        ParsedRequest_destroy(request); // destroying the request
    }
    else if(bytes_send_client < 0){
        perror("Error in receiving from client\n");
    }
    else if(bytes_send_client == 0){
        printf("Client disconnected\n");
    }
    // shutting down the socket and closing the socket
    shutdown(socket, SHUT_RDWR);
    close(socket);
    free(buffer); // freeing the buffer
    sem_post(&semaphore); // 

    sem_getvalue(&semaphore, &p);
    printf("Semaphore post value is %d\n", p);
    free(tempReq);
    return NULL;

}


int main(int argc, char* argv[]){
    int client_socketId, client_len;

    // initialize the socket address struct
    struct sockaddr_in server_addr, client_addr;

    // initialize the semaphore
    sem_init(&semaphore, 0, MAX_CLIENTS);

    //initialize the mutex
    pthread_mutex_init(&lock, NULL);


    // checking  if the port number is provided
    if(argc == 2){ // if two arguments are provided, then the second argument is the port number
        port_number = atoi(argv[1]);
    }
    else{ // if not, then the port number is not provided
        printf("Too few arguments\n");
        exit(1);
    }

    printf("Starting proxy server on port: %d\n", port_number);

    // There is just one proxy socket that will be used to accept connections from clients
    // This proxy socket will spawn a thread for each client that connects to the proxy server
    proxy_socketId = socket(AF_INET, SOCK_STREAM, 0); 

    if(proxy_socketId < 0){
        perror("Failed to create a socket");
        exit(1);
    }
    int reuse = 1;
    if(setsockopt(proxy_socketId, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse)) < 0){
        perror("serSockOpt failed\n");
    }

    bzero((char*)&server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port_number);
    server_addr.sin_addr.s_addr = INADDR_ANY;
    // bind is used to bind the socket to the port number and address
    if(bind(proxy_socketId, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0){
        perror("Port is not available\n");
        exit(1);
    }
    printf("Binding on port: %d\n", port_number);
    int listen_status = listen(proxy_socketId, MAX_CLIENTS);
    if(listen_status < 0){
        perror("Error in listening\n");
        exit(1);
    }

    // Defining iterator, how many client are connected to the proxy server
    int i = 0;
    int Connnected_socketId[MAX_CLIENTS];

    // 
    while(1){
        // cleaning the client address , removing the garbage values
        bzero((char* )&client_addr, sizeof(client_addr));
        client_len = sizeof(client_addr);
        // accept is used to accept the connection from the client
        client_socketId = accept(proxy_socketId, (struct sockaddr*)&client_addr, (socklen_t*)&client_len);
        // if socket is not open
        if(client_socketId < 0){
            printf("Not able to connect to the client\n");
            exit(1);
        }
        else{
            Connnected_socketId[i] = client_socketId; 
        }

        // getting the ip address of the client
        struct sockaddr_in *client_pt = (struct sockaddr_in *)&client_addr;
        struct in_addr ip_addr = client_pt->sin_addr;

        char str[INET_ADDRSTRLEN];

        // Extracting the ip address of the client from network format to string format
        inet_ntop(AF_INET, &ip_addr, str,INET_ADDRSTRLEN);
        printf("Client is connected with port number &d and ip address %s\n", ntohs(client_addr.sin_port), str);

        // creating a thread for each client and execute thread function,
        // And, when another client connects, the thread function will be executed again for the new client
        pthread_create(&tid[i], NULL, thread_fn, (void*)&Connnected_socketId[i]);  
        i++;
    }
    close(proxy_socketId);
    return 0;

}



 

