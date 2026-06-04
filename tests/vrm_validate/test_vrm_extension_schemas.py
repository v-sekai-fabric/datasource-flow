"""
JSON-schema validation for idtx_core's emitted .vrm files.

Copyright 2026 The openusd-fabric authors / V-Sekai contributors.
SPDX-License-Identifier: MIT

Validates the `extensions.VRMC_vrm`, `extensions.VRMC_springBone`, and
each material's `extensions.VRMC_materials_mtoon` against the vendored
schemas from
[vrm-c/vrm-specification](https://github.com/vrm-c/vrm-specification/tree/master/specification).

Schemas vendored under `openusd-fabric/tests/fixtures/vrm_schemas/`.
Auto-skips if `jsonschema` isn't installed.

Plus a structural glTF 2.0 invariant the user got bitten by: every
mesh primitive with an `indices` accessor must have a count that is a
multiple of 3 (glTF only models triangle topology). Independent of
the VRM extension layer.
"""

from __future__ import annotations

import json
import struct
import sys
from pathlib import Path

import pytest

try:
    import jsonschema
    from jsonschema import RefResolver, validators
except ImportError:
    pytest.skip("jsonschema not installed (`pip install jsonschema`)",
                allow_module_level=True)

REPO = Path(__file__).resolve().parents[3]
SCHEMA_ROOT = REPO / "openusd-fabric" / "tests" / "fixtures" / "vrm_schemas"


def _parse_glb(vrm_path: Path) -> dict:
    data = vrm_path.read_bytes()
    if data[:4] != b"glTF":
        raise ValueError(f"{vrm_path} is not a GLB file")
    version, length = struct.unpack("<II", data[4:12])
    assert version == 2, f"unexpected glTF version {version}"
    p = 12
    while p < length:
        chunk_len, chunk_type = struct.unpack("<II", data[p:p+8])
        p += 8
        if chunk_type == 0x4E4F534A:  # JSON
            return json.loads(data[p:p+chunk_len])
        p += chunk_len
    raise ValueError(f"{vrm_path} has no JSON chunk")


def _load_schemas_from(ext_dir: Path) -> tuple[dict, dict]:
    """Return (top-level schema dict, {filename: schema} map for $ref
    resolution). The top-level is the one whose filename matches the
    extension name (e.g. VRMC_vrm.schema.json)."""
    store: dict[str, dict] = {}
    top = None
    expected_top = ext_dir.name.split("-")[0] + ".schema.json"
    for f in sorted(ext_dir.glob("*.json")):
        schema = json.loads(f.read_text(encoding="utf-8"))
        store[f.name] = schema
        if f.name == expected_top:
            top = schema
    assert top is not None, f"top schema not found in {ext_dir}"
    return top, store


def _validate(payload: dict, top_schema: dict, store: dict, ext_dir: Path):
    """Run jsonschema.Draft7Validator over `payload` with $ref resolution
    against the vendored sibling schemas."""
    base_uri = ext_dir.resolve().as_uri() + "/"
    # The store needs to be keyed by URI for RefResolver. We also rewrite
    # the schema's $id-style references to local files. The VRM schemas
    # use relative filenames in $ref, so the base_uri resolves them.
    resolver = RefResolver(
        base_uri=base_uri,
        referrer=top_schema,
        store={base_uri + name: s for name, s in store.items()})
    validator = jsonschema.Draft7Validator(top_schema, resolver=resolver)
    errors = sorted(validator.iter_errors(payload), key=lambda e: e.path)
    if errors:
        msg = "\n".join(
            f"  {list(e.path)}: {e.message}" for e in errors[:5])
        raise AssertionError(
            f"schema validation failed ({len(errors)} error(s), first 5):\n{msg}")


# --- Fixtures: every .vrm we want to gate -------------------------------------

def _candidate_vrms() -> list[Path]:
    """Discover .vrm files under build/ that exist after a recent run.
    Auto-skipped if none exist (e.g. fresh clone without a build)."""
    out: list[Path] = []
    for base in [REPO / "build" / "idtxcli", REPO / "build" / "mire_e2e"]:
        if base.exists():
            out.extend(base.glob("*.vrm"))
    return sorted(out)


@pytest.fixture(params=_candidate_vrms(), ids=lambda p: p.name)
def vrm_doc(request) -> dict:
    return _parse_glb(request.param)


def test_gltf_index_buffers_are_multiples_of_three(vrm_doc):
    """glTF 2.0 mandate: every primitive's index buffer must be a
    multiple of 3 (triangle topology). Catches the n-gon-leak class
    of bug we hit on Mire.vrm: the writer was dumping mixed-arity
    USD index buffers without triangulating."""
    bad: list[str] = []
    for mi, mesh in enumerate(vrm_doc.get("meshes", [])):
        for pi, prim in enumerate(mesh.get("primitives", [])):
            idx_acc = prim.get("indices")
            if idx_acc is None: continue
            count = vrm_doc["accessors"][idx_acc]["count"]
            if count % 3 != 0:
                bad.append(f"mesh[{mi}].primitives[{pi}].indices count={count} "
                           f"(not divisible by 3)")
    assert not bad, ("glTF triangle-only invariant broken:\n  "
                     + "\n  ".join(bad))


def test_vrmc_vrm_extension_schema(vrm_doc):
    """VRMC_vrm block matches the spec schema."""
    block = vrm_doc.get("extensions", {}).get("VRMC_vrm")
    if block is None:
        pytest.skip("no VRMC_vrm extension")
    ext_dir = SCHEMA_ROOT / "VRMC_vrm-1.0"
    if not ext_dir.exists():
        pytest.skip(f"schemas not vendored at {ext_dir}")
    top, store = _load_schemas_from(ext_dir)
    _validate(block, top, store, ext_dir)


def test_vrmc_springbone_extension_schema(vrm_doc):
    """VRMC_springBone block matches the spec schema."""
    block = vrm_doc.get("extensions", {}).get("VRMC_springBone")
    if block is None:
        pytest.skip("no VRMC_springBone extension")
    ext_dir = SCHEMA_ROOT / "VRMC_springBone-1.0"
    if not ext_dir.exists():
        pytest.skip(f"schemas not vendored at {ext_dir}")
    top, store = _load_schemas_from(ext_dir)
    _validate(block, top, store, ext_dir)


def test_vrmc_materials_mtoon_schema(vrm_doc):
    """Each material's VRMC_materials_mtoon block matches the spec."""
    ext_dir = SCHEMA_ROOT / "VRMC_materials_mtoon-1.0"
    if not ext_dir.exists():
        pytest.skip(f"schemas not vendored at {ext_dir}")
    top, store = _load_schemas_from(ext_dir)
    n_mtoon = 0
    for mi, mat in enumerate(vrm_doc.get("materials", [])):
        block = mat.get("extensions", {}).get("VRMC_materials_mtoon")
        if block is None: continue
        n_mtoon += 1
        _validate(block, top, store, ext_dir)
    if n_mtoon == 0:
        pytest.skip("no MToon materials in this VRM")
