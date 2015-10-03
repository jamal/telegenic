#include <err.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

int sendall(int s, char *buf, int *len)
{
    int total = 0;        // how many bytes we've sent
    int bytesleft = *len; // how many we have left to send
    int n;

    while(total < *len) {
        n = send(s, buf+total, bytesleft, 0);
        if (n == -1) { break; }
        total += n;
        bytesleft -= n;
    }

    *len = total; // return number actually sent here

    return n==-1?-1:0; // return -1 on failure, 0 on success
}

int main(int argc, char* argv[])
{
	int buflen = 1024;
	char buf[buflen];
	struct addrinfo hints, *addrs, *p;
	int fd, i, wroten;

	srand(time(NULL));

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;

	if (getaddrinfo("127.0.0.1", "1234", &hints, &addrs) != 0) {
		err(1, "failed to get addrinfo");
	}

	for (p = addrs; p != NULL; p = p->ai_next) {
		if ((fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
			perror("socket");
			continue;
		}

        if (connect(fd, p->ai_addr, p->ai_addrlen) == -1) {
            close(fd);
            perror("connect");
            continue;
        }

        break;
	}

	if (p == NULL) {
		err(1, "failed to connect");
	}

	freeaddrinfo(addrs);

	char *post = "POST /example HTTP/1.1\r\n";
	int postlen = strlen(post);
	sendall(fd, post, &postlen);

	for (;;) {
		for (i = 0; i < buflen; i++) {
			char c = 97 + rand() % 25;
			buf[i] = c;
		}

		wroten = sendall(fd, buf, &buflen);
		if (wroten == -1) {
			break;
		}
	}

	close(fd);

	return 0;
}