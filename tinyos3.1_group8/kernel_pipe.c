#include "tinyos.h"
#include "kernel_pipe.h"
#include "kernel_dev.h"
#include "kernel_streams.h"
#include "kernel_cc.h"
#include "kernel_sched.h"

// File operations for pipe read
static file_ops pipe_read_file_ops = {
	.Open = NULL,
	.Read = pipe_read,
	.Write = NULL,
	.Close = pipe_reader_close};

// File operations for pipe write
static file_ops pipe_write_file_ops = {
	.Open = NULL,
	.Read = NULL,
	.Write = pipe_write,
	.Close = pipe_writer_close};


int sys_Pipe(pipe_t* pipe)
{
	// Allocate two FCBs
	Fid_t fid[2];
	FCB *fcb[2];

	// Reserve two FCBs
	if (FCB_reserve(2, fid, fcb) == 0)
	{
		return -1;
	}
	// Allocate a PIPE_CB
	PIPE_CB *pipe_cb = (PIPE_CB *)xmalloc(sizeof(PIPE_CB));

	// Initialize PIPE_CB
	pipe->read = fid[0];
	pipe->write = fid[1];
	pipe_cb->reader = fcb[0];
	pipe_cb->writer = fcb[1];

	pipe_cb->has_space = COND_INIT;
	pipe_cb->has_data = COND_INIT;

	pipe_cb->w_position = 0;
	pipe_cb->r_position = 0;
	pipe_cb->current_size = 0;

	fcb[0]->streamobj = pipe_cb;
	fcb[1]->streamobj = pipe_cb;
	fcb[0]->streamfunc = &pipe_read_file_ops;
	fcb[1]->streamfunc = &pipe_write_file_ops;

	return 0;
}

int pipe_read(void* pipecb_t, char *buf, unsigned int size) {

	int i = 0;
	int n = 0;

	PIPE_CB* pipe_cb = (PIPE_CB*) pipecb_t;

	if (pipe_cb->reader == NULL) // Reader has already exited
		return -1;
	else if (pipe_cb->writer == NULL && pipe_cb->current_size == 0) // Buffer is empty & writer has exited, can't read
		return 0;

	while (i < size) {

		while (pipe_cb->current_size == 0 && pipe_cb->writer != NULL) {
			kernel_broadcast(&pipe_cb->has_space);
			kernel_wait(&pipe_cb->has_data, SCHED_PIPE);
		}

		if (pipe_cb->writer == NULL && pipe_cb->current_size == 0) // Writer exited & no more characters to write from buffer, exit
			return i;

		if ((n = (size - i)) < pipe_cb->current_size)
			n = size - i;
		else
			n = pipe_cb->current_size;

		if (n >= (PIPE_BUFFER_SIZE - pipe_cb->r_position))
			n = PIPE_BUFFER_SIZE - pipe_cb->r_position;

		memcpy(&(buf[i]), &(pipe_cb->buffer[pipe_cb->r_position]),n);

		pipe_cb->r_position = (pipe_cb->r_position + n) % PIPE_BUFFER_SIZE;

		pipe_cb->current_size = pipe_cb->current_size - n;
		i = i + n;

		kernel_broadcast(&pipe_cb->has_space); // Signal empty space within loop, wait for write
	}

	kernel_broadcast(&pipe_cb->has_space);

	return i;
}

int pipe_write(void* pipecb_t, const char *buf, unsigned int size) {
	
	int i = 0;
	int n;

	PIPE_CB* pipe_cb = (PIPE_CB*) pipecb_t;

	if (pipe_cb->reader == NULL || pipe_cb->writer == NULL)
		return -1;

	while (i < size) {

		while (pipe_cb->current_size == PIPE_BUFFER_SIZE && pipe_cb->reader) {
			kernel_broadcast(&pipe_cb->has_data);
			kernel_wait(&pipe_cb->has_space, SCHED_PIPE);
		}

		if (pipe_cb->reader == NULL || pipe_cb->writer == NULL)
			return i;
		
		if ((n = (size - i)) < (PIPE_BUFFER_SIZE - pipe_cb->current_size))
			n = size - i;
		else
			n = PIPE_BUFFER_SIZE - pipe_cb->current_size;

		if (n >= (PIPE_BUFFER_SIZE - pipe_cb->w_position))
			n = PIPE_BUFFER_SIZE - pipe_cb->w_position;
		
		memcpy(&(pipe_cb->buffer[pipe_cb->w_position]), &(buf[i]), n);
		assert(pipe_cb->w_position < PIPE_BUFFER_SIZE); // Test

		pipe_cb->w_position = (pipe_cb->w_position + n) % PIPE_BUFFER_SIZE;

		pipe_cb->current_size = pipe_cb->current_size + n;
		i = i + n;
	}

	kernel_broadcast(&pipe_cb->has_data);

	return i;

}
int pipe_writer_close(void* _pipecb) { // Test failure on read?

	PIPE_CB* pipe_cb = (PIPE_CB*) _pipecb;
	if (pipe_cb == NULL) // Invalid file id
		return -1;

	pipe_cb->writer = NULL;
	
	if (pipe_cb->reader != NULL) {
		kernel_broadcast(&pipe_cb->has_data);
		return 0;
	}
	free(pipe_cb);
	return 0; //success
}

int pipe_reader_close(void* _pipecb) {

	PIPE_CB* pipe_cb = (PIPE_CB*) _pipecb;

	if (pipe_cb == NULL) // Invalid file id
		return -1;

	pipe_cb->reader = NULL;
	
	if (pipe_cb->writer != NULL) {
		kernel_broadcast(&pipe_cb->has_space);
		return 0;
	}
	free(pipe_cb);
	return 0;
}

PIPE_CB* initialize_socket_pipe() { // Initialization without FCB/Fid for socket use
  // Allocate memory for the PIPE_CB structure
    PIPE_CB* pipecb = (PIPE_CB *)xmalloc(sizeof(PIPE_CB));
    if (pipecb == NULL)
        return NULL;

    // Allocate memory for the FCB structures
    FCB* fcb[2];
    fcb[0] = (FCB *)xmalloc(sizeof(FCB)); // Allocate reader FCB
    fcb[1] = (FCB *)xmalloc(sizeof(FCB)); // Allocate writer FCB

    // Check for allocation failure and clean up if necessary
		if (fcb[0] == NULL || fcb[1] == NULL) {

    	if (fcb[0] != NULL) {
        free(fcb[0]); // Free allocated reader
    }
    	if (fcb[1] != NULL) {
        free(fcb[1]); // Free allocated writer
    }
    
    free(pipecb); // Free the PIPE_CB structure
    return NULL;  // Return NULL to indicate failure
}

    // Initialize the PIPE_CB fields
    pipecb->reader = fcb[0];
    pipecb->writer = fcb[1];
    pipecb->current_size = 0;
    pipecb->r_position = 0;
    pipecb->w_position = 0;
    pipecb->has_data = COND_INIT;
    pipecb->has_space = COND_INIT;

    return pipecb;
}
