#include <stdlib.h>
#include <stdio.h>

#include "mongoose.h"
#include "ipq_stati.h"


static const char *standard_reply = "HTTP/1.1 200 OK\r\n"
"Content-Type: text/xml\r\n"
"Connection: close\r\n\n";

void stati_handler(struct mg_connection *conn,
                   const struct mg_request_info *ri, void *data)
{
    mg_printf(conn, "%s", standard_reply);
    const char *s =
"<?xml version=\"1.0\" encoding=\"utf-8\"?>"
"<!DOCTYPE svg>"
"<svg width=\"220px\" height=\"120px\" xmlns=\"http://www.w3.org/2000/svg\">"
  "<g>"
    "<text font-size=\"32\" x=\"25\" y=\"60\">"
         "Hello, World!"
    "</text>"
    "<rect x=\"100\" y=\"100\" width=\"100\" height=\"100\" style=\"fill:#00ff00\" />"
  "</g>"
"</svg>";
    mg_printf(conn, "%s", s);
}

