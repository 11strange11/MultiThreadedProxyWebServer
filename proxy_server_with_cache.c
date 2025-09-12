#include "proxy_parse.h"
#include <cstddef>
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

        struct sockaddr_in *client_pt = (struct sockaddr_in *)&client_addr;
        // getting the ip address of the client
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



 

