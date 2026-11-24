// SPDX-License-Identifier: BSD-3-Clause

#include "ring_buffer.h"
#include "packet.h"

int ring_buffer_init(so_ring_buffer_t *ring, size_t cap)
{
	if (cap == 0)
		return -1;

	ring->data = malloc(cap);
	if (ring->data == NULL)
		return -1;

	ring->cap = cap;
	ring->len = 0;
	ring->has_stopped = 0;
	ring->order = 0;
	ring->read_pos = 0;
	ring->write_pos = 0;
	ring->print_order = 0;

	if (pthread_mutex_init(&ring->mutex, NULL) != 0)
		goto err_free;
	if (pthread_cond_init(&ring->not_full, NULL) != 0)
		goto err_mutex;
	if (pthread_cond_init(&ring->not_empty, NULL) != 0)
		goto err_full;

	return 0;

err_full:
	pthread_cond_destroy(&ring->not_full);
err_mutex:
	pthread_mutex_destroy(&ring->mutex);
err_free:
	free(ring->data);
	ring->data = NULL;
	return -1;
}

ssize_t ring_buffer_enqueue(so_ring_buffer_t *ring, void *data, size_t size)
{
	size_t first;

	if (size > ring->cap)
		return -1;

	pthread_mutex_lock(&ring->mutex);

	while (!ring->has_stopped && ring->cap - ring->len < size)
		pthread_cond_wait(&ring->not_full, &ring->mutex);

	if (ring->has_stopped) {
		pthread_mutex_unlock(&ring->mutex);
		return -1;
	}

	first = ring->cap - ring->write_pos;
	if (first > size)
		first = size;

	memcpy(ring->data + ring->write_pos, data, first);
	memcpy(ring->data, (char *)data + first, size - first);

	ring->write_pos = (ring->write_pos + size) % ring->cap;
	ring->len += size;
	pthread_cond_signal(&ring->not_empty);

	pthread_mutex_unlock(&ring->mutex);

	return (ssize_t)size;
}

ssize_t ring_buffer_dequeue(so_ring_buffer_t *ring, void *data, size_t size,
			    size_t *order)
{
	size_t first;

	if (size > ring->cap)
		return -1;

	pthread_mutex_lock(&ring->mutex);

	while (ring->len < size && !ring->has_stopped)
		pthread_cond_wait(&ring->not_empty, &ring->mutex);

	/*
	 * Stopped only means no more packets will be added. Serve whatever is
	 * still queued; give up only when the buffer is really empty.
	 */
	if (ring->len < size) {
		pthread_mutex_unlock(&ring->mutex);
		return -1;
	}

	first = ring->cap - ring->read_pos;
	if (first > size)
		first = size;

	memcpy(data, ring->data + ring->read_pos, first);
	memcpy((char *)data + first, ring->data, size - first);

	ring->read_pos = (ring->read_pos + size) % ring->cap;
	ring->len -= size;

	/* Tickets are handed out in dequeue (that is, input) order. */
	ring->order++;
	*order = ring->order;

	pthread_cond_signal(&ring->not_full);
	pthread_mutex_unlock(&ring->mutex);

	return (ssize_t)size;
}

void ring_buffer_destroy(so_ring_buffer_t *ring)
{
	pthread_cond_destroy(&ring->not_empty);
	pthread_cond_destroy(&ring->not_full);
	pthread_mutex_destroy(&ring->mutex);
	free(ring->data);
	ring->data = NULL;
}

void ring_buffer_stop(so_ring_buffer_t *ring)
{
	pthread_mutex_lock(&ring->mutex);
	ring->has_stopped = 1;
	pthread_cond_broadcast(&ring->not_empty);
	pthread_cond_broadcast(&ring->not_full);
	pthread_mutex_unlock(&ring->mutex);
}
