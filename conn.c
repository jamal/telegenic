#include "conn.h"
#include <errno.h>
#include <event2/event.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>

int conn_listen(struct event_base *base, const char *host, const char *port, int backlog)
{
	int status;
	struct addrinfo hints, *servinfo, *p;
	evutil_socket_t serv_fd;
	struct event *serv_event;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;

	if ((status = getaddrinfo(host, port, &hints, &servinfo)) != 0) {
		perror("conn: getaddrinfo failed");
		return -1;
	}

	for (p = servinfo; p != NULL; p = p->ai_next) {
		if ((serv_fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
			perror("conn: socket failed");
			continue;
		}

		if (evutil_make_socket_nonblocking(serv_fd) == -1) {
			perror("conn: set socket nonblocking");
			return -1;
		}

		if (evutil_make_listen_socket_reuseable(serv_fd) == -1) {
			perror("conn: set socket reusable");
			return -1;
		}

		if (bind(serv_fd, p->ai_addr, p->ai_addrlen) == -1) {
			evutil_closesocket(serv_fd);
			perror("conn: bind failed");
			continue;
		}

		break;
	}

	if (p == NULL) {
		perror("conn: failed to find a valid addrinfo");
		return -1;
	}

	freeaddrinfo(servinfo);

	if (listen(serv_fd, backlog) == -1) {
		perror("conn: listen failed");
		return -1;
	}

	if ((serv_event = event_new(base, serv_fd, EV_READ|EV_PERSIST, conn_cb_accept, (void*)base)) == NULL) {
		perror("conn: event_new");
		return -1;
	}

	if (event_add(serv_event, NULL) == -1) {
		perror("conn: event_add");
		return -1;
	}

	event_base_dispatch(base);

	event_base_free(base);
	conn_close(serv_fd);

	return 0;
}

void conn_close(evutil_socket_t fd)
{
	EVUTIL_CLOSESOCKET(fd);
}

struct client * conn_alloc_client(struct event_base *base, evutil_socket_t fd)
{
	struct client *c = malloc(sizeof(struct client));
	if (!c) {
		return NULL;
	}

	c->ev_read = event_new(base, fd, EV_READ|EV_PERSIST, conn_cb_client_read, c);
	if (!c->ev_read) {
		free(c);
		return NULL;
	}

	c->ev_write = event_new(base, fd, EV_WRITE|EV_PERSIST, conn_cb_client_write, c);
	if (!c->ev_write) {
		event_free(c->ev_read);
		free(c);
		return NULL;
	}

	return c;
}

void conn_free_client(struct client *c)
{
	event_free(c->ev_read);
	event_free(c->ev_write);
	free(c);
}

void conn_cb_accept(evutil_socket_t serv_fd, short what, void *arg)
{
	struct event_base *base = arg;
	struct sockaddr_storage addr;
	socklen_t addrlen = sizeof(addr);
	evutil_socket_t client_fd;
	struct client *c;

	client_fd = accept(serv_fd, (struct sockaddr*)&addr, &addrlen);
	if (client_fd < 0) {
		perror("accept");
		return;
	} else if (client_fd > FD_SETSIZE) {
		conn_close(client_fd);
		return;
	}

	c = conn_alloc_client(base, client_fd);
	if (c == NULL) {
		perror("failed to allocate client");
		return;
	}

	event_add(c->ev_read, NULL);
}

void conn_cb_client_read(evutil_socket_t fd, short what, void *arg)
{
	struct client *c = arg;
	char buf[1024];
	ssize_t result;

	bzero(buf, 1024);

	result = recv(fd, buf, sizeof(buf), 0);
	if (result == 0) {
		conn_free_client(c);
		return;
	} else if (result < 0) {
		if (errno != EVUTIL_EAI_AGAIN) {
			perror("recv");
			conn_free_client(c);
		}
		return;
	}

	printf("Read: %s", buf);
}

void conn_cb_client_write(evutil_socket_t fd, short what, void *arg)
{
	printf("client write\n");
}
