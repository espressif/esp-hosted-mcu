/*
 * SPDX-FileCopyrightText: 2015-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "serial_if.h"
#include "serial_ll_if.h"
#include "h_wrapper.h"

DEFINE_LOG_TAG(serial);

struct serial_drv_handle_t {
	int handle; /* dummy variable */
};

static serial_ll_handle_t * serial_ll_if_g;
static void * readSemaphore;


static void rpc_rx_indication(void);

/* Global serial handle - shared by RPC RX and TX threads */
static struct serial_drv_handle_t* g_serial_drv_handle = NULL;

/* -------- Serial Drv ---------- */
struct serial_drv_handle_t* serial_drv_open(const char *transport)
{
	if (!transport) {
		H_LOGE(TAG, "Invalid parameter in open");
		return NULL;
	}

	/* Return existing handle if already opened */
	if(g_serial_drv_handle) {
		H_LOGD(TAG, "Serial already open, returning existing handle");
		return g_serial_drv_handle;
	}

	/* Allocate new handle */
	g_serial_drv_handle = (struct serial_drv_handle_t*) h_calloc
		(1,sizeof(struct serial_drv_handle_t));
	if (!g_serial_drv_handle) {
		H_LOGE(TAG, "Failed to allocate memory \n");
		return NULL;
	}

	H_LOGD(TAG, "Serial handle allocated");
	return g_serial_drv_handle;
}

int serial_drv_write (struct serial_drv_handle_t* serial_drv_handle,
	uint8_t* buf, int in_count, int* out_count)
{
	int ret = 0;
	if (!serial_drv_handle || !buf || !in_count || !out_count) {
		H_LOGE(TAG,"Invalid parameters in write\n\r");
		return H_ERR_INVALID_ARG;
	}

	if( (!serial_ll_if_g) ||
		(!serial_ll_if_g->fops) ||
		(!serial_ll_if_g->fops->write)) {
		H_LOGE(TAG,"serial interface not valid\n\r");
		return H_ERR_INVALID_ARG;
	}

	H_LOGV(TAG, "serial_write buf=%p len=%u", buf, in_count);
	ret = serial_ll_if_g->fops->write(serial_ll_if_g, buf, in_count);
	if (ret != H_OK) {
		*out_count = 0;
		H_LOGE(TAG,"Failed to write data\n\r");
		return H_FAIL;
	}

	*out_count = in_count;
	return H_OK;
}


