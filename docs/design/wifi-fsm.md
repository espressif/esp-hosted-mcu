# Wi-Fi state machines

[Home](../../README.md) · [Design](README.md) · [Wi-Fi Design](wifi.md)

How ESP-Hosted keeps the host app's Wi-Fi view and the CP's real Wi-Fi in step.
Every state machine is a flat `[state][event]` table on `eh_fsm`
(`common/eh_common/include/eh_fsm.h`): every cell names one handler, so every
event has one defined outcome in every state.

## The rule in one line

**The CP is transparent; the host is smart.**

- The CP runs real ESP-IDF Wi-Fi. It executes what the host asks and reports
  what IDF reports.
- The host decides what its app is told, so the app behaves as if Wi-Fi were
  on its own chip.

## Who talks to whom

```mermaid
flowchart LR
  subgraph CP["Coprocessor"]
    cpapp["CP app (optional)"]
    cpsta["cp_sta FSM<br/>station"]
    cpap["cp_ap FSM<br/>soft-AP"]
    owner["ip_owner FSM<br/>who runs DHCP<br/>(network split)"]
  end
  subgraph HOST["Host"]
    app["host app<br/>(IDF Wi-Fi API)"]
    sta["sta FSM<br/>what the app sees"]
    copy["sta_cp FSM<br/>copy of the CP station"]
    hap["ap FSM<br/>what the app sees (AP)"]
    lease["lease FSM<br/>host IP (network split)"]
  end
  app -- "init/start/connect/..." --> sta
  sta -- "RPC" --> cpsta
  cpapp -- "esp_wifi_*" --> cpsta
  cpsta -- "events + replay" --> copy
  cpsta -- "events + replay" --> sta
  cpap -- "events" --> hap
  owner -- "lease / owner" --> lease
  sta -- "WIFI_EVENT_STA_*" --> app
  hap -- "WIFI_EVENT_AP_*" --> app
  lease -- "IP_EVENT_*" --> app
```

| FSM | Lives on | Job |
|---|---|---|
| `cp_sta` | CP | The station as IDF runs it: radio, link, attempt, held after a disconnect. |
| `cp_ap` | CP | Soft-AP on or off. |
| `ip_owner` | CP | Network split: who runs DHCP for the shared address, the CP or the host. |
| `sta_cp` | host | Copy of the CP station (off / on / connected), whatever the app did. |
| `sta` | host | The station as the host app sees it, in IDF's event order. |
| `ap` | host | The soft-AP as the host app sees it. |
| `lease` | host | Network split: puts the shared IP on the host interface. |

---

## CP side

### `cp_sta`: the CP's station

```mermaid
stateDiagram-v2
  [*] --> OFF
  OFF --> IDLE: RADIO_STARTED
  OFF --> OFF_CONNECT: CONNECT (radio off)
  OFF_CONNECT --> CONNECTING: RADIO_STARTED
  IDLE --> CONNECTING: CONNECT / LOCAL_CONNECT
  CONNECTING --> CONNECTED: ASSOCIATED
  CONNECTING --> IDLE: DISASSOCIATED (attempt failed)
  CONNECTED --> IDLE: DISASSOCIATED (dropped)
  CONNECTED --> SWITCHING: CONNECT with a new config
  SWITCHING --> CONNECTING: DISASSOCIATED (old link gone)
  CONNECTED --> DISCONNECTING: DISCONNECT
  DISCONNECTING --> HELD: DISASSOCIATED
  HELD --> CONNECTING: CONNECT (host)
  IDLE --> OFF: RADIO_STOPPED
  CONNECTED --> OFF: RADIO_STOPPED
```

| State | Meaning |
|---|---|
| OFF | radio stopped |
| OFF_CONNECT | a connect arrived while stopped; runs when the radio starts |
| IDLE | radio on, no link, no attempt |
| CONNECTING | an attempt runs |
| CONNECTED | associated |
| SWITCHING | associated, a new config asked: drop first, then connect |
| DISCONNECTING | the host asked to leave; waiting for IDF's report |
| HELD | the host left; CP code may not reconnect until the host connects again |

