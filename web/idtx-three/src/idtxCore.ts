// JS interface to libidtx_core compiled to WASM (Emscripten). Loads the module
// (public/idtx_core.js + .wasm built by wasm/build-wasm.sh), writes a .usda into
// MEMFS, and calls the reader glue (idtxweb_usd_to_json) to get geometry — the
// same core every other host consumes, here in the browser.
//
// Copyright 2026 The openusd-fabric authors / V-Sekai contributors.
// SPDX-License-Identifier: Apache-2.0 OR MPL-2.0

export interface IdtxPart {
  name: string;
  verts: number[];   // flat [x,y,z, ...]
  indices: number[];
}
export interface IdtxAvatarData {
  name: string;
  parts: IdtxPart[];
}

// The Emscripten MODULARIZE/EXPORT_ES6 factory shape (see build-wasm.sh).
interface CoreModule {
  ccall: (name: string, ret: string, argTypes: string[], args: unknown[]) => unknown;
  FS: { writeFile: (path: string, data: Uint8Array | string) => void };
  UTF8ToString: (ptr: number) => string;
  _idtxweb_free: (ptr: number) => void;
}

let modPromise: Promise<CoreModule> | null = null;

async function loadModule(): Promise<CoreModule> {
  // Built artifact; absent until `npm run build:wasm` (needs emsdk + USD-WASM).
  // Path is a variable so TS/Vite don't statically resolve the (optional) module.
  const url = "/idtx_core.js";
  const mod = (await import(/* @vite-ignore */ url)) as { default: () => Promise<CoreModule> };
  return mod.default();
}

function getModule(): Promise<CoreModule> {
  modPromise ??= loadModule();
  return modPromise;
}

/** True if the WASM core has been built and is loadable. */
export async function coreAvailable(): Promise<boolean> {
  try { await getModule(); return true; } catch { return false; }
}

/** Read a .usda (as text/bytes) into geometry via the WASM core reader. */
export async function usdToAvatar(usd: string | Uint8Array): Promise<IdtxAvatarData> {
  const mod = await getModule();
  const path = "/in.usda";
  mod.FS.writeFile(path, usd);
  const ptr = mod.ccall("idtxweb_usd_to_json", "number", ["string"], [path]) as number;
  const json = mod.UTF8ToString(ptr);
  mod._idtxweb_free(ptr);
  return JSON.parse(json) as IdtxAvatarData;
}

/** Fallback: load pre-baked geometry JSON (USD already read by the core offline). */
export async function loadPrebaked(url: string): Promise<IdtxAvatarData> {
  const res = await fetch(url);
  if (!res.ok) throw new Error(`failed to load ${url}: ${res.status}`);
  return (await res.json()) as IdtxAvatarData;
}
