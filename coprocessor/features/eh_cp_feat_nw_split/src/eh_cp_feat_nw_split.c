/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
/* LWIP packet filtering implementation */

#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_netif_net_stack.h"
#include "lwip/netif.h"
#include "esp_private/wifi.h"
#include "eh_cp_master_config.h"
#if EH_CP_FEAT_HOST_PS_READY
#include "eh_cp_feat_host_ps_apis.h"
#endif
#include "eh_nw_split_lwip_src_port_hook.h"
#include "eh_cp_feat_nw_split_events.h"
#include "eh_cp_feat_nw_split_apis.h"
#include "eh_cp_feat_wifi.h"
#include "eh_cp_feat_wifi_event.h"
#include "eh_log.h"
#include "eh_common.h"
#include "eh_cp_core.h"           /* EH_CP_FEAT_REGISTER */
#include "eh_common_caps.h"
#include "eh_cp_event.h"
#include "eh_cp_feat_nw_split.h"
#include "eh_cp_nw_split_ip_owner_fsm.h"

ESP_EVENT_DEFINE_BASE(ESP_HOSTED_CP_FEAT_NW_SPLIT_EVENT);

#if EH_CP_FEAT_NW_SPLIT_READY
static esp_err_t nw_split_auto_init(void)
{
    eh_cp_feat_nw_split_config_t cfg = EH_CP_FEAT_NW_SPLIT_CONFIG_INIT_DEFAULT();
    return eh_cp_feat_nw_split_init(&cfg);
}

#if EH_CP_FEAT_NW_SPLIT_AUTO_INIT
EH_CP_FEAT_REGISTER(nw_split_auto_init,
                   eh_cp_feat_nw_split_deinit,
                   "feat_nw_split", tskNO_AFFINITY, 100);
#endif
#else
static esp_err_t nw_split_auto_init(void)
{
    /* nw_split disabled by Kconfig — nothing to do */
    return ESP_OK;
}

#if EH_CP_FEAT_NW_SPLIT_AUTO_INIT
EH_CP_FEAT_REGISTER(nw_split_auto_init,
                   NULL,
                   "feat_nw_split", tskNO_AFFINITY, 100);
#endif
#endif

#if EH_CP_FEAT_NW_SPLIT_READY
#include "lwip/opt.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "lwip/etharp.h"
#include "lwip/prot/iana.h"
#include "lwip/prot/ip.h"
#include "lwip/prot/tcp.h"
#include "lwip/prot/udp.h"
#include "lwip/prot/icmp.h"
#include "lwip/tcp.h"
#include "lwip/udp.h"
#include "lwip/priv/tcp_priv.h"

#include "eh_cp_feat_nw_split.h"
#include "eh_check.h"

static const char *TAG = "ehcp_nw_split";

/* Kconfig DHCP owner: the boot owner, and whether the host may own the IP. */
static eh_cp_feat_nw_split_nw_stack_e s_dhcp_policy;

/* The core's default STA callback (frames to the host) and ours, which splits. */
static eh_cp_traffic_cb_t orig_wlan_rx_cb_for_host;
static eh_cp_traffic_cb_t nw_split_wlan_sta_rx_cb;

/* Coprocessor station netif */
static esp_netif_t *coprocessor_sta_netif = NULL;
static esp_netif_t *s_owned_sta_netif = NULL;

static bool nw_split_netif_input_ready(esp_netif_t *netif)
{
	void *netif_impl = esp_netif_get_netif_impl(netif);
	if (!netif_impl) {
		return false;
	}
	struct netif *lwip_netif = (struct netif *)netif_impl;
	return lwip_netif->input != NULL;
}

static bool nw_split_is_netif_ready_fast(esp_netif_t *netif)
{
	/* Verify lwip_netif->input non-NULL each call — cached flag is racy across reconnect. */
	return nw_split_netif_input_ready(netif);
}

static esp_netif_t *nw_split_get_active_sta_netif(void)
{
	/* Prefer currently-registered default STA netif — avoids stale ptr after WiFi reinit. */
	esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
	if (netif) {
		coprocessor_sta_netif = netif;
		return netif;
	}
	return coprocessor_sta_netif;
}
/* Port configuration */
#define MQTT_PORT 1883
#define WAKEUP_HOST_STRING "wakeup-host"
#define DEFAULT_IPERF_PORT 5001
#define MAX_ALLOWED_PORTS 10
#define MAX_PORT_STR_LEN 128


#if defined(CONFIG_ESP_HOSTED_DEFAULT_NETWORK_STACK_COPROCESSOR)
  #define DEFAULT_NW_STACK_TO_SEND NW_STACK_COPROCESSOR
#elif defined(CONFIG_ESP_HOSTED_DEFAULT_NETWORK_STACK_HOST)
  #define DEFAULT_NW_STACK_TO_SEND NW_STACK_HOST
#elif defined(CONFIG_ESP_HOSTED_DEFAULT_NETWORK_STACK_BOTH)
  #define DEFAULT_NW_STACK_TO_SEND NW_STACK_BOTH
#else
  #error "Select one of the NW_STACK to forward"
#endif

/* Port array definitions */
#define MAX_ALLOWED_TCP_SRC_PORTS 10
#define MAX_ALLOWED_UDP_SRC_PORTS 10
#define MAX_ALLOWED_TCP_DST_PORTS 10
#define MAX_ALLOWED_UDP_DST_PORTS 10

static uint16_t allowed_tcp_src_ports[MAX_ALLOWED_TCP_SRC_PORTS] = {0};
static uint16_t allowed_udp_src_ports[MAX_ALLOWED_UDP_SRC_PORTS] = {0};
static uint16_t allowed_tcp_dst_ports[MAX_ALLOWED_TCP_DST_PORTS] = {0};
static uint16_t allowed_udp_dst_ports[MAX_ALLOWED_UDP_DST_PORTS] = {0};
static int allowed_tcp_src_ports_count = 0;
static int allowed_udp_src_ports_count = 0;
static int allowed_tcp_dst_ports_count = 0;
static int allowed_udp_dst_ports_count = 0;

