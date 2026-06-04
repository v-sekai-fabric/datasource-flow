import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";

export default defineConfig({
  plugins: [react()],
  // The Emscripten glue + .wasm live in public/ and are fetched at runtime.
  assetsInclude: ["**/*.wasm"],
});
