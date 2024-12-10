
#include "tinyos.h"
#include "kernel_socket.h"
#include "kernel_streams.h"
#include "kernel_pipe.h"
#include "kernel_proc.h"
#include "kernel_cc.h"

int socket_read(void *socket_cb_t, char *buf, unsigned int n) {
	
	if (socket_cb_t == NULL)
		return -1;

	socket_cb* socketcb = (socket_cb*) socket_cb_t;

	if (socketcb->type != SOCKET_PEER || socketcb->peer_s.read_pipe == NULL)
		return -1;
	
	return pipe_read(socketcb->peer_s.read_pipe, buf, n);
}

int socket_write(void *socket_cb_t, const char *buf, unsigned int n) {
	
	if (socket_cb_t == NULL)
		return -1;

	socket_cb* socketcb = (socket_cb*) socket_cb_t;

	if (socketcb->peer_s.write_pipe == NULL || socketcb->type != SOCKET_PEER)
		return -1;

	return pipe_write(socketcb->peer_s.write_pipe, buf, n);
}


int socket_close(void *socket_cb_t) {
	
	if (socket_cb_t == NULL)
		return -1;
	
	socket_cb* socketcb = (socket_cb*) socket_cb_t;

	if (socketcb->type == SOCKET_LISTENER) {

		while (! is_rlist_empty(&socketcb->listener_s.queue)){
			free(rlist_pop_front(&socketcb->listener_s.queue));
		}

		kernel_broadcast(&socketcb->listener_s.req_available);
		
		PORT_MAP[socketcb->port] = NULL;
	}
	else if (socketcb->type == SOCKET_PEER) {

		pipe_reader_close(socketcb->peer_s.read_pipe);
		pipe_writer_close(socketcb->peer_s.write_pipe);
	}
	// If SOCKET_UNBOUND or otherwise, ignore

	socketcb->refcount--;
	if (socketcb->refcount < 0) // No connections, free socket
		free(socketcb);

	return 0;
}


static file_ops socket_file_ops = {
	.Open = NULL,
	.Read = socket_read,
	.Write = socket_write,
	.Close = socket_close
};

Fid_t sys_Socket(port_t port)
{
	FCB* fcb;	
	Fid_t fid;

	if (port < 0 || port > MAX_PORT){  // Illegal Port
		
		return NOFILE;
	}	
		

	if (FCB_reserve(1, &fid, &fcb) == NULL){
		
		return NOFILE; // Failed to acquire FCBs & fids.
	}

	socket_cb* socketcb = (socket_cb*) xmalloc(sizeof(socket_cb)); // Allocate memory for socket_cb, terminates on failure

	socketcb->fcb = fcb;
	socketcb->refcount = 0;
	socketcb->port = port;
	socketcb->type = SOCKET_UNBOUND;

	fcb->streamobj = socketcb;
	fcb->streamfunc = &socket_file_ops;

	return fid;
	
}

int sys_Listen(Fid_t sock)
{
	if (sock == NOFILE || get_fcb(sock) == NULL || sock < 0 || sock > MAX_FILEID){  // Invalid fid
		
		return -1;
	} 
		
	socket_cb* socketcb = get_fcb(sock)->streamobj;

	if (PORT_MAP[socketcb->port] != NULL || socketcb->port == NOPORT || socketcb->type != SOCKET_UNBOUND){  // Rest error conditions described in tinyos.h
		
		return -1;
	} 

	// Install socket to the PORTMAP[]	
	PORT_MAP[socketcb->port] = socketcb; 
	
	socketcb->type = SOCKET_LISTENER;
	
	// Initialize listener
	socketcb->listener_s.req_available = COND_INIT;
	rlnode_init(&socketcb->listener_s.queue, NULL);

	return 0;
}


