# Test scenarios

What the Wi-Fi, network-split and power-save tests prove, from the top level
down to each row. How to run them: [README.md](README.md). The state machines
they test: [docs/design/wifi-fsm.md](../docs/design/wifi-fsm.md).

---

## 1. The rule every scenario checks

**The host app behaves as if Wi-Fi were on its own chip.** It gets the events of
ESP-IDF, in the order of ESP-IDF, with the return codes of ESP-IDF. The CP runs
the real Wi-Fi and stays transparent.

```
  host app ──esp_wifi_*──► host FSMs ──RPC──► CP FSMs ──► IDF Wi-Fi ──► AP
     ▲                        │                  │
     └── WIFI_EVENT_* /       └◄── CP reports ───┘
         IP_EVENT_*  (as IDF)
```

A test reads the full host log and counts each app event. A missing event or
one extra event fails the test.

| Count | Meaning |
|---|---|
| `conn` | `sta connected` (STA_CONNECTED) |
| `disc` | `sta disconnected` (STA_DISCONNECTED) |
| `got` / `lost` | `got ip` / `lost ip` |
| `ping` | the app pings the gateway |

## 2. Dimensions

Each network-split test sets every dimension. No result depends on a default.

| Dimension | Values |
|---|---|
| Transport | SDIO, SPI-FD, SPI-HD, UART |
| Host netif mode | `app_external` (the app makes WIFI_STA_DEF), `internal_static`, `internal_dhcp` |
| IP owner (CP policy) | `cp`, `host`, `both` |
| Who connects | app CLI, host reconnect policy, CP auto-connect, CP app |
| Split | on both sides, host only, CP only, off (plain) |
| Host power | awake, deep sleep + wake, restart, CP kept or reset |

## 3. Wi-Fi as the app sees it

`suites/eh_test_feat_api/test_api_wifi_lifecycle.py`, `test_api_control_plane.py`

| Test | Rows | Proves |
|---|---|---|
| `test_wifi_init_deinit_rc` | 1 | each init / deinit / start / connect call gives the IDF return code (`INVALID_STATE`, `NOT_INIT`, `NOT_STOPPED`, …); a refused call changes nothing |
| `test_control_plane` | SDIO, SDIO 1-bit, UART, … | every Wi-Fi request returns the rc and posts the events that IDF does (measured on a real ESP32) |
| `test_wifi_connected` | SDIO, UART, SPI-HD | connect, then the connect-only queries (AP info, RSSI, AID) give live data |
| `test_softap_stop_start_back_to_back` | AP and APSTA × wires | each APSTA step posts the IDF events in IDF order; stop + start with no wait gives one STOP, one START |
| `test_disconnect_before_sta_start_wins` | 1 | start, then disconnect at once: the disconnect wins, no association |
| `test_reboot_connect_stored_config` | flash / RAM × SDIO / UART | after a reboot, connect without set_config: flash storage connects, RAM storage gives `ESP_ERR_WIFI_SSID` |
| `test_plain_restart_leftover_link` | link, link moved, attempt | the host reboots, the CP keeps running (SDIO): a kept link is adopted (one CONNECTED); a moved config switches AP (one DISCONNECTED, one CONNECTED); a leftover attempt stays hidden |
| `suites/…examples/test_sta_example_restart_connects_afresh` | 1 | the unmodified IDF station example restarts with `esp_restart`: one new STA_CONNECTED |

## 4. Network split

`suites/eh_test_feat_nw_split/test_nw_split_scenarios.py`. Scenario IDs (`Sxx`)
appear in the test IDs.

### 4.1 Link up, link down

