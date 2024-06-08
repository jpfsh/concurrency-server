#include "server.h"
#include "protocol.h"
#include <pthread.h>
#include <signal.h>

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

    // CREATE WRITER THREAD HERE

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