uint8_t * serial_drv_read(struct serial_drv_handle_t *serial_drv_handle,
		uint32_t *out_nbyte)
{
	uint16_t init_read_len = 0;
	uint16_t rx_buf_len = 0;
	uint8_t* read_buf = NULL;
	int ret = 0;
	/* Any of `RPC_EP_NAME_EVT` and `RPC_EP_NAME_RSP` could be used,
	 * as both have same strlen in esp_hosted_transport.h */
	const char* ep_name = RPC_EP_NAME_RSP;
	uint8_t *buf = NULL;
	uint32_t buf_len = 0;


	if (!serial_drv_handle || !out_nbyte) {
		H_LOGE(TAG,"Invalid parameters in read\n\r");
		return NULL;
	}

	*out_nbyte = 0;

	if(!readSemaphore) {
		H_LOGE(TAG,"Semaphore not initialized\n\r");
		return NULL;
	}

	H_LOGV(TAG, "Wait for serial_ll_semaphore");
	h_sem_take(readSemaphore, -1);

	if( (!serial_ll_if_g) ||
		(!serial_ll_if_g->fops) ||
		(!serial_ll_if_g->fops->read)) {
		H_LOGE(TAG,"serial interface refusing to read\n\r");
		return NULL;
	}
	H_LOGV(TAG, "Starting serial_ll read");

	/* Get buffer from serial interface */
	read_buf = serial_ll_if_g->fops->read(serial_ll_if_g, &rx_buf_len);
	if ((!read_buf) || (!rx_buf_len)) {
		H_LOGE(TAG,"serial read failed\n\r");
		return NULL;
	}
	H_LOGV(TAG, "serial_read buf=%p len=%u", read_buf, rx_buf_len);

/*
 * Read Operation happens in two steps because total read length is unknown
 * at first read.
 *      1) Read fixed length of RX data
 *      2) Read variable length of RX data
 *
 * (1) Read fixed length of RX data :
 * Read fixed length of received data in below format:
 * ----------------------------------------------------------------------------
 *  Endpoint Type | Endpoint Length | Endpoint Value  | Data Type | Data Length
 * ----------------------------------------------------------------------------
 *
 *  Bytes used per field as follows:
 *  ---------------------------------------------------------------------------
 *      1         |       2         | Endpoint Length |     1     |     2     |
 *  ---------------------------------------------------------------------------
 *
 *  int_read_len = 1 + 2 + Endpoint length + 1 + 2
 */

	init_read_len = SIZE_OF_TYPE + SIZE_OF_LENGTH + strlen(ep_name) +
		SIZE_OF_TYPE + SIZE_OF_LENGTH;

	if(rx_buf_len < init_read_len) {
		h_free(read_buf);
		H_LOGE(TAG,"Incomplete serial buff, return\n");
		return NULL;
	}

	buf = h_calloc(1, init_read_len);
	if (!buf) {
		H_LOGE(TAG, "Failed to allocate memory");
		goto free_bufs;
	}

	h_memcpy(buf, read_buf, init_read_len);

	/* parse_tlv function returns variable payload length
	 * of received data in buf_len
	 **/
	ret = parse_tlv(buf, &buf_len);
	if (ret || !buf_len) {
		h_free(buf);
		H_LOGE(TAG,"Failed to parse RX data \n\r");
		goto free_bufs;
	}
	H_LOGV(TAG, "TLV parsed");

	if (rx_buf_len < (init_read_len + buf_len)) {
		H_LOGE(TAG,"Buf read on serial iface is smaller than expected len\n");
		h_free(buf);
		goto free_bufs;
	}

	if (rx_buf_len > (init_read_len + buf_len)) {
		H_LOGE(TAG,"Buf read on serial iface is smaller than expected len\n");
	}

	h_free(buf);
/*
 * (2) Read variable length of RX data:
 */
	buf = h_calloc(1, buf_len);
	if (!buf) {
		H_LOGE(TAG, "Failed to allocate memory");
		goto free_bufs;
	}

	h_memcpy((buf), read_buf+init_read_len, buf_len);

	h_free(read_buf);

	*out_nbyte = buf_len;
	H_LOGV(TAG, "Serial payload size(after removing TLV): %" PRIu32, *out_nbyte);
	return buf;

free_bufs:
	h_free(read_buf);
	h_free(buf);
	return NULL;
}

int serial_drv_close(struct serial_drv_handle_t** serial_drv_handle)
{
	if (!serial_drv_handle || !(*serial_drv_handle)) {
		H_LOGE(TAG,"Invalid parameter in close \n\r");
		return H_ERR_INVALID_ARG;
	}

	H_LOGD(TAG, "Freeing serial handle");
	h_free(*serial_drv_handle);
	*serial_drv_handle = NULL;
	g_serial_drv_handle = NULL;  /* Clear global so next open allocates fresh */

	return H_OK;
}

int rpc_platform_init(void)
{
	/* rpc semaphore */
	h_err_t ret = h_sem_create(H_MAX_SYNC_RPC_REQUESTS +
			H_MAX_ASYNC_RPC_REQUESTS, 1, &readSemaphore);
	assert(readSemaphore && ret == H_OK);

	/* grab the semaphore, so that task will be mandated to wait on semaphore */
	h_sem_take(readSemaphore, 0);

	serial_ll_if_g = serial_ll_init(rpc_rx_indication);
	if (!serial_ll_if_g) {
		H_LOGE(TAG,"Serial interface creation failed\n\r");
		assert(serial_ll_if_g);
		return H_FAIL;
	}
	if (H_OK != serial_ll_if_g->fops->open(serial_ll_if_g)) {
		H_LOGE(TAG,"Serial interface open failed\n\r");
		return H_FAIL;
	}
	return H_OK;
}

/* TODO: Why this is not called in transport_pserial_close() */
int rpc_platform_deinit(void)
{
	if (serial_ll_if_g) {
		if (H_OK != serial_ll_if_g->fops->close(serial_ll_if_g)) {
			H_LOGE(TAG,"Serial interface close failed\n\r");
			return H_FAIL;
		}
		/* serial_ll_close frees the handle, NULL our pointer */
		serial_ll_if_g = NULL;
	}

	if (readSemaphore) {
		h_sem_delete(readSemaphore);
		readSemaphore = NULL;
	}

	return H_OK;
}

static void rpc_rx_indication(void)
{
	/* heads up to rpc for read */
	if(readSemaphore) {
		h_sem_give(readSemaphore);
	}
}
