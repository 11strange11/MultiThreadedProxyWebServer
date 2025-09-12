#include "proxy_parse.h"
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
pthread_t threadid[MAX_CLIENTS];

/*
Semaphore and mutex locks are used when there is a shared resource (cache)
*/

// semaphore has multiple states: available and unavailable (eg. 10)
sem_t semaphore;

// lock has only two states: locked and unlocked
pthread_mutex_t lock; 



 