static esp_err_t nw_split_get_status(uint8_t iface, eh_cp_feat_nw_split_status_t *status);
static esp_err_t nw_split_set_config(uint8_t iface, const char *ip, const char *nm,
	const char *gw, const char *dns_ip, uint8_t dns_type);


/* Parse a comma-separated list of ports into an array; replaces the previous list. */
static int init_allowed_ports(const char *ports_str, int *ports_count, uint16_t *ports_array, int max_ports, const char *port_type) {
	/* Reset counter */
	*ports_count = 0;

	/* If no ports string provided, return */
	if (!ports_str || !ports_str[0]) {
		ESP_LOGI(TAG, "No %s ports configured", port_type);
		return 0;
	}

	uint32_t port = 0;
	bool digits = false;

	for (const char *c = ports_str; ; c++) {
		if (isdigit((unsigned char)*c)) {
			port = port * 10 + (uint32_t)(*c - '0');
			if (port > UINT16_MAX) {
				port = UINT16_MAX + 1;      /* stays invalid until the separator */
			}
			digits = true;
			continue;
		}
		if (*c != ',' && *c != '\0') {
			continue;                       /* spaces */
		}
		if (digits) {
			if (port == 0 || port > UINT16_MAX) {
				ESP_LOGE(TAG, "%s: invalid port in \"%s\"", port_type, ports_str);
			} else if (*ports_count == max_ports) {
				ESP_LOGE(TAG, "%s: more than %d ports, rest ignored", port_type, max_ports);
				break;
			} else {
				ports_array[(*ports_count)++] = (uint16_t)port;
			}
		}
		port = 0;
		digits = false;
		if (*c == '\0') {
			break;
		}
	}

	/* Log the results */
	ESP_LOGI(TAG, "Initialized %d allowed %s ports:", *ports_count, port_type);
	for (int i = 0; i < *ports_count; i++) {
		ESP_LOGI(TAG, "  - Port %d", ports_array[i]);
	}

	return 0;
}

/* Initialize TCP port forwarding rules */
static int init_allowed_tcp_ports(const char *ports_str_src, const char *ports_str_dst) {
	int ret1 = 0, ret2 = 0;

	ret1 = init_allowed_ports(ports_str_src, &allowed_tcp_src_ports_count,
				 allowed_tcp_src_ports, MAX_ALLOWED_TCP_SRC_PORTS, "tcp_src");
	ret2 = init_allowed_ports(ports_str_dst, &allowed_tcp_dst_ports_count,
				 allowed_tcp_dst_ports, MAX_ALLOWED_TCP_DST_PORTS, "tcp_dst");

	if (ret1) {
		ESP_LOGE(TAG, "Failed to initialize allowed TCP src ports");
		return ret1;
	}
	if (ret2) {
		ESP_LOGE(TAG, "Failed to initialize allowed TCP dst ports");
		return ret2;
	}
	return 0;
}

/* Initialize UDP port forwarding rules */
static int init_allowed_udp_ports(const char *ports_str_src, const char *ports_str_dst) {
	int ret1 = 0, ret2 = 0;

	ret1 = init_allowed_ports(ports_str_src, &allowed_udp_src_ports_count,
				 allowed_udp_src_ports, MAX_ALLOWED_UDP_SRC_PORTS, "udp_src");
	ret2 = init_allowed_ports(ports_str_dst, &allowed_udp_dst_ports_count,
				 allowed_udp_dst_ports, MAX_ALLOWED_UDP_DST_PORTS, "udp_dst");

	if (ret1) {
		ESP_LOGE(TAG, "Failed to initialize allowed udp src ports");
		return ret1;
	}
	if (ret2) {
		ESP_LOGE(TAG, "Failed to initialize allowed udp dst ports");
		return ret2;
	}
	return 0;
}

/* Configure host static port forwarding rules */
int eh_cp_feat_nw_split_config_host_static_port_forwarding_rules(
	const char *ports_str_tcp_src,
	const char *ports_str_tcp_dst,
	const char *ports_str_udp_src,
	const char *ports_str_udp_dst) {

	int ret1 = 0, ret2 = 0;

	ESP_LOGI(TAG, "Host reserved TCP src ports: %s",
			 ports_str_tcp_src && strlen(ports_str_tcp_src) > 0 ? ports_str_tcp_src : "none");
	ESP_LOGI(TAG, "Host reserved TCP dst ports: %s",
			 ports_str_tcp_dst && strlen(ports_str_tcp_dst) > 0 ? ports_str_tcp_dst : "none");
	ret1 = init_allowed_tcp_ports(ports_str_tcp_src, ports_str_tcp_dst);

	ESP_LOGI(TAG, "Host reserved UDP src ports: %s",
			 ports_str_udp_src && strlen(ports_str_udp_src) > 0 ? ports_str_udp_src : "none");
	ESP_LOGI(TAG, "Host reserved UDP dst ports: %s",
			 ports_str_udp_dst && strlen(ports_str_udp_dst) > 0 ? ports_str_udp_dst : "none");
	ret2 = init_allowed_udp_ports(ports_str_udp_src, ports_str_udp_dst);

	if (ret1) {
		ESP_LOGE(TAG, "Failed to initialize allowed TCP ports");
		return ret1;
	}
	if (ret2) {
		ESP_LOGE(TAG, "Failed to initialize allowed UDP ports");
		return ret2;
	}
	return 0;
}

/* Port checking functions */
static inline bool is_tcp_src_port_allowed(uint16_t port) {
	for (int i = 0; i < allowed_tcp_src_ports_count; i++) {
		if (port == allowed_tcp_src_ports[i]) {
			return true;
		}
	}
	return false;
}

static inline bool is_tcp_dst_port_allowed(uint16_t port) {
	for (int i = 0; i < allowed_tcp_dst_ports_count; i++) {
		if (port == allowed_tcp_dst_ports[i]) {
			return true;
		}
	}
	return false;
}

static inline bool is_udp_src_port_allowed(uint16_t port) {
	for (int i = 0; i < allowed_udp_src_ports_count; i++) {
		if (port == allowed_udp_src_ports[i]) {
			return true;
		}
	}
	return false;
}

static inline bool is_udp_dst_port_allowed(uint16_t port) {
	for (int i = 0; i < allowed_udp_dst_ports_count; i++) {
		if (port == allowed_udp_dst_ports[i]) {
			return true;
		}
	}
	return false;
}

