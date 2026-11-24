// SPDX-License-Identifier: BSD-3-Clause

#include "ring_buffer.h"
#include "packet.h"

static pthread_mutex_t ring_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t ring_not_full = PTHREAD_COND_INITIALIZER;
static pthread_cond_t ring_not_empty = PTHREAD_COND_INITIALIZER;

int ring_buffer_init(so_ring_buffer_t *ring, size_t cap)
{

	ring->data = malloc(cap);
	ring->cap = cap / PKT_SZ;

	ring->len = 0;
	ring->has_stopped = 0;
	ring->order = 0;
	ring->read_pos = 0;
	ring->write_pos = 0;
	ring->print_order = 0;

	return 0;
}

ssize_t ring_buffer_enqueue(so_ring_buffer_t *ring, void *data, size_t size)
{
	pthread_mutex_lock(&ring_mutex);

	while (!ring->has_stopped && ring->cap <= ring->len)
		pthread_cond_wait(&ring_not_full, &ring_mutex);

	if (ring->has_stopped) {
		pthread_mutex_unlock(&ring_mutex);
		return -1;
	}

	memcpy(&(ring->data[ring->write_pos]), data, PKT_SZ);

	ring->write_pos = (ring->write_pos + 1) % ring->cap;
	ring->len ++;
	pthread_cond_signal(&ring_not_empty);

	pthread_mutex_unlock(&ring_mutex);

	return (ssize_t)size;
}

ssize_t ring_buffer_dequeue(so_ring_buffer_t *ring, void *data, size_t size,
			    size_t *order)
{
	pthread_mutex_lock(&ring_mutex);

	while (ring->len == 0 && !ring->has_stopped) {
		pthread_cond_wait(&ring_not_empty, &ring_mutex);
	}

	if (ring->has_stopped && ring->len == 0) {
		pthread_mutex_unlock(&ring_mutex);
		return -1;
	}

	memcpy(data, &(ring->data[ring->read_pos]), PKT_SZ);


	ring->read_pos = (ring->read_pos + 1) % ring->cap;
	ring->len --;
	
	ring->order++;
	*order = ring->order;

	pthread_cond_signal(&ring_not_full);
	pthread_mutex_unlock(&ring_mutex);

	return (ssize_t)size;
}

void ring_buffer_destroy(so_ring_buffer_t *ring)
{
	free(ring->data);
	ring->data = NULL;
}

void ring_buffer_stop(so_ring_buffer_t *ring)
{
	pthread_mutex_lock(&ring_mutex);
	ring->has_stopped = 1;
	pthread_cond_broadcast(&ring_not_empty);
	pthread_mutex_unlock(&ring_mutex);
}
