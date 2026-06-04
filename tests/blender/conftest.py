# Copyright 2026 The openusd-fabric authors.
# SPDX-License-Identifier: MIT
#
# pytest hook: ensure the V-Sekai schema directory is on
# PXR_PLUGINPATH_NAME *before* pytest imports anything that pulls in
# pxr.Usd. USD's plugin registry warms at the first `from pxr import
# Usd` call (not at first stage open as the docstring upstream
# suggests), so a fixture in the test module would be too late — the
# UsdAPISchemaBase derived schemas would have been frozen out by then.

import os
from pathlib import Path

_SCHEMA = (Path(__file__).resolve().parents[2] / "schema").resolve()
_current = os.environ.get("PXR_PLUGINPATH_NAME", "")
_parts = [p for p in _current.split(os.pathsep) if p]
if str(_SCHEMA) not in _parts:
    _parts.insert(0, str(_SCHEMA))
    os.environ["PXR_PLUGINPATH_NAME"] = os.pathsep.join(_parts)
