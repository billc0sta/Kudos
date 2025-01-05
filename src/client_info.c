#include "client_info.h"
#include "includes.h"

struct client_group make_client_group() {
  struct client_group clients;
  clients.cap = 0;
  clients.len = 0;
  clients.data = NULL;
  return clients;
}

int reset_client_info(struct client_info* client) {
  if (!client) {
    http_log(stderr, "[reset_client_info] passed NULL pointers for mandatory parameters.\n");
    return 1;
  }

  client->sockfd = INVALID_SOCKET;
  client->bufflen = 0;
  client->used = 0;
  client->addrlen = sizeof(client->addr);
  reset_request_info(&client->request, client->sockfd, client->addr);
  return 0;
}

struct client_info* add_client(struct client_group* clients, SOCKET sockfd, struct sockaddr_in* addr) {
  if (!clients || !addr) {
    http_log(stderr, "[add_client] passed NULL pointers for mandatory parameters.\n");
    return NULL;
  }

  const size_t len = clients->len;
  const size_t cap = clients->cap;
  if (len < cap) {
    struct client_info* data = clients->data;
    for (size_t i = 0; i < cap; ++i) {
      if (data[i].used == 0) {
        struct client_info* client = &data[i];
        client->addr = *addr;
        client->sockfd = sockfd;
        client->used = 1;
        reset_client_info(client);
        ++clients->len;
        return client;
      }
    }
  }

  else {
    size_t new_cap = cap == 0 ? 8 : cap * 2;
    struct client_info* new_data = calloc(new_cap, sizeof(struct client_info));
    if (new_data == NULL) {
      http_log(stderr, "[add_client] realloc() failed.\n");
      return NULL;
    }
    struct client_info* old_data = clients->data;
    size_t counter = 0;
    for (size_t i = 0; i < cap; ++i) {
      if (old_data[i].used == 1) {
        new_data[counter++] = old_data[i];
      }
    }
    free(old_data);
		
    clients->data = new_data;
    clients->cap = new_cap;
    struct client_info* client = &clients->data[counter++];
    client->addr = *addr;
    client->sockfd = sockfd;
    client->used = 1;
    clients->len = counter;
    make_request_info(&client->request, client->sockfd, client->addr);

    return client;
  }

  // unreachable 
  return NULL;
}

int drop_client(struct client_info* client) {
  if (!client) {
    http_log(stderr, "[drop_client] passed NULL pointers for mandatory parameters.\n");
    return 1;
  }

  closesocket(client->sockfd);
  client->sockfd = 0;
  client->bufflen = 0;
  client->used = 0;
  return 0;
}

int free_client_info(struct client_info* client) {
  if (!client) {
    http_log(stderr, "[free_client_info] passed NULL pointers for mandatory parameters.\n");
    return 1;
  }
  reset_client_info(client);
  free_request_info(&client->request);
  return 0; 
}

int free_clients_group(struct client_group* clients) {
  if (!clients) {
    http_log(stderr, "[free_clients] passed NULL pointers for mandatory parameters.\n");
    return 1;
  }
  size_t cap = clients->cap;
  for (size_t i = 0; i < cap; ++i) {
    free_client_info(&clients->data[i]);
  }
  free(clients->data);
  clients->cap = 0;
  clients->len = 0;
  clients->data = NULL;
  return 0;
}

int ready_clients(struct client_group* clients, SOCKET server_sockfd, fd_set* read) {
  if (!clients) {
    http_log(stderr, "[ready_clients] passed NULL pointers for mandatory parameters.\n");
    return 1;
  }
  const size_t cap = clients->cap;
  const struct client_info* data = clients->data;
  FD_ZERO(read);
  FD_SET(server_sockfd, read); 
  int max_socket = (int)server_sockfd;
  for (size_t i = 0; i < cap; ++i) {
    if (data[i].used == 1) {
      SOCKET sockfd = data[i].sockfd;
      FD_SET(sockfd, read);
      if (sockfd > max_socket) max_socket = (int)sockfd;
    }
  }
  struct timeval timeout;
  timeout.tv_sec = SELECT_SEC;
  timeout.tv_usec = SELECT_USEC;
  if (select(max_socket + 1, read, NULL, NULL, &timeout) < 0) {
    http_log(stderr, "[ready_clients] select() failed - %d.\n", GET_ERROR());
    return 1;
  }
  return 0;
}

int print_client_address(struct client_info* client) {
  if (!client) {
    http_log(stderr, "[print_client_address] passed NULL pointers for mandatory parameters.\n");
    return 1;
  }
  static char address[100];
  getnameinfo((struct sockaddr*)&client->addr, sizeof(client->addr), address, 100, NULL, 0, NI_NUMERICHOST);
  printf("the client's address: %s\n", address);
  return 0;
}
