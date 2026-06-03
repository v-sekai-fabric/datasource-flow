#!/usr/bin/env bash
# Copyright 2026 The openusd-fabric authors / V-Sekai contributors.
# SPDX-License-Identifier: Apache-2.0 OR MPL-2.0
#
# Parity smoke test (ART-47).
#
# Runs the same fixture through three deployment paths and asserts
# byte-equality:
#
#   1. CLI                    — tools/idtxcli (idtxcli bake / fetch)
#   2. Godot GDExtension      — addon/IDTXFlow/'s IDTXFlowChunker class
#                               (driven via `godot --headless --script`)
#   3. Godot ENGINE module    — multiplayer-fabric-godot's existing
#                               FabricMMOGAsset class after the
#                               libidtx_core wrapper port (ART-47 #13)
#
# All three compile from the same core/src/*.cpp files in libidtx_core,
# so the algorithm CANNOT diverge — this test only validates that the
# marshaling layers don't drop data.
#
# Prereqs:
#   - aria-storage running at $ARIA_URL (default http://localhost:4000)
#   - godot binary on $PATH (or set $GODOT)
#   - python3 with hashlib (stdlib)
#
# Usage:
#   ./parity_smoke_test.sh [fixture.bin]

set -euo pipefail

ARIA_URL="${ARIA_URL:-http://localhost:4000}"
GODOT="${GODOT:-godot}"
IDTXCLI="${IDTXCLI:-./bin/idtxcli}"

FIXTURE="${1:-fixture.bin}"

if [[ ! -f "$FIXTURE" ]]; then
    # Generate a deterministic 512KB fixture so the test runs without
    # external assets. Uses a PRNG seeded with a constant so the chunk
    # boundaries fall in known places.
    echo "fixture not found, generating $FIXTURE (512KB pseudo-random)"
    python3 - "$FIXTURE" <<'PY'
import random, sys
random.seed(0xdeadbeef)
with open(sys.argv[1], "wb") as f:
    f.write(bytes(random.randint(0, 255) for _ in range(512 * 1024)))
PY
fi

WORK=$(mktemp -d -t idtx-parity-XXXXXX)
trap 'rm -rf "$WORK"' EXIT
echo "workdir: $WORK"

cd "$WORK"
cp "$OLDPWD/$FIXTURE" fixture.bin
ORIG_SHA=$(python3 -c "import sys,hashlib; print(hashlib.sha256(open('fixture.bin','rb').read()).hexdigest())")
echo "fixture sha256: $ORIG_SHA"

# ── Path 1: CLI ─────────────────────────────────────────────────────────
echo "=== CLI bake/fetch ==="
CAIBX_CLI=$("$OLDPWD/$IDTXCLI" bake fixture.bin --aria "$ARIA_URL")
echo "  caibx: $CAIBX_CLI"
"$OLDPWD/$IDTXCLI" fetch "$CAIBX_CLI" --output cli_out.bin --aria "$ARIA_URL"
CLI_SHA=$(python3 -c "import sys,hashlib; print(hashlib.sha256(open('cli_out.bin','rb').read()).hexdigest())")
echo "  out sha256: $CLI_SHA"

# ── Path 2: Godot GDExtension headless ─────────────────────────────────
echo "=== Godot GDExtension bake/fetch ==="
cat > parity.gd <<'GD'
extends SceneTree

func _init() -> void:
    var args := OS.get_cmdline_user_args()
    var fixture := args[0]
    var aria := args[1]
    var c := IDTXFlowChunker.new()
    if not c.open(aria):
        push_error("open failed")
        quit(2)
        return
    var blob := FileAccess.get_file_as_bytes(fixture)
    var url := c.bake("parity_godot", blob)
    if url.is_empty():
        push_error("bake failed: " + c.last_error())
        quit(3)
        return
    print(url)
    var out := c.fetch(url)
    var f := FileAccess.open("godot_out.bin", FileAccess.WRITE)
    f.store_buffer(out)
    f.close()
    quit(0)
GD

if command -v "$GODOT" >/dev/null 2>&1; then
    GD_CAIBX=$("$GODOT" --headless --script parity.gd -- fixture.bin "$ARIA_URL" 2>&1 | tail -1)
    echo "  caibx: $GD_CAIBX"
    GODOT_SHA=$(python3 -c "import sys,hashlib; print(hashlib.sha256(open('godot_out.bin','rb').read()).hexdigest())")
    echo "  out sha256: $GODOT_SHA"
else
    echo "  godot not found on PATH; skipping GDExtension path"
    GODOT_SHA="$ORIG_SHA"
    GD_CAIBX="$CAIBX_CLI"
fi

# ── Path 3: Godot engine module ────────────────────────────────────────
# Skipped until ART-47 #13 lands (FabricMMOGAsset → libidtx_core
# wrapper in multiplayer-fabric-godot/modules/multiplayer_fabric_asset).
# When that ships, the same parity.gd will run against an engine-module
# build by setting GODOT to the engine-module-enabled binary.
echo "=== engine module path: pending ART-47 #13 ==="

# ── Asserts ────────────────────────────────────────────────────────────
echo "=== asserts ==="

fail=0
if [[ "$CLI_SHA" != "$ORIG_SHA" ]]; then
    echo "  FAIL: CLI round-trip sha mismatch ($CLI_SHA vs $ORIG_SHA)"
    fail=1
fi
if [[ "$GODOT_SHA" != "$ORIG_SHA" ]]; then
    echo "  FAIL: Godot round-trip sha mismatch ($GODOT_SHA vs $ORIG_SHA)"
    fail=1
fi

# Compare caibx chunk-id lists. Both should parse to identical chunk
# tables since the CDC + sha512_256 are byte-deterministic across hosts.
python3 - "$ARIA_URL" "$CAIBX_CLI" "$GD_CAIBX" <<'PY'
import sys, urllib.request, struct
def chunks_of(url):
    data = urllib.request.urlopen(url).read()
    # FormatIndex(48) + FormatTable(16) header, then [end_offset u64][id 32B]* terminated by 0-offset
    pos = 48 + 16
    out = []
    while True:
        (off,) = struct.unpack_from("<Q", data, pos); pos += 8
        if off == 0:
            break
        cid = data[pos:pos+32]; pos += 32
        out.append((off, cid.hex()))
    return out
a = chunks_of(sys.argv[2])
b = chunks_of(sys.argv[3])
ok = a == b
print(f"  CLI chunks: {len(a)}  Godot chunks: {len(b)}  identical: {ok}")
if not ok:
    sys.exit(1)
PY

if [[ $fail -eq 0 ]]; then
    echo "PASS: byte-identical round-trip across all available paths"
fi
exit $fail
