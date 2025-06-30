#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>
#include <errno.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>

#define PORT 12345
#define BUF_SIZE 1024

/* Function prototypes */
void    error_out(char *err, int i);

int main(int argc, char *argv[])
{
    char buf[BUF_SIZE];
    int sockfd;
    ssize_t n;
    struct sockaddr_in server;
    struct pollfd pfds[2];

    if (argc != 2) {
        printf("Usage: %s <server_ip>\n", argv[0]);
        return 1;
    }

    /* Create an IPv4 socket */
    sockfd = socket(PF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) error_out("socket", 1);

    /* Set up the server address struct */
    memset(&server, 0, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_port = htons(PORT);
    /* Convert the IP address to binary */
    if (inet_pton(AF_INET, argv[1], &server.sin_addr) <= 0) error_out("inet_pton", 1);
    /* Connect to the server */
    if (connect(sockfd, (struct sockaddr *) &server, sizeof(server)) < 0) error_out("connect", 1);

    /* Prepare pollfd array for stdin and the socket */
    pfds[0].fd = STDIN_FILENO;
    pfds[0].events = POLLIN;
    pfds[1].fd = sockfd;
    pfds[1].events = POLLIN;

    while (1) {
        /* Wait for input from stdin or socket */
        if (poll(pfds, 2, -1) < 0) error_out("poll", 1);

        /* If there was input from user, send it to the server */
        if (pfds[0].revents & POLLIN) {
            if (!fgets(buf, sizeof(buf), stdin)) error_out("fgets", 1);
            send(sockfd, buf, strlen(buf), 0);
        }

        /* If there was input from the server, print it*/
        if (pfds[1].revents & POLLIN) {
            n = recv(sockfd, buf, sizeof(buf) - 1, 0);
            /* If error or server disconnected, break */
            if (n <= 0) error_out("recv", 1);
            /* Output the line */
            buf[n] = '\0';
            printf("%s", buf);
            fflush(stdout);
        }
    }

    /* This should never be reached */
    close(sockfd);
    return 0;
}

void
error_out(char *err, int i)
{
    fprintf(stderr, "%s\n", err);
    exit(i);
}
