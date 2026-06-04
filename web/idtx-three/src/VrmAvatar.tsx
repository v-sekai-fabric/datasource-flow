// Loads a VRM avatar with @pixiv/three-vrm inside React-Three-Fiber. VRM is the
// humanoid path for the Miroir-Re avatar (MToon materials + spring bones — the
// same V-Sekai schemas the USD pipeline carries). The avatar reaches the web
// either as a .vrm directly, or as USD that an offline step converts to VRM/glTF.

import { useEffect, useRef, useState } from "react";
import { useFrame } from "@react-three/fiber";
import { GLTFLoader } from "three/examples/jsm/loaders/GLTFLoader.js";
import { VRM, VRMLoaderPlugin, VRMUtils } from "@pixiv/three-vrm";
import * as THREE from "three";

// VRM0 meta carries `title`, VRM1 meta carries `name` — handle the union.
function vrmName(vrm: VRM): string {
  const m = vrm.meta as { name?: string; title?: string } | undefined;
  return m?.name ?? m?.title ?? "avatar";
}

export function VrmAvatar({ url }: { url: string }) {
  const [vrm, setVrm] = useState<VRM | null>(null);
  const groupRef = useRef<THREE.Group>(null);

  useEffect(() => {
    let cancelled = false;
    const loader = new GLTFLoader();
    loader.register((parser) => new VRMLoaderPlugin(parser));
    loader.load(
      url,
      (gltf) => {
        const loaded = gltf.userData.vrm as VRM | undefined;
        if (!loaded || cancelled) return;
        VRMUtils.removeUnnecessaryVertices(gltf.scene);
        VRMUtils.combineSkeletons(gltf.scene);
        loaded.scene.traverse((o) => (o.frustumCulled = false));
        setVrm(loaded);
      },
      undefined,
      (err) => console.error("[idtx] VRM load failed", err),
    );
    return () => {
      cancelled = true;
    };
  }, [url]);

  // three-vrm needs per-frame update for spring bones / look-at / expressions.
  useFrame((_, delta) => {
    vrm?.update(delta);
  });

  if (!vrm) return null;
  return (
    <group ref={groupRef} name={`vrm:${vrmName(vrm)}`}>
      <primitive object={vrm.scene} />
    </group>
  );
}