| Event | From |
|---|---|
| RADIO_STARTED / RADIO_STOPPED / ASSOCIATED / DISASSOCIATED | IDF events on the CP |
| CONNECT, DISCONNECT, SET_CONFIG, START | host RPCs |
| LOCAL_CONNECT | CP app's `esp_wifi_connect` (wrapped) |
| RESYNC | the host (re)connected: replay the link if CONNECTED |
| SYNC, DEINIT | feature started after the app / feature stopped |

**Replay.** When the host connects (boot, wake, handshake) or starts Wi-Fi
while the CP is CONNECTED, the CP re-sends STA_CONNECTED (and, in network split,
its lease). That is how a host that rebooted learns the link the CP kept.

### `cp_ap`: the CP's soft-AP

`OFF ⇄ ON` on IDF's AP_START / AP_STOP. SYNC adopts the driver state, DEINIT
goes OFF.

### `ip_owner`: who runs DHCP (network split)

```mermaid
stateDiagram-v2
  [*] --> STOPPED
  STOPPED --> CP_DOWN: START (Kconfig owner CP)
  STOPPED --> HOST_DOWN: START (Kconfig owner host)
  CP_DOWN --> CP_UP: LINK_UP
  CP_UP --> CP_DOWN: LINK_DOWN
  CP_DOWN --> HOST_DOWN: WANT_HOST (no address yet)
  CP_UP --> CP_UP_TO_HOST: WANT_HOST (address in use)
  CP_UP_TO_HOST --> HOST_DOWN: LINK_DOWN
  HOST_DOWN --> HOST_UP: LINK_UP
  HOST_UP --> HOST_DOWN: LINK_DOWN
  HOST_DOWN --> CP_DOWN: WANT_CP (no address yet)
  HOST_UP --> HOST_UP_TO_CP: WANT_CP (address in use)
  HOST_UP_TO_CP --> CP_DOWN: LINK_DOWN
```

- A switch asked while an address is in use waits for the next link drop.
  Stopping a bound DHCP client would release an address the other side is using.
- HOST_LEASE: the host pushes its address. The CP takes it only when the host
  owns the IP.

---

## Host side

### `sta_cp`: copy of the CP station

```mermaid
stateDiagram-v2
  [*] --> CP_RADIO_OFF
  CP_RADIO_OFF --> CP_RADIO_ON: RADIO_STARTED
  CP_RADIO_ON --> CP_RADIO_OFF: RADIO_STOPPED
  CP_RADIO_OFF --> CP_CONNECTED: ASSOCIATED
  CP_RADIO_ON --> CP_CONNECTED: ASSOCIATED
  CP_CONNECTED --> CP_RADIO_ON: DISASSOCIATED
  CP_CONNECTED --> CP_RADIO_OFF: RADIO_STOPPED
  CP_RADIO_ON --> CP_RADIO_OFF: CP_UNLINKED / HOSTED_DOWN
  CP_CONNECTED --> CP_RADIO_OFF: CP_UNLINKED / HOSTED_DOWN
```

- Fed by every CP report, in every app state. It never posts anything to the app.
- `sta` reads it at one moment only: when the app's station begins (its
  `esp_wifi_start`, or a mode change that adds a station). If the copy is
  CP_CONNECTED, the app adopts the CP's link at once.

### `sta`: what the host app sees