/* Check if the packet is an MQTT wakeup packet */
static bool host_mqtt_wakeup_triggered(const void *payload, uint16_t payload_length)
{
	/* Check if payload contains "wakeup-host" string */
	if (payload_length >= strlen(WAKEUP_HOST_STRING) &&
			memcmp(payload, WAKEUP_HOST_STRING, strlen(WAKEUP_HOST_STRING)) == 0) {
		return true;
	}
	return false;
}

/* Recent port lookups: a flow keeps its route while its packets keep coming
 * (1 s after the last one), so lwIP's PCB lists are walked once per flow. */
#define PORT_CACHE_N      4
#define PORT_CACHE_TTL_US 1000000

typedef struct {
	int64_t  last_us;
	uint16_t port;
	bool     open;
} port_cache_t;

static port_cache_t s_tcp_cache[PORT_CACHE_N];
static port_cache_t s_udp_cache[PORT_CACHE_N];

static bool port_cache_hit(port_cache_t *c, uint16_t port, int64_t now, bool *open)
{
	for (int i = 0; i < PORT_CACHE_N; i++) {
		if (c[i].last_us && c[i].port == port && now - c[i].last_us < PORT_CACHE_TTL_US) {
			c[i].last_us = now;
			*open = c[i].open;
			return true;
		}
	}
	return false;
}

/* Replaces the same port's entry, else the least recently used. */
static void port_cache_put(port_cache_t *c, uint16_t port, bool open, int64_t now)
{
	port_cache_t *e = &c[0];

	for (int i = 0; i < PORT_CACHE_N && e->port != port; i++) {
		if (c[i].port == port || c[i].last_us < e->last_us) {
			e = &c[i];
		}
	}
	*e = (port_cache_t){ .last_us = now, .port = port, .open = open };
}

static bool is_local_tcp_port_open(uint16_t port)
{
	int64_t now = esp_timer_get_time();
	bool found = false;

	if (port_cache_hit(s_tcp_cache, port, now, &found)) {
		return found;
	}
	SYS_ARCH_DECL_PROTECT(old_level);
	SYS_ARCH_PROTECT(old_level);
	for (struct tcp_pcb *pcb = tcp_listen_pcbs.pcbs; pcb; pcb = pcb->next) {
		if (pcb->local_port == port) {
			found = true;
			break;
		}
	}
	SYS_ARCH_UNPROTECT(old_level);
	port_cache_put(s_tcp_cache, port, found, now);
	ESP_LOGD(TAG, "is_local_tcp_port (%u) open: %d", port, found);
	return found;
}

static bool is_local_udp_port_open(uint16_t port)
{
	int64_t now = esp_timer_get_time();
	bool found = false;

	if (port_cache_hit(s_udp_cache, port, now, &found)) {
		return found;
	}
	SYS_ARCH_DECL_PROTECT(old_level);
	SYS_ARCH_PROTECT(old_level);
	for (struct udp_pcb *pcb = udp_pcbs; pcb; pcb = pcb->next) {
		if (pcb->local_port == port) {
			found = true;
			break;
		}
	}
	SYS_ARCH_UNPROTECT(old_level);
	port_cache_put(s_udp_cache, port, found, now);
	ESP_LOGD(TAG, "is_local_udp_port (%u) open: %d", port, found);
	return found;
}

/* True for an IPv4 UDP datagram to the DHCP client port. RFC 2131 lets the
 * server unicast or broadcast its reply, so this looks past the L2 address. */
static bool nw_split_is_dhcp_client_reply(const void *frame_data, uint16_t frame_length)
{
	const struct eth_hdr *ethhdr = (const struct eth_hdr *)frame_data;

	if (ethhdr->type != PP_HTONS(ETHTYPE_IP) ||
	    frame_length < SIZEOF_ETH_HDR + IP_HLEN) {
		return false;
	}
	const struct ip_hdr *iphdr =
		(const struct ip_hdr *)((const u8_t *)frame_data + SIZEOF_ETH_HDR);
	uint16_t ihl = IPH_HL(iphdr) * 4;

	if (IPH_PROTO(iphdr) != IP_PROTO_UDP ||
	    (IPH_OFFSET(iphdr) & PP_HTONS(IP_OFFMASK)) != 0 ||
	    frame_length < SIZEOF_ETH_HDR + ihl + UDP_HLEN) {
		return false;
	}
	const struct udp_hdr *udphdr = (const struct udp_hdr *)((const u8_t *)iphdr + ihl);

	return lwip_ntohs(udphdr->dest) == LWIP_IANA_PORT_DHCP_CLIENT;
}

/* True for an IPv4 UDP datagram from the DHCP client port (DISCOVER/REQUEST/RELEASE). */
static bool nw_split_is_dhcp_client_msg(const void *frame_data, uint16_t frame_length)
{
	const struct eth_hdr *ethhdr = (const struct eth_hdr *)frame_data;

	if (frame_length < SIZEOF_ETH_HDR + IP_HLEN || ethhdr->type != PP_HTONS(ETHTYPE_IP)) {
		return false;
	}
	const struct ip_hdr *iphdr =
		(const struct ip_hdr *)((const u8_t *)frame_data + SIZEOF_ETH_HDR);
	uint16_t ihl = IPH_HL(iphdr) * 4;

	if (IPH_PROTO(iphdr) != IP_PROTO_UDP || frame_length < SIZEOF_ETH_HDR + ihl + UDP_HLEN) {
		return false;
	}
	const struct udp_hdr *udphdr = (const struct udp_hdr *)((const u8_t *)iphdr + ihl);

	return lwip_ntohs(udphdr->src) == LWIP_IANA_PORT_DHCP_CLIENT;
}

/* Bus RX on STA: host frames on their way to the air. While the CP owns the IP
 * the host's DHCP client must not reach the AP (esp_netif starts it on every
 * association): one client on air. ESP_ERR_NOT_FOUND: core sends it to the air. */
