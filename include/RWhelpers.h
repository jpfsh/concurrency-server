#ifndef HELPERS_H
#define HELPERS_H

#define write_log(fmt, ...)                    \
pthread_mutex_lock(&log_file_lock);            \
fprintf(log_file, fmt, __VA_ARGS__);           \
pthread_mutex_unlock(&log_file_lock);

extern pthread_mutex_t client_cnt_lock;
extern pthread_mutex_t max_donations_lock;
extern pthread_mutex_t readers_lock;
extern pthread_mutex_t writers_lock;
extern int readcnt;
extern pthread_mutex_t log_file_lock;
extern FILE *log_file;
extern volatile sig_atomic_t sigint;

extern int writer_fd;
extern pthread_t writer_tid;

// Global variables, statistics collected since server start-up
extern int clientCnt;  // # of client connections made, Updated by the main thread
extern uint64_t maxDonations[3];  // 3 highest total donations amounts (sum of all donations to all  
                           // charities in one connection), updated by client threads
                           // index 0 is the highest total donation
extern charity_t charities[5]; // Global variable, one charity per index

void init_server(const char* log_filenam);
void cleanup_server();
void reader_lock();
void reader_unlock();
void writer_lock();
void writer_unlock();
void *handle_writer(void *ptr);
void *handle_reader(void *ptr);
void print_stats();
void sigint_handler(int sig);

#endif