```mermaid
stateDiagram-v2
  [*] --> NOT_INITED
  NOT_INITED --> INITING: esp_wifi_init
  INITING --> INITED: CP accepted
  INITED --> STARTED: esp_wifi_start (CP not connected)
  INITED --> CONNECTED: esp_wifi_start (CP connected: adopt)
  INITED --> STARTED_NO_STA: esp_wifi_start, no station in the mode
  STARTED_NO_STA --> STARTED: mode gains a station (STA_ADDED)
  STARTED_NO_STA --> CONNECTED: STA_ADDED, CP connected
  STARTED --> CONNECTING: esp_wifi_connect
  STARTED --> DISCONNECTED: esp_wifi_disconnect
  DISCONNECTED --> CONNECTING: esp_wifi_connect
  STARTED --> AUTO_CONNECTING: policy connect (Kconfig)
  STARTED --> CONNECTED: CP link appears (CP app / replay)
  CONNECTING --> CONNECTED: ASSOCIATED
  CONNECTING --> STARTED: attempt failed (STA_DISCONNECTED posted)
  CONNECTED --> RECONNECTING: esp_wifi_connect again
  RECONNECTING --> CONNECTED: ASSOCIATED
  RECONNECTING --> CONNECTING: old link dropped (switch)
  CONNECTED --> DISCONNECTING: esp_wifi_disconnect
  DISCONNECTING --> DISCONNECTED: DISASSOCIATED
  CONNECTED --> STARTED: link dropped
  STARTED --> STOPPING: esp_wifi_stop
  CONNECTED --> STOPPING: esp_wifi_stop
  STOPPING --> INITED: CP radio stopped
  STOPPING --> RESTARTING: esp_wifi_start again
  STARTED --> STARTED_CP_OFF: CP stopped its radio
  CONNECTED --> STARTED_CP_OFF: CP stopped its radio
  STARTED_CP_OFF --> STARTED: CP radio back
  STARTED_CP_OFF --> INITED: esp_wifi_stop
  INITED --> STOPPING: esp_wifi_stop, CP radio still on
  INITED --> NOT_INITED: esp_wifi_deinit / hosted down
```

