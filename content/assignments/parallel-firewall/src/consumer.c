// SPDX-License-Identifier: BSD-3-Clause

#include <pthread.h>
#include <fcntl.h>
#include <unistd.h>

#include "consumer.h"
#include "ring_buffer.h"
#include "packet.h"
#include "utils.h"

/*
 * All consumers share the output file and the print order, so these live at
 * file scope and are initialized statically. `ring->print_order` is only
 * ever touched while holding print_mutex.
 */
static pthread_mutex_t print_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t print_cond = PTHREAD_COND_INITIALIZER;
static so_consumer_ctx_t consumer_ctx;

void *consumer_thread(void *arg)
{
	so_consumer_ctx_t *ctx = arg;
	so_ring_buffer_t *ring = ctx->producer_rb;
	so_packet_t pkt;
	char out_buf[PKT_SZ];
	size_t order = 0;
	ssize_t ret;
	int len;
	so_action_t action;
	unsigned long hash;
	unsigned long timestamp;

	for (;;) {
		/* The dequeue also returns this packet's position in the input. */
		ret = ring_buffer_dequeue(ring, &pkt, sizeof(pkt), &order);
		if (ret <= 0)
			break;

		/* Do the expensive work with no lock held, so consumers run in parallel. */
		action = process_packet(&pkt);
		hash = packet_hash(&pkt);
		timestamp = pkt.hdr.timestamp;

		len = snprintf(out_buf, sizeof(out_buf), "%s %016lx %lu\n",
			       RES_TO_STR(action), hash, timestamp);

		pthread_mutex_lock(&print_mutex);

		/*
		 * order is 1-based, so the packet that may write now is
		 * print_order + 1.
		 */
		while (order != ring->print_order + 1)
			pthread_cond_wait(&print_cond, &print_mutex);

		if (write(ctx->out_fd, out_buf, len) != len) {
			pthread_mutex_unlock(&print_mutex);
			DIE(1, "write");
		}

		ring->print_order++;
		/*
		 * Broadcast rather than signal: the woken thread is not
		 * necessarily the one whose ticket is next, and no other wakeup
		 * would be coming for it.
		 */
		pthread_cond_broadcast(&print_cond);
		pthread_mutex_unlock(&print_mutex);
	}

	return NULL;
}

int create_consumers(pthread_t *tids,
		     int num_consumers,
		     struct so_ring_buffer_t *rb,
		     const char *out_filename)
{
	int rc;
	int i;

	consumer_ctx.producer_rb = rb;

	/* One descriptor, opened once, shared by every consumer. */
	consumer_ctx.out_fd = open(out_filename,
				   O_WRONLY | O_CREAT | O_TRUNC, 0666);
	DIE(consumer_ctx.out_fd < 0, "open output");

	for (i = 0; i < num_consumers; i++) {
		rc = pthread_create(&tids[i], NULL, consumer_thread, &consumer_ctx);
		if (rc != 0) {
			errno = rc;
			DIE(rc != 0, "pthread_create");
		}
	}

	return i;
}

void destroy_consumers(void)
{
	if (consumer_ctx.out_fd >= 0)
		close(consumer_ctx.out_fd);
	consumer_ctx.out_fd = -1;
}
