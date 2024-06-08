#define _GNU_SOURCE
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <arpa/inet.h>
#include <getopt.h>
#include <netdb.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#define USAGE_MSG_RW "ZotDonation_RWserver [-h] R_PORT_NUMBER W_PORT_NUMBER LOG_FILENAME"\
                  "\n  -h                 Displays this help menu and returns EXIT_SUCCESS."\
                  "\n  R_PORT_NUMBER      Port number to listen on for reader (observer) clients."\
                  "\n  W_PORT_NUMBER      Port number to listen on for writer (donor) clients."\
                  "\n  LOG_FILENAME       File to output server actions into. Create/overwrite, if exists\n"

int socket_listen_init(int server_port);

// These are the message types for the protocol
enum msg_types {
    DONATE,
    CINFO,
    TOP,
    LOGOUT,
    STATS,
    ERROR = 0xFF
};

#define SA struct sockaddr

// Define constants
#define CHARITY_COUNT 5
#define MAX_DONATIONS 3
#define MSG_SIZE 32

// Declarations
void emptyDeleter() {}

// Node structure for linked list
typedef struct node {
    void* data;
    struct node* next;
    struct node* prev;
} node_t;

// Doubly linked list structure
typedef struct list {
    node_t* head;
    int length;
    int (*comparator)(const void*, const void*);
    void (*printer)(void*, void*);
    void (*deleter)(void*);
} dlist_t;

// Function prototypes
dlist_t* CreateList(int (*compare)(const void*, const void*), void (*print)(void*, void*), void (*delete)(void*));
void InsertAtHead(dlist_t* list, void* val_ref);

// Charity structure
typedef struct {
    uint64_t totalDonationAmt;
    uint64_t topDonation;
    uint32_t numDonations;
} charity_t;

// Global variables for charity locks
pthread_mutex_t charity_locks[CHARITY_COUNT];

// Message structure
typedef struct {
    uint8_t msgtype;
    union {
        uint64_t maxDonations[3];  // For TOP
        charity_t charityInfo;     // For CINFO response from Server
        struct {
            uint8_t charity;
            uint64_t amount;
        } donation; // For DONATE & CINFO from client
        struct {
            uint8_t charityID_high;
            uint8_t charityID_low;
            uint64_t amount_high;
            uint64_t amount_low;
        } stats; // For STATS (part 2 only)
    } msgdata;
} message_t;

dlist_t* CreateList(int (*compare)(const void*, const void*), void (*print)(void*, void*), void (*delete)(void*)) {
    dlist_t* list = malloc(sizeof(dlist_t));
    list->comparator = compare;
    list->printer = print;
    list->deleter = delete;
    list->length = 0;
    list->head = NULL;
    return list;
}

void InsertAtHead(dlist_t* list, void* val_ref) {
    if (list == NULL) return;
    node_t* new_node = malloc(sizeof(node_t));
    new_node->data = val_ref;
    new_node->next = list->head;
    new_node->prev = NULL;
    if (list->head != NULL) {
        list->head->prev = new_node;
    }
    list->head = new_node;
    list->length++;
}

/**********************DECLARE ALL LOCKS HERE BETWEEN THES LINES FOR MANUAL GRADING*************/

/***********************************************************************************************/

// Global variables, statistics collected since server start-up
int clientCnt;  // # of client connections made, Updated by the main thread
uint64_t maxDonations[3];  // 3 highest total donations amounts (sum of all donations to all  
                           // charities in one connection), updated by client threads
                           // index 0 is the highest total donation
charity_t charities[5]; // Global variable, one charity per index

int main(int argc, char *argv[]) {

    // Arg parsing
    int opt;
    while ((opt = getopt(argc, argv, "h")) != -1) {
        switch (opt) {
            case 'h':
                fprintf(stderr, USAGE_MSG_RW);
                exit(EXIT_FAILURE);
        }
    }

    // 3 positional arguments necessary
    if (argc != 4) {
        fprintf(stderr, USAGE_MSG_RW);
        exit(EXIT_FAILURE);
    }
    unsigned int r_port_number = atoi(argv[1]);
    unsigned int w_port_number = atoi(argv[2]);
    char *log_filename = argv[3];


    // INSERT SERVER INITIALIZATION CODE HERE
// 

// 

    // CREATE WRITER THREAD HERE
// 

// 

    // Initiate server socket for listening for reader clients
    int reader_listen_fd = socket_listen_init(r_port_number); 
    printf("Listening for readers on port %d.\n", r_port_number);

    int reader_fd;
    struct sockaddr_in client_addr;
    unsigned int client_addr_len = sizeof(client_addr);

    while(1) {
        // Wait and Accept the connection from client
        reader_fd = accept(reader_listen_fd, (SA*)&client_addr, &client_addr_len);
        if (reader_fd < 0) {
            printf("server acccept failed\n");
            exit(EXIT_FAILURE);
        }
        
        // INSERT SERVER ACTIONS FOR CONNECTED READER CLIENT CODE HERE
// 

// 
    }

    close(reader_listen_fd);
    return 0;
}

int socket_listen_init(int server_port){
    int sockfd;
    struct sockaddr_in servaddr;

    // socket create and verification
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        printf("socket creation failed...\n");
        exit(EXIT_FAILURE);
    }
    else
        printf("Socket successfully created\n");

    bzero(&servaddr, sizeof(servaddr));

    // assign IP, PORT
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(server_port);

    int opt = 1;
    
    if(setsockopt(sockfd, SOL_SOCKET, SO_REUSEPORT, (char *)&opt, sizeof(opt))<0)
    {
    	perror("setsockopt");exit(EXIT_FAILURE); 
    }

    // Binding newly created socket to given IP and verification
    if ((bind(sockfd, (SA*)&servaddr, sizeof(servaddr))) != 0) {
        printf("socket bind failed\n");
        exit(EXIT_FAILURE);
    }
    else
        printf("Socket successfully binded\n");

    // Now server is ready to listen and verification
    if ((listen(sockfd, 1)) != 0) {
        printf("Listen failed\n");
        exit(EXIT_FAILURE);
    }
    return sockfd;
}


