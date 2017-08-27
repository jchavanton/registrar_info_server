#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <time.h>
#include "../include/storage.h"

#define PORT "9000" // listening port

// get sockaddr, IPv4 or IPv6:
static void *get_in_addr(struct sockaddr *sa) {
	if (sa->sa_family == AF_INET) {
		return &(((struct sockaddr_in*)sa)->sin_addr);
	}
	return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

static int set_socket_options(sockfd) {
	struct timeval timeout;
	timeout.tv_sec = 10;
	timeout.tv_usec = 0;
	if (setsockopt (sockfd, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout)) < 0)
		printf("[error] setsockopt failed\n");
	if (setsockopt (sockfd, SOL_SOCKET, SO_SNDTIMEO, (char *)&timeout, sizeof(timeout)) < 0)
		printf("[error] setsockopt failed\n");
}

typedef struct socket {
	time_t last_activity;
} socket_t;

typedef struct socket_pool {
	fd_set master;    // master file descriptor list
	socket_t *sockets;
	int count;
	int size;
} socket_pool_t;

static int socket_pool_init(socket_pool_t * sp) {
	FD_ZERO(&sp->master);    // clear the master and temp sets
	sp->size = 100;
	sp->sockets = (socket_t *) malloc(sizeof(socket_t)*sp->size);
	memset(sp->sockets, 0, sizeof(socket_t)*sp->size);
	if (!sp->sockets)
		return 0;
	return 1;
}

static int socket_pool_expand(socket_pool_t * sp) {
	int expand_size = 50;
	printf("[socket_pool_expand]+%d\n",expand_size);
	sp->size += expand_size;
	void * ptr = realloc(sp->sockets, sizeof(socket_t)*sp->size);
	if (!ptr)
		return 0;
	memset(sp->sockets-expand_size, 0, sizeof(socket_t)*expand_size);
	return 1;
}

static int socket_pool_add(socket_pool_t * sp, int s) {
	set_socket_options(s);
	FD_SET(s, &sp->master);
	if (s > sp->size)
		socket_pool_expand(sp);
	sp->sockets[s].last_activity = time(NULL);
	sp->count++;
	return 1;
}

static int socket_pool_rm(socket_pool_t * sp, int s) {
	close(s);
	FD_CLR(s, &sp->master);
	sp->sockets[s].last_activity = 0;
	sp->count--;
	return 1;
}

int serve(void) {
	fd_set read_fds;  // temp file descriptor list for select()
	int fdmax;        // maximum file descriptor number
	int listener;     // listening socket descriptor
	int newfd;        // newly accept()ed socket descriptor
	struct sockaddr_storage remoteaddr; // client address
	socklen_t addrlen;
	char buf[256];    // buffer for client data
	int nbytes;
	char remoteIP[INET6_ADDRSTRLEN];
	int yes=1;        // for setsockopt() SO_REUSEADDR, below
	int i, j, rv;
	struct addrinfo hints, *ai, *p;

	socket_pool_t socket_pool;
	if (!socket_pool_init(&socket_pool))
		return 1;
	FD_ZERO(&read_fds);

	// get us a socket and bind it
	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;
	if ((rv = getaddrinfo(NULL, PORT, &hints, &ai)) != 0) {
		fprintf(stderr, "tcp_server: %s\n", gai_strerror(rv));
		return 1;
	}

	for(p = ai; p != NULL; p = p->ai_next) {
		listener = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
		if (listener < 0) {
			continue;
		}
		// lose the pesky "address already in use" error message
		setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));
		if (bind(listener, p->ai_addr, p->ai_addrlen) < 0) {
			close(listener);
			continue;
		}
		break;
	}

	// if we got here, it means we didn't get bound
	if (p == NULL) {
		fprintf(stderr, "tcp_server: failed to bind\n");
		return 2;
	}
	freeaddrinfo(ai); // all done with this

	if (listen(listener, 10) == -1) {
		perror("listen");
		return 3;
	}
	printf("\nregistrar information server: listening 0.0.0.0:%s \n", PORT);

	// add the listener to the master set
	FD_SET(listener, &socket_pool.master);
	// keep track of the biggest file descriptor
	fdmax = listener; // so far, it's this one

	time_t now;
	struct timeval interval;
	interval.tv_sec = 0;
	interval.tv_usec = 250000;

	// main loop
	for(;;) {
		read_fds = socket_pool.master; // copy it
		if (select(fdmax+1, &read_fds, NULL, NULL, &interval) == -1) {
			printf("[error] select");
			return 4;
		}

		interval.tv_usec = 250000;
		now = time(NULL);

		for(i = 0; i <= fdmax; i++) { // loop through the existing connections
			if (socket_pool.sockets[i].last_activity) {
				int idle_sec = now - socket_pool.sockets[i].last_activity;
				if (idle_sec >= 10){
					printf("disconnecting socket_fd[%d] after[%d] idle seconds...\n", i, idle_sec);

					socket_pool_rm(&socket_pool, i);
				}
			}
			if (FD_ISSET(i, &read_fds)) { // we got one!!
				if (i == listener) {
						// handle new connections
						addrlen = sizeof remoteaddr;
						newfd = accept(listener, (struct sockaddr *)&remoteaddr, &addrlen);
						if (newfd == -1) {
							perror("accept");
						} else {
							if (newfd > fdmax)   // keep track of the max
								fdmax = newfd;
							printf("tcp_server: new connection from %s on "
								"socket %d\n",
								inet_ntop(remoteaddr.ss_family,
								get_in_addr((struct sockaddr*)&remoteaddr),
								remoteIP, INET6_ADDRSTRLEN), newfd);
							socket_pool_add(&socket_pool, newfd);
						}
				} else {
					// handle data from a client
					socket_pool.sockets[i].last_activity=now;
					if ((nbytes = recv(i, buf, sizeof buf, 0)) <= 0) {
						// got error or connection closed by client
						if (nbytes == 0) {
							// connection closed
							printf("tcp_server: socket %d hung up\n", i);
						} else {
							perror("recv");
						}
						socket_pool_rm(&socket_pool, i);
					} else if (nbytes >= 2) {
						buf[nbytes-2]='\0';
						char *aor = strdup(buf);
						record_t *record = find_record(aor);
						if (record) {
							printf("aor[%s] found:%s\n", aor, record->data.s);
							if (send(i, record->data.s, record->data.len, 0) == -1)
								perror("send");
						} else {
							printf("aor[%s] not found!\n", aor);
							char *none = "\n\e";
							if (send(i, none, 1, 0) == -1)
								perror("send");
						}
					}
				} // END handle data from client
			} // END got new incoming connection
		} // END looping through file descriptors
	} // END for(;;)
	return 0;
}
