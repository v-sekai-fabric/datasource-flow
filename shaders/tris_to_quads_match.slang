[[vk::binding(0, 0)]]
StructuredBuffer<uint> neighbour;
[[vk::binding(1, 0)]]
StructuredBuffer<float> weight;
[[vk::binding(2, 0)]]
RWStructuredBuffer<uint> out_best;
uint num_tris;

[shader("compute")] [numthreads(64, 1, 1)]
void match_triangles(uint3 tid : SV_DispatchThreadID) {
  uint i = tid.x;
  if ((i >= num_tris)) {
    return;
  }
  float best_w = -1.000000;
  uint best_j = 4294967295u;
  for (uint e = 0u; e < 3u; ++e) {
    uint j = neighbour[((i * 3u) + e)];
    if ((j != 4294967295u)) {
      float w = weight[((i * 3u) + e)];
      if ((w > best_w)) {
        best_w = w;
        best_j = j;
      }
    }
  }
  out_best[i] = best_j;
}