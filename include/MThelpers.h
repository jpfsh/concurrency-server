#ifndef HELPERS_H
#define HELPERS_H

#include "dlinkedlist.h"
// 

extern pthread_mutex_t mt_stats_lock;
extern pthread_mutex_t log_file_lock;
extern pthread_mutex_t charity_locks[5];

extern dlist_t* list;
extern FILE* log_file;
extern volatile sig_atomic_t sigint_flag;

// Global variables, statistics collected since server start-up
extern int clientCnt;  // # of client connections made, Updated by the main thread
extern uint64_t maxDonations[3];  // 3 highest total donations amounts (sum of all donations to all
                           // charities in one connection), updated by client threads
                           // index 0 is the highest total donation
extern charity_t charities[5]; // Global variable, one charity per index
// 

void emptyDeleter();

void cleanup_server();
// void init_server(const char* log_filename, dlist_t* list);
void init_server(const char* log_filename);

void* client_handler(void* client);
void print_stats();
void sigint_handler(int sig);

// discussion:
// remove_joinable_threads(): Remove all joinable threads from your thread list. See next slide for example using pthread_tryjoin_np.
// kill_and_join_all_threads(): Sends kill to all threads running and then calls pthread_join() on them.
// All your typical linked list functions. Single or double is fine, but double might make removing slightly easier in your remove_joinable_threads() function.
// void remove_joinable_threads(dlist_t* list);
void remove_joinable_threads();
// void kill_and_join_all_threads(dlist_t* list);
void kill_and_join_all_threads();

#define write_log(fmt, ...)			\
pthread_mutex_lock(log_file_mutex);	\
fprintf(log_file, fmt, __VA_ARGS__);	\
pthread_mutex_unlock(log_file_mutex);

#endif