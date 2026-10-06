/* SPDX-License-Identifier: Apache-2.0 */
/*
 * DMA-capable allocation.  On ESP: MALLOC_CAP_DMA-backed.  On Linux:
 * aligned_alloc.  `_alloc_aligned` lets the caller request a stricter
 * boundary (e.g. 64 B for ESP32-P4 GDMA).  NOT ISR-safe.
 */

#ifndef EH_HOST_PORT_DMA_H_
#define EH_HOST_PORT_DMA_H_

#include "eh_host_port_config.h"
#include "eh_host_port_master_config.h"

#ifdef __cplusplus
extern "C" {
#endif

#if EH_HOST_PORT_HAS_DMA

#define HOSTED_MEM_ALIGNMENT_4      4
#define HOSTED_MEM_ALIGNMENT_32     32
#define HOSTED_MEM_ALIGNMENT_64     64

/* A PSRAM buffer is reached through the L2 cache: addr and len must be
 * whole lines, or the SPI/SDMMC driver rejects it. */
#if defined(CONFIG_EH_HOST_PORT_DMA_PREFER_SPIRAM) && \
    defined(CONFIG_CACHE_L2_CACHE_LINE_SIZE) && (CONFIG_CACHE_L2_CACHE_LINE_SIZE > 64)
#define HOSTED_MEM_ALIGNMENT    CONFIG_CACHE_L2_CACHE_LINE_SIZE
#else
#define HOSTED_MEM_ALIGNMENT    HOSTED_MEM_ALIGNMENT_64
#endif

/* Allocate at least `n` bytes of DMA-capable memory.  Word-aligned
 * (minimum 4-byte boundary).  Returns NULL on failure. */
void *eh_host_port_dma_alloc(size_t n);

/* As above but caller controls alignment.  `alignment` must be a
 * power of two and a multiple of sizeof(void *).  Returns NULL on
 * failure or invalid alignment. */
void *eh_host_port_dma_alloc_aligned(size_t n, size_t alignment);

/* Free a buffer returned by eh_host_port_dma_alloc{,_aligned}.  Passing
 * NULL is a no-op, matching libc free(). */
void  eh_host_port_dma_free(void *p);

#endif /* EH_HOST_PORT_HAS_DMA */

#ifdef __cplusplus
}
#endif

#endif /* EH_HOST_PORT_DMA_H_ */
