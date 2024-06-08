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

// #define write_log (Emt,
// ... )
// pthread mutex
// lock (log_file_
// mutex) ;
// fprintf(log_file, fmt,
// VA ARGS
// pthread
// mutex unlock (10g_file_mutex) ;
// write_log(“%d LOGOUT\n”, client_fd);

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
#define USAGE_MSG_MT "ZotDonation_MTserver [-h] PORT_NUMBER LOG_FILENAME"

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
pthread_mutex_t server_stats_lock = PTHREAD_MUTEX_INITIALIZER;

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

// Global variables for other components
charity_t charities[CHARITY_COUNT];
pthread_mutex_t log_file_lock = PTHREAD_MUTEX_INITIALIZER;
dlist_t* thread_list;
FILE* log_file;
volatile sig_atomic_t sigint_flag = 0;
int server_fd;

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
    close(server_fd);
    exit(0);
}

// Initialize server resources
void init_server_resources(const char* log_filename) {
    // Initialize server statistics
    clientCnt = 0;
    for (int i = 0; i < MAX_DONATIONS; i++) {
        maxDonations[i] = 0;
    }
    pthread_mutex_init(&server_stats_lock, NULL);

    // Initialize charities and their locks
    for (int i = 0; i < CHARITY_COUNT; i++) {
        charities[i].numDonations = 0;
        charities[i].totalDonationAmt = 0;
        charities[i].topDonation = 0;
        if (pthread_mutex_init(&charity_locks[i], NULL) != 0) {
            perror("Mutex initialization failed");
            exit(EXIT_FAILURE);
        }
    }

    // Initialize log file
    log_file = fopen(log_filename, "a");
    if (log_file == NULL) {
        perror("Log file opening failed");
        exit(EXIT_FAILURE);
    }
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

void* client_thread(void* arg) {
    int client_fd = *(int*)arg;
    free(arg);

    message_t message;
    uint64_t client_total_donations = 0;

    while (1) {
        // Read message type
        ssize_t bytes_read = read(client_fd, &message, sizeof(message_t));
        if (bytes_read <= 0) {
            if (bytes_read < 0) {
                perror("read");
            }
            break; // Client disconnected or error occurred
        }

        // Ensure full message is read
        size_t total_bytes_read = bytes_read;
        while (total_bytes_read < sizeof(message_t)) {
            bytes_read = read(client_fd, ((char*)&message) + total_bytes_read, sizeof(message_t) - total_bytes_read);
            if (bytes_read <= 0) {
                if (bytes_read < 0) {
                    perror("read");
                }
                break; // Client disconnected or error occurred
            }
            total_bytes_read += bytes_read;
        }

        if (total_bytes_read < sizeof(message_t)) {
            // Incomplete message read, handle error
            perror("Incomplete message read");
            break;
        }

        // Log received message
        printf("Received message type: %d\n", message.msgtype);

        switch (message.msgtype) {
            case DONATE:
                if (message.msgdata.donation.charity >= CHARITY_COUNT) {
                    message.msgtype = ERROR;
                    write(client_fd, &message, sizeof(message_t));
                    continue;
                }

                printf("Handling DONATE message\n");
                int charity_id_donate = message.msgdata.donation.charity;
                pthread_mutex_lock(&charity_locks[charity_id_donate]);
                printf("Locked charity_locks[%d] for DONATE\n", charity_id_donate);

                charities[charity_id_donate].numDonations++;
                charities[charity_id_donate].totalDonationAmt += message.msgdata.donation.amount;
                if (message.msgdata.donation.amount > charities[charity_id_donate].topDonation) {
                    charities[charity_id_donate].topDonation = message.msgdata.donation.amount;
                }

                pthread_mutex_unlock(&charity_locks[charity_id_donate]);
                printf("Unlocked charity_locks[%d] for DONATE\n", charity_id_donate);

                client_total_donations += message.msgdata.donation.amount;

                pthread_mutex_lock(&log_file_lock);
                fprintf(log_file, "%d DONATE %d %lu\n", client_fd, charity_id_donate, message.msgdata.donation.amount);
                fflush(log_file);
                pthread_mutex_unlock(&log_file_lock);

                write(client_fd, &message, sizeof(message_t));
                break;

            case CINFO:
                if (message.msgdata.donation.charity >= CHARITY_COUNT) {
                    message.msgtype = ERROR;
                    write(client_fd, &message, sizeof(message_t));
                    continue;
                }

                printf("Handling CINFO message\n");
                int charity_id_cinfo = message.msgdata.donation.charity;
                pthread_mutex_lock(&charity_locks[charity_id_cinfo]);
                printf("Locked charity_locks[%d] for CINFO\n", charity_id_cinfo);

                memcpy(&message.msgdata.charityInfo, &charities[charity_id_cinfo], sizeof(charity_t));

                pthread_mutex_unlock(&charity_locks[charity_id_cinfo]);
                printf("Unlocked charity_locks[%d] for CINFO\n", charity_id_cinfo);

                pthread_mutex_lock(&log_file_lock);
                fprintf(log_file, "%d CINFO %d\n", client_fd, charity_id_cinfo);
                fflush(log_file);
                pthread_mutex_unlock(&log_file_lock);

                printf("Sending CINFO response: numDonations=%u, totalDonationAmt=%lu, topDonation=%lu\n",
                       message.msgdata.charityInfo.numDonations,
                       message.msgdata.charityInfo.totalDonationAmt,
                       message.msgdata.charityInfo.topDonation);

                write(client_fd, &message, sizeof(message_t));
                break;

            case TOP:
                printf("Handling TOP message\n");
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
                printf("Handling LOGOUT message\n");
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

            default:
                printf("Handling ERROR message\n");
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

void print_stats() {
    // Lock the statistics mutex to ensure thread safety
    pthread_mutex_lock(&server_stats_lock);

    // Print charity stats to stdout
    for (int i = 0; i < CHARITY_COUNT; i++) {
        printf("%d, %d, %lu, %lu\n", i, charities[i].numDonations, charities[i].topDonation, charities[i].totalDonationAmt);
    }

    // Print server statistics to stderr
    fprintf(stderr, "%d\n%lu, %lu, %lu\n", clientCnt, maxDonations[0], maxDonations[1], maxDonations[2]);

    // Unlock the statistics mutex
    pthread_mutex_unlock(&server_stats_lock);
}


int main(int argc, char* argv[]) {
    // Parse command line arguments
    int port;
    char* log_filename;
    if (argc != 3) {
        fprintf(stderr, "Usage: %s PORT_NUMBER LOG_FILENAME\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    port = atoi(argv[1]);
    log_filename = argv[2];

    // Initialize server resources
    init_server_resources(log_filename);

    // Set up signal handler for SIGINT
    struct sigaction myaction = {{0}};
    myaction.sa_handler = sigint_handler;
    if (sigaction(SIGINT, &myaction, NULL) == -1) {
        perror("signal handler failed to install");
        exit(EXIT_FAILURE);
    }

    // Create and bind server socket
    struct sockaddr_in server_addr;
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        perror("bind");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 10) == -1) {
        perror("listen");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    while (!sigint_flag) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd == -1) {
            if (errno == EINTR && sigint_flag) {
                break;
            } else {
                perror("accept");
                continue;
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
    close(server_fd);
    node_t* current = thread_list->head;
    while (current) {
        pthread_t tid = *(pthread_t*)current->data;
        pthread_kill(tid, SIGINT);
        pthread_join(tid, NULL);
        current = current->next;
    }

    // Output charity and server statistics
    for (int i = 0; i < CHARITY_COUNT; i++) {
        printf("%d, %u, %lu, %lu\n", i, charities[i].numDonations, charities[i].topDonation, charities[i].totalDonationAmt);
    }
    fprintf(stderr, "%d\n%lu, %lu, %lu\n", clientCnt, maxDonations[0], maxDonations[1], maxDonations[2]);

    // Clean up server resources
    cleanup_server_resources();

    return 0;
}
