[[vk::binding(0, 0)]]
StructuredBuffer<uint> nearest_in;
[[vk::binding(1, 0)]]
RWStructuredBuffer<uint> nearest_out;
uint width_px;
uint stride;

[shader("compute")] [numthreads(8, 8, 1)]
void jfa_step(uint3 tid : SV_DispatchThreadID) {
  uint x = tid.x;
  uint y = tid.y;
  uint i = (x + (y * width_px));
  uint best = nearest_in[i];
  for (uint dy_idx = 0u; dy_idx < 3u; ++dy_idx) {
    for (uint dx_idx = 0u; dx_idx < 3u; ++dx_idx) {
      uint nx = (x + ((dx_idx - 1u) * stride));
      uint ny = (y + ((dy_idx - 1u) * stride));
      uint ni = (nx + (ny * width_px));
      uint cand = nearest_in[ni];
      if ((cand != 0u)) {
        best = ((best == 0u) ? cand : best);
      }
    }
  }
  nearest_out[i] = best;
}