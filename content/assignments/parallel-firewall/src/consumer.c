// SPDX-License-Identifier: BSD-3-Clause

#include <pthread.h>
#include <fcntl.h>
#include <unistd.h>
#include "consumer.h"
#include "ring_buffer.h"

#include "utils.h"

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
	
	while(1) {

		ssize_t ret;
		ret = ring_buffer_dequeue(ring, &pkt, sizeof(pkt), &order);
		if (ret <= 0)
			break;

		so_action_t action;
		action = process_packet(&pkt);
		unsigned long hash;
		hash = packet_hash(&pkt);
		unsigned long timestamp;
		timestamp = pkt.hdr.timestamp;

		snprintf(out_buf, sizeof(out_buf), "%s %016lx %lu\n",
			       RES_TO_STR(action), hash, timestamp);

		pthread_mutex_lock(&print_mutex);

		while (order != ring->print_order + 1) {
			pthread_cond_wait(&print_cond, &print_mutex);
		}

		fprintf(ctx->outfp,"%s", out_buf);
		fflush(ctx->outfp);

		ring->print_order++;
		
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
	consumer_ctx.producer_rb = rb;

	consumer_ctx.outfp = fopen(out_filename, "w");

	int i;
	for (i = 0; i < num_consumers; i++) {
		pthread_create(&tids[i], NULL, consumer_thread, &consumer_ctx);
	}

	return i;
}

void destroy_consumers(void)
{
	fclose(consumer_ctx.outfp);
}