| Test | Rows | Proves |
|---|---|---|
| `test_nwsplit_link_cycles` | S01-S08, S33: modes × owners × who reconnects | each drop: `disc` 1; each association: `conn` 1, `got` 1, ping OK; one DHCP client per MAC; the reconnect policy acts only on drops the app did not ask for |
| `test_nwsplit_drop_without_reconnect` | S15/S16 × modes | a drop with no reconnect: `lost` 1 after the lost-ip interval, no `got` |
| `test_nwsplit_stop_start_back_to_back` | 2 modes | `esp_wifi_stop` + `esp_wifi_start` with no wait: one `disc`, one `conn`, one `got` per cycle |
| `test_nwsplit_disconnect_connect_back_to_back` | 2 modes | disconnect + connect with no wait: `disc` 1, then `conn` 1, `got` 1 |
| `test_nwsplit_quiet_no_events` | S17/S18 × modes | connected and quiet: no event; the soft-AP joins and leaves the radio: the station sees nothing |
| `test_nwsplit_two_reconnect_owners` | S26 × 2 modes | host policy and CP app both reconnect: still one `conn`, one `got` per drop |

### 4.2 Boot, restart and the kept link

| Test | Rows | Proves |
|---|---|---|
| `test_nwsplit_boot_cp_autoconnect` | S32 × modes; B13 restart row | the CP connects at boot; the app's start adopts the link and the IP; with a host restart and the CP kept: one `conn`, one `got`, no CP reset |
| `test_nwsplit_boot_reinit_invisible` | S61 × modes | the app's `esp_wifi_init` config differs from the CP's, so the CP re-inits Wi-Fi: the app sees **one `sta start`, no `sta stop`**, then one `conn`, one `got` |
| `test_nwsplit_cp_reboot_under_host` | S63 × wires × trigger | the CP reboots under a connected host: the host never stays CONNECTED on a link that is gone |
| `test_nwsplit_cp_deinit_init` | S24 × wires × modes | the CP stops and restarts its hosted stack; Wi-Fi stays up: the app sees nothing |
| `test_nwsplit_cp_reconnect_same_lease` | S60 × modes | CP code reconnects to the same AP: one `disc`, one `conn`, the same address, no `lost` |
| `emu-mcu/…/test_cp_deinit_init_cycles_return_heap` | 4 wires + sdio without bus unload | CP deinit / init cycles: each cycle has exactly one core init, one slave-up, one data-path open; the host ping still works; the heap comes back |

### 4.3 IP owner and lease

| Test | Rows | Proves |
|---|---|---|
| `test_nwsplit_ip_owner_negotiated` | S09-S11: mode × CP policy | the host asks for the owner its netif mode wants (SetContext type 1); the CP grants it or keeps its own and says so; one association, one lease, one DHCP client |
| `test_nwsplit_ip_owner_switch_at_disconnect` | 1 | the CP holds a bound lease when the host asks: the switch waits for the next disconnect; no RELEASE of an address in use |
| `test_nwsplit_lease_changes` | S19 × 2 modes | the CP address moves: one `got` with `changed=1`, no link event |
| `test_nwsplit_lease_lost_while_associated` | S20 × 2 modes | the CP loses its lease while associated: `lost` 1, then `got` 1 |
| `test_nwsplit_lease_status_vs_app_disconnect` | S71 | a lease status in flight when the app disconnects: no STA_CONNECTED for a link that is down |
| `test_nwsplit_lease_status_vs_cp_drop` | S71 | the CP changes its lease and drops the link at once: no STA_CONNECTED after the drop |

### 4.4 Who owns the netif

| Test | Rows | Proves |
|---|---|---|
| `test_nwsplit_app_netif_any_order` | mode × before / after `esp_hosted_init` | the app can make WIFI_STA_DEF before or after init; the library adopts it |
| `test_nwsplit_app_netif_after_lease` | B15 | the CP holds a link and a lease before the app makes its netif: netif, then start, as in IDF: `conn` 1, `got` 1 |
| `test_nwsplit_app_external_without_netif_fails_loud` | 1 | `app_external` with no netif: a clear error, no crash |
| `test_nwsplit_wifi_cmd_restart` | modes × wifi-cmd lwIP on / off | `wifi restart` (deinit + init + start) through wifi-cmd works with a third netif creator |
| `test_nwsplit_wifi_deinit_init` | modes | each call returns the IDF rc; a refused call keeps the link and the data path |
| `test_set_config_while_stopped` | split on / off | stop → set_config → connect → start: the new config is used, as in IDF |