static esp_err_t nw_split_filter_bus_rx(void *buf, uint16_t len, void *eb)
{
	(void)eb;
	if (eh_cp_nw_split_ip_owner() == ESP_EXT_CAP_NW_SPLIT_DHCP_CP &&
	    nw_split_is_dhcp_client_msg(buf, len)) {
		ESP_LOGI(TAG, "host DHCP client frame dropped: the CP owns the IP");
		return ESP_OK;
	}
	return ESP_ERR_NOT_FOUND;
}

/* Filter and route packet based on port number and power saving state */
eh_cp_feat_nw_split_nw_stack_e eh_cp_feat_nw_split_filter_packet(void *frame_data, uint16_t frame_length)
{
	if (!frame_data || frame_length < SIZEOF_ETH_HDR) {
		return NW_STACK_INVALID;
	}

	eh_cp_feat_nw_split_nw_stack_e result = DEFAULT_NW_STACK_TO_SEND;
	struct eth_hdr *ethhdr = (struct eth_hdr *)frame_data;
	struct ip_hdr *iphdr;
	u8_t proto;
	u16_t dst_port = 0;
	u16_t src_port = 0;
	uint8_t is_host_power_saving = 0;

	/* C9: direct call — no ops registry */
    #if EH_CP_FEAT_HOST_PS_READY
    is_host_power_saving = (uint8_t)eh_cp_feat_host_ps_is_host_power_saving();
    #else
    is_host_power_saving = 0;
    #endif

	/* DHCP replies reach only the stack that owns the IP: one client on air. A
	 * broadcast OFFER/ACK would otherwise stay on the co-processor (rule below). */
	bool host_owns_ip = eh_cp_nw_split_ip_owner() == ESP_EXT_CAP_NW_SPLIT_DHCP_HOST;
	if (host_owns_ip && nw_split_is_dhcp_client_reply(frame_data, frame_length)) {
		return NW_STACK_HOST;
	}

	/* Check if the frame is a MAC broadcast */
	if (ethhdr->dest.addr[0] & 0x01) {
		result = NW_STACK_COPROCESSOR;
		return result;
	}

	/* Check if it's an IP packet */
	if (ethhdr->type == PP_HTONS(ETHTYPE_IP)) {
		/* Ensure we have enough data for IP header */
		if (frame_length < (SIZEOF_ETH_HDR + IP_HLEN)) {
			return NW_STACK_INVALID;
		}

		iphdr = (struct ip_hdr *)((u8_t *)frame_data + SIZEOF_ETH_HDR);
		proto = IPH_PROTO(iphdr);

		/* Check for TCP packet */
		if (proto == IP_PROTO_TCP) {
			/* Ensure we have enough data for TCP header */
			if (frame_length < (SIZEOF_ETH_HDR + IPH_HL(iphdr) * 4 + TCP_HLEN)) {
				return NW_STACK_INVALID;
			}

			struct tcp_hdr *tcphdr = (struct tcp_hdr *)((u8_t *)iphdr + IPH_HL(iphdr) * 4);
			src_port = lwip_ntohs(tcphdr->src);
			dst_port = lwip_ntohs(tcphdr->dest);

			ESP_LOGV(TAG, "TCP dst_port: %u, src_port: %u", dst_port, src_port);

			/* Check if TCP port is in the allowed list for host */
			if (is_tcp_src_port_allowed(src_port) || is_tcp_dst_port_allowed(dst_port)) {
				ESP_LOGV(TAG, "Priority tcp port traffic detected, forwarding to host");
				result = NW_STACK_HOST;
				return result;
			}

			/* Check for iperf port */
			else if (dst_port == DEFAULT_IPERF_PORT) {
				ESP_LOGV(TAG, "iperf pkt %u", DEFAULT_IPERF_PORT);
				if (is_local_tcp_port_open(dst_port)) {
					result = NW_STACK_COPROCESSOR;
					return result;
				} else if (!is_host_power_saving) {
					result = NW_STACK_HOST;
					return result;
				}
			}

			/* Check if TCP port is in the remote port range */
			else if (IS_REMOTE_TCP_PORT(dst_port)) {
				if (is_host_power_saving) {
					/* Special handling for MQTT packets that should wake up the host */
					if (src_port == MQTT_PORT) {
						u8_t *mqtt_payload = (u8_t *)tcphdr + TCPH_HDRLEN_BYTES(tcphdr);
						u8_t *frame_end = (u8_t *)frame_data + frame_length;
						u16_t mqtt_payload_length = mqtt_payload < frame_end ?
							(u16_t)(frame_end - mqtt_payload) : 0;

						if (host_mqtt_wakeup_triggered(mqtt_payload, mqtt_payload_length)) {
							ESP_LOGV(TAG, "Wakeup host: MQTT wakeup pkt");
							result = NW_STACK_HOST;
							return result;
						} else {
							/* drop any other host destined mqtt packet */
							result = NW_STACK_INVALID;
							ESP_LOGW(TAG, "mqtt pkt DROPPED dst %u src %u => lwip %u", dst_port, src_port, result);
							return result;
						}
					} else {
						result = NW_STACK_INVALID;
						ESP_LOGW(TAG, "host pkt dropped in power save (dst %u src %u)", dst_port, src_port);
						return result;
					}
				} else {
					/* As host is not sleeping, send packets freely */
					ESP_LOGV(TAG, "mqtt pkt -> host. src %u dst %u", src_port, dst_port);
					result = NW_STACK_HOST;
					return result;
				}
			}
			/* Check if TCP port is in the local port range */
			else if (IS_LOCAL_TCP_PORT(dst_port)) {
				result = NW_STACK_COPROCESSOR;
				ESP_LOGV(TAG, "mqtt pkt -> coprocessor. src %u dst %u", src_port, dst_port);
				return result;
			}

			return DEFAULT_NW_STACK_TO_SEND;
		}
		/* Check for UDP packet */
		else if (proto == IP_PROTO_UDP) {
			/* Ensure we have enough data for UDP header */
			if (frame_length < (SIZEOF_ETH_HDR + IPH_HL(iphdr) * 4 + UDP_HLEN)) {
				return NW_STACK_INVALID;
			}

			struct udp_hdr *udphdr = (struct udp_hdr *)((u8_t *)iphdr + IPH_HL(iphdr) * 4);
			src_port = lwip_ntohs(udphdr->src);
			dst_port = lwip_ntohs(udphdr->dest);

			ESP_LOGV(TAG, "UDP dst_port: %u, src_port: %u", dst_port, src_port);

			/* Check if UDP port is in the allowed list for host */
			if (is_udp_src_port_allowed(src_port) || is_udp_dst_port_allowed(dst_port)) {
				ESP_LOGV(TAG, "Priority udp port traffic detected, forwarding to host");
				result = NW_STACK_HOST;
				return result;
			}

			/* Check for iperf UDP port */
			else if (dst_port == DEFAULT_IPERF_PORT) {
				ESP_LOGV(TAG, "Detected iperf UDP packet on port %u", DEFAULT_IPERF_PORT);
				if (is_local_udp_port_open(dst_port)) {
					result = NW_STACK_COPROCESSOR;
					return result;
				} else if (!is_host_power_saving) {
					result = NW_STACK_HOST;
					return result;
				}
			}

			/* Special case for DHCP client port */
			else if (dst_port == LWIP_IANA_PORT_DHCP_CLIENT) {
				return host_owns_ip ? NW_STACK_HOST : NW_STACK_COPROCESSOR;
			}

			/* Check if UDP port is in the remote port range */
			else if (IS_REMOTE_UDP_PORT(dst_port)) {
				if (is_host_power_saving) {
					ESP_LOGW(TAG, "host pkt dropped in power save (dst %u src %u)", dst_port, src_port);
					result = NW_STACK_INVALID;
					return result;
				} else {
					result = NW_STACK_HOST;
					return result;
				}
			}
			/* Check if UDP port is in the local port range */
			else if (IS_LOCAL_UDP_PORT(dst_port)) {
				result = NW_STACK_COPROCESSOR;
				return result;
			}

			return DEFAULT_NW_STACK_TO_SEND;
		}
		/* Check for ICMP packet */
		else if (proto == IP_PROTO_ICMP) {
			/* Ensure we have enough data for ICMP header */
			/*if (frame_length < (SIZEOF_ETH_HDR + IPH_HL(iphdr) * 4 + ICMP_HLEN)) {
				return NW_STACK_INVALID;
			}*/

			ESP_LOGV(TAG, "new icmp packet");
			struct icmp_echo_hdr *icmphdr = (struct icmp_echo_hdr *)((u8_t *)iphdr + IPH_HL(iphdr) * 4);

			/* Handle echo requests (ping) */
			if (ICMPH_TYPE(icmphdr) == ICMP_ECHO) {
				/* ping request */
				result = NW_STACK_COPROCESSOR;
				return result;
			}
			/* Handle echo replies (ping response) */
			else if (ICMPH_TYPE(icmphdr) == ICMP_ER) {
				if (is_host_power_saving) {
					result = NW_STACK_COPROCESSOR;
					return result;
				} else {
					/* ping response */
					ESP_HEXLOGV("icmp_er", frame_data, frame_length, 64);
					result = NW_STACK_BOTH;
					return result;
				}
			}

			return DEFAULT_NW_STACK_TO_SEND;
		}
	}
	/* Check if it's an ARP packet */
	else if (ethhdr->type == PP_HTONS(ETHTYPE_ARP)) {
		/* Ensure we have enough data for ARP header */
		if (frame_length < (SIZEOF_ETH_HDR + SIZEOF_ETHARP_HDR)) {
			return NW_STACK_INVALID;
		}

		ESP_LOGV(TAG, "new arp packet");
		struct etharp_hdr *arphdr = (struct etharp_hdr *)((u8_t *)frame_data + SIZEOF_ETH_HDR);

		/* Handle ARP requests */
		if (arphdr->opcode == PP_HTONS(ARP_REQUEST)) {
			result = NW_STACK_COPROCESSOR;
			return result;
		}
		/* Handle ARP replies */
		else {
			if (is_host_power_saving) {
				result = NW_STACK_COPROCESSOR;
				return result;
			} else {
				ESP_HEXLOGV("arp_reply", frame_data, frame_length, 64);
				result = NW_STACK_BOTH;
				return result;
			}
		}
	}

	/* Default routing for other packet types */
	return result;
}

