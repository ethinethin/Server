#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <poll.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>

#define PORT 12345
#define MAX_CLIENTS 100
#define BUF_SIZE 1024

enum client_state { STATE_USERNAME, STATE_ROOM, STATE_CHAT };

struct client {
    int fd;
    enum client_state state;
    char username[64];
    char room[64];
    char buffer[BUF_SIZE];
    size_t buf_len;
};

struct pollfd POLL_FDS[MAX_CLIENTS];
struct client CLIENTS[MAX_CLIENTS];

/* Function prototypes */
int     start_listening_socket(int port);
void    init_pollfds(struct pollfd poll_fds[], int listener_fd, int n);
void    init_clients(struct client clients[], int listener_fd, int n);
void    handle_client_input(int i);
void    close_client(struct client clients[], struct pollfd poll_fds[], int i);
void    broadcast_to_room(const char *room, const char *sender, const char *msg, int sender_fd);
void    error_out(char *err, int i);

int
main(void)
{
    int client_fd;
    int i;
    int listener_fd;
    int n_ready;

    /* Begin listening on port */
    listener_fd = start_listening_socket(PORT);
    init_pollfds(POLL_FDS, listener_fd, MAX_CLIENTS);
    init_clients(CLIENTS, listener_fd, MAX_CLIENTS);

    /* Main server loop */
    while (1) {
        /* Poll all pollfds, wait for input */
        n_ready = poll(POLL_FDS, MAX_CLIENTS, -1);
        if (n_ready < 0) error_out("poll", 1);

        /* If it is the listener, assign a new client */
        if (POLL_FDS[0].revents & POLLIN) {
            client_fd = accept(listener_fd, NULL, NULL);
            if (client_fd < 0) { perror("accept"); continue; }
            /* Find an inactive entry in the client array */
            for (i = 1; i < MAX_CLIENTS; i += 1) {
                if (CLIENTS[i].fd == -1) {
                    CLIENTS[i].fd = client_fd;
                    CLIENTS[i].state = STATE_USERNAME;
                    CLIENTS[i].buf_len = 0;
                    POLL_FDS[i].fd = client_fd;
                    POLL_FDS[i].events = POLLIN;
                    /* Prompt user for user name */
                    send(client_fd, "Enter user name: ", 17, 0);
                    printf("New connection on fd %d\n", client_fd);
                    break;
                }
            }
            /* If there was no inactive entry, disconnect user */
            if (i == MAX_CLIENTS) {
                send(client_fd, "Server full\n", 12, 0);
                close(client_fd);
            }
        }

        /* Check all clients for input */
        for (int i = 1; i < MAX_CLIENTS; ++i) {
            if (POLL_FDS[i].fd != -1 && (POLL_FDS[i].revents & POLLIN)) {
                handle_client_input(i);
            }
        }
    }
}

int
start_listening_socket(int port)
{
    int listener_fd;
    int opt;
    struct sockaddr_in addr;

    /* Create an IPv4 TCP socket */
    listener_fd = socket(PF_INET, SOCK_STREAM, 0);
    if (listener_fd < 0) error_out("socket", 1);

    /* Allow reuse of the local address */
    opt = 1;
    if (setsockopt(listener_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) error_out("setsockopt", 1);

    /* Bind the socket to all interfaces on the given port */
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    if (bind(listener_fd, (struct sockaddr *) &addr, sizeof(addr)) < 0) error_out("bind", 1);

    /* Mark the socket as passive, ready to accept connections */
    if (listen(listener_fd, 10) < 0) error_out("listen", 1);
    printf("Now listening on port %d\n", port);
    return listener_fd;
}

void
init_pollfds(struct pollfd poll_fds[], int listener_fd, int n)
{
    /* Initialize all pollfds to inactive */
    for (int i = 0; i < n; i += 1) poll_fds[i].fd = -1;
    /* Assign the listener socket to the first pollfd */
    poll_fds[0].fd = listener_fd;
    poll_fds[0].events = POLLIN;
    printf("pollfd array initialized\n");
}

void
init_clients(struct client clients[], int listener_fd, int n)
{
    /* Initialize all clients to inactive */
    for (int i = 0; i < n; i += 1) clients[i].fd = -1;
    /* Assign the listener socket to the first slot (unused) for symmetry */
    clients[0].fd = listener_fd;
    printf("client array initialized\n");
}

void
handle_client_input(int i)
{
    char *buf;
    char *newline;
    char welcome[128];
    size_t remaining;
    ssize_t n;
    struct client *c;

    /* Point to the given client and buffer */
    c = &CLIENTS[i];
    buf = c->buffer;
    /* Receive from the given buffer */
    n = recv(c->fd, buf + c->buf_len, sizeof(c->buffer) - c->buf_len - 1, 0);

    /* If the value is negative, this is an error, or 0, EOF ... so close the connection either way */
    if (n <= 0) {
        if (n < 0) perror("recv");
        close_client(CLIENTS, POLL_FDS, i);
        return;
    }
    /* Increase the buffer length and end with a string terminator */
    c->buf_len += n;
    buf[c->buf_len] = '\0';
    /* If there was a newline, the packet is complete and we can process it */
    while ((newline = strchr(buf, '\n')) != NULL) {
        /* Terminate the string where the newline character is */
        *newline = '\0';
        if (c->state == STATE_USERNAME) {
            /* If they are in username mode, assign their username and prompt for room*/
            strncpy(c->username, buf, sizeof(c->username) - 1);
            c->state = STATE_ROOM;
            send(c->fd, "Enter room: ", 12, 0);
        } else if (c->state == STATE_ROOM) {
            /* If they are in room mode, assign their room */
            strncpy(c->room, buf, sizeof(c->room) - 1);
            c->state = STATE_CHAT;
            snprintf(welcome, sizeof(welcome), "Welcome, %s! You're in room [%s]\n", c->username, c->room);
            send(c->fd, welcome, strlen(welcome), 0);
        } else if (c->state == STATE_CHAT) {
            /* Otherwise send their message to the room */
            broadcast_to_room(c->room, c->username, buf, c->fd);
        }
        /* Shift any data after the newline back to the beginning of the buffer */
        remaining = c->buf_len - (newline - buf + 1);
        memmove(buf, newline + 1, remaining);
        c->buf_len = remaining;
        buf[c->buf_len] = '\0';
    }
}

void
close_client(struct client clients[], struct pollfd poll_fds[], int i)
{
    close(clients[i].fd);
    poll_fds[i].fd = -1;
    clients[i].fd = -1;
    printf("Client %d disconnected\n", i);
}

void
broadcast_to_room(const char *room, const char *sender, const char *msg, int sender_fd)
{
    char out[BUF_SIZE + 128];
    int i;
    size_t len;

    /* Copy the message to be ready to send it */
    snprintf(out, sizeof(out), "%s: %s\n", sender, msg);
    len = strlen(out);
    /* Check each client - only send to those in the same room, but not the same user*/
    for (i = 1; i < MAX_CLIENTS; i += 1) {
        if (CLIENTS[i].fd != -1 && CLIENTS[i].state == STATE_CHAT &&
            strcmp(CLIENTS[i].room, room) == 0 && CLIENTS[i].fd != sender_fd) {
            send(CLIENTS[i].fd, out, len, 0);
        }
    }
}

void
error_out(char *err, int i)
{
    perror(err);
    exit(i);
}
