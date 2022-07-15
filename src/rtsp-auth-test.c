#include <errno.h>
#include <netdb.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <pthread.h>
#include <curl/curl.h>

#define RTSP_URL "rtsp://localhost:9554/test"

static int start_rtsp_server(void);
static void stop_rtsp_server(void);
static int start_rtsp_client(void);

static int server_fd = 0;
static int server_started = 0;
static pthread_t thread_id = 0;

typedef struct rtsp_client_buffer {
	char *buffer;
	size_t size;
} rtsp_client_buffer;
static size_t rtsp_curl_callback(void *payload, size_t size, size_t nmemb, void *data) {
	size_t realsize = size * nmemb;
	rtsp_client_buffer *buf = (struct rtsp_client_buffer *)data;
	/* (Re)allocate if needed */
	buf->buffer = realloc(buf->buffer, buf->size+realsize+1);
	/* Update the buffer */
	memcpy(&(buf->buffer[buf->size]), payload, realsize);
	buf->size += realsize;
	buf->buffer[buf->size] = 0;
	/* Done! */
	return realsize;
}

int main(int argc, char *argv[]) {
	/* Create an RTSP server */
	if(start_rtsp_server() < 0)
		return -1;

	/* Create an RTSP client */
	if(start_rtsp_client() < 0)
		return -1;

	/* When the client is done, we can get rid of the server */
	stop_rtsp_server();

	/* Done */
	exit(0);
}

static void *rtsp_server_thread(void *user_data) {
	/* We'll wait for a client to fake-talk RTSP to */
	int client_fd = 0;
	struct sockaddr_in addr;
	socklen_t addrlen = sizeof(addr);
	int bytes = 0;
	char buffer[1024];
	/* Let's mark the server as started */
	printf("[server] Started\n");
	server_started = 1;
	/* Run the loop (and the server) */
	while(server_started) {
		if(client_fd == 0) {
			/* Wait for a client to connect */
			client_fd = accept(server_fd, (struct sockaddr *)&addr, &addrlen);
			if(client_fd < 0) {
				fprintf(stderr, "Error accepting client connection: %d (%s)\n", errno, strerror(errno));
				break;
			}
		} else {
			bytes = recv(client_fd, buffer, sizeof(buffer), 0);
			if(bytes == 0) {
				/* Connection closed */
				break;
			} else if(bytes < 0) {
				fprintf(stderr, "Error receiving request from the client: %d (%s)\n", errno, strerror(errno));
				break;
			}
			/* We're dumb and assume we just received the whole message; we're
			 * not a real RTSP server either, so we just check if there's an
			 * "Authorization: Basic" header in it with what we expect */
			int cseq = 1;
			char *cseqh = strstr(buffer, "CSeq: ");
			if(cseqh)
				cseq = atoi(cseqh + strlen("CSeq: "));
			time_t t;
			time(&t);
			/* This is the base64-encoded version of user:password */
			char *auth = strstr(buffer, "Authorization: Basic dXNlcjpwYXNzd29yZA==");
			if(auth) {
				/* Authenticated, send a 200 */
				snprintf(buffer, sizeof(buffer)-1,
					"RTSP/1.0 200 OK\r\n"
					"CSeq: %d\r\n"
					"Content-Type: application/sdp\r\n"
					"Content-Base: %s/\r\n"
					"Server: Fake RTSP server\r\n"
					"Date: %s\r\n\r\n",
				cseq, RTSP_URL, ctime(&t));
			} else {
				/* Not authenticated, send a 401 */
				snprintf(buffer, sizeof(buffer)-1,
					"RTSP/1.0 401 Unauthorized\r\n"
					"CSeq: %d\r\n"
					"WWW-Authenticate: Basic realm=\"Fake RTSP Server\"\r\n"
					"Server: Fake RTSP server\r\n"
					"Date: %s\r\n\r\n",
				cseq, ctime(&t));
			}
			if(send(client_fd, buffer, strlen(buffer), 0) < 0) {
				fprintf(stderr, "Error sending response from the client: %d (%s)\n", errno, strerror(errno));
				break;
			}
		}
	}
	/* When the loop breaks, we're done */
	if(server_fd > 0) {
		shutdown(server_fd, SHUT_RDWR);
		close(server_fd);
	}
	if(client_fd > 0) {
		shutdown(client_fd, SHUT_RDWR);
		close(client_fd);
	}
	printf("[server] Stopped\n");
	return NULL;
}