IRAM_ATTR esp_err_t eh_cp_feat_nw_split_wlan_sta_rx_callback(void *buffer, uint16_t len, void *eb)
{
	eh_cp_feat_nw_split_nw_stack_e stack_to_send = NW_STACK_HOST;
	void (*wifi_free_func)(void *data) = esp_wifi_internal_free_rx_buffer;
	assert(orig_wlan_rx_cb_for_host);

	if (!buffer || !eb) {
		if (eb && wifi_free_func) {
			ESP_LOGD(TAG, "drop wifi packet");
			wifi_free_func(eb);
		}
		return ESP_OK;
	}

	if (!orig_wlan_rx_cb_for_host) {
		ESP_LOGW(TAG, "orig_wlan_rx_cb_for_host is NULL, drop packet");
		if (eb && wifi_free_func) {
			wifi_free_func(eb);
		}
		return ESP_OK;
	}

	/* Filter and route the packet based on destination port */
	stack_to_send = eh_cp_feat_nw_split_filter_packet(buffer, len);

	switch (stack_to_send) {
		case NW_STACK_HOST:
			/* Use the default callback to send to host */
			ESP_LOGV(TAG, "Routing packet to HOST");
			return orig_wlan_rx_cb_for_host(buffer, len, eb);

		case NW_STACK_COPROCESSOR: {
			/* Send to local LWIP */
			ESP_LOGV(TAG, "Routing packet to COPROCESSOR");
			esp_netif_t *active_netif = nw_split_get_active_sta_netif();
			if (!active_netif || !esp_netif_is_netif_up(active_netif)
			    || !nw_split_is_netif_ready_fast(active_netif)) {
				ESP_LOGD(TAG, "coprocessor netif not ready, route to HOST");
				return orig_wlan_rx_cb_for_host(buffer, len, eb);
			}
			esp_netif_receive(active_netif, buffer, len, eb);
			break;
		}

		case NW_STACK_BOTH: {
			ESP_LOGV(TAG, "Routing packet to BOTH coprocessor & host");
			esp_netif_t *active_netif = nw_split_get_active_sta_netif();
			if (!active_netif || !esp_netif_is_netif_up(active_netif)) {
				ESP_LOGD(TAG, "coprocessor netif not ready, route to HOST only");
				return orig_wlan_rx_cb_for_host(buffer, len, eb);
			}

			/* Make a copy for the host */
			void *copy_buff = malloc(len);
			if (!copy_buff) {
				ESP_LOGE(TAG, "Failed to allocate memory for packet copy");
				if (eb && wifi_free_func) {
					wifi_free_func(eb);
				}
				return ESP_OK;
			}
			memcpy(copy_buff, buffer, len);

			/* Coprocessor LWIP gets original buffer + eb */
			if (!esp_netif_is_netif_up(active_netif) || !nw_split_is_netif_ready_fast(active_netif)) {
				ESP_LOGD(TAG, "coprocessor netif not ready, drop coprocessor part");
				if (eb && wifi_free_func) {
					wifi_free_func(eb);
				}
			} else {
			esp_netif_receive(active_netif, buffer, len, eb);
			/* netif frees eb after processing */
			}

			/* Host gets copy via orig callback */
			orig_wlan_rx_cb_for_host(copy_buff, len, NULL);
			break;
		}

		default:
			ESP_LOGD(TAG, "Packet filtering failed, drop packet");
			if (eb && wifi_free_func) {
				wifi_free_func(eb);
			}
			break;
	}

	return ESP_OK;
}

