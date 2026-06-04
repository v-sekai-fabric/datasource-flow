# Standard Operating Procedures

Reusable procedures for the idtx-flow USD interop work. Each is the distilled
"how we do X" from building the Unity / Blender / Godot / Web spokes around the
single `libidtx_core`.

---

## 1. The `.sigs` dlopen ABI (every host)

`core/idtx_core.sigs` is the **single source of truth** for the exported C ABI.
No host links `libidtx_core` (nor its static OpenUSD); each loads it at runtime.

1. Add/change an export in `core/include/idtx_core/idtx_core.h` (with `IDTX_CORE_API`).
2. Add the matching one-line declaration to `core/idtx_core.sigs`.
3. The SCons step (`env.GenerateCoreStubs()` via `scons/generate_stubs.py`)
   regenerates `core/generated/`:
   - `idtx_core_stubs.{h,cc}` — POSIX dlsym forwarding thunks.
   - `libidtx_core.windows.def` — Windows delay-load export list.
   - **drift gate:** the build diffs `.sigs` against the header; a mismatch fails.
4. Hosts consume the table:
   - **Windows:** link an import lib derived from the `.def` (`lib /def:…`),
     `/DELAYLOAD:libidtx_core.<plat>.<arch>.dll` + `delayimp`; `LoadLibraryEx`
     the bundled DLL at init so the delay thunks bind it.
   - **POSIX:** compile `idtx_core_stubs.cc`; call `core::InitializeStubs({...})`.
   - **Web:** no delay-load — static-link the whole core into one `.wasm`.

## 2. Adding a host adapter

A host needs **export-to-USD** and **import-from-USD**, both through the core.
Hub-and-spoke: N hosts = **2N adapters**, covering all N(N-1) pairs. Do NOT
write per-pair converters.

- **Native (C/C++):** loader resolves the module dir, dlopens the core (§1),
  builds `idtx_avatar_t` from engine data → `idtx_core_export_avatar_to_usd_ex`,
  or reads via `idtx_core_import_avatar_from_usd` → engine meshes.
- **Managed/scripted (C#, Python, JS):** a thin binding faithful to `.sigs`:
  - C# Unity: `[DllImport("idtx_core")]` + `NativeLibrary.SetDllImportResolver`
    mapping the logical name to the real `libidtx_core.<plat>.<arch>.<ext>`.
  - Python (Blender / Unreal / CLI): `ctypes.CDLL`; declare every `argtypes`/
    `restype` (an undeclared opaque-handle arg is treated as `c_int` → overflow).
    Handle Py3.7 (no `os.add_dll_directory`) by prepending dep dirs to PATH.
  - JS (web): Emscripten `ccall` over a glue `.cpp` (§6).
- **Drift gate (always):** assert every binding entry point exists in
  `core/idtx_core.sigs` (Unity: `IdtxCoreAbiValidator`; Python: a regex check).

## 3. Git workflow

- Work on the **`v-sekai`** branch; push to `origin/v-sekai`. Sync at logical
  points with a descriptive commit (no `--amend`, no `--no-verify`).
- **Submodule → subtree:** `git submodule deinit -f X && git rm -f X &&
  rm -rf .git/modules/X`, commit the removal, then
  `git subtree add --prefix=X <url> <branch-or-SHA> --squash`. Vendor a **pinned
  SHA** (fetch it first) to preserve the exact revision; recurse for every
  submodule until `.gitmodules` is gone.
- **LFS is banned on the fork** (`@fire can not upload new objects to public
  fork`). If a `git add` routes a file through the LFS clean filter:
  `git lfs uninstall` + set `filter.lfs.{clean,smudge}=cat`, `filter.lfs.process=`,
  `filter.lfs.required=false`; re-checkout so the file is a normal blob.
  Subtree-vendored binaries arrive as plain blobs (read-tree bypasses clean) —
  only explicit `git add` of an LFS-attributed path trips it.

## 4. MCP servers per engine

One MCP per engine so an agent can drive it; prefer HTTP-streaming first-class.

- **Godot:** the addon **is** the server (`addons/godot-mcp/addons/godot_mcp/`,
  no Python) — Streamable-HTTP on `127.0.0.1:8788` via Godot's `JSONRPC`,
  constant-work command buffer, Origin enforcement opt-in. Loads from `addons/`
  (plural) per `project.godot`; keep tests pointed at the same dir (see §5).
- **Blender:** stdio via `uvx --from <repo> blender-mcp`; talks to the addon on
  `:9876`. **Unity:** IvanMurzak `com.ivanmurzak.unity.mcp` (HTTP `:23109`).
- Register: `claude mcp add --transport http <name> <url>` (HTTP) or
  `claude mcp add <name> -- <cmd…>` (stdio). The engine editor must be running
  with its addon for tool calls to reach it.

## 5. Verification

- **Native USD:** `usdchecker` is the authority — it is upstream, so it is more
  correct than we are; fix to satisfy it (idempotent xform via `MakeMatrixXform`,
  separate Materials scope to avoid mesh/material name collisions).
- **Godot MCP:** headless GDScript suites (`godot --headless --script …`); load
  from `res://addons/godot_mcp/` (the dir the editor loads — NOT a parallel
  `addon/` copy, which causes a stale split-brain). Clear
  `.godot/global_script_class_cache.cfg` after moving addon files.
- **ctypes host paths:** pytest against the real DLL (NEW_FLAT → OVERLAY →
  re-import → FLATTEN). Also run under the host's *own* interpreter (e.g. UE's
  bundled `python.exe`) to catch version gaps.
