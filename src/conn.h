#ifndef __TELEGENIC_CONN_H__
#define __TELEGENIC_CONN_H__

#include "log.h"

#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/listener.h>

#define MEM_ALLOC_SIZE 80*1024

enum protocol {
	protocol_none,
	protocol_rtmp
};

struct consumer {
	struct conn_client *client;
	struct consumer* next;
};

struct producer {
	struct conn_client *client;
	struct consumer* consumer_list;
};

struct conn_client {
	struct bufferevent *bev;
	char *path;
	int is_producer;
	struct producer *producer;

	enum protocol proto;
	void *proto_data;
};

void conn_init();
void conn_terminate();
void conn_buffer_write(struct conn_client *client, char *data, size_t len);

struct conn_client *conn_alloc_client(struct bufferevent *bev);

void conn_free_client(struct conn_client *client);

void conn_read_cb(struct bufferevent *bev, void *ctx);

void conn_write_cb(struct bufferevent *bev, void *ctx);

void conn_event_cb(struct bufferevent *bev, short events, void *ctx);

void conn_accept_cb(struct evconnlistener *listener, evutil_socket_t fd,
	struct sockaddr *address, int socklen, void *ctx);

void conn_accept_error_cb(struct evconnlistener *listener, void *ctx);

#endif