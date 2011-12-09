#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "mongoose.h"
#include "rtvd.h"


static const char *standard_reply = "HTTP/1.1 200 OK\r\n"
"Conntent-Type: text/html\r\n"
"Connection: close\r\n\r\n";
static struct mg_context *ctx;


extern void stati_handler(struct mg_connection *conn,
                   const struct mg_request_info *ri, void *data);
extern void stream_page_handler(struct mg_connection *conn,
                         const struct mg_request_info *ri, void *data);
extern void stream_info_handler(struct mg_connection *conn,
                   const struct mg_request_info *ri, void *data);
extern void stream_static_handler(struct mg_connection *conn,
                   const struct mg_request_info *ri, void *data);
extern void stream_pcr_handler(struct mg_connection *conn,
                   const struct mg_request_info *ri, void *data);

static void
test_error(struct mg_connection *conn, const struct mg_request_info *ri,
        void *user_data)
{
    const char *value;

    mg_printf(conn, "%s", standard_reply);
    mg_printf(conn, "Error: [%d]", ri->status_code);
}


//#########################################################################//
/*
 * This callback function is attached to the "/" and "/abc.html" URIs,
 * thus is acting as "index.html" file. It shows a bunch of links
 * to other URIs, and allows to change the value of program's
 * internal variable. The pointer to that variable is passed to the
 * callback function as arg->user_data.
 */
static void
show_index(struct mg_connection *conn,
		const struct mg_request_info *request_info,
		void *user_data)
{
	char		*value;
	const char	*host;

	/* Change the value of integer variable */
	value = mg_get_var(conn, "name1");
	if (value != NULL) {
		* (int *) user_data = atoi(value);
		free(value);

		/*
		 * Suggested by Luke Dunstan. When POST is used,
		 * send 303 code to force the browser to re-request the
		 * page using GET method. This prevents the possibility of
		 * the user accidentally resubmitting the form when using
		 * Refresh or Back commands in the browser.
		 */
		if (!strcmp(request_info->request_method, "POST")) {
			(void) mg_printf(conn, "HTTP/1.1 303 See Other\r\n"
				"Location: %s\r\n\r\n", request_info->uri);
			return;
		}
	}

	mg_printf(conn, "%s",
		"HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n"
		"<html><body><h1>Welcome to embedded example of Mongoose");
#if 0

	mg_printf(conn, " v. %s </h1><ul>", mg_version());

	mg_printf(conn, "<li><code>REQUEST_METHOD: %s "
	    "REQUEST_URI: \"%s\" QUERY_STRING: \"%s\""
	    " REMOTE_ADDR: %lx REMOTE_USER: \"(null)\"</code><hr>",
	    request_info->request_method, request_info->uri,
	    request_info->query_string ? request_info->query_string : "(null)",
	    request_info->remote_ip);
	mg_printf(conn, "<li>Internal int variable value: <b>%d</b>",
			* (int *) user_data);

	mg_printf(conn, "%s",
		"<form method=\"GET\">Enter new value: "
		"<input type=\"text\" name=\"name1\"/>"
		"<input type=\"submit\" value=\"set new value using GET method\"></form>");
	mg_printf(conn, "%s",
		"<form method=\"POST\">Enter new value: "
		"<input type=\"text\" name=\"name1\"/>"
		"<input type=\"submit\" "
		"value=\"set new value using POST method\"></form>");
		
		mg_printf(conn, "%s",
		"<li><a href=\"/Makefile\">Regular file (Makefile)</a><hr>"
		"<li><a href=\"/ssi_test.shtml\">SSI file "
			"(ssi_test.shtml)</a><hr>"
		"<li><a href=\"/users/joe/\">Wildcard URI example</a><hr>"
		"<li><a href=\"/not-existent/\">Custom 404 handler</a><hr>");

	host = mg_get_header(conn, "Host");
	mg_printf(conn, "<li>'Host' header value: [%s]<hr>",
	    host ? host : "NOT SET");
#endif

	mg_printf(conn, "<li>Upload file example. "
	    "<form method=\"post\" enctype=\"multipart/form-data\" "
	    "action=\"/post\"><input type=\"file\" name=\"file\">"
	    "<input type=\"submit\"></form>");

	mg_printf(conn, "%s", "</body></html>");
}

/*
 * This callback is attached to the URI "/post"
 * It uploads file from a client to the server. This is the demostration
 * of how to use POST method to send lots of data from the client.
 * The uploaded file is saved into "uploaded.txt".
 * This function is called many times during single request. To keep the
 * state (how many bytes we have received, opened file etc), we allocate
 * a "struct state" structure for every new connection.
 */
static void
show_post(struct mg_connection *conn,
		const struct mg_request_info *request_info,
		void *user_data)
{
    printf("post start\n");
	const char	*path = "ipq.tar.gz";
	FILE		*fp;

	mg_printf(conn, "HTTP/1.0 200 OK\nContent-Type: text/plain\n\n");

	/*
	 * Open a file and write POST data into it. We do not do any URL
	 * decoding here. File will contain form-urlencoded stuff.
	 */
	if ((fp = fopen(path, "wb+")) == NULL) {
		(void) fprintf(stderr, "Error opening %s: %s\n",
		    path, strerror(errno));
	} else if (fwrite(request_info->post_data,
	    request_info->post_data_len, 1, fp) != 1) {
		(void) fprintf(stderr, "Error writing to %s: %s\n",
		    path, strerror(errno));
	} else {
		/* Write was successful */
		(void) fclose(fp);
	}
    printf("post end\n");
}

//#########################################################################//

int main(int argc, char **argv)
{
    printf("rtvd %s\n", RTVD_VERSION);

    char *port = "8080";

    if (argc > 1)
        port = argv[1];

    char *webPath   = malloc(128);
    strcpy(webPath,"./"); 
    //if (get_conf_string("System", "WebServerPath", webPath) != RETURN_SUCCESS) {
    //    mg_set_option(ctx, "root", "./www");
   // } else {
    mg_set_option(ctx, "root", webPath);
   // }
    mg_set_option(ctx, "ports", port);
    //Test_InPutTs();
    mg_bind_to_uri(ctx, "/test", &show_post, "7");
    mg_bind_to_uri(ctx, "/stati", &stati_handler, "8");
    mg_bind_to_uri(ctx, "/s", &stream_page_handler, "9");
    mg_bind_to_uri(ctx, "/si", &stream_info_handler, "10");
    mg_bind_to_uri(ctx, "/ss", &stream_static_handler, "11");
    mg_bind_to_uri(ctx, "/pcr", &stream_pcr_handler, "12");

    mg_bind_to_error_code(ctx, 404, &test_error, NULL);
    ctx = mg_start();
    printf("Mongoose %s started on port(s) [%s], serving directory [%s]\n",
            mg_version(),
            mg_get_option(ctx, "listening_ports"),
            mg_get_option(ctx, "root"));
    while (1) {sleep(1);}
}

//停止webserver
void stop_webserver()
{
    mg_stop(ctx);//Stop server thread, and release the context.
}



