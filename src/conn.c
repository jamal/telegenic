#include "conn.h"
#include "rtmp.h"

#include <apr-1/apr_general.h>
#include <apr-1/apr_pools.h>
#include <apr-1/apr_hash.h>
#include <ctype.h>
#include <stdlib.h>

static apr_pool_t *mp;
static apr_hash_t *ht;

static void
conn_add_producer(const char *path, struct conn_client *client)
{
	log_debug("Adding producer for: %s", client->path);
	struct producer *producer = malloc(sizeof(struct producer));
	producer->client = client;
	producer->consumer_list = NULL;
	client->producer = producer;
	client->is_producer = 1;
	apr_hash_set(ht, path, APR_HASH_KEY_STRING, producer);
}

static struct producer *
conn_get_producer(const char *path)
{
	struct producer *producer = apr_hash_get(ht, path, APR_HASH_KEY_STRING);
	return producer;
}

static void
conn_del_producer(struct conn_client *client)
{
	struct producer *producer = client->producer;
	if (producer == NULL) {
		return;
	}
	struct consumer *tmp_c, *c = producer->consumer_list;
	while (c != NULL) {
		tmp_c = c;
		c = c->next;
		free(tmp_c);
	}
	if (producer != NULL) {
		apr_hash_set(ht, client->path, APR_HASH_KEY_STRING, NULL);
		free(producer);
	}
}

static void
conn_add_consumer(struct producer *producer, struct conn_client *client)
{
	log_debug("Adding consumer to: %s", producer->client->path);
	struct consumer *c, *consumer;
	consumer = malloc(sizeof(struct consumer));
	consumer->client = client;
	consumer->next = NULL;
	if (producer->consumer_list == NULL) {
		producer->consumer_list = consumer;
		return;
	}
	c = producer->consumer_list;
	while (c->next != NULL) {
		c = c->next;
	}
	c->next = consumer;
}

static void
conn_del_consumer(struct conn_client *client)
{
	struct producer *producer = client->producer;
	struct consumer *c, *c_prev;
	if (client->producer == NULL) {
		return;
	}
	c = producer->consumer_list;
	while (c != NULL) {
		if (c->client == client) {
			if (c == producer->consumer_list) {
				producer->consumer_list = c->next;
			} else {
				c_prev->next = c->next;
			}

			free(c);
		}
		c_prev = c;
	}
}

void
conn_init()
{
	apr_initialize();
	apr_pool_create(&mp, NULL);
	apr_palloc(mp, MEM_ALLOC_SIZE);
	ht = apr_hash_make(mp);
}

void
conn_terminate()
{
	apr_pool_destroy(mp);
	apr_terminate();
}

void
conn_buffer_write(struct conn_client *client, char *data, size_t len)
{
	struct evbuffer *out = bufferevent_get_output(client->bev);
	evbuffer_add(out, data, len);
}

struct conn_client *
conn_alloc_client(struct bufferevent *bev)
{
	struct conn_client *client = malloc(sizeof(struct conn_client));
	client->bev = bev;
	client->path = NULL;
	client->is_producer = 0;
	client->producer = NULL;
	return client;
}

void
conn_free_client(struct conn_client *client)
{
	// Socket is closed when bufferevent is free'd
	bufferevent_free(client->bev);
	free(client);
}

static enum protocol
conn_determine_protocol(const char *data, size_t len)
{
	// RTMP: First packet will be the client RTMP version
	if (data[0] >= 0x03 && data[0] <= 0x1F) {
		log_debug("Detected protocol: rtmp");
		return protocol_rtmp;
	}

	return protocol_none;
}

