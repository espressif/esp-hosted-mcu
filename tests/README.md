# ESP-Hosted tests

One test corpus runs on two benches: the **emulator** (no hardware) and **real
boards**. A test does not know which bench it runs on.

User guide: [docs/testing.md](../docs/testing.md).

---

## 1. Architecture

```
  python3 tools/eh.py test emu | hw   [-k …] [--regression] [--jobs N]
                    │
                    ▼
  pytest + eh plugin ── select (sanity / regression / -k), budgets, verdict line
                    │
                    ▼
  bench(example, role, transport)        one factory for every test
                    │
        ┌───────────┴─────────────────────────────┐
        ▼ emu                                     ▼ hw
  ┌─────────────┐  SDIO / SPI-FD /  ┌─────────────┐   ┌──────────┐ wire ┌──────────┐
  │ host        │  SPI-HD / UART    │ CP          │   │ host     │◄────►│ CP       │
  │ esp-emu P4  │◄──── bridge ─────►│ esp-emu C6  │   │ board    │      │ board    │
  └──────┬──────┘                   └──────┬──────┘   └────┬─────┘      └────┬─────┘
         │ user net: wake, inbound         │ modelled AP    │ serial console  │ real AP
         ▼                                 ▼                ▼                 ▼
     the test drives both consoles and reads both logs (host and CP)
```

- **Build cache.** Each firmware is built once per source + config fingerprint
  and shared by all tests that need it (`tests/.work/cache`). A change in source
  or config makes a new image. Nothing else is rebuilt.
- **Prewarm.** Before the first test, the run builds every image it needs, in
  parallel. The tests then spend no time on builds.
- **Bench caps.** The bench reports what it can do (`wake`, `net`,
  `gpio_loopback`, …). A test asks for a cap. It does not check the bench name.

## 2. Run tests

| Command | Runs |
|---|---|
| `python3 tools/eh.py test emu` | the sanity set on the emulator |
| `python3 tools/eh.py test emu --regression` | every test on the emulator |
| `python3 tools/eh.py test emu -k <expr>` | only the tests that match `<expr>` |
| `python3 tools/eh.py test emu --regression --jobs 3` | the regression, 3 tests in parallel |
| `python3 tools/eh.py test hw -k <expr>` | the same tests on real boards |

- `-k` filters the chosen set. `eh.py` adds `--sanity` only when you name
  nothing.
- `--jobs auto` = CPUs / 2. Each test runs 2 emulators.
- A full regression takes approximately 2.5 h (1 h builds, 1.5 h tests). Start
  it detached (`setsid nohup …`), so that it continues when the shell closes.

## 3. Layout

```
tests/
├── suites/<feature>/    shared tests: emu and hw
├── emu-mcu/, emu-linux/ emulator-only tests (power save, Linux host)
├── unit/                host C unit tests, no target
├── fw/                  test-only firmware (CP test apps, host super image)
├── infra/               build cache, prewarm, DUTs, flasher, wake helper, reporter
├── targets/             bench providers: emu.py, serial.py (hw), linux.py, rpi.py
├── boards/              board configs for real benches (p4-c5-core-board.json)
├── conftest.py          fixtures (bench, wifi_ap) and the selection plugin
└── pytest.ini           marks, timeouts
```

## 4. Marks and verdict

| Mark | Meaning |
|---|---|
| `sanity` | the fast set; it runs when you name nothing |
| `allow_fail(reason)` | a known open bug; a failure does not fail the run |
| `second_chance` | one rerun for a known emulator flake |
| `retired` | another test covers it; it never runs |
| `wifi`, `nw_split`, `power_save`, … | feature tags, for `-k` and `-m` |

The run ends with one verdict line:

```
eh verdict: 245 passed · 2 allow_fail ⊗ · 12 blocking ✗ · 5 skipped  →  RED
```

## 5. Logs

```
tests/.work/runs/<date-time>/
├── live.log             the console of the run
├── events.jsonl         one line per test start and end
├── report.md            the summary
└── logs/<test>[.FAIL]/  host*.log, cp*.log, build logs, flash.log (hw)
```

## 6. Real boards

Write the bench in `tests/lab.local.json`. Git ignores this file. Do not commit it.

```json
{
  "host_port": "/dev/serial/by-id/<host>",
  "cp_port": "/dev/serial/by-id/<cp>",
  "host_target": "esp32p4", "cp_target": "esp32c5",
  "board": "p4-c5-core-board",
  "transports": ["sdio"],
  "wifi_ap": {"ssid": "<ssid>", "password": "<password>"}
}
```

- Use `/dev/serial/by-id/` names. The `ttyN` numbers change when you re-plug.
- The flasher writes only what changed (app image hash, partition layout).
- The build log masks secrets (`*PASSWORD*`, `*PSK*`, …).
- Some tests use emulator-only helpers (inbound traffic, the fixed emulator
  lease). On a bench, these parts need a bench variant.

## 7. Add a test

1. Select the feature folder in `suites/` (or `emu-mcu/` if the test needs the emulator).
2. Get a bench: `b = bench('<example>', 'mcu_host', transport)`.
3. Drive the console, read the log, assert one expected outcome.
4. Count events in the full log, not at the first match, so that a duplicate fails.
5. Run it: `python3 tools/eh.py test emu -k <name>`.
