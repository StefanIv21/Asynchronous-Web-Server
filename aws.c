#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "aws.h"
#include "util.h"
#include "debug.h"
#include "sock_util.h"
#include "w_epoll.h"
#include "http_parser.h"
#include <fcntl.h>
#include <sys/sendfile.h>
#define ERROR_MSG "HTTP/1.1 404 not found\r\n\r\n"
#define OK_MSG "HTTP/1.1 200\r\n\r\n"
/* server socket file descriptor */
static int listenfd;
/* epoll file descriptor */
static int epollfd;
enum connection_state {
	STATE_DATA_RECEIVED,
	STATE_CONTINUE,
	STATE_BAD_HTTP,
	STATE_CONNECTION_CLOSED
};
/* structure acting as a connection handler */
struct connection {
	int sockfd;
	/* buffers used for receiving messages and then echoing them back */
	char recv_buffer[BUFSIZ];
	size_t recv_len;
	char send_buffer[BUFSIZ];
	size_t send_len;
	enum connection_state state;
	int err;
    int noufd;
	int filesize;
	int after_parser;
	int nr_bytes;
	off_t offset;
	struct stat buffer;
};
char request_path[BUFSIZ];
http_parser request_parser;
/*
 * Callback is invoked by HTTP request parser when parsing request path.
 * Request path is stored in global request_path variable.
 */
static int on_path_cb(http_parser *p, const char *buf, size_t len) {
	assert(p == &request_parser);
	memcpy(request_path, buf, len);
	return 0;
}
/* Use mostly null settings except for on_path callback. */
static http_parser_settings settings_on_path = {
	/* on_message_begin */ 0,
	/* on_header_field */ 0,
	/* on_header_value */ 0,
	/* on_path */ on_path_cb,
	/* on_url */ 0,
	/* on_fragment */ 0,
	/* on_query_string */ 0,
	/* on_body */ 0,
	/* on_headers_complete */ 0,
	/* on_message_complete */ 0
};
/*
 * Initialize connection structure on given socket.
 */
static struct connection *connection_create(int sockfd)
{
	struct connection *conn = malloc(sizeof(*conn));
		DIE(conn == NULL, "malloc");
	conn->sockfd = sockfd;
	memset(conn->recv_buffer, 0, BUFSIZ);
	memset(conn->send_buffer, 0, BUFSIZ);
	conn->recv_len = 0;
	conn->send_len = 0;
	conn->noufd = -1;
	conn->filesize = 0;
	conn->after_parser = 0;
	conn->state = 0;
	conn->offset = 0;
	return conn;
}
/*
 * Remove connection handler.
 */
static void connection_remove(struct connection *conn)
{
	close(conn->sockfd);
	conn->state = STATE_CONNECTION_CLOSED;
	free(conn);
}
/*
 * Handle a new connection request on the server socket.
 */
static void handle_new_connection(void)
{
	static int sockfd;
	socklen_t addrlen = sizeof(struct sockaddr_in);
	struct sockaddr_in addr;
	struct connection *conn;
	int rc;
	/* accept new connection */
	sockfd = accept(listenfd, (SSA *) &addr, &addrlen);
	DIE(sockfd < 0, "accept");
	/* fac socketul sa fie non-blocant*/
	fcntl(sockfd, F_SETFL, O_RDWR | O_NONBLOCK);
	/* instantiate new connection handler */
	conn = connection_create(sockfd);
	/* add socket to epoll */
	rc = w_epoll_add_ptr_in(epollfd, sockfd, conn);
	DIE(rc < 0, "w_epoll_add_in");
}
/*
 * Receive message on socket.
 * Store message in recv_buffer in struct connection.
 */