- **Web:** `tsc --noEmit`; run the Vite dev server and screenshot with
  `npx playwright screenshot --wait-for-timeout=4000 <url> out.png` (§7).

## 6. Building the core for the Web (Emscripten) — OUR build

Not an external "USD-WASM" dependency; we compile our own sources.

1. Install: `git clone emscripten-core/emsdk && python emsdk.py install latest &&
   python emsdk.py activate latest`. On Windows from bash the tools are
   `emcc.bat` / `emcmake.bat` (bash does not auto-resolve `.bat`).
2. `source <emsdk>/emsdk_env.sh` per shell (Bash-tool shell state does not
   persist — source inside the same command that builds).
3. `web/idtx-three/wasm/build-wasm.sh`: `emcmake` builds OpenUSD monolithic
   static (no Python/imaging), `em++` compiles `core/src` (reader + IR) + the
   glue, excluding engine-glue / transport / crypto sources. Output:
   `public/idtx_core.{js,wasm}`.

## 7. UE C++ project + build (when an Unreal host is in scope)

- **Blueprint → C++:** add `Source/<Name>{.Target.cs, Editor.Target.cs}` +
  `Source/<Name>/<Name>.{Build.cs,h,cpp}` with `IMPLEMENT_PRIMARY_GAME_MODULE`,
  and a `Modules` entry in the `.uproject`. Validate with
  `UnrealBuildTool.exe -projectfiles -project=<uproj> -game -engine`.
- **Build only the game target**, not the whole `.sln` — VS rebuilds the engine
  C# tools into `Program Files` (access-denied without elevation). Use
  `Build.bat <Name>Editor Win64 Development -project=<uproj>` (PowerShell `&`
  handles the spaced path; bash mangles it), or build just the game `.vcxproj`.
- A duplicate plugin module name (two `X.Build.cs` defining `class X`) fails the
  whole UBT rules assembly with `CS0101` — remove one copy.
- The VS MCP (`build_project`, `errors_list`, `output_read "Build"`) gives a live
  error loop, but UE C++ errors land in the **Build output pane**, not always the
  Error List.

## 8. Environment constraints

- **`E:\` is exFAT** — no reparse points, so junctions/symlinks/dir-hardlinks
  fail with "Incorrect function". Share a folder into a project via Unity's
  `file:` package ref (`Packages/manifest.json`) — resolves `src=Local`, read in
  place. A junction whose *link* is on C: (NTFS) pointing at an E: target works.
- **UE4.27 vs UE5:** 4.27 Python is 3.7 (no `os.add_dll_directory`); UE5-only
  types (`FVector3f/2f/2d`, `FTransform3f`, `TObjectPtr`) need a `#if
  ENGINE_MAJOR_VERSION < 5` compat shim; `FMath::Clamp(float, 0., 1.)` is
  ambiguous in 4.27 (use float literals).