### 4.5 One side only, and defaults

| Test | Rows | Proves |
|---|---|---|
| `test_nwsplit_cp_split_off_degrades` | S12/S13 × modes | host split on, CP split off: the host names the CP option and runs as plain hosted |
| `test_nwsplit_host_split_off_degrades` | S14 | CP split on, host split off: the CP keeps the IP; one DHCP client; no crash |
| `test_nwsplit_shipped_defaults` | 1 | no pins at all: what a user gets works |

### 4.6 Failure and race cases

| Test | Rows | Proves |
|---|---|---|
| `test_nwsplit_connect_retries_after_failure` | S64: CP app / host feature | no such AP: each retry owner keeps retrying; the app sees only its own failures |
| `test_nwsplit_cp_app_reconnects_after_drop` | 1 | CP code drops the link; the CP app reconnects; the host policy is off |
| `test_nwsplit_new_config_while_connected` | S66 | connected, the app connects to another SSID: one drop, one failed attempt, no loop |
| `test_nwsplit_policy_connect_fails` | S67 | the host connects on start with no SSID: one error, no retry storm, no event |
| `test_nwsplit_app_disconnect_stops_retries` | S68 | attempts keep failing; the app's disconnect stops them; the policy does not reconnect |
| `test_nwsplit_stop_with_reconnect_policy` | S73 | stop while connected, reconnect-on-drop on: one `disc`, one `sta stop`, no new association |
| `test_host_disconnect_stops_cp_retry` | 2 modes | the CP app retries on DISCONNECTED; a disconnect the host asked for wins |

### 4.7 Deep sleep and wake (adoption)

| Test | Rows | Proves |
|---|---|---|
| `test_nwsplit_host_deep_sleep_wake` | S21-S23: modes on SDIO; `app_external` on UART, SPI-FD, SPI-HD; plain | each wake is a new host boot: the kept link and lease come back once per wake, no `lost`, IP within 1.5 s of the wake |
| `test_wake_after_link_lost` | B7: `app_external`, plain | CP code drops the link while the host sleeps: after the wake the app sees STA_START only |

## 5. Unit tests

| Test | Proves |
|---|---|
| `unit/eh_fsm/test_eh_fsm_engine` | the FSM engine: every cell is called, entry and exit order, re-entry, out-of-range events |
| `unit/eh_ctx/test_eh_ctx_payload_compat` | SetContext payload rule: an older peer's missing fields read 0; a newer peer's extra fields are ignored; a too-short payload is refused |

## 6. Host power save and wake delivery

`emu-mcu/eh_test_feat_power_save/`, `test_nw_split_scenarios.py`

```
  AP ──frame──► CP (host asleep) ──wake pin──► host boots ──► host path opens
                  │                                            ▲
                  └── unicast or ARP only (broadcast ignored)  │
                  frames before the path opens: held (max 10), delivered at GOT_IP / AP_START
```

| Test | Rows | Proves |
|---|---|---|
| `test_power_save_wake_mem` | SDIO, UART, SPI-FD, SPI-HD | one datagram on a host port wakes the host, and that same datagram reaches the host app; then GOT_IP and ping; the heap comes back |
| `test_power_save_wake_burst` | 4 wires | more frames than the host can hold arrive before its path opens: the waking frame is still delivered (the hold refuses the newest, never the first) |
| `test_plain_wake_app_quiet` | B5: no Wi-Fi call / init only | plain mode, CP code holds the link: an app that has not started Wi-Fi sees nothing at boot or after a wake; its start then adopts the link: `conn` 1, `got` 1 |

## 7. Bring-up

| Test | Rows | Proves |
|---|---|---|
| `test_nwsplit_netif_ready_after_connect` | S72: 4 wires × 2 internal modes | the netif that hosted makes exists when `esp_hosted_connect_to_slave()` returns, also with a slow feature init (esp_hosted_mcu#12) |
