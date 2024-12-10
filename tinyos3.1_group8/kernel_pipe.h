#ifndef __KERNEL_PIPE_H
#define __KERNEL_PIPE_H

#include "tinyos.h"
#include "kernel_streams.h"
#include "kernel_dev.h"
#include "kernel_cc.h"

#define PIPE_BUFFER_SIZE 16500

/**
 * @brief Structure representing a pipe control block.
 * 
 * This structure contains the necessary fields to manage a pipe, including
 * the reader and writer control blocks, condition variables for space and data,
 * positions for writing and reading, current size, and a buffer to hold the data.
 */
typedef struct pipe_control_block{
	FCB* reader;            /**< Pointer to the reader control block */
	FCB* writer;            /**< Pointer to the writer control block */

	CondVar has_space;      /**< Condition variable to signal availability of space in the pipe */
	CondVar has_data;       /**< Condition variable to signal availability of data in the pipe */

	int w_position;         /**< Position for writing in the buffer */
	int r_position;         /**< Position for reading from the buffer */
	int current_size;       /**< Current size of the data in the buffer */

	char buffer[PIPE_BUFFER_SIZE];   /**< Buffer to hold the data */
} PIPE_CB;

int sys_Pipe(pipe_t* pipe);

int pipe_write(void* pipecb_t, const char* buf, unsigned int size);

int pipe_read(void* pipecb_t, char* buf, unsigned int size);

int pipe_writer_close(void* _pipecb);

int pipe_reader_close(void* _pipecb);

PIPE_CB* initialize_socket_pipe();

#endif