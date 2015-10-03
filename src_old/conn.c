#include "conn.h"
#include <ctype.h>
#include <errno.h>
#include <event2/event.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>

// Linked-list of producers
// TODO: Use a Hashmap with path keys instead
static struct producer* producer_list;

struct producer * add_producer(struct client *c)
{
	struct producer *p, *new_p;
	new_p = malloc(sizeof(struct producer));
	new_p->client = c;
	new_p->next = NULL;

	if (producer_list == NULL) {
		producer_list = new_p;
	} else {
		for (p = producer_list; p != NULL; p = p->next) {
			if (p->next == NULL) {
				p->next = new_p;
				break;
			}
		}
	}

	return new_p;
}

void del_producer(struct producer *producer)
{
	struct producer *p, *p_prev;
	struct consumer *c, *c_tmp;

	for (p = producer_list; p != NULL; p = p->next) {
		if (p == producer) {
			if (p_prev == NULL) {
				producer_list = p->next;
			} else {
				p_prev->next = p->next;
			}

			// Disconnect all consumers
			c = p->consumer_list;
			while (c != NULL) {
				if (c->client != NULL) {
					conn_close_client(c->client);
				}
				c_tmp = c;
				c = c_tmp->next;
				free(c_tmp);
			}

			free(p);
			return;
		}

		p_prev = p;
	}
}

struct producer * get_producer_with_path(char *path)
{
	struct producer *p;
	for (p = producer_list; p != NULL; p = p->next) {
		if (strcmp(p->client->path, path) == 0) {
			return p;
		}
	}
	return NULL;
}

void add_consumer(struct producer *p, struct client *client)
{
	struct consumer *c, *new_c;
	new_c = malloc(sizeof(struct consumer));
	new_c->client = client;
	new_c->next = NULL;

	if (p->consumer_list == NULL) {
		p->consumer_list = new_c;
		return;
	}

	for (c = p->consumer_list; c != NULL; c = c->next) {
		if (c->next == NULL) {
			c->next = new_c;
			return;
		}
	}
}

void del_consumer(struct producer *p, struct consumer *consumer)
{
	struct consumer *c, *c_prev;
	for (c = p->consumer_list; c != NULL; c = c->next) {
		if (c == consumer) {
			if (c_prev == NULL) {
				p->consumer_list = c->next;
			} else {
				c_prev->next = c->next;
			}

			free(c);
			return;
		}

		c_prev = c;
	}
}

int conn_listen(struct event_base *base, const char *host, const char *port, int backlog)
{
	struct addrinfo hints, *servinfo, *p;
	evutil_socket_t serv_fd;
	struct event *serv_event;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;

	if (getaddrinfo(host, port, &hints, &servinfo) != 0) {
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

	c->path = NULL;
	c->producer = 0;
	c->fd = fd;

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
	free(c->path);
	event_free(c->ev_read);
	event_free(c->ev_write);
	free(c);
}

void conn_close_client(struct client *c) {
	if (c->producer != NULL) {
		del_producer(c->producer);
	}

	conn_close(c->fd);
	conn_free_client(c);
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

	if (evutil_make_socket_nonblocking(client_fd) == -1) {
		perror("conn: set socket nonblocking");
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
	int get = 0, len;
	struct client *c = arg;
	char buf[1024], *pch, *pch2;
	ssize_t result;
	struct producer *p;
	struct consumer *consumer;

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

	// Read HTTP header
	if (c->path == NULL) {
		// Read HTTP Method
		pch = buf;
		switch (toupper(pch[0])) {
			case 'P':
				get = 0;
				p = add_producer(c);
				c->producer = p;
				break;

			case 'G':
				get = 1;
				break;

			default:
				conn_close_client(c);
				return;
		}

		// Read path
		pch = strpbrk(pch, " ");
		if (pch == NULL) {
			conn_close_client(c);
			return;
		}
		pch = pch + 1;
		pch2 = strpbrk(pch, " ");
		if (pch2 == NULL) {
			conn_close_client(c);
			return;
		}
		*pch2 = '\0';
		c->path = malloc(strlen(pch) + 1);
		strcpy(c->path, pch);

		if (get) {
			printf("Adding consumer %s\n", c->path);
			p = get_producer_with_path(c->path);
			if (p == NULL) {
				// TODO: Write back to the client
				conn_close_client(c);
				return;
			}
			add_consumer(p, c);
			// Stop reading from this client
			event_del(c->ev_read);
		}

		// Set pch to end of http header
		pch = pch2 + 1;
		pch = strstr(pch, "\r\n");
		if (pch == NULL) {
			conn_close_client(c);
			return;
		}
		pch = pch + 2;
	} else {
		pch = buf;
	}

	if (c->producer != NULL) {
		consumer = c->producer->consumer_list;
		while (consumer != NULL) {
			if (consumer->client) {
				len = strlen(pch);
				if (conn_sendall(consumer->client->fd, pch, &len) != 0) {
					perror("sendall");
					del_consumer(c->producer, consumer);
				}
			} else {
				printf("Clientis null\n");
				del_consumer(c->producer, consumer);
			}
		}
	}
}

void conn_cb_client_write(evutil_socket_t fd, short what, void *arg)
{
	printf("client write\n");
}

int conn_sendall(int s, char *buf, int *len)
{
    int total = 0;        // how many bytes we've sent
    int bytesleft = *len; // how many we have left to send
    int n;

    while(total < *len) {
        n = send(s, buf+total, bytesleft, 0);
        if (n < 0) {
        	if (errno != EVUTIL_EAI_AGAIN) {
        		return 0;
        	}
        	return -1;
        }

        total += n;
        bytesleft -= n;
    }

    *len = total; // return number actually sent here

    return 0;
}
