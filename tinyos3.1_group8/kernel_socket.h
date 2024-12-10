#ifndef __KERNEL_SOCKET_H
#define __KERNEL_SOCKET_H

#include "tinyos.h"
#include "kernel_pipe.h"
#include "kernel_streams.h"

// Declaration of socket_cb requires socket_type declarations & peer_socket_type requires declaration of socket_cb
typedef struct socket_control_block socket_cb; 

typedef enum socket_type {
	SOCKET_LISTENER,
	SOCKET_UNBOUND,
	SOCKET_PEER
} socket_type;

typedef struct listener_socket_type {
	rlnode queue;
	CondVar req_available;
} listener_socket;

typedef struct unbound_socket_type {
	rlnode unbound_socket;
} unbound_socket;

typedef struct peer_socket_type {
	socket_cb* peer;
	PIPE_CB* write_pipe;
	PIPE_CB* read_pipe;
} peer_socket;

typedef struct socket_connection_request {
	int admitted;
	socket_cb* peer;

	CondVar connected_cv;
	rlnode queue_node;
} connection_request;

socket_cb* PORT_MAP[MAX_PORT+1] = {NULL}; // Initialize all ports as null.

typedef struct socket_control_block {
	unsigned int refcount;
	FCB* fcb;
	socket_type type;
	port_t port;

	union {
		listener_socket listener_s;
		unbound_socket unbound_s;
		peer_socket peer_s;
	};
} socket_cb;

// Test below definitions, redundant?
int socket_close();
int socket_read();
int socket_write();

int grab_fid(FCB* fcb);
void initialize_Socket(port_t port, FCB* fcb);

Fid_t sys_Socket(port_t port);
int sys_Listen(Fid_t sock);
Fid_t sys_Accept(Fid_t lsock);
int sys_Connect(Fid_t sock, port_t port, timeout_t timeout);
int sys_ShutDown(Fid_t sock, shutdown_mode how);

#endif