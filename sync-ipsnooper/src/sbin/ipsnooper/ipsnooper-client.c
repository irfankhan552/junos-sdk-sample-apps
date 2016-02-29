/*
 * $Id: ipsnooper-client.c 346460 2009-11-14 05:06:47Z ssiano $
 *
 * This code is provided as is by Juniper Networks SDK Developer Support.
 * It is provided with no warranties or guarantees, and Juniper Networks
 * will not provide support or maintenance of this code in any fashion.
 * The code is provided only to help a developer better understand how
 * the SDK can be used.
 *
 * Copyright (c) 2008, Juniper Networks, Inc.
 * All rights reserved.
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <arpa/inet.h>

#define MAX_READ_BUF_SIZE   256
#define IPSNOOPER_PORT      48008

static int client_socket = -1;

/**
 * The main function of IP Snooper client.
 *
 * @param[in] argc
 *      Number of command line arguments
 *
 * @param[in] argv
 *      String array of command line arguments
 *
 * @return 0 always
 */
int
main (int argc, char **argv)
{
    struct sockaddr_in  server_addr;
    int read_len = 0;
    char buf[MAX_READ_BUF_SIZE];
    unsigned short port = IPSNOOPER_PORT;

    if (argc < 2) {
        printf("Usage: %s server_address\n", argv[0]);
        return 0;
    }

    if (argc == 3) {
        port = (unsigned short)strtol(argv[2], NULL, 0);
    }

    bzero(&server_addr, sizeof(struct sockaddr_in));

    /* Create socket to connect to the server. */
    client_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (client_socket < 0) {
        printf("ERROR: Create client socket!\n");
        goto exit;
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = inet_addr(argv[1]);
    if (connect(client_socket, (struct sockaddr *)&server_addr,
            sizeof(server_addr)) < 0) {
        printf("ERROR: Connect to the server: %s\n", strerror(errno));
        goto exit;
    }
    printf("Connected to the IP Snooper server %s.\n", argv[1]);

    while (1) {
        read_len = recv(client_socket, buf, MAX_READ_BUF_SIZE - 1, 0);
        if (read_len < 0) {
            printf("ERROR: Read from socket!\n");
            break;
        } else if (read_len == 0) {
            printf("The server closed.\n");
            break;
        } else {
            buf[read_len] = 0;
            printf("%s", buf);
        }
    }

exit:
    if (client_socket >= 0) {
        close(client_socket);
    }
    return 0;
}