static int start_rtsp_server(void) {
	/* We spawn a dumb TCP server that just mimics an actual RTSP server */
	printf("[server] Starting...\n");
	server_fd = socket(AF_INET, SOCK_STREAM, 0);
	if(server_fd < 0) {
		fprintf(stderr, "Error creating server socket: %d (%s)\n", errno, strerror(errno));
		return -1;
	}
	int opt_val = 1;
	setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt_val, sizeof opt_val);
	struct sockaddr_in addr = { 0 };
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_port = htons(9554);
	size_t addrlen = sizeof(addr);
	if(bind(server_fd, (struct sockaddr *)&addr, addrlen) < 0) {
		fprintf(stderr, "Error binding server socket: %d (%s)\n", errno, strerror(errno));
		close(server_fd);
		server_fd = -1;
		return -1;
	}
    if(listen(server_fd, 1) < 0) {
		fprintf(stderr, "Error listening on server socket: %d (%s)\n", errno, strerror(errno));
		close(server_fd);
		server_fd = -1;
		return -1;
	}
	/* Since we'll poll, we spawn a thread and run the server there */
	int res = pthread_create(&thread_id, NULL, rtsp_server_thread, NULL);
	if(res < 0) {
		fprintf(stderr, "Error trying to launch the RTSP server thread\n");
		shutdown(server_fd, SHUT_RDWR);
		close(server_fd);
		server_fd = -1;
		return -1;
	}
	while(!server_started)
		usleep(10000);
	/* Server started, start the client now */
	return 0;
}

static void stop_rtsp_server(void) {
	server_started = 0;
	if(thread_id)
		pthread_join(thread_id, NULL);
}

static int start_rtsp_client(void) {
	printf("[client] Starting\n");
	CURL *curl = curl_easy_init();
	if(curl == NULL) {
		fprintf(stderr, "Error initializing libcurl easy session\n");
		return -1;
	}
	curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
	curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 1L);
	curl_easy_setopt(curl, CURLOPT_URL, RTSP_URL);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 0L);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	/* Ugly workaround needed after curl 7.66 */
	curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS, CURLPROTO_RTSP);
	curl_easy_setopt(curl, CURLOPT_HTTP09_ALLOWED, 1L);
	/* Enable authentication */
	curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_ANY);
	curl_easy_setopt(curl, CURLOPT_USERNAME, "user");
	curl_easy_setopt(curl, CURLOPT_PASSWORD, "password");
	/* Prepare an RTSP DESCRIBE */
	rtsp_client_buffer *curldata = malloc(sizeof(rtsp_client_buffer));
	curldata->buffer = malloc(1);
	curldata->size = 0;
	curl_easy_setopt(curl, CURLOPT_RTSP_STREAM_URI, RTSP_URL);
	curl_easy_setopt(curl, CURLOPT_RTSP_REQUEST, (long)CURL_RTSPREQ_DESCRIBE);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, rtsp_curl_callback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, curldata);
	curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, rtsp_curl_callback);
	curl_easy_setopt(curl, CURLOPT_HEADERDATA, curldata);

	/* Start the client */
	printf("[client] Sending DESCRIBE\n");
	int res = curl_easy_perform(curl);
	if(res != CURLE_OK) {
		fprintf(stderr, "Couldn't send DESCRIBE request: %s\n", curl_easy_strerror(res));
		curl_easy_cleanup(curl);
		free(curldata->buffer);
		free(curldata);
		return -1;
	}
	long code = 0;
	res = curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
	if(res != CURLE_OK) {
		fprintf(stderr, "Couldn't get DESCRIBE answer: %s\n", curl_easy_strerror(res));
		curl_easy_cleanup(curl);
		free(curldata->buffer);
		free(curldata);
		return -1;
	} else if(code != 200) {
		fprintf(stderr, "DESCRIBE failed: %ld\n", code);
		curl_easy_cleanup(curl);
		free(curldata->buffer);
		free(curldata);
		return -1;
	}

	/* Done */
	curl_easy_cleanup(curl);
	free(curldata->buffer);
	free(curldata);

	return 0;
}
