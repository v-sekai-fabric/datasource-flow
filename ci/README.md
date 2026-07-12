# Unity plugin CI/CD

CI for the **Unity** side of the project (`flow/adapters/unity/IdtxCore`, the
`com.vsekai.idtxcore` UPM package). The existing `gdextension-*` workflows
cover the Godot extension; these cover the Unity native plugin, which has a
distinct failure mode: a missing native companion DLL makes Unity's first
P/Invoke throw `DllNotFoundException("idtx_core")` and USD/USDZ export fails
with no obvious cause. That is exactly what these workflows guard against.

## The plugin builds without the Unity Editor

The Unity Editor is only needed to *run* the plugin, never to *build* it. The
whole package is produced by SCons:

```bash
scons unity_plugin target=template_release
```

`unity_plugin` (alias added in `scons/idtxcore.py`) builds only the Unity
deploy and its prerequisites — the OpenUSD extension (`libidtx_usd`) and the
core (`idtx_core`) — and skips the Godot/GDExtension build. It installs into
`flow/adapters/unity/IdtxCore/Plugins/<arch>/`:

| File | Role |
|---|---|
| `idtx_core.<ext>`   | the P/Invoke target (`[DllImport("idtx_core")]`) |
| `usd_ms.<ext>`      | monolithic OpenUSD, needed by idtx_core |
| `tbb12.<ext>`       | TBB, needed by usd_ms |
| `libidtx_usd.<ext>` | the V-Sekai OpenUSD schema extension, needed by idtx_core |
| `usd~/`             | OpenUSD plugin registry (PXR_PLUGINPATH_NAME target) |

The OS loader resolves the three companions from the same directory, so all
must be present beside `idtx_core`. `ci/verify_unity_plugin.py` asserts this.

### Per-platform builds are native

SCons builds OpenUSD from source for the host platform (`env.BuildOpenUSD`)
and auto-detects the platform — there is no cross-compile path. So the Windows
plugin is built on a Windows runner, the Linux plugin on an Ubuntu runner, etc.
(Locally on Windows you can produce a Linux build through WSL or Docker, but it
is the same native `scons unity_plugin` run inside a Linux environment.)

## Workflows

| File | Trigger | What it does |
|---|---|---|
| `unity-plugin-build.yaml`   | `workflow_call` | Build (Windows + macOS + Linux/Steam Deck on `ubuntu-22.04`), run the completeness guard, upload `com.vsekai.idtxcore-<OS>.zip`. |
| `unity-plugin-test.yaml`    | `workflow_call` | Consume the Linux build artifact, then run the headless EditMode export test (`ci/UnityExportTest`) via game-ci. Skipped if no Unity license secret. Call after `build` (`needs: build`). |

> **Steam Deck**: Steam Deck / SteamOS is x86_64 Linux — the `ubuntu-22.04`
> build (glibc 2.35) is the plugin it runs; its older glibc keeps the `.so`
> forward-compatible with SteamOS's newer glibc.
| `unity-plugin-pr.yaml`      | `pull_request` to `main` | Calls build + test on relevant path changes. |
| `unity-plugin-release.yaml` | `workflow_dispatch` | Version check from `package.json`, build, publish a `unity-v<version>` GitHub release with the packaged UPM zips. |

## Required secrets (headless test only)

The build + guard + release jobs need **no** secrets. The headless EditMode
export test needs a Unity license to activate the editor in CI:

- `UNITY_LICENSE` — contents of a personal `.ulf` license file, **or**
- `UNITY_EMAIL` + `UNITY_PASSWORD` (+ `UNITY_SERIAL` for Plus/Pro).

See <https://game.ci/docs/github/activation>. Without these the test job logs a
warning and is skipped, so forks still pass.

> Before enabling on a protected branch, pin `game-ci/unity-test-runner@v4` to
> a release commit SHA, matching the SHA-pinning convention in the other
> workflows.

## The bug this guards against

1. `scons/idtxcore.py` deployed companions under an `if os.path.exists(c)`
   guard evaluated at graph-construction time — before `libidtx_usd` was built
   — so it was silently dropped from the Unity deploy. (Fixed: companions are
   now installed unconditionally; a missing one halts the build.)
2. `.gitignore`'s `*.dll` meant only `idtx_core.dll` + `usd_ms.dll` were ever
   committed, so a fresh clone was missing `tbb12.dll` + `libidtx_usd.dll`.
   CI now builds and packages the complete set, so releases don't depend on
   committed binaries.
