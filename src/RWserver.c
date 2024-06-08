#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>
#include <semaphore.h>

#define MAX_CLIENTS 100
#define MAX_CHARITIES 10
#define MAX_DONATIONS 3
#define BUFFER_SIZE 256
#define RESPONSE_SIZE 512 // Increased buffer size for responses
#define SBUF_SIZE 16

typedef struct {
    int charityId;
    char charityName[BUFFER_SIZE];
    int totalDonations;
} Charity;

typedef struct {
    int clientCnt;
    int maxDonations[MAX_DONATIONS];
} ServerStats;

typedef struct dlist {
    struct dlist *prev;
    struct dlist *next;
    void *data;
} dlist_t;

// sbuf structure
typedef struct {
    int *buf;      // Buffer array
    int n;         // Maximum number of slots
    int front;     // buf[(front+1)%n] is the first item
    int rear;      // buf[rear%n] is the last item
    sem_t slots;   // Counts available slots
    sem_t items;   // Counts available items
    pthread_mutex_t mutex; // Protects accesses to buf
} sbuf_t;

// Global variables
Charity charities[MAX_CHARITIES];
ServerStats stats;
pthread_mutex_t statsMutex;
pthread_mutex_t logMutex;
sem_t readerSem;
sem_t writerSem;
int readerCount = 0;
int writerCount = 0;
int serverRunning = 1;
FILE *logFile;
dlist_t *clientList;
sbuf_t sbuf;

// Function declarations
void sbuf_init(sbuf_t *sp, int n);
void sbuf_deinit(sbuf_t *sp);
void sbuf_insert(sbuf_t *sp, int item);
int sbuf_remove(sbuf_t *sp);
void *reader_thread(void *arg);
void *writer_thread(void *arg);
void handle_signal(int sig);
void init_server(int readerPort, int writerPort, const char *logFilename);
void cleanup_server();
void process_donate(int clientSocket, const char *buffer);
void process_cinfo(int clientSocket, const char *buffer);
void process_top(int clientSocket);
void process_stats(int clientSocket);
void log_message(const char *message);

int main(int argc, char *argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <READER_PORT> <WRITER_PORT> <LOG_FILENAME>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int readerPort = atoi(argv[1]);
    int writerPort = atoi(argv[2]);
    const char *logFilename = argv[3];

    init_server(readerPort, writerPort, logFilename);
    sbuf_init(&sbuf, SBUF_SIZE);

    pthread_t writerThread;
    if (pthread_create(&writerThread, NULL, writer_thread, (void *)(intptr_t)writerPort) != 0) {
        perror("Failed to create writer thread");
        cleanup_server();
        sbuf_deinit(&sbuf);
        exit(EXIT_FAILURE);
    }

    int serverSocket;
    struct sockaddr_in serverAddr;
    serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket < 0) {
        perror("Failed to create socket");
        cleanup_server();
        sbuf_deinit(&sbuf);
        exit(EXIT_FAILURE);
    }

    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(readerPort);
    serverAddr.sin_addr.s_addr = INADDR_ANY;

    if (bind(serverSocket, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0) {
        perror("Failed to bind socket");
        close(serverSocket);
        cleanup_server();
        sbuf_deinit(&sbuf);
        exit(EXIT_FAILURE);
    }

    if (listen(serverSocket, MAX_CLIENTS) < 0) {
        perror("Failed to listen on socket");
        close(serverSocket);
        cleanup_server();
        sbuf_deinit(&sbuf);
        exit(EXIT_FAILURE);
    }

    printf("Listening for readers on port %d.\n", readerPort);

    while (serverRunning) {
        int clientSocket = accept(serverSocket, NULL, NULL);
        if (clientSocket < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("Failed to accept connection");
            break;
        }

        sbuf_insert(&sbuf, clientSocket);

        pthread_t readerThread;
        if (pthread_create(&readerThread, NULL, reader_thread, (void *)(intptr_t)clientSocket) != 0) {
            perror("Failed to create reader thread");
            close(clientSocket);
            continue;
        }

        pthread_mutex_lock(&statsMutex);
        stats.clientCnt++;
        pthread_mutex_unlock(&statsMutex);
    }

    close(serverSocket);
    pthread_join(writerThread, NULL);
    cleanup_server();
    sbuf_deinit(&sbuf);
    return 0;
}

