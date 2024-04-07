#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#include <semaphore.h>


#define PORT 7379

sem_t semaphores;

struct Argouments{
    int client_sockfd;
    struct LinkedList* list;
};

struct Node {
    char* key;
    char* value;
    int enter_time;
    int total_time;
    struct Node* next;
};

struct LinkedList {
    struct Node* head;
};

void removeElement(struct LinkedList* list, char* key) {
    if (list->head == NULL) {
        return;
    }

    // If the key to be removed is at the head
    if (strcmp(list->head->key, key) == 0) {
        struct Node* temp = list->head;
        list->head = list->head->next;
        free(temp);
        return;
    }

    // Find the node with the key to be removed
    struct Node* current = list->head;
    struct Node* prev = NULL;
    while (current != NULL && strcmp(current->key, key) != 0) {
        prev = current;
        current = current->next;
    }

    // If the key is not found
    if (current == NULL) {
        return;
    }

    // Remove the node
    prev->next = current->next;
    free(current);
}

void add(struct LinkedList* list, char* key, char *value, int enter_time, int total_time) {
    struct Node* newNode = (struct Node*)malloc(sizeof(struct Node));
    newNode->key = key;
    newNode->value = value;
    newNode->enter_time = enter_time;
    newNode->total_time = total_time;
    newNode->next = NULL;

    if (list->head == NULL) {
        list->head = newNode;
    }
    else {
        struct Node* current = list->head;
        while (current->next != NULL) {
            current = current->next;
        }
        current->next = newNode;
    }
}

void freeLinkedList(struct LinkedList* list) {
    struct Node* current = list->head;
    while (current != NULL) {
        struct Node* temp = current;
        current = current->next;
        free(temp);
    }
    list->head = NULL;
}

void print(struct LinkedList* list) {
    struct Node* current = list->head;
    while (current != NULL) {
        printf("Key: %s, Value: %s, Enter time: %d, total time: %d\n ", current->key, current->value, current->enter_time, current->total_time);
        current = current->next;
    }
}


struct Node* search(struct LinkedList* list, char * key) {
    struct Node* current = list->head;
    while (current != NULL) {
        if (strcmp(current->key, key) == 0) {
            return current;
        }
        current = current->next;
    }
    return NULL;
}

void exit_with_error(const char * msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}


void reading_from_socket(struct Argouments* t){
    char buf[1024];
    

    while(1){

        //reset buf
        memset(buf, '\0', sizeof(buf));

        //receive message
        int bytes_read = recv(t->client_sockfd, buf, sizeof(buf), 0);  
        if(bytes_read == 0) break;



        // parsing
        char* command = strtok(buf, "\r\n");

        while(command != NULL){
            
            // SET
            if(strcmp(command, "SET") == 0){

                char* value= calloc(64,sizeof(char));
                char* key = calloc(64,sizeof(char));
                int enter_time = -1;
                int total_time = -1;

                for (int i = 0; i<8;i++){
                    command = strtok(NULL,"\r\n");
                    if(command == NULL) break;

                    if(i == 1){
                        strcpy (key, command);
                    }
                    else if (i == 3){
                        strcpy(value, command);
                    }
                    else if(i == 7){
                        total_time = atoi(command);
                        enter_time = time(NULL);
                    }
                    
                }

                
                /*CS*/
                if(sem_wait(&semaphores)==-1)exit_with_error("error in sem_wait during set"); 
                add(t->list, key, value, enter_time, total_time);
                if(sem_post(&semaphores)==-1)exit_with_error("error in sem_post during set"); 
                /*end CS*/


                int bytes_written = write(t->client_sockfd, "+OK\r\n", strlen("+OK\r\n"));
                if (bytes_written == -1) {
                    exit_with_error("Error writing to client");
                }
                
            }

            if(command == NULL) break;

            // GET
            if(strcmp(command, "GET") == 0){
                char* key = calloc(64,sizeof(char));

                for (int i = 0; i<2;i++){
                    command = strtok(NULL,"\r\n");
                    if(i == 1){
                       strcpy (key, command);
                    }
                }

                if(sem_wait(&semaphores)==-1)exit_with_error("error in sem_wait during set"); 


                struct Node* n = search(t->list, key);

                if(n == NULL){
                    int bytes_written = write(t->client_sockfd, "$-1\r\n", strlen("$-1\r\n"));
                    if (bytes_written == -1) {
                        exit_with_error("Error writing to client");
                    }
                    
                }
                else{

                    int actual_time = time(NULL);


                    if(n->enter_time!= -1 && n->total_time != -1 && actual_time-n->enter_time>n->total_time){
                        removeElement(t->list,key);
                        int bytes_written = write(t->client_sockfd, "$-1\r\n", strlen("$-1\r\n"));
                        if (bytes_written == -1) {
                            exit_with_error("Error writing to client");
                        }
                    }
                    else{
                        char response[256];
                        sprintf(response,"$%ld\r\n%s\r\n", strlen(n->value), n->value);

                        int bytes_written = write(t->client_sockfd, response, strlen(response));
                        if (bytes_written == -1) {
                            exit_with_error("Error writing to client");
                        }
                    }


                    
                }
                if(sem_post(&semaphores)==-1)exit_with_error("error in sem_post during set"); 
                free(key);    
            }

            //Connection
            if(strcmp(command, "CLIENT") == 0){
                int bytes_written = write(t->client_sockfd, "+OK\r\n", strlen("+OK\r\n"));
                if (bytes_written == -1) {
                    exit_with_error("Error writing to client");
                }
                break;
            }

            command = strtok(NULL,"\r\n");
        }

        
        
    }

    free(t);
    pthread_exit(EXIT_SUCCESS);
}

// https://redis.io/docs/reference/protocol-spec/

int main(int argc, const char * argv[]) {

    struct LinkedList* list = malloc(sizeof(struct LinkedList));
    list->head =NULL;

    // Open Socket and receive connections

    // Keep a key, value store (you are free to use any data structure you want)

    // Create a process for each connection to serve set and get requested

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        exit_with_error("Error create socket");
    }

    int enable = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int));


    struct sockaddr_in server_addr;
    int port_num = PORT;
    memset((char*)&server_addr, 0, sizeof(server_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(port_num);

    if (bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        exit_with_error("Error bind socket");
    }

    if (listen(sockfd, 10) == -1) {
        exit_with_error("Error listen on socket");
    }

    if(sem_init(&semaphores, 1, 1) == -1) exit_with_error("Error initialization semaphore");



    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_addr_len = sizeof(client_addr);

        int client_sockfd = accept(sockfd, (struct sockaddr *)&client_addr, &client_addr_len);
        if (client_sockfd == -1) {
            exit_with_error("Error accept connection");
        }

       

        struct Argouments* t = malloc(sizeof(struct Argouments));
        t->list = list;
        t->client_sockfd = client_sockfd;

        pthread_t threads;
        pthread_create(&threads, NULL, (void *)reading_from_socket, (void*)t);
        
    }

    close(sockfd);
    freeLinkedList(list);
    free(list);
        
}

