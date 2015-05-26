#ifndef __CONN_H__
#define __CONN_H__

#include <event2/event.h>

struct client {
	struct event *ev_read;
	struct event *ev_write;
};

int conn_listen(struct event_base *base, const char *host, const char *port, int backlog);
void conn_close(evutil_socket_t fd);
void conn_cb_accept(evutil_socket_t serv_fd, short what, void *arg);

struct client * conn_alloc_client(struct event_base *base, evutil_socket_t fd);
void conn_free_client(struct client* c);

void conn_cb_client_read(evutil_socket_t serv_fd, short what, void *arg);
void conn_cb_client_write(evutil_socket_t serv_fd, short what, void *arg);

#endif