/**
 @file  unix.c
 @brief ENet Unix system specific functions
*/
#if defined(__wasi__)

#define ENET_BUILDING_LIB 1
#include "enet/enet.h"

// Global variable handled by STK
extern int isIPv6Socket(void);

int enet_initialize(void) { return 0; }

void enet_deinitialize(void) {}

enet_uint32 enet_host_random_seed(void) { }

enet_uint32 enet_time_get(void) {
  return 0;
}

void enet_time_set(enet_uint32 newTimeBase) {
}

int enet_address_set_host_ip(ENetAddress *address, const char *name) {
  return -1;
}

int enet_address_set_host(ENetAddress *address, const char *name) {
  return -1;
}

int enet_address_get_host_ip(const ENetAddress *address, char *name,
                             size_t nameLength) {
  return -1;
}

int enet_address_get_host(const ENetAddress *address, char *name,
                          size_t nameLength) {
  return -1;
}

int enet_socket_bind(ENetSocket socket, const ENetAddress *address) {
  return -1;  
}

int enet_socket_get_address(ENetSocket socket, ENetAddress *address) {
  return -1;
}

int enet_socket_listen(ENetSocket socket, int backlog) {
  return -1;
}

ENetSocket enet_socket_create(ENetSocketType type) {
  return ENET_SOCKET_NULL;
}

int enet_socket_set_option(ENetSocket socket, ENetSocketOption option,
                           int value) {
  return -1;
}

int enet_socket_get_option(ENetSocket socket, ENetSocketOption option,
                           int *value) {
  return -1;
}

int enet_socket_connect(ENetSocket socket, const ENetAddress *address) {
  return 0;
}

ENetSocket enet_socket_accept(ENetSocket socket, ENetAddress *address) {
  return ENET_SOCKET_NULL;
}

int enet_socket_shutdown(ENetSocket socket, ENetSocketShutdown how) {
  return -1;
}

void enet_socket_destroy(ENetSocket socket) {
  
}

int enet_socket_send(ENetSocket socket, const ENetAddress *address,
                     const ENetBuffer *buffers, size_t bufferCount) {
  return -1;
}

int enet_socket_receive(ENetSocket socket, ENetAddress *address,
                        ENetBuffer *buffers, size_t bufferCount) {
  return -1;
}

int enet_socketset_select(ENetSocket maxSocket, ENetSocketSet *readSet,
                          ENetSocketSet *writeSet, enet_uint32 timeout) {
  return -1;
}

int enet_socket_wait(ENetSocket socket, enet_uint32 *condition,
                     enet_uint32 timeout) {
  return -1;
}

#endif