Fid_t sys_Accept(Fid_t lsock)
{
	if (lsock == NOFILE || lsock < 0 || lsock > MAX_FILEID || get_fcb(lsock) == NULL){ // Invalid fid
		return NOFILE;
	} 
		
	socket_cb* socketcb = get_fcb(lsock)->streamobj;

	if (socketcb == NULL || socketcb->type != SOCKET_LISTENER || PORT_MAP[socketcb->port] == NULL){  // Errors
		return NOFILE;
	} 
		
	socketcb->refcount++;

	// While Request Queue is empty & socket has not exited
	while (is_rlist_empty(&socketcb->listener_s.queue) && PORT_MAP[socketcb->port] != NULL){
		kernel_wait(&socketcb->listener_s.req_available, SCHED_PIPE);//SCHED_PIPE);
	} 
		
	
	if (PORT_MAP[socketcb->port] == NULL) { // Exited while waiting 
		socketcb->refcount--;

		if (socketcb->refcount < 0){ // den exei ksekatharistei
			free(socketcb);
		}

		return NOFILE;
	}

	connection_request* req = (rlist_pop_front(&socketcb->listener_s.queue))->request; // Get request from queue
	Fid_t listen_peer = sys_Socket(socketcb->port);

	if (listen_peer == NOFILE){
		socketcb->refcount--;	

		kernel_signal(&req->connected_cv);

		if (socketcb->refcount < 0){
			free(socketcb);
		}

		return NOFILE;
	}

	// Cache sockets for efficiency & visual clarity
	socket_cb* clientp = req->peer;
	socket_cb* listenp = get_fcb(listen_peer)->streamobj;

	PIPE_CB* p1 = initialize_socket_pipe();
	PIPE_CB* p2 = initialize_socket_pipe();

	clientp->peer_s.read_pipe = p1;
	clientp->peer_s.write_pipe = p2;

	listenp->peer_s.read_pipe = p2;
	listenp->peer_s.write_pipe = p1;

	clientp->type = SOCKET_PEER;
	listenp->type = SOCKET_PEER;

	clientp->peer_s.peer = listenp;
	listenp->peer_s.peer = clientp;

	req->admitted = 1;
	socketcb->refcount--;

	if (socketcb->refcount < 0){
		free(socketcb);
	}

	kernel_signal(&(req->connected_cv));

	return listen_peer;
}


int sys_Connect(Fid_t sock, port_t port, timeout_t timeout)
{
	if (sock == NOFILE || sock < 0 || sock > MAX_FILEID || get_fcb(sock) == NULL) // bad fid
		return NOFILE;

	socket_cb* client = get_fcb(sock)->streamobj;

	if (client == NULL || port < 1 || port > MAX_PORT || client->type != SOCKET_UNBOUND){
		return -1;
	}
	
	socket_cb* listener = PORT_MAP[port];
	if (listener == NULL || listener->type != SOCKET_LISTENER){
		return -1;
	}

	connection_request* req = (connection_request *) xmalloc(sizeof(connection_request));
	req->admitted = 0;

	rlnode_init(&req->queue_node, req);

	req->peer = client;
	req->connected_cv = COND_INIT;	
	rlist_push_back(&listener->listener_s.queue, &req->queue_node); //rlnode_init(&req->queue_node, &req)); // Add peer node to server waiting queue
	

	kernel_signal(&listener->listener_s.req_available); // signal to listener the initialization of peer connection

	client->refcount ++;
	 
	/*if (timeout > 0)
		kernel_timedwait(&(req->connected_cv), SCHED_PIPE, timeout);
	else
		kernel_wait(&(req->connected_cv), SCHED_PIPE);
	*/
	kernel_timedwait(&req->connected_cv, SCHED_PIPE, timeout); // Wait for connection, if timeout time passes, we stop waiting

	client->refcount --;

	if (client->refcount < 0){
		free(client);
	}
	
	// We want to keep return value after freeing the request node
	int ret = req->admitted ? 0 : -1; // 0 on successful connection, -1 on failure
	
	rlist_remove(&req->queue_node);
	free(req);

	return ret;
}


int sys_ShutDown(Fid_t sock, shutdown_mode how)
{
	if (sock == NOFILE || sock < 0 || sock > MAX_FILEID || get_fcb(sock) == NULL) // bad fid
		return NOFILE;

	socket_cb* socketcb = get_fcb(sock)->streamobj;

	if (socketcb == NULL || socketcb->type != SOCKET_PEER) // Error
		return -1;


	switch (how) { // Close pipes
		case SHUTDOWN_READ:
			pipe_reader_close(socketcb->peer_s.read_pipe);
			socketcb->peer_s.read_pipe = NULL;
			break;
		case SHUTDOWN_WRITE:
			pipe_writer_close(socketcb->peer_s.write_pipe);
			socketcb->peer_s.write_pipe = NULL;
			break;
		case SHUTDOWN_BOTH:
			pipe_writer_close(socketcb->peer_s.write_pipe);
			socketcb->peer_s.write_pipe = NULL;
			pipe_reader_close(socketcb->peer_s.read_pipe);
			socketcb->peer_s.read_pipe = NULL;
			break;
		default:
			break;
	}
	return 0;
}

