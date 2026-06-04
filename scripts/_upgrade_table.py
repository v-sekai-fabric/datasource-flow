"""
Shared runner for the Lean-emitted upgrade tables under maps/*.json.

Copyright 2026 The openusd-fabric authors.
SPDX-License-Identifier: MIT

Both scss_mtoon.py and the future shader-bridge modules use the same
"per-entry named transform" pattern: each JSON map entry has `xform`
strings (`color3_identity`, `fresnel_width_to_power`, ...) and the
runner dispatches forward and reverse transforms by name. Centralising
this avoids the three (Python / C# / future Rust) consumers drifting on
the transform semantics.

This module is intentionally NOT one of the consumer entry points.
Consumers (scss_mtoon.py etc.) compose a transform table and call
`run_forward` / `run_reverse`.
"""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any, Callable, Iterable

Transform = Callable[..., Any]


def load_map(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def run_forward(
    src_params: dict[str, Any],
    entries: Iterable[dict[str, Any]],
    transforms: dict[str, Transform],
    src_key: str = "scss",
    dst_key: str = "mtoon",
    extra_inputs: dict[str, dict[str, str]] | None = None,
) -> dict[str, Any]:
    """Run the forward direction of an upgrade table.

    For each entry whose `src_key` is in `src_params`, apply the named
    transform from `transforms`, optionally pulling extra parameters
    from `src_params` (e.g. `_Shadow` for `color3_via_shadow`) using
    the `extra_inputs[xform] = {kwarg_name: src_param_name}` map.
    """
    out: dict[str, Any] = {}
    extra_inputs = extra_inputs or {}
    for entry in entries:
        if entry[src_key] not in src_params:
            continue
        xform = entry["xform"]
        fn = transforms[xform]
        value = src_params[entry[src_key]]
        extras = extra_inputs.get(xform, {})
        if extras:
            kwargs = {kw: src_params.get(src_param) for kw, src_param in extras.items()}
            kwargs = {k: v for k, v in kwargs.items() if v is not None}
            out[entry[dst_key]] = fn(value, **kwargs)
        else:
            out[entry[dst_key]] = fn(value)
    return out


def run_reverse(
    src_params: dict[str, Any],
    entries: Iterable[dict[str, Any]],
    transforms: dict[str, Transform],
    src_key: str = "mtoon",
    dst_key: str = "scss",
    defaults: dict[str, Any] | None = None,
) -> dict[str, Any]:
    """Inverse of run_forward.

    `defaults` lists destination keys (e.g. `_Shadow`, `_Matcap1Strength`)
    that the reverse direction pins to a fixed value so the round trip
    is stable.
    """
    out: dict[str, Any] = {}
    for entry in entries:
        if entry[src_key] not in src_params:
            continue
        bn = transforms[entry["xform"]]
        out[entry[dst_key]] = bn(src_params[entry[src_key]])
    if defaults:
        for k, v in defaults.items():
            out.setdefault(k, v)
    return out
