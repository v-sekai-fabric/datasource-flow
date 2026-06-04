// idtx-flow web spoke — React-Three-Fiber stage. Renders the Miroir avatar two
// ways: the USD geometry path (via the libidtx_core WASM reader / pre-baked
// JSON) and, when a .vrm URL is given, the @pixiv/three-vrm humanoid path.

import { Canvas } from "@react-three/fiber";
import { OrbitControls, Grid, Environment } from "@react-three/drei";
import { Suspense } from "react";
import { UsdAvatar } from "./UsdAvatar";
import { VrmAvatar } from "./VrmAvatar";

// Optional: point at a .vrm to use the three-vrm humanoid path.
const VRM_URL = import.meta.env.VITE_VRM_URL as string | undefined;
// Optional: point at a .usda to read live via the WASM core (else pre-baked JSON).
const USD_URL = import.meta.env.VITE_USD_URL as string | undefined;

export default function App() {
  return (
    <div style={{ position: "fixed", inset: 0, background: "#1a1c20" }}>
      <Canvas camera={{ position: [0, 1.2, 2.4], fov: 45 }} shadows>
        <hemisphereLight args={["#ffffff", "#404050", 1.0]} />
        <directionalLight position={[3, 5, 2]} intensity={1.2} castShadow />
        <Suspense fallback={null}>
          <Environment preset="city" />
          {VRM_URL ? <VrmAvatar url={VRM_URL} /> : <UsdAvatar usdUrl={USD_URL} />}
        </Suspense>
        <Grid args={[10, 10]} cellColor="#333" sectionColor="#555" infiniteGrid fadeDistance={20} />
        <OrbitControls target={[0, 0.9, 0]} makeDefault />
      </Canvas>
      <div style={{ position: "absolute", top: 12, left: 14, color: "#9aa", font: "13px monospace" }}>
        idtx-flow web spoke · three.js + R3F + three-vrm · USD via libidtx_core (wasm)
      </div>
    </div>
  );
}