| State | What the app has been told |
|---|---|
| NOT_INITED, INITING | nothing; Wi-Fi not initialised |
| INITED | initialised, not started: nothing reaches the app, whatever the CP does |
| STARTED_NO_STA | started, but its mode has no station (AP only): no station events |
| STARTED | STA_START; not connected; no attempt of its own |
| CONNECTING / AUTO_CONNECTING | its own (or the policy's) attempt runs; only that attempt's failure is posted |
| CONNECTED | STA_CONNECTED |
| RECONNECTING | connected; it connected again: the CP replays (same AP, no event) or switches |
| DISCONNECTING / DISCONNECTED | it disconnected; DISCONNECTED keeps CP code from reconnecting it |
| STARTED_CP_OFF | started, but the CP stopped its radio (mode change, CP re-init) |
| STOPPING, RESTARTING, RESTARTING_CONNECTING | stop / quick stop-start in flight, waiting for the CP's report |

**What `sta` posts.** Its edges post IDF's events in IDF's order:
- STA_START when entering a radio-on state, STA_STOP when leaving one;
- STA_CONNECTED when entering an associated state, STA_DISCONNECTED when
  leaving one;
- STA_DISCONNECTED always before STA_STOP, STA_CONNECTED always after STA_START.

**Order of CP reports.** A CP report sent before an RPC reply is applied before
the request that reply completes: the CP lets its event loop send what a Wi-Fi
request caused before it replies, and the host applies CP reports in the RX
task in arrival order. Reports that come after a reply (the CP's STA_START of a
start, its STA_STOP of a stop) are absorbed by the STOPPING / RESTARTING cells.

**Return codes.** `esp_wifi_connect` before start returns
`ESP_ERR_WIFI_NOT_STARTED`; queries before init return
`ESP_ERR_WIFI_NOT_INIT`; same as IDF.

| Event | From |
|---|---|
| INIT_REQ, START_REQ, STOP_REQ, CONNECT_REQ, DISCONNECT_REQ, DEINIT_REQ | the app's IDF call, before the RPC |
| INIT_CLAIM / INIT_FAIL, CONNECT_FAIL, DISCONNECT_FAIL | RPC results |
| AUTO_CONNECT_REQ | connect-on-start / reconnect-on-drop policy (Kconfig) |
| STA_ADDED | `esp_wifi_set_mode` added a station while the app's Wi-Fi runs |
| RADIO_STARTED, RADIO_STOPPED, ASSOCIATED, DISASSOCIATED | CP reports |
| LEASE_READY | network split: the CP's lease arrived |
| CP_UNLINKED | the CP came back without a link (it rebooted) |
| HOSTED_DOWN | hosted deinit |

### `ap`: what the host app sees for the soft-AP

| State | Meaning |
|---|---|
| OFF_IDLE / ON_IDLE | app not started; CP AP off / on |
| STARTING | app started, CP's start report pending |
| ON | AP_START posted |
| OFF_APP | app started, no AP in its mode (or the CP stopped it) |
| STOPPING / RESTARTING | stop / quick stop-start in flight |

### `lease`: the host's IP (network split only)

```mermaid
stateDiagram-v2
  [*] --> OFF
  OFF --> WAIT_NETIF: START (app interface not made yet)
  OFF --> CP_DOWN: START (owner CP)
  OFF --> HOST_DOWN: START (owner host)
  OFF --> SPLIT_OFF: START, CP has no split
  WAIT_NETIF --> CP_DOWN: app creates WIFI_STA_DEF
  CP_DOWN --> CP_NO_IP: LINK_UP (app's STA_CONNECTED)
  CP_NO_IP --> CP_IP: CP lease applied (GOT_IP)
  CP_IP --> CP_ROAMING: LINK_DOWN while roaming (address kept)
  CP_ROAMING --> CP_IP: link back
  CP_IP --> CP_DOWN: LINK_DOWN / lease lost (LOST_IP)
  CP_IP --> CP_IP_TO_HOST: owner change asked (waits for the drop)
  CP_IP_TO_HOST --> HOST_DOWN: LINK_DOWN
  HOST_DOWN --> HOST_NO_IP: LINK_UP (host DHCP runs)
  HOST_NO_IP --> HOST_IP: host lease (pushed to the CP)
  HOST_IP --> HOST_DOWN: LINK_DOWN
  HOST_IP --> HOST_IP_TO_CP: owner change asked
  HOST_IP_TO_CP --> CP_DOWN: LINK_DOWN
```

- CP_* rows apply the CP's lease to the host interface; the host DHCP client
  never runs. It is stopped at STA_CONNECTED and re-armed at STA_DISCONNECTED,
  both in the same handler pass as esp_netif. A reconnect queued behind the
  disconnect then finds it armed; a stopped client makes esp_netif post the old
  address as a static GOT_IP.
- HOST_* rows run the host DHCP client and push the address to the CP.
- The link comes from the app's STA_CONNECTED. So in network split **the app
  must call `esp_wifi_init` + `esp_wifi_start`**. Connect is optional: a link
  the CP holds is adopted.

---

## Flows

### Host wakes from deep sleep; the CP kept the link (network split)

```mermaid
sequenceDiagram
  participant App as host app
  participant H as host (sta, sta_cp, lease)
  participant CP as CP (cp_sta)
  Note over CP: CONNECTED, lease held
  CP->>H: wake pin, then handshake replay: STA_CONNECTED, lease
  Note over H: sta_cp = ASSOC (app not started: nothing posted)
  App->>H: esp_wifi_init, esp_wifi_start
  H-->>App: STA_START, STA_CONNECTED (adopted)
  H-->>App: GOT_IP (CP lease)
```

### Host app AP only; CP code connects the station; app later adds a station

```mermaid
sequenceDiagram
  participant App as host app
  participant H as host
  participant CP as CP
  App->>H: set_mode(AP), start
  H-->>App: AP_START
  CP->>H: STA started, STA_CONNECTED (CP code)
  Note over H: sta = STARTING, sta_cp = ASSOC: no station events
  App->>H: set_mode(APSTA)
  H-->>App: STA_START, STA_CONNECTED (adopted)
```

### The app connects (plain mode)

```mermaid
sequenceDiagram
  participant App as host app
  participant H as host
  participant CP as CP
  App->>H: esp_wifi_connect
  H->>CP: RPC connect (sta: CONNECTING)
  CP-->>H: STA_CONNECTED
  H-->>App: STA_CONNECTED, then host DHCP: GOT_IP
```

### A failed attempt the app did not make

The CP app connects to a missing AP. `cp_sta` goes CONNECTING → IDLE and
reports DISASSOCIATED. Host `sta` is IDLE, so nothing reaches the host app: the
attempt was not its own.

---

## Known limits

- Network split: a host app that never calls `esp_wifi_init` + `esp_wifi_start`
  gets no IP, even when the CP is connected.
- The CP restarting on its own is not detected on SDIO/SPI-HD (open, "bug B").
- SDIO, "reset CP only if needed": after a host reboot, the CP's hello can be
  lost if a CP message was unread at the moment of the reset.
