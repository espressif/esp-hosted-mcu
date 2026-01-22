/*
 * SPDX-FileCopyrightText: 2015-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "mempool.h"
#include "port_esp_hosted_host_config.h"
#include "stats.h"
#include "esp_log.h"

#define MEMPOOL_DEBUG 1

static const char * MEM_TAG = "mpool";
#if H_MEM_STATS
#include "esp_log.h"
#endif

struct mempool * mempool_create(uint32_t block_size)
{
#ifdef H_USE_MEMPOOL
	struct mempool * new = (struct mempool *)g_h.funcs->_h_malloc(MEMPOOL_ALIGNED(sizeof(struct mempool)));

	if (!new) {
		ESP_LOGE(MEM_TAG, "Prob to create mempool size(%u)", MEMPOOL_ALIGNED(sizeof(struct mempool)));
		return NULL;
	}

	if (!IS_MEMPOOL_ALIGNED((long)new)) {

		ESP_LOGV(MEM_TAG, "Nonaligned");
		g_h.funcs->_h_free(new);
		new = (struct mempool *)g_h.funcs->_h_malloc(MEMPOOL_ALIGNED(sizeof(struct mempool)));
	}

	if (!new) {
		ESP_LOGE(MEM_TAG, "failed to create mempool size(%u)", MEMPOOL_ALIGNED(sizeof(struct mempool)));
		return NULL;
	}

	new->spinlock = g_h.funcs->_h_create_lock_mempool();

	new->block_size = MEMPOOL_ALIGNED(block_size);
	new->alloc_count = 0;
	new->free_count = 0;
	SLIST_INIT(&(new->head));


	ESP_LOGV(MEM_TAG, "Create mempool %p with block_size:%lu", new, (unsigned long int)block_size);
	return new;
#else
	return NULL;
#endif
}

void mempool_destroy(struct mempool* mp)
{
#ifdef H_USE_MEMPOOL
	void * node1 = NULL;
	int freed_count = 0;

	if (!mp)
		return;

	ESP_LOGD(MEM_TAG, "Destroy mempool %p: alloc_count=%lu free_count=%lu",
		mp, (unsigned long)mp->alloc_count, (unsigned long)mp->free_count);

	if (mp->alloc_count != mp->free_count) {
		ESP_LOGW(MEM_TAG, "MEMPOOL LEAK: %lu buffers allocated but only %lu freed (leaked: %ld buffers)",
			(unsigned long)mp->alloc_count, (unsigned long)mp->free_count,
			(long)(mp->alloc_count - mp->free_count));
	}

	while ((node1 = SLIST_FIRST(&(mp->head))) != NULL) {
		SLIST_REMOVE_HEAD(&(mp->head), entries);
		g_h.funcs->_h_free(node1);
		freed_count++;
	}
	ESP_LOGD(MEM_TAG, "Freed %d buffers from mempool free list", freed_count);
	SLIST_INIT(&(mp->head));

	g_h.funcs->_h_destroy_lock_mempool(mp->spinlock);
	g_h.funcs->_h_free(mp);
#endif
}

void * mempool_alloc(struct mempool* mp, int nbytes, int need_memset)
{
	void *buf = NULL;

#ifdef H_USE_MEMPOOL
	if (!mp || mp->block_size < nbytes)
		return NULL;


	g_h.funcs->_h_lock_mempool(mp->spinlock);


	if (!SLIST_EMPTY(&(mp->head))) {
		buf = SLIST_FIRST(&(mp->head));
		SLIST_REMOVE_HEAD(&(mp->head), entries);
		mp->alloc_count++;


	g_h.funcs->_h_unlock_mempool(mp->spinlock);



#if H_MEM_STATS
	h_stats_g.mp_stats.num_reuse++;
	ESP_LOGV(MEM_TAG, "%p: num_reuse: %lu", mp, (unsigned long int)(h_stats_g.mp_stats.num_reuse));
#endif
	} else {

		g_h.funcs->_h_unlock_mempool(mp->spinlock);

		buf = g_h.funcs->_h_malloc_align(MEMPOOL_ALIGNED(mp->block_size), MEMPOOL_ALIGNMENT_BYTES);
		if (buf) {
			g_h.funcs->_h_lock_mempool(mp->spinlock);
			mp->alloc_count++;
			g_h.funcs->_h_unlock_mempool(mp->spinlock);
		}
#if H_MEM_STATS
		h_stats_g.mp_stats.num_fresh_alloc++;
		ESP_LOGV(MEM_TAG, "%p: num_alloc: %lu", mp, (unsigned long int)(h_stats_g.mp_stats.num_fresh_alloc));
#endif
	}
#else
	buf = g_h.funcs->_h_malloc_align(MEMPOOL_ALIGNED(nbytes), MEMPOOL_ALIGNMENT_BYTES);
#endif
	ESP_LOGV(MEM_TAG, "alloc %u bytes at %p", nbytes, buf);

	if (buf && need_memset)
		g_h.funcs->_h_memset(buf, 0, nbytes);

	return buf;

}

void mempool_free(struct mempool* mp, void *mem)
{
	if (!mem)
		return;
#ifdef H_USE_MEMPOOL
	if (!mp)
		return;

	g_h.funcs->_h_lock_mempool(mp->spinlock);

	mp->free_count++;
	SLIST_INSERT_HEAD(&(mp->head), (struct mempool_entry *)mem, entries);

	g_h.funcs->_h_unlock_mempool(mp->spinlock);

#if H_MEM_STATS
	h_stats_g.mp_stats.num_free++;
	ESP_LOGV(MEM_TAG, "%p: num_ret: %lu", mp, (unsigned long int)(h_stats_g.mp_stats.num_free));
#endif

#else
	ESP_LOGV(MEM_TAG, "free at %p", mem);
	g_h.funcs->_h_free_align(mem);
#endif
}
