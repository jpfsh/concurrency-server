#define _GNU_SOURCE
#include "server.h"
#include "protocol.h"
#include <pthread.h>
#include <signal.h>

#include "RWhelpers.h"
#include <stdbool.h>
#include <errno.h>

void init_server(const char* log_filename) {
    pthread_mutex_init(&client_cnt_lock, NULL);
    pthread_mutex_init(&max_donations_lock, NULL);
    for (int i = 0; i < 3; ++i) {
        maxDonations[i] = 0;
    }

    pthread_mutex_init(&readers_lock, NULL);
    pthread_mutex_init(&writers_lock, NULL);
    for (int i = 0; i < 5; ++i) {
        charities[i].totalDonationAmt = 0;
        charities[i].topDonation = 0;
        charities[i].numDonations = 0;
    }

    log_file = fopen(log_filename, "w");
    if (log_file == NULL) {
        printf("log fopen err\n");
        exit(EXIT_FAILURE);
    }
    pthread_mutex_init(&log_file_lock, NULL);
}

void cleanup_server() {
    fclose(log_file);
    pthread_mutex_destroy(&client_cnt_lock);
    pthread_mutex_destroy(&max_donations_lock);
    pthread_mutex_destroy(&readers_lock);
    pthread_mutex_destroy(&writers_lock);
    pthread_mutex_destroy(&log_file_lock);
}

// sync pt 2 slide 11
void reader_lock() {
    pthread_mutex_lock(&readers_lock);
    readcnt++;
    // specifically the FIRST one in, not just != 0
    if (readcnt == 1) {
        pthread_mutex_lock(&writers_lock);
    }
    pthread_mutex_unlock(&readers_lock);
}

void reader_unlock() {
    pthread_mutex_lock(&readers_lock);
    readcnt--;
    if (!readcnt) {
        pthread_mutex_unlock(&writers_lock);
    }
    pthread_mutex_unlock(&readers_lock);
}

void writer_lock() {
    pthread_mutex_lock(&writers_lock);
}

void writer_unlock() {
    pthread_mutex_unlock(&writers_lock);
}

void sigint_handler(int sig) {
    sigint = 1;
}

void print_stats() {
    for (int i = 0; i < 5; i++) {
        printf("%d, %u, %lu, %lu\n", i, charities[i].numDonations, charities[i].topDonation, charities[i].totalDonationAmt);
    }
    fprintf(stderr, "%d\n%lu, %lu, %lu\n", clientCnt, maxDonations[0], maxDonations[1], maxDonations[2]);
}

