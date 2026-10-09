# Serializers — protobuf RPC messages

The RPC messages exchanged between host and co-processor are defined in `.proto`
files. The C code for them is generated and committed; the build only compiles it.

```
common/serializers/
├── eh_proto_v1/                     Ctrl* messages (eh_cp_feat_rpc_ext_v1)
│   ├── include/gen_v1.h             generated
│   ├── src/gen_v1.c                 generated
│   └── src/proto/
│       ├── rpc_v1.proto             source of truth
│       └── build_proto.sh           regenerates gen_v1.{c,h}
├── eh_proto_v2/                     Rpc* messages (eh_*_feat_rpc_ext_v2, host and CP)
│   ├── include/gen_v2.h             generated
│   ├── include/gen_v2_names.h       generated (eh_rpc_id_name_v2())
│   ├── src/gen_v2.c                 generated
│   ├── src/gen_v2_names.c           generated (RPC id -> short name)
│   ├── src/eh_rpc_id_map_v2_asserts.c   compile-time id-range checks (hand-written)
│   └── src/proto/
│       ├── rpc_v2.proto             source of truth
│       ├── build_proto.sh           regenerates every generated v2 file
│       └── gen_rpc_id_names.py      writes gen_v2_names.{c,h}
└── third_party/msg_codec/           protobuf-c runtime (see DESIGN_NOTES.md)
```

## Regenerating

Edit the `.proto`, then run its `build_proto.sh`. Never edit a generated file.

```
rpc_v2.proto ──► build_proto.sh ─┬─► protoc + protoc-gen-c 1.5.2 ──► include/gen_v2.h
                                 │                                   src/gen_v2.c
                                 └─► gen_rpc_id_names.py ──────────► src/gen_v2_names.c
                                                                     include/gen_v2_names.h

rpc_v1.proto ──► build_proto.sh ───► protoc + protoc-gen-c 1.5.2 ──► include/gen_v1.h
                                                                     src/gen_v1.c
```

- Needs `protoc` and `protoc-gen-c` from protobuf-c **1.5.2**, the version the committed
  files were made with; another version rewrites the whole file. Without a local
  install, a container works, e.g. `alpine:3.22` with `protobuf-c-compiler protobuf bash python3`.
- Commit the `.proto` and every regenerated file together. The `rpc-id-names` pre-commit
  hook (also run by CI's `pre_commit` job) fails when `gen_v2_names.{c,h}` no longer match
  `rpc_v2.proto`.

## Flash footprint

Both protos set `option optimize_for = CODE_SIZE`, so protobuf-c leaves message and
field names out of the descriptors (~38 KB less flash on host and CP). Nothing reads
those names at runtime. For readable logs the host uses the generated short-name table:

```
RpcId enum in rpc_v2.proto            gen_v2_names.c
  Req_WifiScanStart   = 0x11E   ─►      "WifiScanStart"   (Req_ and Resp_ share one entry)
  Resp_WifiScanStart  = 0x21E   ─►
  Event_StaConnected  = 0x307   ─►      "StaConnected"

eh_rpc_id_name_v2(msg_id) -> "WifiScanStart", or NULL for an unknown id
```

## RPC ids

```
0x100  Req_Base   ... requests ...    Req_Max
0x200  Resp_Base  ... responses ...   Resp_Max     Resp_X - Resp_Base == Req_X - Req_Base
0x300  Event_Base ... events ...      Event_Max
```

- A request and its response sit at the same offset from their base.
  `gen_rpc_id_names.py` stops with an error if they stop mirroring.
- `eh_rpc_id_map_v2_asserts.c` fails the build if the proto ranges outgrow the ranges
  the dispatcher maps.

## Adding an RPC

1. Add the `Req_X` / `Resp_X` (or `Event_X`) ids and messages to `rpc_v2.proto`,
   keeping the request and response at the same offset, and bump `Req_Max` /
   `Resp_Max` (or `Event_Max`).
2. Run `src/proto/build_proto.sh`.
3. Implement the host wrapper and the CP handler; the short name shows up in host logs
   with no further change.
