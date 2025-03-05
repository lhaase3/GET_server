// compile: gcc server.c -o server -lpthread -g


#include <stdio.h>
#include <string.h>	
#include <stdlib.h>	
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h> 
#include <sys/stat.h>
#include <sys/types.h>
#include <signal.h>
#include <errno.h>

#define BUFSIZE 4096
#define ROOT "./www"

typedef struct {
	char *ext;
	char *mime;
} Content;

int socket_desc;
int server_running = 1;

/* Hash table containing all of the content types */
static const Content cont_table[] = {
	{".html", "text/html"}, {".txt", "text/plain"}, {".png", "image/png"}, {".gif", "image/gif"}, {".jpg", "image/jpg"}, {".ico", "image/x-icon"}, {".css", "text/css"}, {".js", "application/javascript"},
	{NULL, NULL}
};

/* handles when the user does ctrl+c */
void sigint_handler(int sig) {
	printf("\nExiting server...\n");
	server_running = 0;
	close(socket_desc);
}

/* goes through the hash table to find correct content type */
char* get_content_type(const char *path) {
	char *ext = strrchr(path, '.');
	for (int i = 0; cont_table[i].ext != NULL; i++) {
		if (strcmp(ext, cont_table[i].ext) == 0) return cont_table[i].mime;
	}
	return "unknown";
}

/* handles when a status code error occurs */
void error_handler(int sock, const char *status_code) {
	char response[1024];
	snprintf(response, sizeof(response), "HTTP/1.1 %s\r\nContent-Type: N/A\r\nContent-Length: 0\r\n\r\n", status_code);
	send(sock, response, strlen(response), 0);
}

void connection_handler(int sock) {
    char client_message[1024];
    char method[256], uri[256], version[256], header[256];
    char path[1024];
    char buf[BUFSIZE];
    ssize_t request_size;

    struct timeval timeout;
    timeout.tv_sec = 10;
    timeout.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    while (1) {
        request_size = recv(sock, client_message, sizeof(client_message) - 1, 0);

		/* https://man7.org/linux/man-pages/man2/recv.2.html?utm */
        if (request_size <= 0) {
            if (errno == EWOULDBLOCK || errno == EAGAIN) {
                printf("Timeout: Closing connection\n");
            }
            break;
        }

        if (sscanf(client_message, "%s %s %s", method, uri, version) != 3) {
			error_handler(sock, "400 Bad Request");
			break;
		}
        printf("Request: %s %s %s\n", method, uri, version);

		/* user enters in method other than GET */
        if (strcmp(method, "GET") != 0) {
            error_handler(sock, "405 Method Not Allowed");
            break;
        }

		/* user enters in version other than 1.0 or 1.1 */
        if (strcmp(version, "HTTP/1.1") != 0 && strcmp(version, "HTTP/1.0") != 0) {
            error_handler(sock, "505 HTTP Version Not Supported");
            break;
        }

		/* if user enters in / */
        if (strcmp(uri, "/") == 0 || strcmp(uri, "/inside/") == 0) {
            strcpy(uri, "/index.html");
        }

        snprintf(path, sizeof(path), "%s%s", ROOT, uri);

		/* https://man7.org/linux/man-pages/man2/stat.2.html */
        struct stat fd_status;

		// if the file does not exist then stat will return -1
		// S_ISDIR checks if the file is a directory
        if (stat(path, &fd_status) < 0 || S_ISDIR(fd_status.st_mode)) {
            error_handler(sock, "404 Not Found");
            break;
        }

		// performs a bitwise AND operation and if it returns 0 then it measn the read permission for others bit is not set
        if ((fd_status.st_mode & S_IROTH) == 0) {
            error_handler(sock, "403 Forbidden");
            break;
        }

        FILE *file = fopen(path, "rb");
        if (!file) {
            error_handler(sock, "404 Not Found");
            break;
        }

        char *content_type = get_content_type(path);
        snprintf(header, sizeof(header), "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %ld\r\n\r\n", content_type, fd_status.st_size);
        send(sock, header, strlen(header), 0);

        ssize_t bytes_read;
        while ((bytes_read = fread(buf, 1, sizeof(buf), file)) > 0) {
            send(sock, buf, bytes_read, 0);
        }

        fclose(file);

		// checking to see if 1.0 and if so close socket 
        if (strcmp(version, "HTTP/1.0") == 0) {
            printf("closing connection\n");
            close(sock); 
            return;
        }
    }

    close(sock);
}

/* sets up each client for their own thread */
void *client_handler(void *arg) {
	int sock = *((int *)arg);
	free(arg);
	connection_handler(sock);
	return NULL;
}

int main(int argc, char *argv[]) {
	int c, *new_sock, optval;
	struct sockaddr_in server, client;

	int port = atoi(argv[1]);

	/* handles the SIGINT from user */
	/* https://man7.org/linux/man-pages/man2/sigaction.2.html */
	struct sigaction exit;
	exit.sa_handler = sigint_handler;
	exit.sa_flags = 0; // defines the signal handler to default 
	sigaction(SIGINT, &exit, NULL);

	socket_desc = socket(AF_INET, SOCK_STREAM, 0);
	if (socket_desc == -1) {
		printf("Could not create socket\n");
		return 1;
	}
	puts("Socket created");

	optval = 1;
	setsockopt(socket_desc, SOL_SOCKET, SO_REUSEADDR, (const void *)&optval, sizeof(int));

	server.sin_family = AF_INET;
	server.sin_addr.s_addr = INADDR_ANY;
	server.sin_port = htons(port);

	if (bind(socket_desc, (struct sockaddr *)&server, sizeof(server)) < 0) {
		perror("Bind failed");
		return 1;
	}
	puts("Bind done");

	if (listen(socket_desc, 10) < 0) {
		puts("Listen failed");
		return 1;
	}
	puts("Server is listening...");

	c = sizeof(struct sockaddr_in);

	// loop that will continue to go and accept new clients while the server is still running
	while (server_running) {
		struct sockaddr_in client_addr;
		socklen_t client_addr_len = sizeof(client_addr);
		int *sock = malloc(sizeof(int));

		if (!sock) {
			puts("Memory allocation failed");
			continue;
		}

		*sock = accept(socket_desc, (struct sockaddr *)&client_addr, &client_addr_len);
		if (*sock < 0) {
			if (errno == EINTR) {
				free(sock);
				break;
			}
			free(sock);
			perror("Accept failed");
			continue;
		}

		puts("Connection accepted");

		pthread_t thread;
		if (pthread_create(&thread, NULL, client_handler, sock) != 0) {
			free(sock);
			perror("Thread creation failed");
			continue;
		}

		pthread_detach(thread);
	}

	close(socket_desc);
	return 0;
}