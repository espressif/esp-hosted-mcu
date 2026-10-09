# Wi-Fi coverage check

`check_wifi_coverage.py` shows which IDF Wi-Fi structs, struct fields and APIs ESP-Hosted covers. It fails when IDF has a struct, field or API that hosted neither covers nor classifies.

## What it compares

**IDF side.** For each IDF ref:
1. The script reads every public header in `components/esp_wifi/include`, `components/esp_wifi/include/local` and `components/wpa_supplicant/esp_supplicant/include`. It does not read `esp_private/`. A header that IDF adds later is checked automatically. A header that does not reach the parse is an error.
2. It preprocesses them once per Wi-Fi target, in two config variants:
   - only the target's SOC caps set;
   - every `CONFIG_*` that the headers test also set to 1.
3. It takes the union over all targets and both variants, so an element that exists only under one target or one config is still checked.

**Hosted side.**
- **Structs:** `wifi_coverage_manifest.yml` maps each struct field by field (`carried` / `skip` / `gap`), or marks the whole struct. Each `carried` field names its `rpc_v2.proto` field. The proto field must exist, and a bit name must exist in both the host and the CP bitmask headers.
- **APIs:** the script reads the hosted sources under `host/` and `port/`. Comments and `#if 0` blocks are ignored. An API is:
  - `supported` when hosted defines the function itself, or the `esp_wifi_remote_*` / `esp_eap_client_remote_*` hook that the esp_wifi_remote forwarder calls. Hooks count only for `esp_wifi.h` and `esp_eap_client.h`, the headers that esp_wifi_remote forwards. When the IDF tree has the in-tree forwarder (IDF ≥ 6.1), only the APIs it forwards count;
  - `stub` when that definition only returns `ESP_ERR_NOT_SUPPORTED`;
  - otherwise classified in the manifest: per API under `apis:`, or per header under `headers:`.

## Acceptance criteria

The check passes (exit 0) only when, for every checked IDF ref:

| Element | Must be |
| --- | --- |
| Every struct | listed in the manifest, or covered by a `headers:` entry |
| Every field of a field-by-field struct | listed as `carried`, `skip` or `gap` |
| Every `carried` field | a proto field that exists; each bit name exists on both sides |
| Every API | defined by hosted (`supported` or `stub`), or classified under `apis:` or `headers:` |
| Every `apis:` entry other than `local` | for an API that hosted does not define |

It fails (exit 1) with one of these lines:

| Line | Meaning | Fix |
| --- | --- | --- |
| `MISSING` | IDF has a field the manifest does not list | carry it, or mark it `skip` or `gap` |
| `NEW STRUCT` | IDF has a struct the manifest does not classify | map it, or mark it as a whole |
| `NO STRUCT` | a mapped struct is gone from a ref at or after `since` | add `aliases` or `since` |
| `WIRE` | a proto field or bit name in the manifest does not exist | fix the manifest or the proto |
| `MISSING API` | IDF has an API that hosted does not define and the manifest does not classify | implement it, or classify it |
| `STALE API` | hosted now implements an API that the manifest says is not carried | remove the entry |

Both reports group by header file: file → APIs, and file → struct → field. Failing files come first.

These lines print but do not fail: `GAP`, `STUB`, `SKIP`, `LOCAL`, `NOT CARRIED`. Each line names the IDF refs and targets where the element exists. A whole header classified under `headers:` prints as one line with counts; `-v` and the HTML report list its names.

It errors (exit 2, never a pass) when:
- a header does not preprocess or parse;
- a sentinel struct or API is missing;
- no Wi-Fi target or no hosted definition is found;
- a fetch fails;
- the manifest schema is bad.

A manifest entry that the checked IDF does not have passes: hosted may cover more than that IDF version offers.

## Manifest entries

`wifi_coverage_manifest.yml`:

```yaml
headers:
  esp_wpa.h: {skip: the supplicant runs on the CP}           # every struct and API of one header
apis:
  esp_wifi_set_csi: {unsupported: CSI API is not implemented in hosted}
  esp_wifi_set_okc_support: {gap: no esp_wifi_remote_set_okc_support glue}
structs:
  wifi_init_config_t:
    proto: wifi_init_config
    fields:
      static_rx_buf_num: carried
      osi_funcs: {skip: CP-local OS adapter table pointer}   # one field
  wpa_crypto_funcs_t: {skip: CP-local function table}         # a whole struct
```

Every status needs a reason. A per-API or per-struct entry takes precedence over a `headers:` entry.

## Run

```sh
python3 tools/wifi_coverage/check_wifi_coverage.py --refs-file tools/wifi_coverage/idf_refs.yml --html report.html
python3 tools/wifi_coverage/check_wifi_coverage.py --refs-file tools/wifi_coverage/idf_refs.yml --live
python3 tools/wifi_coverage/check_wifi_coverage.py --idf-repo ~/esp-idf --ref origin/master --ref v6.0.3
python3 tools/wifi_coverage/check_wifi_coverage.py --idf $IDF_PATH --dump wifi_sta_config_t
python3 tools/wifi_coverage/test_check_wifi_coverage.py
```

With `--refs-file`, the first run makes a shallow, blobless fetch of a few MB into `~/.cache/esp_hosted/wifi_coverage_idf` (see `--cache-dir`). Later runs with the same pinned SHAs need no network.

## CI and manual runs

- **CI** checks only the tracked IDF versions pinned in `idf_refs.yml`, so a new IDF commit never fails an MR. The job keeps the HTML report as an artifact.
- **Manual runs** find what hosted misses in newer IDF: use `--live` for the branch heads, or `--idf-repo` with `--ref` for any branch or tag. To track a new IDF version, add it to `idf_refs.yml`, or update its SHA after the check passes.

## Limits

- **Version guards:** the script does not check IDF version guards in hosted code. A field can be listed and still be dropped by a wrong `#if`.
- **Signatures:** it does not compare API signatures. A changed prototype breaks the hosted build, but only for the IDF versions that CI builds the host on.
- **Feature gating:** an API that hosted implements behind a Kconfig feature (for example EAP or DPP) counts as `supported`.
- **Depth:** a definition counts as `supported` when its own body is more than a "not supported" return. If it forwards to an `eh_host_*` function that itself returns `ESP_ERR_NOT_SUPPORTED`, the script does not see that.
- **Scope:** enum values and event IDs that have no payload struct are not checked.
- **Direction:** a field carried in one direction only shows as carried; the manifest records this in a `note`.
