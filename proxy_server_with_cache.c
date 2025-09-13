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
#define MAX_BYTES 4096
#define MAX_CLIENTS 400
#define MAX_SIZE 200 * (1<<20)
#define MAX_ELEMENT_SIZE 10 * (1<<20)

typedef struct cache_element cache_element;

// list of all elements in cache
struct cache_element{
    char* data;
    int len;
    char* url;
    time_t lru_time_track;
    cache_element* next; 
};



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

// remove element from the cache
void remove_cache_element(){
    // If cache is not empty searches for the node which has the least lru_time_track and deletes it
    cache_element * p ;  	// Cache_element Pointer (Prev. Pointer)
	cache_element * q ;		// Cache_element Pointer (Next Pointer)
	cache_element * temp;	// Cache element to remove
    //sem_wait(&cache_lock);
    int temp_lock_val = pthread_mutex_lock(&lock);
	printf("Remove Cache Lock Acquired %d\n",temp_lock_val); 
	if( head != NULL) { // Cache != empty
		for (q = head, p = head, temp =head ; q -> next != NULL; 
			q = q -> next) { // Iterate through entire cache and search for oldest time track
			if(( (q -> next) -> lru_time_track) < (temp -> lru_time_track)) {
				temp = q -> next;
				p = q;
			}
		}
		if(temp == head) { 
			head = head -> next; /*Handle the base case*/
		} else {
			p->next = temp->next;	
		}
		cache_size = cache_size - (temp -> len) - sizeof(cache_element) - 
		strlen(temp -> url) - 1;     //updating the cache size
		free(temp->data);     		
		free(temp->url); // Free the removed element 
		free(temp);
	} 
    // Releasing the lock
    temp_lock_val = pthread_mutex_unlock(&lock);
	printf("Remove Cache Lock Unlocked %d\n",temp_lock_val); 
};


// function to find an element in the cache
cache_element* find(char* url){
    cache_element* site = NULL;

    int temp_lock_val = pthread_mutex_lock(&lock);
    printf("Remove Cache Lock Acquired %d\n",temp_lock_val); 
    if(head!=NULL){
        site = head;
        while(site!=NULL){
            if(!strcmp(site->url,url)){
                printf("LRU Time Track Before : %ld", site->lru_time_track);
                printf("\nurl found\n");
				// Updating the time_track
				site->lru_time_track = time(NULL);
				printf("LRU Time Track After : %ld", site->lru_time_track);
				break;
            }
            site = site->next;
        }
    }
    else{
        printf("No url found\n");
    }

    // Releasing the lock
    temp_lock_val = pthread_mutex_unlock(&lock);
	printf("Remove Cache Lock Unlocked %d\n",temp_lock_val); 
    return site;

}// first request will always be made to the remote server
 // because cache is empty (No url found, will be returned)
 // Thus, it will be added to the cache using the following function

// function to add an element to the cache
int add_cache_element(char* data, int size, char* url){
    int temp_lock_val = pthread_mutex_lock(&lock);
	printf("Add Cache Lock Acquired %d\n", temp_lock_val);
    int element_size=size+1+strlen(url)+sizeof(cache_element); // Size of the new element which will be added to the cache
    if(element_size>MAX_ELEMENT_SIZE){
		//sem_post(&cache_lock);
        // If element size is greater than MAX_ELEMENT_SIZE we don't add the element to the cache
        temp_lock_val = pthread_mutex_unlock(&lock);
		printf("Add Cache Lock Unlocked %d\n", temp_lock_val);
        return 0;
    }
    else{
        while(cache_size+element_size>MAX_SIZE){
            // We keep removing elements from cache until we get enough space to add the element
            remove_cache_element();
        }
        cache_element* element = (cache_element*) malloc(sizeof(cache_element)); // Allocating memory for the new cache element
        element->data= (char*)malloc(size+1); // Allocating memory for the response to be stored in the cache element
		strcpy(element->data,data); 
        element -> url = (char*)malloc(1+( strlen( url )*sizeof(char)  )); // Allocating memory for the request to be stored in the cache element (as a key)
		strcpy( element -> url, url );
		element->lru_time_track=time(NULL);    // Updating the time_track
        element->next=head; 
        element->len=size;
        head=element;
        cache_size+=element_size;
        temp_lock_val = pthread_mutex_unlock(&lock);
		printf("Add Cache Lock Unlocked %d\n", temp_lock_val);
		//sem_post(&cache_lock);
		// free(data);
		// printf("--\n");
		// free(url);
        return 1;
    }
    return 0;
};

// function to remove an element from the cache