void *handle_writer(void *ptr) {
    int writer_listen_fd = *(int *) ptr;
    bool error = false;
    uint64_t donation_total = 0;
    message_t msg;
    bool update = false;

    int writer_fd;
    struct sockaddr_in client_addr;
    unsigned int client_addr_len = sizeof(client_addr);

    while (1) {
        writer_fd = accept(writer_listen_fd, (SA*)&client_addr, &client_addr_len);
        if (writer_fd < 0) {
            printf("writer acccept failed\n");
            // disc
            if (errno == EINTR) {
                if (sigint) {
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
            // endmodif
        }
        pthread_mutex_lock(&client_cnt_lock);
        clientCnt++;
        pthread_mutex_unlock(&client_cnt_lock);
        donation_total = 0; 
        
        while ((read(writer_fd, &msg, sizeof(msg))) > 0) {
        
            uint64_t which_charity = msg.msgdata.donation.charity;

            switch (msg.msgtype) {
                case DONATE:

                    if (which_charity >= 5) {
                        error = true;
                    } else {
                        writer_lock();
                        charities[which_charity].totalDonationAmt += msg.msgdata.donation.amount;
                        charities[which_charity].numDonations++;
                        if (msg.msgdata.donation.amount > charities[which_charity].topDonation) {
                            charities[which_charity].topDonation = msg.msgdata.donation.amount;
                        }
                        donation_total += msg.msgdata.donation.amount;
                        writer_unlock();

                        write_log("%d DONATE %lu %lu\n", writer_fd, (unsigned long) which_charity, msg.msgdata.donation.amount);
                        write(writer_fd, &msg, sizeof(msg));
                    }
                    break;

                case LOGOUT: 
                    // "act like a reader"
                    reader_lock();
                    pthread_mutex_lock(&max_donations_lock);
                    for (int i = 0; i < 3; ++i) {
                        if (maxDonations[i] < donation_total) {
                            update = true;
                            break;
                        }
                    }
                    if (update) {
                        if (donation_total > maxDonations[0]) {
                            maxDonations[2] = maxDonations[1];
                            maxDonations[1] = maxDonations[0];
                            maxDonations[0] = donation_total;
                        } else if (donation_total > maxDonations[1]) {
                            maxDonations[2] = maxDonations[1];
                            maxDonations[1] = donation_total;
                        } else if (donation_total > maxDonations[2]) {
                            maxDonations[2] = donation_total;
                        }
                    }
                    pthread_mutex_unlock(&max_donations_lock);
                    reader_unlock();

                    write_log("%d LOGOUT\n", writer_fd);
                    write(writer_fd, &msg, sizeof(msg));
                    close(writer_fd);
                    // return NULL;
                    break;

                default: 
                    error = true;
                    break;
            }
            if (error) {
                write_log("%d ERROR\n", writer_fd);
                msg.msgtype = ERROR;
                write(writer_fd, &msg, sizeof(message_t));
            }
        }

        close(writer_fd);
        if (sigint) {
            break;
        }
    }

    close(writer_listen_fd);
    return NULL;
}

void *handle_reader(void *ptr) {
    int reader_fd = *(int *) ptr;
    message_t msg;
    bool error = false;

    while ((read(reader_fd, &msg, sizeof(msg))) > 0) {
        uint64_t which_charity = msg.msgdata.donation.charity;

        switch (msg.msgtype) {
            case CINFO: 
                if (which_charity >= 5) {
                    error = true;
                }
                reader_lock();
                memcpy(&msg.msgdata.charityInfo, &charities[which_charity], sizeof(charity_t));
                write(reader_fd, &msg, sizeof(msg));
                reader_unlock();
                write_log("%d CINFO %lu\n", reader_fd, (unsigned long)which_charity);
                break;

            case TOP: 
                reader_lock();
                pthread_mutex_lock(&max_donations_lock);
                for (int i = 0; i < 3; i++) {
                    msg.msgdata.maxDonations[i] = maxDonations[i];
                }
                pthread_mutex_unlock(&max_donations_lock);
                write(reader_fd, &msg, sizeof(message_t));
                reader_unlock();
                write_log("%d TOP\n", reader_fd);
                break;

            case STATS:
                reader_lock();
                uint64_t amt_high = 0; 
                uint64_t amt_low = UINT64_MAX;
                int charity_high = 0, charity_low = 0;
                for (int i = 0; i < 5; ++i) {
                    if (charities[i].totalDonationAmt > amt_high) {
                        amt_high = charities[i].totalDonationAmt;
                        charity_high = i;
                    }
                    if (charities[i].totalDonationAmt < amt_low) {
                        amt_low = charities[i].totalDonationAmt;
                        charity_low = i;
                    }
                }
                msg.msgdata.stats.charityID_high = charity_high;
                msg.msgdata.stats.charityID_low = charity_low;
                msg.msgdata.stats.amount_high = amt_high;
                msg.msgdata.stats.amount_low = amt_low;
                write(reader_fd, &msg, sizeof(msg));
                reader_unlock();

                write_log("%d STATS %d:%lu %d:%lu\n", reader_fd, charity_high, amt_high, charity_low, amt_low);
                break;

            case LOGOUT: 
                write_log("%d LOGOUT\n", reader_fd);
                write(reader_fd, &msg, sizeof(msg));
                close(reader_fd);
                return NULL;

            default: 
                error = true;
                break;
        }
        if (error) {
            write_log("%d ERROR\n", reader_fd);
            msg.msgtype = ERROR;
            write(reader_fd, &msg, sizeof(message_t));
        }
    }
    close(reader_fd);
    return NULL;
}
