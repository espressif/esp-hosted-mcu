"""Name the callers of allocations a cycle borrowed and did not return.

The firmware already decides WHETHER a cycle leaked: it traces exactly one
init and its matching deinit, so any record still outstanding belongs to an
allocation our init made. This module only answers WHO, by resolving the caller
addresses the firmware printed. It is a diagnostic, never the verdict - so it
can never turn an unresolved dump into a pass.
"""
import re

from .backtrace import _decode_addrs, _find_addr2line

# "[recycle] borrowed 84 B callers 0x4080 0x4200 0x4200 0x4200"
REC   = re.compile(r'\[recycle\] rec (\d+) B callers ([0-9a-fx ]+)')
CYCLE = re.compile(r'\[recycle\] cycle=(\d+) ')

_OURS = ('/esp_hosted/coprocessor/', '/esp_hosted/common/', '/esp_hosted/port/')


def table(named):
    """Per-cycle record table: bytes, block count, scope, and every frame."""
    import collections
    grouped = collections.OrderedDict()
    for cycle, size, frames, ours in named:
        key = (cycle, tuple(frames), ours)
        e = grouped.setdefault(key, [0, 0])
        e[0] += 1
        e[1] += size

    lines = [f"{'cycle':>5} {'bytes':>6} {'blocks':>6}  {'scope':<7} {'action':<7}",
             '-' * 44]
    for (cycle, frames, ours), (blocks, total) in grouped.items():
        scope = 'HOSTED' if ours else ('idf' if ours is False else 'unknown')
        action = 'FIX' if ours else ('ignore' if ours is False else 'RESOLVE')
        blk = str(blocks) if blocks > 1 else ''
        lines.append(f'{cycle:>5} {total:>6} {blk:>6}  {scope:<7} {action:<7}')
        for i, f in enumerate(frames):
            lines.append(f'          #{i} {f}')
    return lines

# The tracer wraps every allocation, so these frames head every chain. Skipping
# them is what lets the real caller be seen at all.
# Match on FILE, never on function name: 'malloc' as a substring also matches
# lwIP's mem_malloc and do_memp_malloc_pool, which threw away the real caller.
_ALLOCATOR = ('heap_trace.inc', 'heap_caps.c', 'multi_heap.c', 'heap_tlsf.c',
              'heap_private.h')


def name_borrowers(cp_text, cp_elf, chip='esp32c6'):
    """[(cycle, bytes, site, ours)] for each record the CP reported. Records are
    printed after their cycle line, so order gives the attribution."""
    recs, cycle = [], 0
    for line in cp_text.splitlines():
        m = CYCLE.search(line)
        if m:
            cycle = int(m.group(1))
            continue
        m = REC.search(line)
        if m:
            addrs = [a for a in m.group(2).split()
                     if a.startswith('0x') and a != '0x0']
            recs.append((cycle, int(m.group(1)), addrs))
    if not recs:
        return []

    addr2line = _find_addr2line(chip)
    syms = {}
    if addr2line and cp_elf:
        uniq = sorted({a for _, _, ch in recs for a in ch})
        # full paths: a leak report needs the file, not just the basename
        syms = _decode_addrs(addr2line, cp_elf, uniq, full_paths=True)

    out = []
    for cycle, size, addrs in recs:
        frames, ours = [], None
        for a in addrs:
            txt = syms.get(a)
            if not txt or any(n in txt for n in _ALLOCATOR):
                continue
            frames.append(txt)
            if ours is None:
                ours = True if any(n in txt for n in _OURS) else False
            elif not ours and any(n in txt for n in _OURS):
                ours = True                      # a hosted frame further out
        out.append((cycle, size, frames or ['unresolved'], ours))
    return out