// Different error messages
int sendErrorMessage(int socket, int status_code)
{
	char str[1024];
	char currentTime[50];
	time_t now = time(0);

	struct tm data = *gmtime(&now);
	strftime(currentTime,sizeof(currentTime),"%a, %d %b %Y %H:%M:%S %Z", &data);

	switch(status_code)
	{
		case 400: snprintf(str, sizeof(str), "HTTP/1.1 400 Bad Request\r\nContent-Length: 95\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>400 Bad Request</TITLE></HEAD>\n<BODY><H1>400 Bad Rqeuest</H1>\n</BODY></HTML>", currentTime);
				  printf("400 Bad Request\n");
				  send(socket, str, strlen(str), 0);
				  break;

		case 403: snprintf(str, sizeof(str), "HTTP/1.1 403 Forbidden\r\nContent-Length: 112\r\nContent-Type: text/html\r\nConnection: keep-alive\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>403 Forbidden</TITLE></HEAD>\n<BODY><H1>403 Forbidden</H1><br>Permission Denied\n</BODY></HTML>", currentTime);
				  printf("403 Forbidden\n");
				  send(socket, str, strlen(str), 0);
				  break;

		case 404: snprintf(str, sizeof(str), "HTTP/1.1 404 Not Found\r\nContent-Length: 91\r\nContent-Type: text/html\r\nConnection: keep-alive\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>404 Not Found</TITLE></HEAD>\n<BODY><H1>404 Not Found</H1>\n</BODY></HTML>", currentTime);
				  printf("404 Not Found\n");
				  send(socket, str, strlen(str), 0);
				  break;

		case 500: snprintf(str, sizeof(str), "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 115\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>500 Internal Server Error</TITLE></HEAD>\n<BODY><H1>500 Internal Server Error</H1>\n</BODY></HTML>", currentTime);
				  //printf("500 Internal Server Error\n");
				  send(socket, str, strlen(str), 0);
				  break;

		case 501: snprintf(str, sizeof(str), "HTTP/1.1 501 Not Implemented\r\nContent-Length: 103\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>404 Not Implemented</TITLE></HEAD>\n<BODY><H1>501 Not Implemented</H1>\n</BODY></HTML>", currentTime);
				  printf("501 Not Implemented\n");
				  send(socket, str, strlen(str), 0);
				  break;

		case 505: snprintf(str, sizeof(str), "HTTP/1.1 505 HTTP Version Not Supported\r\nContent-Length: 125\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>505 HTTP Version Not Supported</TITLE></HEAD>\n<BODY><H1>505 HTTP Version Not Supported</H1>\n</BODY></HTML>", currentTime);
				  printf("505 HTTP Version Not Supported\n");
				  send(socket, str, strlen(str), 0);
				  break;

		default:  return -1;

	}
	return 1;
}

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

    bzero(buf, MAX_BYTES);

    // Receiving bytes from the remote server
    bytes_send = recv(remoteSocketID, buf, MAX_BYTES-1, 0);
	char *temp_buffer = (char*)malloc(sizeof(char)*MAX_BYTES); //temp buffer
	int temp_buffer_size = MAX_BYTES;
	int temp_buffer_index = 0;

    while(bytes_send > 0){
        bytes_send = send(clientSocket, buf, bytes_send, 0);

        for(int i=0;i<bytes_send/sizeof(char);i++){
			temp_buffer[temp_buffer_index] = buf[i];
			// printf("%c",buf[i]); // Response Printing
			temp_buffer_index++;
		}
        temp_buffer_size += MAX_BYTES;
        temp_buffer=(char*)realloc(temp_buffer,temp_buffer_size);

        if(bytes_send < 0){
            perror("Error in sending data to client socket\n");
			break;
        }
        // Freeing the buffer
        bzero(buf, MAX_BYTES);
        // Receiving bytes from the remote server
		bytes_send = recv(remoteSocketID, buf, MAX_BYTES-1, 0);

    }
    // Adding the response to the cache
    temp_buffer[temp_buffer_index]='\0';
	free(buf);
	add_cache_element(temp_buffer, strlen(temp_buffer), tempReq);
	printf("Done\n");

    // Freeing the temp buffer
	free(temp_buffer);
	
	// Closing the remote socket
 	close(remoteSocketID);
	return 0;
}

int checkHTTPversion(char* msg){
    int version = -1;
    if(strncmp(msg, "HTTP/1.1",8) == 0){
        version = 1;
    }
    else if(strncmp(msg, "HTTP/1.0", 8) == 0){
        version = 1;
    }
    else if(strncmp(msg, "HTTP/1.0", 8) == 0){
        version = 1;
    }
    else{
        version = -1;
    }
    return version;
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
        printf("Client is connected with port number %d and ip address %s \n", ntohs(client_addr.sin_port), str);

        // creating a thread for each client and execute thread function,
        // And, when another client connects, the thread function will be executed again for the new client
        pthread_create(&tid[i], NULL, thread_fn, (void*)&Connnected_socketId[i]);  
        i++;
    }
    close(proxy_socketId);
    return 0;

}



 

