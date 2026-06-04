"""
Smoke test: load the hdVSekaiOutlineJFA Hydra plugin and verify
TfType registration plus discovery via PXR_PLUGINPATH_NAME.

Copyright 2026 The openusd-fabric authors.
SPDX-License-Identifier: MIT

Run via:
    pixi run python -m pytest tests/hydra/

If the plugin's shared library has not been built yet, the test is
skipped (not failed) so CI without the C++ build still passes the
Python-only suites. The CI matrix that includes the C++ build runs
this test with the build artifact in place.
"""

from __future__ import annotations

import os
from pathlib import Path

import pytest

REPO_ROOT = Path(__file__).resolve().parents[2]
PLUGIN_BUILD_DIR = REPO_ROOT / "hydra" / "HdVSekaiOutlineJFA" / "build"


def _plugin_dll() -> Path | None:
    for name in ("hdVSekaiOutlineJFA.dll",
                 "libhdVSekaiOutlineJFA.so",
                 "libhdVSekaiOutlineJFA.dylib"):
        p = PLUGIN_BUILD_DIR / name
        if p.exists():
            return p
    return None


def test_plugin_dll_built():
    dll = _plugin_dll()
    if dll is None:
        pytest.skip("hdVSekaiOutlineJFA plugin not built; "
                    "run `pixi run hydra-jfa` first")
    assert dll.stat().st_size > 0


def test_plugin_loads_and_registers_task():
    dll = _plugin_dll()
    if dll is None:
        pytest.skip("hdVSekaiOutlineJFA plugin not built; "
                    "run `pixi run hydra-jfa` first")

    # Point USD at the plugin's resources/ before importing pxr — that's
    # where plugInfo.json lives; LibraryPath in the JSON is a relative
    # `../hdVSekaiOutlineJFA.dll`.
    resources = PLUGIN_BUILD_DIR / "resources"
    assert (resources / "plugInfo.json").exists(), (
        f"plugInfo.json missing under {resources}; CMakeLists configure_file "
        "did not run")

    sep = os.pathsep
    existing = os.environ.get("PXR_PLUGINPATH_NAME", "")
    os.environ["PXR_PLUGINPATH_NAME"] = (
        str(resources) + (sep + existing if existing else ""))

    from pxr import Plug, Tf  # noqa: E402

    # Force a plugin rescan against the resources directory in case
    # USD already initialised the registry before this test ran.
    Plug.Registry().RegisterPlugins(str(resources))

    plugin = Plug.Registry().GetPluginWithName("hdVSekaiOutlineJFA")
    assert plugin is not None, \
        "plugin not discovered under PXR_PLUGINPATH_NAME"
    # The library is loaded lazily; trigger a load by asking for our task
    # type and verifying it resolves.
    tf_type = Tf.Type.FindByName("HdVSekaiOutlineJFATask")
    assert not tf_type.isUnknown, \
        "HdVSekaiOutlineJFATask TfType is unknown after plugin load"
    # Loading the library should produce a non-zero plugin info record.
    assert plugin.metadata.get("Types", {}).get("HdVSekaiOutlineJFATask"), \
        "plugInfo.json Types entry for HdVSekaiOutlineJFATask missing"


if __name__ == "__main__":
    import sys
    sys.exit(pytest.main([__file__, "-v"]))