static enum connection_state receive_message(struct connection *conn)
{
	ssize_t bytes_recv;
	int rc;
	char abuffer[64];
	/*daca requestul pentru socket a fost deja parsat ies din functie*/
	if (conn->after_parser == 1)
		return STATE_CONTINUE;
	rc = get_peer_address(conn->sockfd, abuffer, 64);
	if (rc < 0)
		goto remove_connection;
	bytes_recv = recv(conn->sockfd, conn->recv_buffer + conn->recv_len, BUFSIZ - conn->recv_len, 0);
	if (bytes_recv < 0)/* error in communication */
		goto remove_connection;
	if (bytes_recv == 0)/* connection closed */
		goto remove_connection;
	conn->recv_len += bytes_recv;
	/*cat timp este un raspund valid parsez requestul http*/
	if (strstr(conn->recv_buffer, "\r\n\r\n") != NULL) {
		/* HTTP request parser */
		http_parser_init(&request_parser, HTTP_REQUEST);
		http_parser_execute(&request_parser, &settings_on_path, conn->recv_buffer, strlen(conn->recv_buffer));
		conn->after_parser = 1;
		/*pun calea catre fisier in variabila path si incerc sa deschid fisierul */
		/* daca exista fisierul, pun in send_buffer mesajul de OK, si aflu lungimea fisierului */
		/* daca nu exista fisierul, pun in send_buffer mesajul de Eroare si notific strcutura de conectare la socket(schimb variabila err) */
		char path[BUFSIZ] = AWS_DOCUMENT_ROOT;

		strcat(path, request_path+1);
		conn->noufd = open(path, O_RDONLY);
		if ((conn)->noufd == -1) {
			conn->err = 1;
			memcpy((conn)->send_buffer, ERROR_MSG, strlen(ERROR_MSG));
			(conn)->send_len = strlen(ERROR_MSG);
		} else {
			stat(path, &conn->buffer);
			(conn)->filesize = conn->buffer.st_size;
			memcpy((conn)->send_buffer, OK_MSG, strlen(OK_MSG));
			(conn)->send_len = strlen(OK_MSG);
			}
		return STATE_DATA_RECEIVED;
		}
	return STATE_BAD_HTTP;

remove_connection:
	rc = w_epoll_remove_ptr(epollfd, conn->sockfd, conn);
	DIE(rc < 0, "w_epoll_remove_ptr");
	/* remove current connection */
	connection_remove(conn);
	return STATE_CONNECTION_CLOSED;
}
/*
 * Send message on socket.
 * Store message in send_buffer in struct connection.
 */
static enum connection_state send_message(struct connection *conn)
{
	ssize_t bytes_send;
	ssize_t bytes_sendfile;
	int rc;
	char abuffer[64];

	rc = get_peer_address(conn->sockfd, abuffer, 64);
	if (rc < 0)
		goto remove_connection;
	/* trimit mesajul ca exista sau nu fisierul cerut la socket  */
	/* daca mesajul este cel de eroare,opresc conexiunea cu socketul*/
	bytes_send = send(conn->sockfd, conn->send_buffer+conn->nr_bytes, conn->send_len, 0);
	if (conn->err == 1) {
		conn->state = 0;
		goto remove_connection;
	}
	conn->nr_bytes += bytes_send;
	conn->send_len -= bytes_send;
	/*dupa ce am trimis mesajul ca exista fisierul cerut, trimit continutul fisierul cu zero-copy */
	/*cand numarul de bytes cititi este egala cu numarul de bytes din fisier */
	/*(adica am trimis tot continutul) opresc conexiunea cu socketul */
	if (conn->send_len == 0) {
		bytes_sendfile = sendfile(conn->sockfd, conn->noufd, NULL, conn->filesize);
		DIE(bytes_sendfile < 0, "sendfile");
		conn->offset += bytes_sendfile;
		lseek(conn->noufd, conn->offset, SEEK_SET);
		conn->filesize -= bytes_sendfile;
		if (conn->offset ==  conn->buffer.st_size)
			goto remove_connection;
	}
	return 0;
remove_connection:

	rc = w_epoll_remove_ptr(epollfd, conn->sockfd, conn);
	DIE(rc < 0, "w_epoll_remove_ptr");
	/* remove current connection */
	connection_remove(conn);
	return STATE_CONNECTION_CLOSED;
}
/*
 * Handle a client request on a client connection.
 */
static void handle_client_request(struct connection *conn)
{
	int rc;
	enum connection_state ret_state;

	ret_state = receive_message(conn);
	if (ret_state == STATE_DATA_RECEIVED) {
		/* add socket to epoll for out events */
		rc = w_epoll_update_ptr_inout(epollfd, conn->sockfd, conn);
		DIE(rc < 0, "w_epoll_add_ptr_inout");
	}
}
int main(void)
{
	int rc;
	/* init multiplexing */
	epollfd = w_epoll_create();
	DIE(epollfd < 0, "w_epoll_create");
	/* create server socket */
	listenfd = tcp_create_listener(AWS_LISTEN_PORT, DEFAULT_LISTEN_BACKLOG);
	DIE(listenfd < 0, "tcp_create_listener");
	/* fac socketul sa fie non-blocant*/
	fcntl(listenfd, F_SETFL, O_RDWR | O_NONBLOCK);
	rc = w_epoll_add_fd_in(epollfd, listenfd);
	DIE(rc < 0, "w_epoll_add_fd_in");
	/* server main loop */
	while (1) {
		struct epoll_event rev;
		/* wait for events */
		rc = w_epoll_wait_infinite(epollfd, &rev);
		DIE(rc < 0, "w_epoll_wait_infinite");
		/*
		 * switch event types; consider
		 *   - new connection requests (on server socket)
		 *   - socket communication (on connection sockets)
		 */
		if (rev.data.fd == listenfd) {
			if (rev.events & EPOLLIN)
				handle_new_connection();
		} else {
			if (rev.events & EPOLLIN)
				handle_client_request(rev.data.ptr);
			if (rev.events & EPOLLOUT)
				send_message(rev.data.ptr);
		}
	}
	return 0;
}
