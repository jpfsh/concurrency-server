#define _GNU_SOURCE
#include "server.h"
#include "protocol.h"
#include <pthread.h>
#include <signal.h>

#include "MThelpers.h"

void emptyDeleter() {}

void sigint_handler(int sig) {
    sigint_flag = 1;
}

// void init_server(const char* log_filename, dlist_t* list) {
void init_server(const char* log_filename) {
    clientCnt = 0;
    for (int i = 0; i < MAX_DONATIONS; i++) {
        maxDonations[i] = 0;
    }
    pthread_mutex_init(&mt_stats_lock, NULL);

    for (int i = 0; i < CHARITY_COUNT; i++) {
        charities[i].numDonations = 0;
        charities[i].totalDonationAmt = 0;
        charities[i].topDonation = 0;
        if (pthread_mutex_init(&charity_locks[i], NULL) != 0) {
            perror("Mutex initialization failed");
            exit(EXIT_FAILURE);
        }
    }

    log_file = fopen(log_filename, "w");
    if (log_file == NULL) {
        perror("Log file opening failed");
        exit(EXIT_FAILURE);
    }
    pthread_mutex_init(&log_file_lock, NULL);

    list = CreateList(NULL, NULL, emptyDeleter);
}

void cleanup_server() {
    fclose(log_file);
    pthread_mutex_destroy(&mt_stats_lock);
    for (int i = 0; i < CHARITY_COUNT; i++) {
        pthread_mutex_destroy(&charity_locks[i]);
    }
    pthread_mutex_destroy(&log_file_lock);
}

// void remove_joinable_threads(dlist_t* list) {
void remove_joinable_threads() {
    node_t* current = list->head;
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
            if (list->head == to_delete) {
                list->head = to_delete->next;
            }
            list->length--;
            free(to_delete->data);
            free(to_delete);
        } else {
            current = current->next;
        }
    }
}

// void kill_and_join_all_threads(dlist_t* list) {
void kill_and_join_all_threads() {
    node_t* current = list->head;
    while (current) {
        pthread_t tid = *(pthread_t*)current->data;
        pthread_kill(tid, SIGINT);
        pthread_join(tid, NULL);
        current = current->next;
    }
}

void print_stats() {
	for (int i = 0; i < CHARITY_COUNT; i++) {
        printf("%d, %u, %lu, %lu\n", i, charities[i].numDonations, charities[i].topDonation, charities[i].totalDonationAmt);
    }
    fprintf(stderr, "%d\n%lu, %lu, %lu\n", clientCnt, maxDonations[0], maxDonations[1], maxDonations[2]);
}

// 
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
                pthread_mutex_lock(&mt_stats_lock);
                for (int i = 0; i < MAX_DONATIONS; i++) {
                    message.msgdata.maxDonations[i] = maxDonations[i];
                }
                pthread_mutex_unlock(&mt_stats_lock);

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
                pthread_mutex_lock(&mt_stats_lock);
                for (int i = 0; i < MAX_DONATIONS; i++) {
                    if (client_total_donations > maxDonations[i]) {
                        for (int j = MAX_DONATIONS - 1; j > i; j--) {
                            maxDonations[j] = maxDonations[j - 1];
                        }
                        maxDonations[i] = client_total_donations;
                        break;
                    }
                }
                pthread_mutex_unlock(&mt_stats_lock);

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