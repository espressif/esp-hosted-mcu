# Board definitions

One file per physical board: `<board>.json` with a `host` and a `cp` list of
sdkconfig lines.

A bench names its board once, in `tests/lab.local.json`:

```json
{ "board": "p4-c5-core-board" }
```

## Why a file and not a list in every bench config

The board symbol (`CONFIG_ESP_HOSTED_P4_C5_CORE_BOARD=y`) makes Kconfig select
that board's bus pins, reset line and wake pin. It does not carry the SoC facts
that sit beside it: which chip the coprocessor is, the host silicon revision, and
which console the board exposes. Those lines were copied by hand into each bench
config, and a bench that copied only some of them produced a board that built and
flashed but printed nothing.

## Precedence, lowest to highest

1. `tests/env.json` → `build.host_board_sdkconfig` / `build.slave_board_sdkconfig`
2. `tests/boards/<board>.json`, selected by `board` in `tests/lab.local.json`
3. `host_board_sdkconfig` / `cp_board_sdkconfig` in `tests/lab.local.json`
4. `EH_HOST_BOARD_SDKCONFIG` / `EH_CP_BOARD_SDKCONFIG` (comma-separated, one run)

Each level replaces the level below it. `host_extra` / `cp_extra` in
`tests/lab.local.json` are additive and apply on top of whichever level won.

## Adding a board

Copy the example's `sdkconfig.defaults.<board>` lines into a new
`<board>.json`, then set `board` in the bench config. No harness change.
