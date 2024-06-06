#ifndef RWSERVER_H
#define RwSERVER_H

#include <arpa/inet.h>
#include <getopt.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define SA struct sockaddr

#define USAGE_MSG_MT "ZotDonation_MTserver [-h] PORT_NUMBER LOG_FILENAME"\
                  "\n  -h                 Displays this help menu and returns EXIT_SUCCESS."\
                  "\n  PORT_NUMBER        Port number to listen on."\
                  "\n  LOG_FILENAME       File to output server actions into. Create/overwrite, if exists\n"

#define USAGE_MSG_RW "ZotDonation_RWserver [-h] R_PORT_NUMBER W_PORT_NUMBER LOG_FILENAME"\
                  "\n  -h                 Displays this help menu and returns EXIT_SUCCESS."\
                  "\n  R_PORT_NUMBER      Port number to listen on for reader (observer) clients."\
                  "\n  W_PORT_NUMBER      Port number to listen on for writer (donor) clients."\
                  "\n  LOG_FILENAME       File to output server actions into. Create/overwrite, if exists\n"

int socket_listen_init(int server_port);


#endif
