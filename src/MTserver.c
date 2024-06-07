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

#define SA struct sockaddr
#define USAGE_MSG_MT "ZotDonation_MTserver [-h] PORT_NUMBER LOG_FILENAME"

// These are the message types for the protocol
enum msg_types {
    DONATE,
    CINFO,
    TOP,
    LOGOUT,
    STATS,
    ERROR = 0xFF
};

typedef struct {
    uint64_t totalDonationAmt; // Sum of donations made to charity by donors (clients)
    uint64_t topDonation;      // Largest donation made to this charity
    uint32_t numDonations;     // Count of donations made to charity
} charity_t;

// This is the struct for each message sent to the server
typedef struct { 
    uint8_t msgtype;  
    union {
        uint64_t maxDonations[3];  // For TOP
        charity_t charityInfo;     // For CINFO response from Server
        struct {uint8_t charity; uint64_t amount;} donation; // For DONATE & CINFO from client
        struct {uint8_t charityID_high; uint8_t charityID_low; 
        uint64_t amount_high; uint64_t amount_low;} stats;   // For STATS (part 2 only)
    } msgdata; 
} message_t;

pthread_mutex_t server_stats_lock;
pthread_mutex_t charity_locks[5];
pthread_mutex_t log_file_lock;
pthread_mutex_t thread_list_lock;

charity_t charities[5];
uint32_t clientCnt = 0;
uint64_t maxDonations[3] = {0};
FILE *log_file;
int listen_fd;

typedef struct thread_node {
    pthread_t thread_id;
    struct thread_node *next;
} thread_node_t;

thread_node_t *thread_list_head = NULL;

void sigint_handler(int sig);
void *client_handler(void *client_fd_ptr);
int socket_listen_init(int server_port);
void cleanup_threads();

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "%s\n", USAGE_MSG_MT);
        exit(EXIT_FAILURE);
    }

    int port_number = atoi(argv[1]);
    char *log_filename = argv[2];

    // Open log file
    log_file = fopen(log_filename, "w");
    if (log_file == NULL) {
        perror("Failed to open log file");
        exit(EXIT_FAILURE);
    }

    // Initialize locks
    pthread_mutex_init(&server_stats_lock, NULL);
    pthread_mutex_init(&log_file_lock, NULL);
    pthread_mutex_init(&thread_list_lock, NULL);
    for (int i = 0; i < 5; i++) {
        pthread_mutex_init(&charity_locks[i], NULL);
    }

    // Initialize SIGINT handler
    struct sigaction myaction = {{0}};
    myaction.sa_handler = sigint_handler;
    if (sigaction(SIGINT, &myaction, NULL) == -1) {
        printf("signal handler failed to install\n");
        exit(EXIT_FAILURE);
    }

    // Initialize server statistics and charity data structures
    clientCnt = 0;
    memset(maxDonations, 0, sizeof(maxDonations));
    for (int i = 0; i < 5; i++) {
        charities[i].totalDonationAmt = 0;
        charities[i].topDonation = 0;
        charities[i].numDonations = 0;
    }

    // Initiate server socket for listening
    listen_fd = socket_listen_init(port_number);
    printf("Currently listening on port: %d.\n", port_number);
    int client_fd;
    struct sockaddr_in client_addr;
    unsigned int client_addr_len = sizeof(client_addr);

    while(1) {
        // Wait and Accept the connection from client
        client_fd = accept(listen_fd, (SA*)&client_addr, &client_addr_len);
        if (client_fd < 0) {
            printf("server accept failed\n");
            exit(EXIT_FAILURE);
        }

        // Clean up terminated threads
        cleanup_threads();

        // Create a new thread for each client connection
        pthread_t client_thread;
        int *client_fd_ptr = malloc(sizeof(int));
        *client_fd_ptr = client_fd;
        pthread_create(&client_thread, NULL, client_handler, client_fd_ptr);
        pthread_detach(client_thread);

        // Add thread to the thread list
        pthread_mutex_lock(&thread_list_lock);
        thread_node_t *new_node = malloc(sizeof(thread_node_t));
        new_node->thread_id = client_thread;
        new_node->next = thread_list_head;
        thread_list_head = new_node;
        pthread_mutex_unlock(&thread_list_lock);

        // Update client connection count
        pthread_mutex_lock(&server_stats_lock);
        clientCnt++;
        pthread_mutex_unlock(&server_stats_lock);
    }

    close(listen_fd);
    return 0;
}

void sigint_handler(int sig) {
    printf("shutting down server\n");
    close(listen_fd);
    exit(0);
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
    if ((listen(sockfd, 1)) != 0) {
        printf("Listen failed\n");
        exit(EXIT_FAILURE);
    }
    return sockfd;
}

