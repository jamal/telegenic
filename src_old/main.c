#include "log.h"

#include <arpa/inet.h>
#include <event2/listener.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <err.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

struct conn_client {
	struct bufferevent *bev;
	char *path;
};

struct conn_client *
conn_alloc_client(struct bufferevent *bev)
{
	struct conn_client *client = malloc(sizeof(struct conn_client));
	client->bev = bev;
	return client;
}

void
conn_free_client(struct conn_client *client)
{
	bufferevent_free(client->bev);
	free(client);
}

static void
read_cb(struct bufferevent *bev, void *ctx)
{
	log_debug("Read callback");
	struct conn_client *client = ctx;
	struct evbuffer *input = bufferevent_get_input(bev);
	size_t len = evbuffer_get_length(input);
	char *data = malloc(sizeof(char) * len);

	if (len == 0) {
		log_debug("Client disconnected");
		conn_free_client(client);
		return;
	}

	evbuffer_remove(input, data, len);
	log_debug("Read: %s", data);
}

static void
write_cb(struct bufferevent *bev, void *ctx)
{
	log_debug("Write callback");
}

static void
event_cb(struct bufferevent *bev, short events, void *ctx)
{
	log_debug("Event callback");
}

static void
accept_conn_cb(struct evconnlistener *listener, evutil_socket_t fd,
	struct sockaddr *address, int socklen, void *ctx)
{
	log_debug("New client connection");

	struct event_base *base = evconnlistener_get_base(listener);
	struct bufferevent *bev = bufferevent_socket_new(
		base, fd, BEV_OPT_CLOSE_ON_FREE);
	struct conn_client *client = conn_alloc_client(bev);

	bufferevent_setcb(bev, read_cb, write_cb, event_cb, client);
	bufferevent_enable(bev, EV_READ|EV_WRITE);
}

static void
accept_error_cb(struct evconnlistener *listener, void *ctx)
{
	struct event_base *base = evconnlistener_get_base(listener);
	int err = EVUTIL_SOCKET_ERROR();
	log_err("Accept error: %d:%s", err, evutil_socket_error_to_string(err));
	event_base_loopexit(base, NULL);
}

int
main(int argc, char *argv[])
{
	// TODO: Make port configurable
	int port = 1234;

	struct event_base *base;
	struct evconnlistener *listener;
	struct sockaddr_in sin;

	if ((base = event_base_new()) == NULL) {
		err(1, "Failed to open base event");
	}

	memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = htonl(0);
	sin.sin_port = htons(port);

	listener = evconnlistener_new_bind(base, accept_conn_cb, NULL,
		LEV_OPT_CLOSE_ON_FREE|LEV_OPT_REUSEABLE, -1,
		(struct sockaddr*)&sin, sizeof(sin));
	if (!listener) {
		perror("Couldn't create listener");
		return 1;
	}

	evconnlistener_set_error_cb(listener, accept_error_cb);

	event_base_dispatch(base);

	return 0;
}