static uint32_t dhcp_owner_cap(void)
{
	switch (s_dhcp_policy) {
	case NW_STACK_COPROCESSOR: return ESP_EXT_CAP_NW_SPLIT_DHCP_CP;
	case NW_STACK_HOST:        return ESP_EXT_CAP_NW_SPLIT_DHCP_HOST;
	case NW_STACK_BOTH:        return ESP_EXT_CAP_NW_SPLIT_DHCP_BOTH;
	default:                   return ESP_EXT_CAP_NW_SPLIT_DHCP_NONE;
	}
}

static void nw_split_send_status_event(void);

/* The station as the CP Wi-Fi feature publishes it (a config switch is one link),
 * and its replay to a host that woke. */
static void on_cp_station(void *arg, esp_event_base_t base, int32_t id, void *data)
{
	switch (id) {
	case EH_CP_FEAT_WIFI_EVT_STA_CONNECTED_TO_AP:
		eh_cp_nw_split_ip_owner_link(true);
		break;
	case EH_CP_FEAT_WIFI_EVT_STA_DISCONNECTED_FROM_AP:
		eh_cp_nw_split_ip_owner_link(false);
		nw_split_send_status_event();
		break;
	case EH_CP_FEAT_WIFI_EVT_STA_STARTED: {
		/* A drop before the Wi-Fi feature ran (boot) was never published. */
		bool started, linked;

		eh_cp_feat_wifi_driver_state(&started, &linked, NULL);
		if (!linked) {
			eh_cp_nw_split_ip_owner_link(false);
		}
		break;
	}
	case EH_CP_FEAT_WIFI_EVT_STA_REPLAYED:
		eh_cp_nw_split_ip_owner_resync();
		break;
	default:
		break;
	}
}

static const int32_t s_station_ids[] = {
	EH_CP_FEAT_WIFI_EVT_STA_CONNECTED_TO_AP, EH_CP_FEAT_WIFI_EVT_STA_DISCONNECTED_FROM_AP,
	EH_CP_FEAT_WIFI_EVT_STA_REPLAYED, EH_CP_FEAT_WIFI_EVT_STA_STARTED,
};

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
							  int32_t event_id, void *event_data)
{
	if (event_base == IP_EVENT) {
		switch (event_id) {
			case IP_EVENT_STA_GOT_IP: {
				nw_split_send_status_event();
				break;
			}
			case IP_EVENT_STA_LOST_IP:
				/* The host shares this lease: tell it, even with the link still up. */
				nw_split_send_status_event();
				break;


			default:
				break;
		}
	}
}

static void nw_split_send_status_event(void)
{
	eh_cp_feat_nw_split_status_t status = {0};
	esp_err_t ret = ESP_OK;

	/* Get current network status */
	ret = nw_split_get_status(WIFI_IF_STA, &status);
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "Failed to get network status: %d", ret);
		return;
	}

	/* Send event using standard ESP event API */
	eh_cp_feat_nw_split_evt_t event_id = status.net_link_up ?
							   ESP_HOSTED_CP_FEAT_NW_SPLIT_EVT_NETWORK_UP :
							   ESP_HOSTED_CP_FEAT_NW_SPLIT_EVT_NETWORK_DOWN;

	ESP_LOGI(TAG, "Sending network %s event: iface=%d, IP=%s",
			 status.net_link_up ? "UP" : "DOWN",
			 status.iface,
			 status.dhcp_ip);

	/* Bounded: the IP-owner FSM calls this holding its lock, on the loop task. */
	ret = esp_event_post(ESP_HOSTED_CP_FEAT_NW_SPLIT_EVENT,
						event_id,
						&status,
						sizeof(eh_cp_feat_nw_split_status_t),
						pdMS_TO_TICKS(100));
	if (ret != ESP_OK) {
		ESP_LOGE(TAG, "Failed to post network event: %d", ret);
	}
}

void eh_cp_nw_split_replay_status(void)
{
	eh_cp_feat_nw_split_status_t status = {0};

	if (nw_split_get_status(WIFI_IF_STA, &status) != ESP_OK) {
		return;
	}

	/* Replay only when DHCP/IP is already assigned */
	if (status.dhcp_up) {
		nw_split_send_status_event();
	}
}

