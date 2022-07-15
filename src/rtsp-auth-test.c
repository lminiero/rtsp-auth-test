#include <curl/curl.h>
#include <gst/gst.h>
#include <gst/rtsp-server/rtsp-server.h>

#define RTSP_URL "rtsp://localhost:9554/test"

static int start_rtsp_server(void);
static void stop_rtsp_server(void);
static int start_rtsp_client(void);

static volatile int server_started = 0;
static GMainLoop *loop = NULL;
static GThread *thread = NULL;

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
	gst_init(&argc, &argv);

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
	/* Create a new RTSP server on port 9554 using GStreamer */
	printf("[server] Starting...\n");
	loop = g_main_loop_new(NULL, FALSE);
	GstRTSPServer *server = gst_rtsp_server_new();
	GstRTSPMountPoints *mapping = gst_rtsp_server_get_mount_points(server);
	gst_rtsp_server_set_service(server, "9554");
	GstRTSPMediaFactory *factory = gst_rtsp_media_factory_new();
	gst_rtsp_media_factory_set_launch(factory,
		"( videotestsrc ! video/x-raw,width=640,height=480,framerate=15/1 ! videoconvert ! timeoverlay ! x264enc ! video/x-h264 ! rtph264pay name=pay0 pt=96 )");
	/* Setup the permissions */
	GstRTSPPermissions *permissions = gst_rtsp_permissions_new();
	gst_rtsp_permissions_add_role(permissions, "user",
		GST_RTSP_PERM_MEDIA_FACTORY_ACCESS, G_TYPE_BOOLEAN, TRUE,
		GST_RTSP_PERM_MEDIA_FACTORY_CONSTRUCT, G_TYPE_BOOLEAN, TRUE, NULL);
	gst_rtsp_media_factory_set_permissions(factory, permissions);
	gst_rtsp_permissions_unref(permissions);
	/* Enable basic authentication, as we'll need it for the test */
	GstRTSPAuth *auth = gst_rtsp_auth_new();
	GstRTSPToken *token = gst_rtsp_token_new(GST_RTSP_TOKEN_MEDIA_FACTORY_ROLE, G_TYPE_STRING, "user", NULL);
	gchar *basic = gst_rtsp_auth_make_basic("user", "password");
	gst_rtsp_auth_add_basic(auth, basic, token);
	g_free(basic);
	gst_rtsp_token_unref(token);
	gst_rtsp_server_set_auth(server, auth);
	g_object_unref(auth);
	/* Add a /test mountpoint to access the stream */
	gst_rtsp_mount_points_add_factory(mapping, "/test", factory);
	g_object_unref(mapping);
	gst_rtsp_server_attach(server, NULL);
	/* Let's mark the server as started */
	printf("[server] Started\n");
	g_atomic_int_set(&server_started, 1);
	/* Run the loop (and the server) */
	g_main_loop_run(loop);
	/* When this function returns, we're done */
	printf("[server] Stopped\n");
	return NULL;
}

static int start_rtsp_server(void) {
	/* Since the RTSP server will be run by Gstreamer in a dedicated loop,
	 * we spawn a dedicated thread and wait for the server to start */
	GError *error = NULL;
	thread = g_thread_try_new("rtsp-server", rtsp_server_thread, NULL, &error);
	if(error != NULL) {
		fprintf(stderr, "Error trying to launch the RTSP server thread: %d (%s)\n",
			error->code, error->message ? error->message : "??");
		g_error_free(error);
		return -1;
	}
	while(!g_atomic_int_get(&server_started))
		usleep(10000);
	/* Server started, wait a bit and start the client now */
	sleep(1);
	return 0;
}

static void stop_rtsp_server(void) {
	if(loop && g_main_loop_is_running(loop))
		g_main_loop_quit(loop);
	if(thread)
		g_thread_join(thread);
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
	rtsp_client_buffer *curldata = g_malloc(sizeof(rtsp_client_buffer));
	curldata->buffer = g_malloc0(1);
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
		g_free(curldata->buffer);
		g_free(curldata);
		return -1;
	}
	long code = 0;
	res = curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
	if(res != CURLE_OK) {
		fprintf(stderr, "Couldn't get DESCRIBE answer: %s\n", curl_easy_strerror(res));
		curl_easy_cleanup(curl);
		g_free(curldata->buffer);
		g_free(curldata);
		return -1;
	} else if(code != 200) {
		fprintf(stderr, "DESCRIBE failed: %ld\n", code);
		curl_easy_cleanup(curl);
		g_free(curldata->buffer);
		g_free(curldata);
		return -1;
	}

	/* Done */
	curl_easy_cleanup(curl);
	g_free(curldata->buffer);
	g_free(curldata);

	return 0;
}
