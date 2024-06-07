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

pthread_mutex_t server_stats_lock;
pthread_mutex_t log_file_lock;
pthread_mutex_t charity_locks[5];

#define SA struct sockaddr
#define USAGE_MSG_MT "ZotDonation_MTserver [-h] PORT_NUMBER LOG_FILENAME"

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

// Global variables for server statistics
int clientCnt = 0;
uint64_t maxDonations[MAX_DONATIONS] = {0};

// Charity structure
typedef struct {
    int numDonations;
    uint64_t totalDonationAmt;
    uint64_t topDonation;
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

// Global variables for other components
charity_t charities[CHARITY_COUNT];
dlist_t* thread_list;
FILE* log_file;
volatile sig_atomic_t sigint_flag = 0;
// int server_fd;
int listen_fd;

// Function definitions
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

// Signal handler
void sigint_handler(int signum) {
    sigint_flag = 1;
}

// Initialize server resources
void init_server_resources(const char* log_filename) {
    // Initialize server statistics
    clientCnt = 0;
    for (int i = 0; i < MAX_DONATIONS; i++) {
        maxDonations[i] = 0;
    }
    pthread_mutex_init(&server_stats_lock, NULL);

    // Initialize charities
    for (int i = 0; i < CHARITY_COUNT; i++) {
        charities[i].numDonations = 0;
        charities[i].totalDonationAmt = 0;
        charities[i].topDonation = 0;
        pthread_mutex_init(&charity_locks[i], NULL);
    }

    // Initialize log file
    log_file = fopen(log_filename, "w");
    pthread_mutex_init(&log_file_lock, NULL);

    // Initialize thread list
    thread_list = CreateList(NULL, NULL, emptyDeleter);
}

// Cleanup server resources
void cleanup_server_resources() {
    // Close log file
    fclose(log_file);

    // Destroy mutexes
    pthread_mutex_destroy(&server_stats_lock);
    for (int i = 0; i < CHARITY_COUNT; i++) {
        pthread_mutex_destroy(&charity_locks[i]);
    }
    pthread_mutex_destroy(&log_file_lock);
}

// Thread function to handle client requests
void* client_thread(void* arg) {
    int client_fd = *(int*)arg;
    free(arg);

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
                for (int i = 0; i < MAX_DONATIONS; i++) {
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
                for (int i = 0; i < MAX_DONATIONS; i++) {
                    if (client_total_donations > maxDonations[i]) {
                        for (int j = MAX_DONATIONS - 1; j > i; j--) {
                            maxDonations[j] = maxDonations[j - 1];
                        }
                        maxDonations[i] = client_total_donations;
                        break;
                    }
                }
                pthread_mutex_unlock(&server_stats_lock);

                close(client_fd);
                return NULL;
            case STATS:
            {
                // Lock all charity locks to ensure consistent stats
                for (int i = 0; i < CHARITY_COUNT; i++) {
                    pthread_mutex_lock(&charity_locks[i]);
                }

                // Prepare the statistics message
                message_t response;
                response.msgtype = STATS;
                for (int i = 0; i < CHARITY_COUNT; i++) {
                    response.msgdata.stats.charityID_low = i;
                    response.msgdata.stats.amount_low = charities[i].totalDonationAmt;
                    // Send the stats for each charity
                    write(client_fd, &response, sizeof(response));
                    // Log the stats request
                    pthread_mutex_lock(&log_file_lock);
                    fprintf(log_file, "<%d> STATS %d:%llu\n", client_fd, i, (unsigned long long)charities[i].totalDonationAmt);
                    pthread_mutex_unlock(&log_file_lock);
                }

                // Unlock all charity locks
                for (int i = 0; i < CHARITY_COUNT; i++) {
                    pthread_mutex_unlock(&charity_locks[i]);
                }
            }
                break;
            case ERROR:
            {
                // Prepare the error message
                message_t response;
                response.msgtype = ERROR;
                write(client_fd, &response, sizeof(response));
                
                // Log the error
                pthread_mutex_lock(&log_file_lock);
                fprintf(log_file, "<%d> ERROR\n", client_fd);
                pthread_mutex_unlock(&log_file_lock);
            }
                break;
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

int main(int argc, char* argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s PORT_NUMBER LOG_FILENAME\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    // port = atoi(argv[1]);
    // log_filename = argv[2];
    int port_number = atoi(argv[1]);
    char *log_filename = argv[2];

    // Initialize server resources
    init_server_resources(log_filename);

    // Set up signal handler for SIGINT
    struct sigaction myaction = {{0}};
    myaction.sa_handler = sigint_handler;
    if (sigaction(SIGINT, &myaction, NULL) == -1) {
        perror("signal handler failed to install");
        exit(EXIT_FAILURE);
    }

    // Initiate server socket for listening
    listen_fd = socket_listen_init(port_number);
    printf("Currently listening on port: %d.\n", port_number);
    int client_fd;
    struct sockaddr_in client_addr;
    unsigned int client_addr_len = sizeof(client_addr);

    while (!sigint_flag) {
        // Wait and Accept the connection from client
        client_fd = accept(listen_fd, (SA*)&client_addr, &client_addr_len); 
        if (client_fd == -1) {
            if (errno == EINTR && sigint_flag) {
                break;
            }
        }

        // Clean up terminated threads
        node_t* current = thread_list->head;
        while (current) {
            pthread_t tid = *(pthread_t*)current->data;
            if (pthread_tryjoin_np(tid, NULL) == 0) {
                node_t* to_delete = current;
                current = current->next;
                if (to_delete->prev) {
                    to_delete->prev->next = to_delete->next;
                }
                if (to_delete->next) {
                    to_delete->next->prev = to_delete->prev;
                }
                if (thread_list->head == to_delete) {
                    thread_list->head = to_delete->next;
                }
                thread_list->length--;
                free(to_delete->data);
                free(to_delete);
            } else {
                current = current->next;
            }
        }

        // Increment client count
        pthread_mutex_lock(&server_stats_lock);
        clientCnt++;
        pthread_mutex_unlock(&server_stats_lock);

        // Create client thread
        pthread_t tid;
        int* client_fd_ptr = malloc(sizeof(int));
        *client_fd_ptr = client_fd;
        if (pthread_create(&tid, NULL, client_thread, client_fd_ptr) != 0) {
            perror("pthread_create");
            close(client_fd);
            free(client_fd_ptr);
            continue;
        }

        // Add thread to linked list
        pthread_t* tid_ptr = malloc(sizeof(pthread_t));
        *tid_ptr = tid;
        InsertAtHead(thread_list, tid_ptr);
    }

    // Clean up and terminate all client threads
    close(listen_fd);
    node_t* current = thread_list->head;
    while (current) {
        pthread_t tid = *(pthread_t*)current->data;
        pthread_kill(tid, SIGINT);
        pthread_join(tid, NULL);
        current = current->next;
    }

    // Output charity and server statistics
    for (int i = 0; i < CHARITY_COUNT; i++) {
        printf("%d, %d, %lu, %lu\n", i, charities[i].numDonations, charities[i].topDonation, charities[i].totalDonationAmt);
    }
    fprintf(stderr, "%d\n%lu, %lu, %lu\n", clientCnt, maxDonations[0], maxDonations[1], maxDonations[2]);

    // Clean up server resources
    cleanup_server_resources();

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
    if ((listen(sockfd, 1)) != 0) {
        printf("Listen failed\n");
        exit(EXIT_FAILURE);
    }
    return sockfd;
}

