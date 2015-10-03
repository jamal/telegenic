#include "conn.h"
#include "log.h"

#include <arpa/inet.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/listener.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

int
main(int argc, char *argv[])
{
	// TODO: Make port configurable
	int port = 1234;

	struct event_base *base;
	struct evconnlistener *listener;
	struct sockaddr_in sin;

	conn_init();

	if ((base = event_base_new()) == NULL) {
		log_err("Failed to open base event");
		return 1;
	}

	memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = htonl(0);
	sin.sin_port = htons(port);

	listener = evconnlistener_new_bind(base, conn_accept_cb, NULL,
		LEV_OPT_CLOSE_ON_FREE|LEV_OPT_REUSEABLE, -1,
		(struct sockaddr*)&sin, sizeof(sin));
	if (!listener) {
		log_err("Couldn't create listener");
		return 1;
	}

	evconnlistener_set_error_cb(listener, conn_accept_error_cb);

	event_base_dispatch(base);

	conn_terminate();

	return 0;
}