// sbuf functions
void sbuf_init(sbuf_t *sp, int n) {
    sp->buf = calloc(n, sizeof(int));
    sp->n = n;
    sp->front = sp->rear = 0;
    sem_init(&sp->slots, 0, n);
    sem_init(&sp->items, 0, 0);
    pthread_mutex_init(&sp->mutex, NULL);
}

void sbuf_deinit(sbuf_t *sp) {
    free(sp->buf);
    pthread_mutex_destroy(&sp->mutex);
    sem_destroy(&sp->slots);
    sem_destroy(&sp->items);
}

void sbuf_insert(sbuf_t *sp, int item) {
    sem_wait(&sp->slots);
    pthread_mutex_lock(&sp->mutex);
    sp->buf[(++sp->rear) % sp->n] = item;
    pthread_mutex_unlock(&sp->mutex);
    sem_post(&sp->items);
}

int sbuf_remove(sbuf_t *sp) {
    int item;
    sem_wait(&sp->items);
    pthread_mutex_lock(&sp->mutex);
    item = sp->buf[(++sp->front) % sp->n];
    pthread_mutex_unlock(&sp->mutex);
    sem_post(&sp->slots);
    return item;
}

void *reader_thread(void *arg) {
    int clientSocket = (intptr_t)arg;
    char buffer[BUFFER_SIZE];

    while (1) {
        ssize_t bytesRead = read(clientSocket, buffer, sizeof(buffer) - 1);
        if (bytesRead <= 0) {
            if (bytesRead < 0) {
                perror("Failed to read from client socket");
            }
            break;
        }
        buffer[bytesRead] = '\0';

        sem_wait(&readerSem);
        readerCount++;
        if (readerCount == 1) {
            sem_wait(&writerSem);
        }
        sem_post(&readerSem);

        if (strncmp(buffer, "CINFO", 5) == 0) {
            process_cinfo(clientSocket, buffer);
        } else if (strncmp(buffer, "TOP", 3) == 0) {
            process_top(clientSocket);
        } else if (strncmp(buffer, "STATS", 5) == 0) {
            process_stats(clientSocket);
        } else {
            const char *errorMsg = "ERROR\n";
            write(clientSocket, errorMsg, strlen(errorMsg));
            log_message("ERROR\n");
        }

        sem_wait(&readerSem);
        readerCount--;
        if (readerCount == 0) {
            sem_post(&writerSem);
        }
        sem_post(&readerSem);
    }

    close(clientSocket);
    pthread_mutex_lock(&statsMutex);
    stats.clientCnt--;
    pthread_mutex_unlock(&statsMutex);
    return NULL;
}

