#include "server.h"
#include "protocol.h"
#include <pthread.h>
#include <signal.h>

#include "MThelpers.h"
#include <errno.h>

/**********************DECLARE ALL LOCKS HERE BETWEEN THES LINES FOR MANUAL GRADING*************/
// pthread_mutex_t server_stats_lock = PTHREAD_MUTEX_INITIALIZER;
// pthread_mutex_t log_file_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t server_stats_lock;
pthread_mutex_t log_file_lock;
pthread_mutex_t charity_locks[5];
/***********************************************************************************************/

dlist_t* list;
FILE* log_file;
volatile sig_atomic_t sigint_flag = 0;

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
                fprintf(stderr, USAGE_MSG_MT);
                exit(EXIT_FAILURE);
        }
    }

    // 3 positional arguments necessary
    if (argc != 3) {
        fprintf(stderr, USAGE_MSG_MT);
        exit(EXIT_FAILURE);
    }
    unsigned int port_number = atoi(argv[1]);
    char *log_filename = argv[2];


    // INSERT SERVER INITIALIZATION CODE HERE
    init_server(log_filename);
    struct sigaction myaction = {{0}};
    myaction.sa_handler = sigint_handler;
    if (sigaction(SIGINT, &myaction, NULL) == -1) {
        perror("signal handler failed to install");
        exit(EXIT_FAILURE);
    }
    // end modif

    // Initiate server socket for listening
    int listen_fd = socket_listen_init(port_number);
    printf("Currently listening on port: %d.\n", port_number);
    int client_fd;
    struct sockaddr_in client_addr;
    unsigned int client_addr_len = sizeof(client_addr);

    while(1) {
        // Wait and Accept the connection from client
        client_fd = accept(listen_fd, (SA*)&client_addr, &client_addr_len);
        if (client_fd < 0) {
            printf("server acccept failed\n");
            // disc
            if (errno == EINTR) {
                if (sigint_flag) {
                    // Kill all threads in thread_list, print stats, exit.
                    break;
                }
                // Continue to read again.
                continue;
            } else {
                // Accept failed for other reason, print error, exit.
                printf("other reason\n");
                exit(EXIT_FAILURE);
            }
            // 
        }

        // INSERT SERVER ACTIONS FOR CONNECTED CLIENT CODE HERE
        
        // Call the helper function you made to join terminated threads.
        remove_joinable_threads();
        
        // Create your new thread.
        // tid_t new_tid = pthread_create(client_function);
        pthread_t tid;
        int* client_fd_ptr = malloc(sizeof(int));
        *client_fd_ptr = client_fd;
        if (pthread_create(&tid, NULL, client_thread, client_fd_ptr) != 0) {
            perror("pthread_create");
            close(client_fd);
            free(client_fd_ptr);
            continue;
        }
        
        // Put new_tid into the thread_list.
        pthread_t* tid_ptr = malloc(sizeof(pthread_t));
        *tid_ptr = tid;
        InsertAtHead(list, tid_ptr);
        // InsertAtHead(thread_list, new_tid);
        
        // Need to see if SIGINT occurred between accept and here.
        if (sigint_flag) {
            // Kill all threads in thread_list, print stats, exit.
            break;
        }
        // 
    }
    // 
    kill_and_join_all_threads();
    cleanup_server();
    print_stats();
    // 
    close(listen_fd);
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