static esp_err_t nw_split_get_status(uint8_t iface, eh_cp_feat_nw_split_status_t *status)
{
	if (!status) {
		ESP_LOGE(TAG, "Invalid parameters");
		return ESP_FAIL;
	}

	esp_netif_t *netif = NULL;
	if (iface == WIFI_IF_STA) {
		netif = coprocessor_sta_netif ? coprocessor_sta_netif : esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
	} else if (iface == WIFI_IF_AP) {
		netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
	} else {
		ESP_LOGE(TAG, "Unsupported interface: %u", iface);
		return ESP_FAIL;
	}

	if (!netif) {
		ESP_LOGE(TAG, "Network interface not available");
		return ESP_FAIL;
	}

	/* Fill status structure */
	status->iface = iface;
	status->net_link_up = esp_netif_is_netif_up(netif) ? 1 : 0;

	/* Get IP info */
	esp_netif_ip_info_t ip_info = {0};
	if (esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
		status->dhcp_up = (ip_info.ip.addr != 0) ? 1 : 0;
		snprintf(status->dhcp_ip, sizeof(status->dhcp_ip), IPSTR, IP2STR(&ip_info.ip));
		snprintf(status->dhcp_nm, sizeof(status->dhcp_nm), IPSTR, IP2STR(&ip_info.netmask));
		snprintf(status->dhcp_gw, sizeof(status->dhcp_gw), IPSTR, IP2STR(&ip_info.gw));
	}

	/* Get DNS info */
	esp_netif_dns_info_t dns_info = {0};
	if (esp_netif_get_dns_info(netif, ESP_NETIF_DNS_MAIN, &dns_info) == ESP_OK) {
		status->dns_up = (dns_info.ip.u_addr.ip4.addr != 0) ? 1 : 0;
		snprintf(status->dns_ip, sizeof(status->dns_ip), IPSTR, IP2STR(&dns_info.ip.u_addr.ip4));
		status->dns_type = ESP_NETIF_DNS_MAIN;
	}

	ESP_LOGD(TAG, "Network status: iface=%u, link_up=%u, dhcp_up=%u, dns_up=%u",
			 status->iface, status->net_link_up, status->dhcp_up, status->dns_up);
	ESP_LOGD(TAG, "IP: %s, NM: %s, GW: %s, DNS: %s",
			 status->dhcp_ip, status->dhcp_nm, status->dhcp_gw, status->dns_ip);

	return ESP_OK;
}


static esp_err_t nw_split_set_config(uint8_t iface, const char *ip, const char *nm,
									 const char *gw, const char *dns_ip, uint8_t dns_type)
{
	if (!ip || !nm || !gw || !dns_ip) {
		ESP_LOGE(TAG, "Invalid parameters");
		return ESP_FAIL;
	}

	esp_netif_t *netif = NULL;
	if (iface == WIFI_IF_STA) {
		netif = coprocessor_sta_netif ? coprocessor_sta_netif : esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
	} else if (iface == WIFI_IF_AP) {
		netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
	} else {
		ESP_LOGE(TAG, "Unsupported interface: %u", iface);
		return ESP_FAIL;
	}

	if (!netif) {
		ESP_LOGE(TAG, "Network interface not available");
		return ESP_FAIL;
	}

	ESP_LOGI(TAG, "Setting network config: iface=%u, IP=%s, NM=%s, GW=%s, DNS=%s",
			 iface, ip, nm, gw, dns_ip);
	if (iface != WIFI_IF_STA) {
		return ESP_ERR_NOT_SUPPORTED;
	}

	esp_netif_ip_info_t ip_info = {0};
	ip_info.ip.addr = esp_ip4addr_aton(ip);
	ip_info.netmask.addr = esp_ip4addr_aton(nm);
	ip_info.gw.addr = esp_ip4addr_aton(gw);

	/* 0.0.0.0: no DNS server (set_dns_info rejects it). */
	esp_netif_dns_info_t dns_info = {0};
	dns_info.ip.u_addr.ip4.addr = esp_ip4addr_aton(dns_ip);
	dns_info.ip.type = ESP_IPADDR_TYPE_V4;

	return eh_cp_nw_split_ip_owner_host_lease(&ip_info, &dns_info, dns_type);
}

static esp_netif_t *eh_cp_feat_nw_split_init_coprocessor_netif(eh_cp_feat_nw_split_config_t *config)
{
	if (coprocessor_sta_netif) {
		ESP_LOGD(TAG, "Coprocessor netif already exists, reusing");
		return coprocessor_sta_netif;
	}

	/* Reuse app-created WIFI_STA_DEF if present — else netif->input NULL crash. */
	esp_netif_t *existing = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
	if (existing) {
		ESP_LOGI(TAG, "Reusing existing WIFI_STA_DEF netif");
		coprocessor_sta_netif = existing;
		return coprocessor_sta_netif;
	}

	/* With the DHCP client flag in every owner mode: the owner FSM runs or stops it. */
	esp_netif_inherent_config_t netif_cfg;
	memcpy(&netif_cfg, ESP_NETIF_BASE_DEFAULT_WIFI_STA, sizeof(netif_cfg));

	esp_netif_config_t cfg_sta = {
		.base = &netif_cfg,
		.stack = ESP_NETIF_NETSTACK_DEFAULT_WIFI_STA,
	};
	esp_netif_t *netif_sta = esp_netif_new(&cfg_sta);
	assert(netif_sta);

	EH_CHECK_OK(esp_netif_attach_wifi_station(netif_sta));
	EH_CHECK_OK(esp_wifi_set_default_wifi_sta_handlers());

	coprocessor_sta_netif = netif_sta;
	s_owned_sta_netif = netif_sta;
	assert(coprocessor_sta_netif);

	/* netif start happens in WIFI_EVENT_STA_START default handler — don't start here. */

	if (coprocessor_sta_netif) {
		esp_netif_ip_info_t ip_info = {0};
		if (esp_netif_get_ip_info(coprocessor_sta_netif, &ip_info) == ESP_OK) {
			ESP_LOGD(TAG, "Coprocessor IP: " IPSTR, IP2STR(&ip_info.ip));
			ESP_LOGD(TAG, "Coprocessor NM: " IPSTR, IP2STR(&ip_info.netmask));
			ESP_LOGD(TAG, "Coprocessor GW: " IPSTR, IP2STR(&ip_info.gw));
		}

		ESP_LOGI(TAG, "Netif is up: %s", esp_netif_is_netif_up(coprocessor_sta_netif) ? "YES" : "NO");
	}
	ESP_LOGD(TAG, "========================");

	return netif_sta;
}


