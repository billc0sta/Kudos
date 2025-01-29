#ifndef HTTP_H_
#define HTTP_H_ 
#include "includes.h"
#include "client_info.h" 
#include "request_info.h"
#include "headers.h"
typedef void (*request_handler) (http_request*, http_response*);

typedef struct {
  struct sockaddr_in addr; 
  uint16_t    port;
  ipv4_t      ip;
  SOCKET      sockfd;
  request_handler request_handler;
  request_handler error_handler; 
  struct client_group clients;
  http_constraints constraints;
} http_server;

int http_init(void);
int http_quit(void);
http_server* http_server_new(const char*, const char*, request_handler, http_constraints*);
int http_server_free(http_server*);
int http_server_set_error_handler(http_server*, request_handler);
int http_server_listen(http_server*);
http_constraints http_make_default_constraints();

/*
  I will write some usage notes here:
  - anything that isn't prefixed with `http` is not meant for the user
*/

#endif 
