#include "server.h"
#include "protocol.h"
#include <pthread.h>
#include <signal.h>

// empty comment lines indicate the start of my modification blocks
// 
#include "RWhelpers.h"
#include <errno.h>
FILE* log_file;
volatile sig_atomic_t sigint = 0;
int readcnt = 0;
int writer_fd;
// endmodif

/**********************DECLARE ALL LOCKS HERE BETWEEN THES LINES FOR MANUAL GRADING*************/
pthread_mutex_t client_cnt_lock;
pthread_mutex_t max_donations_lock;
pthread_mutex_t readers_lock;
pthread_mutex_t writers_lock;
pthread_mutex_t log_file_lock;
/***********************************************************************************************/

// Global variables, statistics collected since server start-up
int clientCnt = 0;  // # of client connections made, Updated by the main thread
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
    init_server(log_filename);

    struct sigaction myaction = {{0}};
    myaction.sa_handler = sigint_handler;
    if (sigaction(SIGINT, &myaction, NULL) == -1) {
        printf("signal handler failed to install\n");
        exit(EXIT_FAILURE);
    }

    // CREATE WRITER THREAD HERE
    pthread_t writer_tid;
    writer_fd = socket_listen_init(w_port_number);
    pthread_create(&writer_tid, NULL, handle_writer, &writer_fd);
    printf("Listening for writers on port %d.\n", w_port_number);
    // endmodif

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
            if (errno == EINTR) {
                if (sigint) {
                    // Kill all threads in thread_list, print stats, exit.
                    break;
                }
                // Continue to read again.
                continue;
            } else {
                // Accept failed for other reason, print error, exit.
                printf("Accept failed\n");
                exit(EXIT_FAILURE);
            }
        }

        pthread_mutex_lock(&client_cnt_lock);
        clientCnt++;
        pthread_mutex_unlock(&client_cnt_lock);

        // INSERT SERVER ACTIONS FOR CONNECTED READER CLIENT CODE HERE
        // 
        int *reader_fd_ptr = malloc(sizeof(int));
        if (!reader_fd_ptr) {
            exit(EXIT_FAILURE);
        }
        *reader_fd_ptr = reader_fd;
        pthread_t reader_tid;
        pthread_create(&reader_tid, NULL, handle_reader, reader_fd_ptr);
        // conc prog slide 46, and doc said to not detach for writer
        pthread_detach(reader_tid);

        if (sigint) {
            break;
        }
        // endmodif
    }

    close(reader_listen_fd);
    pthread_cancel(writer_tid);
    pthread_join(writer_tid, NULL);
    print_stats();
    cleanup_server();
    return 0;
}

int socket_listen_init(int server_port) {
    int sockfd;
    struct sockaddr_in servaddr;

    // socket create and verification
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        printf("socket creation failed...\n");
        exit(EXIT_FAILURE);
    } else {
        printf("Socket successfully created\n");
    }

    bzero(&servaddr, sizeof(servaddr));

    // assign IP, PORT
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(server_port);

    int opt = 1;
    
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEPORT, (char *)&opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        exit(EXIT_FAILURE); 
    }

    // Binding newly created socket to given IP and verification
    if ((bind(sockfd, (SA*)&servaddr, sizeof(servaddr))) != 0) {
        printf("socket bind failed\n");
        exit(EXIT_FAILURE);
    } else {
        printf("Socket successfully binded\n");
    }

    // Now server is ready to listen and verification
    if ((listen(sockfd, 10)) != 0) { // increased backlog to 10
        printf("Listen failed\n");
        exit(EXIT_FAILURE);
    }
    return sockfd;
}
