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

int pipe_write(void *pipecb_t, const char *buf, unsigned int size){

	// Get the PIPE_CB
	PIPE_CB *pipe_cb = (PIPE_CB *)pipecb_t;

	// Get the write position
	int *w_position = &pipe_cb->w_position;
	int bytes_written = 0;

	// Check if the writer is still alive
	if (pipe_cb->reader == NULL || pipe_cb->writer == NULL){
		
		return -1;
	}

	
	int empty_space = PIPE_BUFFER_SIZE - pipe_cb->current_size; // Declare here!// Static for the initial state.
	
	while (pipe_cb->current_size == PIPE_BUFFER_SIZE && pipe_cb->reader != NULL){
			
			kernel_broadcast(&pipe_cb->has_data);
			kernel_wait(&pipe_cb->has_space, SCHED_PIPE);
		}

	// If the reader or the writer are closed, return -1
	if (pipe_cb->reader == NULL){
			
			return -1;
		}
		
    // Write the data to the buffer
    while (bytes_written < size) {

    

    	// Recalculate empty_space if needed (e.g., if current_size changes mid-loop).
        if (pipe_cb->current_size != PIPE_BUFFER_SIZE - empty_space) {
            empty_space = PIPE_BUFFER_SIZE - pipe_cb->current_size;
        }

       
        int chunk_size = (size - bytes_written < empty_space) 
                         ? size - bytes_written 
                         : empty_space;

        int copy_size = (chunk_size < PIPE_BUFFER_SIZE - *w_position) 
                        ? chunk_size 
                        : PIPE_BUFFER_SIZE - *w_position;

        memcpy(&pipe_cb->buffer[*w_position], &buf[bytes_written], copy_size);
        //assert(pipe_cb->w_position < PIPE_BUFFER_SIZE);

        bytes_written += copy_size;
        pipe_cb->current_size += copy_size;
        *w_position = (*w_position + copy_size) % PIPE_BUFFER_SIZE;
	}

	// Wake up the reader
	kernel_broadcast(&pipe_cb->has_data);

	return bytes_written;
}

int pipe_read(void *pipecb_t, char *buf, unsigned int size){

	// Get the PIPE_CB
	PIPE_CB *pipe_cb = (PIPE_CB *)pipecb_t;

	// Get the read position
	int *r_position = &pipe_cb->r_position;
	int bytes_read = 0;

	if(pipe_cb->writer == NULL && pipe_cb->current_size == 0){

		return 0;
	}
	
	while (bytes_read < size){

		// If the buffer is empty, wait for the writer to write some data
	while (pipe_cb->current_size == 0 && pipe_cb->writer != NULL){

			//kernel_broadcast(&pipe_cb->has_space);
			kernel_wait(&pipe_cb->has_data, SCHED_PIPE);
		}

		// If the writer is closed, return the number of bytes read
		if (pipe_cb->writer == NULL){

			return bytes_read;
		}

		 // Calculate how much to read
        int chunk_size = (size - bytes_read < pipe_cb->current_size)
                         ? size - bytes_read
                         : pipe_cb->current_size;

        int copy_size = (chunk_size < PIPE_BUFFER_SIZE - pipe_cb->r_position)
                        ? chunk_size
                        : PIPE_BUFFER_SIZE - pipe_cb->r_position;

        // Copy data from the buffer
        memcpy(&buf[bytes_read], &pipe_cb->buffer[pipe_cb->r_position], copy_size);

        bytes_read += copy_size;
        pipe_cb->current_size -= copy_size;
        pipe_cb->r_position = (pipe_cb->r_position + copy_size) % PIPE_BUFFER_SIZE;

        // Wake up the writer if space is now available
       // kernel_broadcast(&pipe_cb->has_space);
        }

    kernel_broadcast(&pipe_cb->has_space);
    return bytes_read;
}

int pipe_writer_close(void *_pipecb)
{
	// Ensure that the pipe is valid
    PIPE_CB* pipe = (PIPE_CB*)_pipecb;

    if (pipe == NULL) {  // Invalid pointer
        return -1;  // Error, invalid pipe
    }

    // Close the writer
    pipe->writer = NULL;

    // If there is an active reader, broadcast to wake it up
    if (pipe->reader != NULL) {
        kernel_broadcast(&pipe->has_data);
    } else {
        // If no reader is available, free the pipe structure
        free(pipe);  // Free memory, as no reader is present
    }

    return 0;  // Success
}

int pipe_reader_close(void *_pipecb)
{
	PIPE_CB* pipe = (PIPE_CB*) _pipecb;

	if (pipe == NULL) // Invalid file id
		return -1;

	pipe->reader = NULL;
	
	// If there is an active writer, broadcast to wake it up
	if (pipe->writer != NULL) {
		kernel_broadcast(&pipe->has_space);
	} else{
		// If no writer is available, free the pipe structure
		free(pipe);
	}

	return 0;
}