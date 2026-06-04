// Renders an idtx avatar read from USD (via the WASM core, or pre-baked JSON)
// as THREE.BufferGeometry meshes — the through-USD path of the web spoke,
// mirroring how the Godot spoke builds ArrayMesh from the same core reader.

import { useEffect, useMemo, useState } from "react";
import * as THREE from "three";
import {
  IdtxAvatarData,
  coreAvailable,
  loadPrebaked,
  usdToAvatar,
} from "./idtxCore";

function partToGeometry(verts: number[], indices: number[]): THREE.BufferGeometry {
  const g = new THREE.BufferGeometry();
  g.setAttribute("position", new THREE.Float32BufferAttribute(verts, 3));
  g.setIndex(indices);
  g.computeVertexNormals();
  return g;
}

export function UsdAvatar({
  usdUrl,
  prebakedUrl = "/miroir.json",
}: {
  usdUrl?: string;
  prebakedUrl?: string;
}) {
  const [data, setData] = useState<IdtxAvatarData | null>(null);
  const [source, setSource] = useState<"wasm" | "prebaked" | "error">("prebaked");

  useEffect(() => {
    let cancelled = false;
    (async () => {
      try {
        // Prefer the live WASM core reading the .usda; fall back to pre-baked.
        if (usdUrl && (await coreAvailable())) {
          const usd = new Uint8Array(await (await fetch(usdUrl)).arrayBuffer());
          const avatar = await usdToAvatar(usd);
          if (!cancelled) { setData(avatar); setSource("wasm"); }
          return;
        }
        const avatar = await loadPrebaked(prebakedUrl);
        if (!cancelled) { setData(avatar); setSource("prebaked"); }
      } catch (e) {
        console.error("[idtx] USD load failed", e);
        if (!cancelled) setSource("error");
      }
    })();
    return () => { cancelled = true; };
  }, [usdUrl, prebakedUrl]);

  const meshes = useMemo(
    () =>
      data?.parts.map((p) => ({
        name: p.name,
        geometry: partToGeometry(p.verts, p.indices),
      })) ?? [],
    [data],
  );

  if (!data) return null;
  return (
    <group name={`usd:${data.name} (${source})`}>
      {meshes.map((m) => (
        <mesh key={m.name} name={m.name} geometry={m.geometry}>
          <meshStandardMaterial color="#c8b8a8" roughness={0.7} metalness={0.0} flatShading />
        </mesh>
      ))}
    </group>
  );
}