void *writer_thread(void *arg) {
    int writerPort = (intptr_t)arg;
    int serverSocket;
    struct sockaddr_in serverAddr;
    serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket < 0) {
        perror("Failed to create socket");
        return NULL;
    }

    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(writerPort);
    serverAddr.sin_addr.s_addr = INADDR_ANY;

    if (bind(serverSocket, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0) {
        perror("Failed to bind socket");
        close(serverSocket);
        return NULL;
    }

    if (listen(serverSocket, 1) < 0) {
        perror("Failed to listen on socket");
        close(serverSocket);
        return NULL;
    }

    printf("Listening for writers on port %d.\n", writerPort);

    while (serverRunning) {
        int clientSocket = accept(serverSocket, NULL, NULL);
        if (clientSocket < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("Failed to accept connection");
            break;
        }

        while (serverRunning) {
            char buffer[BUFFER_SIZE];
            ssize_t bytesRead = read(clientSocket, buffer, sizeof(buffer) - 1);
            if (bytesRead <= 0) {
                if (bytesRead < 0) {
                    perror("Failed to read from client socket");
                }
                break;
            }
            buffer[bytesRead] = '\0';

            if (strncmp(buffer, "DONATE", 6) == 0) {
                process_donate(clientSocket, buffer);
            } else if (strncmp(buffer, "LOGOUT", 6) == 0) {
                const char *logoutMsg = "LOGOUT\n";
                write(clientSocket, logoutMsg, strlen(logoutMsg));
                log_message("LOGOUT\n");
                break;
            } else {
                const char *errorMsg = "ERROR\n";
                write(clientSocket, errorMsg, strlen(errorMsg));
                log_message("ERROR\n");
            }
        }

        close(clientSocket);
    }

    close(serverSocket);
    return NULL;
}

void handle_signal(int sig) {
    if (sig == SIGINT) {
        serverRunning = 0;
    }
}

void init_server(int readerPort, int writerPort, const char *logFilename) {
    memset(&stats, 0, sizeof(stats));
    memset(charities, 0, sizeof(charities));

    if ((logFile = fopen(logFilename, "a")) == NULL) {
        perror("Failed to open log file");
        exit(EXIT_FAILURE);
    }

    pthread_mutex_init(&statsMutex, NULL);
    pthread_mutex_init(&logMutex, NULL);
    sem_init(&readerSem, 0, 1);
    sem_init(&writerSem, 0, 1);

    struct sigaction sa;
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGINT, &sa, NULL) == -1) {
        perror("Failed to install SIGINT handler");
        exit(EXIT_FAILURE);
    }
}

void cleanup_server() {
    fclose(logFile);
    pthread_mutex_destroy(&statsMutex);
    pthread_mutex_destroy(&logMutex);
    sem_destroy(&readerSem);
    sem_destroy(&writerSem);
}

void process_donate(int clientSocket, const char *buffer) {
    sem_wait(&writerSem);
    // Parse the donation information from buffer and update the charity
    // Example: "DONATE <charityId> <amount>\n"
    int charityId, amount;
    sscanf(buffer, "DONATE %d %d", &charityId, &amount);
    if (charityId >= 0 && charityId < MAX_CHARITIES) {
        charities[charityId].totalDonations += amount;
        char logEntry[RESPONSE_SIZE];
        snprintf(logEntry, sizeof(logEntry), "DONATE %d %d\n", charityId, amount);
        log_message(logEntry);
    }
    sem_post(&writerSem);

    write(clientSocket, buffer, strlen(buffer)); // Echo back the donation message
}

void process_cinfo(int clientSocket, const char *buffer) {
    // Example: "CINFO <charityId>\n"
    int charityId;
    sscanf(buffer, "CINFO %d", &charityId);
    if (charityId >= 0 && charityId < MAX_CHARITIES) {
        char response[RESPONSE_SIZE];
        snprintf(response, sizeof(response), "CINFO %d %s %d\n", charityId,
                 charities[charityId].charityName, charities[charityId].totalDonations);
        write(clientSocket, response, strlen(response));
        log_message(buffer);
    } else {
        const char *errorMsg = "ERROR\n";
        write(clientSocket, errorMsg, strlen(errorMsg));
        log_message("ERROR\n");
    }
}

void process_top(int clientSocket) {
    char response[RESPONSE_SIZE] = "TOP\n";
    write(clientSocket, response, strlen(response));
    log_message("TOP\n");
}

void process_stats(int clientSocket) {
    char response[RESPONSE_SIZE] = "STATS\n";
    write(clientSocket, response, strlen(response));
    log_message("STATS\n");
}

void log_message(const char *message) {
    pthread_mutex_lock(&logMutex);
    fprintf(logFile, "%s", message);
    fflush(logFile);
    pthread_mutex_unlock(&logMutex);
}
