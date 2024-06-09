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

void reader_lock() {
    pthread_mutex_lock(&readers_lock);
    readcnt++;
    if (readcnt) {
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
    // close(writer_fd);
    // fclose(log_file);
    // pthread_cancel(writer_tid);
    // pthread_join(writer_tid, NULL);
    // print_stats();
    // cleanup_server_data();
    // exit(0);
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
        
        while ((read(writer_fd, &msg, sizeof(msg))) > 0) {
        
            uint64_t which_charity = msg.msgdata.donation.charity;

            switch (msg.msgtype) {
                case DONATE:

                    if (which_charity >= 5) {
                        error = true;
                        msg.msgtype = ERROR;
                        write_log("<%d> ERROR: Invalid charity index %lu\n", writer_fd, (unsigned long)which_charity);
                        write(writer_fd, &msg, sizeof(msg));
                        continue;
                    }

                    writer_lock();
                    charities[which_charity].totalDonationAmt += msg.msgdata.donation.amount;
                    charities[which_charity].numDonations++;
                    if (msg.msgdata.donation.amount > charities[which_charity].topDonation) {
                        charities[which_charity].topDonation = msg.msgdata.donation.amount;
                    }
                    donation_total += msg.msgdata.donation.amount;
                    writer_unlock();

                    write_log("<%d> DONATE %lu %lu\n", writer_fd, (unsigned long)which_charity, msg.msgdata.donation.amount);
                    write(writer_fd, &msg, sizeof(msg));
                    break;

                case LOGOUT: 
                    // "act like a reader"
                    reader_lock();
                    update = false;
                    for (int i = 0; i < 3; ++i) {
                        if (maxDonations[i] < donation_total) {
                            update = true;
                            break;
                        }
                    }
                    reader_unlock();

                    if (update) {
                        writer_lock();
                        for (int i = 0; i < 3; ++i) {
                            if (maxDonations[i] < donation_total) {
                                maxDonations[i] = donation_total;
                                break;
                            }
                        }
                        writer_unlock();
                    }

                    write_log("<%d> LOGOUT\n", writer_fd);
                    write(writer_fd, &msg, sizeof(msg));
                    close(writer_fd);
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
                reader_unlock();
                write(reader_fd, &msg, sizeof(msg));
                write_log("<%d> CINFO %lu\n", reader_fd, (unsigned long)which_charity);
                break;

            case TOP: 
                reader_lock();
                pthread_mutex_lock(&max_donations_lock);
                for (int i = 0; i < 3; i++) {
                    msg.msgdata.maxDonations[i] = maxDonations[i];
                }
                // write(reader_fd, maxDonations, sizeof(maxDonations));
                write(reader_fd, &msg, sizeof(message_t));
                pthread_mutex_unlock(&max_donations_lock);
                reader_unlock();
                write_log("<%d> TOP\n", reader_fd);
                break;

            case STATS:
                reader_lock();
                uint64_t max_amount = 0; 
                uint64_t min_amount = UINT64_MAX;
                int max_charity = 0, min_charity = 0;
                for (int i = 0; i < 5; ++i) {
                    if (charities[i].totalDonationAmt > max_amount) {
                        max_amount = charities[i].totalDonationAmt;
                        max_charity = i;
                    }
                    if (charities[i].totalDonationAmt < min_amount) {
                        min_amount = charities[i].totalDonationAmt;
                        min_charity = i;
                    }
                }
                msg.msgdata.stats.charityID_high = max_charity;
                msg.msgdata.stats.charityID_low = min_charity;
                msg.msgdata.stats.amount_high = max_amount;
                msg.msgdata.stats.amount_low = min_amount;
                reader_unlock();

                write(reader_fd, &msg, sizeof(msg));
                write_log("<%d> STATS %d:%lu %d:%lu\n", reader_fd, min_charity, min_amount, max_charity, max_amount);
                break;

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

void print_stats() {
    for (int i = 0; i < 5; i++) {
        printf("%d, %u, %lu, %lu\n", i, charities[i].numDonations, charities[i].topDonation, charities[i].totalDonationAmt);
    }
    fprintf(stderr, "%d\n%lu, %lu, %lu\n", clientCnt, maxDonations[0], maxDonations[1], maxDonations[2]);
}

// void print_stats() {
//     printf("%d\n", clientCnt);
//     fprintf(stderr, "%d\n", clientCnt);

//     printf("%lu,%lu,%lu\n", maxDonations[0], maxDonations[1], maxDonations[2]);
//     fprintf(stderr, "%lu,%lu,%lu\n", maxDonations[0], maxDonations[1], maxDonations[2]);

//     for (int i = 0; i < 5; ++i) {
//         printf("%lu,%lu,%u\n", charities[i].totalDonationAmt, charities[i].topDonation, charities[i].numDonations);
//         fprintf(stderr, "%lu,%lu,%u\n", charities[i].totalDonationAmt, charities[i].topDonation, charities[i].numDonations);
//     }
// }
