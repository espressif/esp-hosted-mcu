/*
 * SPDX-FileCopyrightText: 2015-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifdef ESP_PLATFORM
#include "esp_log.h"
#include "sdkconfig.h"
#else
#include "h_wrapper.h"
#define ESP_LOGE(tag, fmt, ...) H_LOGE(tag, fmt, ##__VA_ARGS__)
#define ESP_LOGI(tag, fmt, ...) H_LOGI(tag, fmt, ##__VA_ARGS__)
#endif

#include "mempool.h"
#include "mempool_ll.h"

static const char *TAG = "HS_MP";

#define MEMPOOL_NAME_STR_SIZE            32

#define IS_MEMPOOL_ALIGNED(VAL, BYTES)   (!((VAL) & (BYTES - 1)))
#define MEMPOOL_ALIGNED(VAL, BYTES)      ((VAL) + (BYTES) -    \
		((VAL) & (BYTES - 1)))

typedef struct hosted_mempool_t {
	void * pool_mem;
	hosted_mempool_ll_t pool_ll;
	hosted_mempool_config_t config;
} hosted_mempool_t;

hosted_mempool_t * hosted_mempool_create(hosted_mempool_config_t * config)
{
	hosted_mempool_t * mempool = NULL;
	size_t pool_size = 0;
	size_t actual_block_size = 0;
	int i = 0;
	void * block = NULL;

	if (!config || !config->malloc || !config->calloc || !config->free ||
			!config->num_blocks || !config->block_size) {
		ESP_LOGE(TAG, "invalid config in mempool create");
		return NULL;
	}

	mempool = (hosted_mempool_t *)config->calloc(1, sizeof(hosted_mempool_t), HOSTED_MEM_CAP_NONE);
	if (!mempool) {
		ESP_LOGE(TAG, "failed to allocate mempool");
		return NULL;
	}

	mempool->config = *config;

	actual_block_size = config->block_size;
	if (config->alignment_in_bytes) {
		actual_block_size = MEMPOOL_ALIGNED(config->block_size, config->alignment_in_bytes);
	}

	pool_size = config->num_blocks * actual_block_size;

	if (config->pre_allocated_mem) {
		if (config->pre_allocated_mem_size < pool_size) {
			ESP_LOGE(TAG, "preallocated mem size is smaller than required");
			mempool->config.free(mempool);
			return NULL;
		}
		mempool->pool_mem = config->pre_allocated_mem;
	} else {
		mempool->pool_mem = config->malloc(pool_size, HOSTED_MEM_CAP_DMA);
		if (!mempool->pool_mem) {
			ESP_LOGE(TAG, "failed to allocate pool memory");
			mempool->config.free(mempool);
			return NULL;
		}
	}

	hosted_mempool_ll_init(&mempool->pool_ll);

	for (i = 0; i < config->num_blocks; i++) {
		block = (void *)((uint8_t *)mempool->pool_mem + (i * actual_block_size));
		hosted_mempool_ll_push(&mempool->pool_ll, block);
	}

	ESP_LOGI(TAG, "mempool created: blocks=%u, block_size=%u", (uint32_t)config->num_blocks, (uint32_t)config->block_size);

	return mempool;
}

void hosted_mempool_destroy(hosted_mempool_t *mempool)
{
	if (!mempool)
		return;

	if (!mempool->config.pre_allocated_mem && mempool->pool_mem) {
		mempool->config.free(mempool->pool_mem);
	}

	mempool->config.free(mempool);
}

void * hosted_mempool_alloc(hosted_mempool_t *mempool,
		size_t nbytes, uint8_t need_memset)
{
	void * mem = NULL;

	if (!mempool || nbytes > mempool->config.block_size)
		return NULL;

	mem = hosted_mempool_ll_pop(&mempool->pool_ll);

	if (mem && need_memset && mempool->config.memset) {
		mempool->config.memset(mem, 0, mempool->config.block_size);
	}

	return mem;
}

int hosted_mempool_free(hosted_mempool_t *mempool, void *mem)
{
	if (!mempool || !mem)
		return MEMPOOL_FAIL;

	hosted_mempool_ll_push(&mempool->pool_ll, mem);

	return MEMPOOL_OK;
}
