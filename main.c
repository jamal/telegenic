#include "conn.h"
#include <err.h>
#include <event2/event.h>
#include <stdlib.h>

int main(int argc, char *argv[])
{
	struct event_base *base;

	if ((base = event_base_new()) == NULL) {
		err(1, "failed to start libevent");
		return -1;
	}

	if (conn_listen(base, NULL, "1234", 10) == -1) {
		err(1, "failed to start server");
	}

	event_base_free(base);
}