/* Initialize the network split functionality */
esp_err_t eh_cp_feat_nw_split_init(eh_cp_feat_nw_split_config_t *config)
{
	assert(config);
	assert(config->wlan_sta_rx_callback_for_coprocessor);

	esp_log_level_set("esp_netif_lwip", ESP_LOG_DEBUG);
	esp_log_level_set("esp_netif_handlers", ESP_LOG_DEBUG);

	/* Ensure LwIP/netif core is initialized before we create or attach netifs.
	 * The app may call esp_netif_init() later; this is safe to call twice. */
	EH_CHECK_OK(esp_netif_init());

	orig_wlan_rx_cb_for_host = eh_cp_get_traffic_tx_cb(ESP_STA_IF);
	if (!orig_wlan_rx_cb_for_host) {
		ESP_LOGE(TAG, "Failed to get default hosted wlan rx callback. have you called eh_cp_init() first?");
		return ESP_FAIL;
	}

	/* Configure default port forwarding rules */
#ifdef CONFIG_ESP_HOSTED_HOST_ONLY_PORTS_CONFIGURED
	eh_cp_feat_nw_split_config_host_static_port_forwarding_rules(
		config->host_reserved_ports.tcp_src_ports,
		config->host_reserved_ports.tcp_dst_ports,
		config->host_reserved_ports.udp_src_ports,
		config->host_reserved_ports.udp_dst_ports
	);
#endif

	s_dhcp_policy = config->default_nw_stack_dhcp;
	eh_cp_add_feat_cap_bits(0, ESP_EXT_CAP_NW_SPLIT |
	                           dhcp_owner_cap() << ESP_EXT_CAP_NW_SPLIT_DHCP_SHIFT);

	/* Create coprocessor netif for station mode */
	coprocessor_sta_netif = eh_cp_feat_nw_split_init_coprocessor_netif(config);
	if (!coprocessor_sta_netif) {
		ESP_LOGE(TAG, "Failed to create coprocessor netif");
		return ESP_FAIL;
	}
	/* Handlers first, then the driver's state: no link change falls in between. */
	EH_CHECK_OK(esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID,
		wifi_event_handler, NULL));
	for (size_t k = 0; k < sizeof(s_station_ids) / sizeof(s_station_ids[0]); k++) {
		EH_CHECK_OK(esp_event_handler_register(EH_CP_FEAT_WIFI_EVENT, s_station_ids[k],
			on_cp_station, NULL));
	}

	nw_split_wlan_sta_rx_cb = config->wlan_sta_rx_callback_for_coprocessor;
	/* Ours from now to deinit: it sends to the host while the CP netif is not up. */
	EH_CHECK_OK(eh_cp_set_traffic_tx_cb(ESP_STA_IF, nw_split_wlan_sta_rx_cb));
	EH_CHECK_OK(eh_cp_set_traffic_rx_cb(ESP_STA_IF, nw_split_filter_bus_rx));

	EH_CHECK_OK(eh_cp_nw_split_ip_owner_fsm_start(coprocessor_sta_netif, s_owned_sta_netif != NULL,
		s_dhcp_policy == NW_STACK_HOST ? ESP_EXT_CAP_NW_SPLIT_DHCP_HOST : ESP_EXT_CAP_NW_SPLIT_DHCP_CP,
		s_dhcp_policy != NW_STACK_COPROCESSOR));
#if EH_CP_FEAT_NW_SPLIT_WIFI_AUTO_CONNECT_ON_STA_START
	eh_cp_feat_wifi_set_auto_connect(true);     /* last: nothing above can fail after it */
#endif

	return ESP_OK;
}

esp_err_t eh_cp_feat_nw_split_deinit(void)
{
	ESP_LOGI(TAG, "Deinitializing network split component");

    /* Unsubscription is handled by each RPC adapter in its own deinit(). */

    /* Unregister event handlers. */
	EH_CHECK_OK(esp_event_handler_unregister(IP_EVENT, ESP_EVENT_ANY_ID,
											   wifi_event_handler), ESP_ERR_NOT_FOUND);
	for (size_t k = 0; k < sizeof(s_station_ids) / sizeof(s_station_ids[0]); k++) {
		EH_CHECK_OK(esp_event_handler_unregister(EH_CP_FEAT_WIFI_EVENT, s_station_ids[k],
			on_cp_station), ESP_ERR_NOT_FOUND);
	}
#if EH_CP_FEAT_NW_SPLIT_WIFI_AUTO_CONNECT_ON_STA_START
	eh_cp_feat_wifi_set_auto_connect(false);
#endif
	(void)eh_cp_set_traffic_tx_cb(ESP_STA_IF, orig_wlan_rx_cb_for_host);
	(void)eh_cp_set_traffic_rx_cb(ESP_STA_IF, NULL);
	eh_cp_nw_split_ip_owner_fsm_stop();

	/* Stop the STA netif before destroying an owned netif. */
	esp_netif_t *sta = coprocessor_sta_netif ? coprocessor_sta_netif
	                 : esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
	if (sta) {
		EH_CHECK_OK_WARN(esp_netif_dhcpc_stop(sta),
		                 ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED,
		                 ESP_ERR_ESP_NETIF_IF_NOT_READY);
		esp_netif_action_stop(sta, NULL, 0, NULL);
	}

	if (s_owned_sta_netif) {
		EH_CHECK_OK_WARN(esp_wifi_clear_default_wifi_driver_and_handlers(s_owned_sta_netif));
		esp_netif_destroy(s_owned_sta_netif);
		s_owned_sta_netif = NULL;
	}
	coprocessor_sta_netif = NULL;
	eh_cp_clear_feat_cap_bits(0, ESP_EXT_CAP_NW_SPLIT | ESP_EXT_CAP_NW_SPLIT_DHCP_MASK);

	ESP_LOGI(TAG, "Network split deinitialization completed");
	return ESP_OK;
}

esp_err_t eh_cp_feat_nw_split_get_status(uint8_t iface,
                                                    eh_cp_feat_nw_split_status_t *status)
{
    return nw_split_get_status(iface, status);
}

esp_err_t eh_cp_feat_nw_split_set_config(uint8_t iface,
                                                    const char *ip, const char *nm,
                                                    const char *gw,
                                                    const char *dns_ip, uint8_t dns_type)
{
    return nw_split_set_config(iface, ip, nm, gw, dns_ip, dns_type);
}

#endif