void *client_handler(void *client_fd_ptr) {
    int client_fd = *((int *)client_fd_ptr);
    free(client_fd_ptr);
    message_t message;
    uint64_t client_total_donations = 0;

    while (read(client_fd, &message, sizeof(message_t)) > 0) {
        switch (message.msgtype) {
            case DONATE:
                if (message.msgdata.donation.charity < 0 || message.msgdata.donation.charity >= 5) {
                    // Invalid charity index, send error message
                    message.msgtype = ERROR;
                    pthread_mutex_lock(&log_file_lock);
                    fprintf(log_file, "%d ERROR\n", client_fd);
                    fflush(log_file);
                    pthread_mutex_unlock(&log_file_lock);
                    write(client_fd, &message, sizeof(message_t));
                    continue;
                }

                pthread_mutex_lock(&charity_locks[message.msgdata.donation.charity]);
                charities[message.msgdata.donation.charity].totalDonationAmt += message.msgdata.donation.amount;
                if (message.msgdata.donation.amount > charities[message.msgdata.donation.charity].topDonation) {
                    charities[message.msgdata.donation.charity].topDonation = message.msgdata.donation.amount;
                }
                charities[message.msgdata.donation.charity].numDonations++;
                pthread_mutex_unlock(&charity_locks[message.msgdata.donation.charity]);

                client_total_donations += message.msgdata.donation.amount;

                pthread_mutex_lock(&log_file_lock);
                fprintf(log_file, "%d DONATE %d %lu\n", client_fd, message.msgdata.donation.charity, message.msgdata.donation.amount);
                fflush(log_file);
                pthread_mutex_unlock(&log_file_lock);

                write(client_fd, &message, sizeof(message_t));
                break;

            case CINFO:
                if (message.msgdata.donation.charity < 0 || message.msgdata.donation.charity >= 5) {
                    // Invalid charity index, send error message
                    message.msgtype = ERROR;
                    pthread_mutex_lock(&log_file_lock);
                    fprintf(log_file, "%d ERROR\n", client_fd);
                    fflush(log_file);
                    pthread_mutex_unlock(&log_file_lock);
                    write(client_fd, &message, sizeof(message_t));
                    continue;
                }

                pthread_mutex_lock(&charity_locks[message.msgdata.donation.charity]);
                message.msgdata.charityInfo = charities[message.msgdata.donation.charity];
                pthread_mutex_unlock(&charity_locks[message.msgdata.donation.charity]);

                pthread_mutex_lock(&log_file_lock);
                fprintf(log_file, "%d CINFO %d\n", client_fd, message.msgdata.donation.charity);
                fflush(log_file);
                pthread_mutex_unlock(&log_file_lock);

                write(client_fd, &message, sizeof(message_t));
                break;

            case TOP:
                pthread_mutex_lock(&server_stats_lock);
                for (int i = 0; i < 3; i++) {
                    message.msgdata.maxDonations[i] = maxDonations[i];
                }
                pthread_mutex_unlock(&server_stats_lock);

                pthread_mutex_lock(&log_file_lock);
                fprintf(log_file, "%d TOP\n", client_fd);
                fflush(log_file);
                pthread_mutex_unlock(&log_file_lock);

                write(client_fd, &message, sizeof(message_t));
                break;

            case LOGOUT:
                pthread_mutex_lock(&log_file_lock);
                fprintf(log_file, "%d LOGOUT\n", client_fd);
                fflush(log_file);
                pthread_mutex_unlock(&log_file_lock);

                // Update maxDonations if necessary
                pthread_mutex_lock(&server_stats_lock);
                for (int i = 0; i < 3; i++) {
                    if (client_total_donations > maxDonations[i]) {
                        for (int j = 2; j > i; j--) {
                            maxDonations[j] = maxDonations[j-1];
                        }
                        maxDonations[i] = client_total_donations;
                        break;
                    }
                }
                pthread_mutex_unlock(&server_stats_lock);

                close(client_fd);
                return NULL;

            default:
                pthread_mutex_lock(&log_file_lock);
                fprintf(log_file, "%d ERROR\n", client_fd);
                fflush(log_file);
                pthread_mutex_unlock(&log_file_lock);

                message.msgtype = ERROR;
                write(client_fd, &message, sizeof(message_t));
                break;
        }
    }

    close(client_fd);
    return NULL;
}

void cleanup_threads() {
    pthread_mutex_lock(&thread_list_lock);
    thread_node_t *current = thread_list_head;
    thread_node_t *prev = NULL;

    while (current != NULL) {
        if (pthread_tryjoin_np(current->thread_id, NULL) == 0) {
            // Thread has terminated, remove from the list
            if (prev == NULL) {
                thread_list_head = current->next;
            } else {
                prev->next = current->next;
            }
            thread_node_t *temp = current;
            current = current->next;
            free(temp);
        } else {
            // Thread is still running, move to the next
            prev = current;
            current = current->next;
        }
    }

    pthread_mutex_unlock(&thread_list_lock);
}