static char *
conn_read_header(const char *data, struct conn_client *client)
{
	size_t len;
	int is_producer;
	struct producer *producer;
	char *pos;

	// Read HTTP Method
	switch (toupper(data[0])) {
		case 'P': // POST
			log_debug("HTTP method POST");
			is_producer = 1;
			break;
		case 'G': // GET
			log_debug("HTTP method GET");
			is_producer = 0;
			break;

		default:
			return NULL;
	}

	// Read path
	pos = strpbrk(data, " ") + 1;
	if (!pos) return NULL;

	len = (strchr(pos, ' ') - pos);
	client->path = malloc(len + 1);
	strncpy(client->path, pos, len);
	log_debug("Read path %s", client->path);

	// Move pos to the end of the HTTP header
	pos = strstr(pos, "\r\n") + 2;
	if (!pos) return NULL;

	producer = conn_get_producer(client->path);

	if (is_producer) {
		if (producer != NULL) {
			return NULL;
		}

		conn_add_producer(client->path, client);
	} else {
		if (producer == NULL) {
			return NULL;
		}

		conn_add_consumer(producer, client);
	}

	return pos;
}

void
conn_read_cb(struct bufferevent *bev, void *ctx)
{
	struct conn_client *client = ctx;
	struct evbuffer *input = bufferevent_get_input(bev);
	size_t len = evbuffer_get_length(input);
	char *data;
	// struct consumer *consumer;

	if (len) {
		data = calloc(1, sizeof(char) * len);
		evbuffer_remove(input, data, len);

		if (client->proto == protocol_none) {
			client->proto = conn_determine_protocol(data, len);
		}

		switch (client->proto) {
			case protocol_rtmp:
				rtmp_read(client, data, len);
				break;

			default:
				log_info("Failed to determine client protocol: %s", data);
				conn_free_client(client);
				return;
		}

		// printf("Reading data\n");
		// for (int i = 0; i < strlen(data); i++) {
		// 	printf("%#08x ", data[i]);
		// }
		// printf("\n");

		// if (client->path == NULL) {
		// 	pos = conn_read_header(data, client);
		// 	if (pos == NULL) {
		// 		log_debug("Failed to read HTTP header: %s", data);
		// 		conn_free_client(client);
		// 		return;
		// 	}
		// } else {
		// 	pos = data;
		// }

		// if (client->is_producer && client->producer) {
		// 	consumer = client->producer->consumer_list;
		// 	while (consumer != NULL) {
		// 		out = bufferevent_get_output(consumer->client->bev);
		// 		evbuffer_add(out, pos, len);
		// 		consumer = consumer->next;
		// 	}
		// }

		free(data);
	}
}

void
conn_write_cb(struct bufferevent *bev, void *ctx)
{
	// ...
}

void
conn_event_cb(struct bufferevent *bev, short events, void *ctx)
{
	struct conn_client *client = ctx;
    if (events & BEV_EVENT_ERROR) {
		log_err("Error from bufferevent");
    }
    if (events & (BEV_EVENT_EOF | BEV_EVENT_ERROR)) {
		log_debug("Client connection closed");
		if (client->is_producer) {
			conn_del_producer(client);
		} else {
			conn_del_consumer(client);
		}
		conn_free_client(client);
    }
}

void
conn_accept_cb(struct evconnlistener *listener, evutil_socket_t fd,
	struct sockaddr *address, int socklen, void *ctx)
{
	log_debug("New client connection");

	struct event_base *base = evconnlistener_get_base(listener);
	struct bufferevent *bev = bufferevent_socket_new(
		base, fd, BEV_OPT_CLOSE_ON_FREE);
	struct conn_client *client = conn_alloc_client(bev);

	bufferevent_setcb(bev, conn_read_cb, conn_write_cb, conn_event_cb, client);
	bufferevent_enable(bev, EV_READ|EV_WRITE);
}

void
conn_accept_error_cb(struct evconnlistener *listener, void *ctx)
{
	struct event_base *base = evconnlistener_get_base(listener);
	int err = EVUTIL_SOCKET_ERROR();
	log_err("Accept error: %d:%s", err, evutil_socket_error_to_string(err));
	event_base_loopexit(base, NULL);
}