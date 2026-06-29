// http_server.h
// HTTP server with SPIFFS static file serving

#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

#include "esp_http_server.h"

// ============================================================
// Function Prototypes
// ============================================================

// Initialize the web server
void webserver_init(void);

// Get the server handle (for registering endpoints)
httpd_handle_t webserver_get_handle(void);

// Register a URI handler
void webserver_register_uri(httpd_uri_t *uri);

#endif // HTTP_SERVER_H