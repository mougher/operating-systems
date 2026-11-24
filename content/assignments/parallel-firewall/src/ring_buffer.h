/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef __SO_RINGBUFFER_H__
#define __SO_RINGBUFFER_H__

#include <sys/types.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>

typedef struct so_ring_buffer_t {
	char *data;

	size_t read_pos;
	size_t write_pos;

	size_t len;
	size_t cap;		/* size of data, in bytes */

	char has_stopped;

	/* Packet counters used to write the log in input order. */
	size_t order;		/* last ticket handed out by dequeue */
	size_t print_order;	/* last ticket written to the log */

	pthread_mutex_t mutex;
	pthread_cond_t not_full;
	pthread_cond_t not_empty;
} so_ring_buffer_t;

int     ring_buffer_init(so_ring_buffer_t *rb, size_t cap);
ssize_t ring_buffer_enqueue(so_ring_buffer_t *rb, void *data, size_t size);
ssize_t ring_buffer_dequeue(so_ring_buffer_t *ring, void *data, size_t size,
			    size_t *order);
void    ring_buffer_destroy(so_ring_buffer_t *rb);
void    ring_buffer_stop(so_ring_buffer_t *rb);

#endif /* __SO_RINGBUFFER_H__ */
