#define _GNU_SOURCE
#include "server.h"
#include "protocol.h"
#include <pthread.h>
#include <signal.h>

#include "MThelpers.h"

void emptyDeleter() {}

void sigint_handler(int sig) {
    sigint = 1;
}

// void init_server(const char* log_filename, dlist_t* list) {
void init_server(const char* log_filename) {
    clientCnt = 0;
    for (int i = 0; i < 3; i++) {
        maxDonations[i] = 0;
    }
    pthread_mutex_init(&mt_stats_lock, NULL);

    for (int i = 0; i < 5; i++) {
        charities[i].numDonations = 0;
        charities[i].totalDonationAmt = 0;
        charities[i].topDonation = 0;
        if (pthread_mutex_init(&charity_locks[i], NULL) != 0) {
            perror("mutex init err");
            exit(EXIT_FAILURE);
        }
    }

    log_file = fopen(log_filename, "w");
    if (log_file == NULL) {
        perror("log fopen err");
        exit(EXIT_FAILURE);
    }
    pthread_mutex_init(&log_file_lock, NULL);

    list = CreateList(NULL, NULL, emptyDeleter);
}

void cleanup_server() {
    fclose(log_file);
    pthread_mutex_destroy(&mt_stats_lock);
    for (int i = 0; i < 5; i++) {
        pthread_mutex_destroy(&charity_locks[i]);
    }
    pthread_mutex_destroy(&log_file_lock);
}

// void remove_joinable_threads(dlist_t* list) {
void remove_joinable_threads() {
    node_t* walker = list->head;
    while (walker) {
        pthread_t tid = *(pthread_t*) walker->data;
        if (pthread_tryjoin_np(tid, NULL) == 0) {
            node_t* temp = walker;
            walker = walker->next;
            if (temp->prev) {
                temp->prev->next = temp->next;
            }
            if (temp->next) {
                temp->next->prev = temp->prev;
            }
            if (list->head == temp) {
                list->head = temp->next;
            }
            list->length--;
            free(temp->data);
            free(temp);
        } else {
            walker = walker->next;
        }
    }
}

// void kill_and_join_all_threads(dlist_t* list) {
void kill_and_join_all_threads() {
    node_t* walker = list->head;
    while (walker) {
        pthread_t tid = *(pthread_t*) walker->data;
        pthread_kill(tid, SIGINT);
        pthread_join(tid, NULL);
        walker = walker->next;
    }
}

void print_stats() {
	for (int i = 0; i < 5; i++) {
        printf("%d, %u, %lu, %lu\n", i, charities[i].numDonations, charities[i].topDonation, charities[i].totalDonationAmt);
    }
    fprintf(stderr, "%d\n%lu, %lu, %lu\n", clientCnt, maxDonations[0], maxDonations[1], maxDonations[2]);
}

// 
void* client_handler(void* client) {
    int client_fd = *((int*) client);
    free(client);

    bool error = false;
    message_t message;
    uint64_t donation_total = 0;

    while (read(client_fd, &message, sizeof(message_t)) > 0) {
    	int which_charity = message.msgdata.donation.charity;
        switch (message.msgtype) {
            case DONATE:
                if (which_charity >= 5) {
                    message.msgtype = ERROR;
                    error = true;
                    write(client_fd, &message, sizeof(message_t));
                } else {
	                pthread_mutex_lock(&charity_locks[which_charity]);

	                printf("donate: locked %d\n", which_charity);
	                uint64_t amt = message.msgdata.donation.amount;
	                charities[which_charity].numDonations++;
	                charities[which_charity].totalDonationAmt += amt;
	                if (amt > charities[which_charity].topDonation) {
	                    charities[which_charity].topDonation = amt;
	                }

	                pthread_mutex_unlock(&charity_locks[which_charity]);
					printf("donate: unlocked %d\n", which_charity);

	                donation_total += amt;

	                pthread_mutex_lock(&log_file_lock);
	                fprintf(log_file, "%d DONATE %d %lu\n", client_fd, which_charity, amt);
	                pthread_mutex_unlock(&log_file_lock);

	                write(client_fd, &message, sizeof(message_t));
                }
                break;
            case CINFO:
                if (which_charity >= 5) {
                    message.msgtype = ERROR;
                    error = true;
                    write(client_fd, &message, sizeof(message_t));
                } else {
	                pthread_mutex_lock(&charity_locks[which_charity]);
	                printf("cinfo: locked %d\n", which_charity);

	                memcpy(&message.msgdata.charityInfo, &charities[which_charity], sizeof(charity_t));

	                pthread_mutex_unlock(&charity_locks[which_charity]);
	                printf("cinfo: unlocked locked %d\n", which_charity);

	                pthread_mutex_lock(&log_file_lock);
	                fprintf(log_file, "%d CINFO %d\n", client_fd, which_charity);
	                pthread_mutex_unlock(&log_file_lock);

	                write(client_fd, &message, sizeof(message_t));
                }
                break;
            case TOP:
                pthread_mutex_lock(&mt_stats_lock);
                printf("locked top\n");
                for (int i = 0; i < 3; i++) {
                    message.msgdata.maxDonations[i] = maxDonations[i];
                }
                pthread_mutex_unlock(&mt_stats_lock);
                printf("unlocked top\n");

                write_log("%d TOP\n", client_fd);

                write(client_fd, &message, sizeof(message_t));
                break;

            case LOGOUT:
            	write_log("%d LOGOUT\n", client_fd);

                pthread_mutex_lock(&mt_stats_lock);
                for (int i = 0; i < 3; i++) {
                    if (donation_total > maxDonations[i]) {
                        for (int j = 2; j > i; j--) {
                            maxDonations[j] = maxDonations[j - 1];
                        }
                        maxDonations[i] = donation_total;
                        break;
                    }
                }
                pthread_mutex_unlock(&mt_stats_lock);

                close(client_fd);
                return NULL;

            default:
            	error = true;
                break;
        }
        if (error) {
    	    write_log("%d ERROR\n", client_fd);

            message.msgtype = ERROR;
            write(client_fd, &message, sizeof(message_t));
        }
    }

    close(client_fd);
    return NULL;
}