/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef __SO_CONSUMER_H__
#define __SO_CONSUMER_H__

#include "ring_buffer.h"
#include "packet.h"
#include "stdio.h"
#include "packet.h"

typedef struct so_consumer_ctx_t {
	struct so_ring_buffer_t *producer_rb;
	FILE *outfp;

} so_consumer_ctx_t;

int create_consumers(pthread_t *tids,
		     int num_consumers,
		     so_ring_buffer_t *rb,
		     const char *out_filename);
void destroy_consumers(void);

#endif /* __SO_CONSUMER_H__ */
