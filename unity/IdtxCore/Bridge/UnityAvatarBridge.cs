// Copyright 2026 V-Sekai contributors.
// SPDX-License-Identifier: Apache-2.0 OR MPL-2.0
//
// UnityAvatarBridge — walks a UnityEngine.GameObject hierarchy and
// populates an idtx_avatar_t via the C ABI. Symmetric to the Godot
// adapter's GodotAvatarBuilder. The reverse direction (idtx_avatar
// -> GameObject) lives in this same class as AvatarToGameObject.

using System.Collections.Generic;
using IdtxCore.Native;
using UnityEngine;

namespace IdtxCore.Bridge
{
    public static class UnityAvatarBridge
    {
        /// <summary>
        /// Walk `root`'s descendants and build an IdtxAvatar populated with
        /// skeleton (from any SkinnedMeshRenderer's bone array) and meshes
        /// (from every MeshFilter+MeshRenderer and SkinnedMeshRenderer).
        ///
        /// Caller owns the returned IdtxAvatar and must Dispose it.
        /// </summary>
        public static IdtxAvatar AvatarFromGameObject(GameObject root)
        {
            if (root == null) return null;

            var avatar = new IdtxAvatar { Name = root.name };
            avatar.SetRootTransform(MatrixToFloat16(root.transform.localToWorldMatrix));

            // Skeleton: use the first SkinnedMeshRenderer's bones as the
            // canonical bone list. Multi-skeleton avatars are reduced to
            // one for the MVP — same constraint as the Godot adapter.
            var firstSmr = root.GetComponentInChildren<SkinnedMeshRenderer>();
            Transform[] skeletonBones = null;
            int[] boneIndexInSkeleton = null;
            if (firstSmr != null && firstSmr.bones != null && firstSmr.bones.Length > 0)
            {
                skeletonBones = firstSmr.bones;
                boneIndexInSkeleton = new int[skeletonBones.Length];
                var skel = BuildSkeleton(skeletonBones, firstSmr.rootBone, boneIndexInSkeleton);
                NativeMethods.idtx_avatar_set_skeleton(avatar.Handle, skel);
            }

            // Materials are deduplicated by reference identity so two
            // renderers sharing a Material end up referencing the same
            // idtx_material slot on the avatar.
            var materialCache = new Dictionary<Material, int>();

            // Static meshes (MeshFilter + MeshRenderer)
            foreach (var mf in root.GetComponentsInChildren<MeshFilter>())
            {
                var mesh = mf.sharedMesh;
                if (mesh == null) continue;
                var mr = mf.GetComponent<MeshRenderer>();
                AddMeshSurfaces(avatar.Handle, mesh, mf.gameObject.name, mr?.sharedMaterials, materialCache, null, null);
            }

            // Skinned meshes (SkinnedMeshRenderer)
            foreach (var smr in root.GetComponentsInChildren<SkinnedMeshRenderer>())
            {
                var mesh = smr.sharedMesh;
                if (mesh == null) continue;
                AddMeshSurfaces(avatar.Handle, mesh, smr.gameObject.name, smr.sharedMaterials, materialCache,
                                smr.bones, boneIndexInSkeleton);
            }

            return avatar;
        }

        // ---------------------------------------------------------------
        // Helpers
        // ---------------------------------------------------------------

        // Pack a UnityEngine.Matrix4x4 (column-major) into a row-major
        // float[16] — the layout idtx_core uses. Unity stores m[col,row]
        // so transposing here lines up with idtx's row-major convention.
        private static float[] MatrixToFloat16(Matrix4x4 m)
        {
            // Unity's Matrix4x4 is column-vector (translation in column 3, i.e.
            // m[0,3]/m[1,3]/m[2,3]). The idtx C ABI is USD's row-vector layout:
            // translation in row 3, bytes 12..14 (see FlatTreeTypeConverter). The
            // boundary pins ONE canonical convention, so each host adapter converts
            // to it exactly once -- transpose here, or translations land in the
            // wrong half and read back as zero (every bone collapses to the origin).
            var o = new float[16];
            for (int row = 0; row < 4; ++row)
            {
                for (int col = 0; col < 4; ++col)
                {
                    o[row * 4 + col] = m[col, row];
                }
            }
            return o;
        }

        private static SkeletonHandle BuildSkeleton(
            Transform[] bones,
            Transform rootBone,
            int[] outBoneIndexInSkeleton)
        {
            var skel = NativeMethods.idtx_skeleton_create();
            NativeMethods.idtx_skeleton_set_name(skel, rootBone != null ? rootBone.name : "Skeleton");

            // Build a Transform -> array-index map for parent lookup.
            var transformToIdx = new Dictionary<Transform, int>(bones.Length);
            for (int i = 0; i < bones.Length; ++i)
            {
                if (bones[i] != null && !transformToIdx.ContainsKey(bones[i]))
                    transformToIdx[bones[i]] = i;
            }

            for (int i = 0; i < bones.Length; ++i)
            {
                var t = bones[i];
                if (t == null)
                {
                    NativeMethods.idtx_skeleton_add_bone(skel, $"_missing_{i}", -1,
                        IdentityMatrix16(), IdentityMatrix16());
                    outBoneIndexInSkeleton[i] = i;
                    continue;
                }

                // Parent index = index of t.parent in the bones array,
                // or -1 if the parent is the skeleton root / outside.
                int parent = -1;
                if (t.parent != null && transformToIdx.TryGetValue(t.parent, out int p))
                    parent = p;

                // rest = local transform; bind = world transform (the
                // bone's pose when it was bound, which Unity's
                // SkinnedMeshRenderer captures via bindposes — but
                // bindposes[i] is inverse(boneToWorld), so the bind
                // matrix proper is boneToWorld = t.localToWorldMatrix.
                int added = NativeMethods.idtx_skeleton_add_bone(
                    skel,
                    t.name,
                    parent,
                    MatrixToFloat16(Matrix4x4.TRS(t.localPosition, t.localRotation, t.localScale)),
                    MatrixToFloat16(t.localToWorldMatrix));
                outBoneIndexInSkeleton[i] = added;
            }
            return skel;
        }

        // Where extracted diffuse textures are written. Absolute paths get baked
        // into the exported UsdUVTexture `file` inputs; the importer reads them
        // back via OpenUSD's asset resolver. Override before AvatarFromGameObject
        // to place them next to the .usd output.
        public static string TextureDir =
            System.IO.Path.Combine(System.IO.Path.GetTempPath(), "idtx_export_textures");

        // Blit any (possibly non-readable / compressed) Texture to a readable RT,
        // encode to PNG, and write it. Returns the absolute path, or null on error.
        private static string ExportTexturePng(Texture tex, string baseName)
        {
            if (tex == null) return null;
            try
            {
                System.IO.Directory.CreateDirectory(TextureDir);
                int w = Mathf.Max(tex.width, 4), h = Mathf.Max(tex.height, 4);
                var rt = RenderTexture.GetTemporary(w, h, 0, RenderTextureFormat.ARGB32, RenderTextureReadWrite.sRGB);
                Graphics.Blit(tex, rt);
                var prev = RenderTexture.active;
                RenderTexture.active = rt;
                var readable = new Texture2D(w, h, TextureFormat.RGBA32, false);
                readable.ReadPixels(new Rect(0, 0, w, h), 0, 0);
                readable.Apply();
                RenderTexture.active = prev;
                RenderTexture.ReleaseTemporary(rt);
                byte[] png = readable.EncodeToPNG();
                Object.DestroyImmediate(readable);
                string safe = baseName;
                foreach (char c in System.IO.Path.GetInvalidFileNameChars())
                    safe = safe.Replace(c, '_');
                string path = System.IO.Path.Combine(TextureDir, safe + ".png").Replace("\\", "/");
                System.IO.File.WriteAllBytes(path, png);
                return path;
            }
            catch (System.Exception e)
            {
                Debug.LogWarning("[IdtxCore] texture export failed for " + baseName + ": " + e.Message);
                return null;
            }
        }

        private static int AddOrLookupMaterial(
            AvatarHandle avatar,
            Dictionary<Material, int> cache,
            Material mat)
        {
            if (mat == null) return -1;
            if (cache.TryGetValue(mat, out int existing)) return existing;
            var mh = NativeMethods.idtx_material_create();
            NativeMethods.idtx_material_set_name(mh, mat.name);
            // Best-effort: read color/glossiness from Standard/URP-Lit
            // shader uniforms. Unrecognised shaders fall through to the
            // default PBR baseline set by idtx_material_create.
            if (mat.HasProperty("_Color"))
            {
                var c = mat.GetColor("_Color");
                NativeMethods.idtx_material_set_base_color(mh, c.r, c.g, c.b, c.a);
            }
            else if (mat.HasProperty("_BaseColor"))
            {
                var c = mat.GetColor("_BaseColor");
                NativeMethods.idtx_material_set_base_color(mh, c.r, c.g, c.b, c.a);
            }
            if (mat.HasProperty("_Metallic"))
                NativeMethods.idtx_material_set_metallic(mh, mat.GetFloat("_Metallic"));
            if (mat.HasProperty("_Glossiness"))
                NativeMethods.idtx_material_set_roughness(mh, 1.0f - mat.GetFloat("_Glossiness"));
            else if (mat.HasProperty("_Smoothness"))
                NativeMethods.idtx_material_set_roughness(mh, 1.0f - mat.GetFloat("_Smoothness"));

            // Diffuse texture: lilToon/UTS/Standard expose the albedo on _MainTex
            // (URP-Lit on _BaseMap). Extract it to a PNG and reference it by
            // absolute path so it round-trips through the exported UsdUVTexture.
            Texture mainTex = null;
            if (mat.HasProperty("_MainTex")) mainTex = mat.GetTexture("_MainTex");
            else if (mat.HasProperty("_BaseMap")) mainTex = mat.GetTexture("_BaseMap");
            if (mainTex != null)
            {
                string texPath = ExportTexturePng(mainTex, mat.name + "_albedo");
                if (!string.IsNullOrEmpty(texPath))
                    NativeMethods.idtx_material_set_base_color_texture(mh, texPath);
            }

            int idx = NativeMethods.idtx_avatar_add_material(avatar, mh);
            cache[mat] = idx;
            return idx;
        }

        private static void AddMeshSurfaces(
            AvatarHandle avatar,
            Mesh mesh,
            string baseName,
            Material[] perSubmeshMaterials,
            Dictionary<Material, int> materialCache,
            Transform[] smrBones,
            int[] boneIndexInSkeleton)
        {
            int subCount = mesh.subMeshCount;
            var positions = MeshToFloat3Array(mesh.vertices);
            var normals = mesh.normals != null && mesh.normals.Length == mesh.vertexCount
                          ? MeshToFloat3Array(mesh.normals)
                          : null;
            var uvs = mesh.uv != null && mesh.uv.Length == mesh.vertexCount
                      ? MeshToFloat2Array(mesh.uv)
                      : null;
            var colors = mesh.colors != null && mesh.colors.Length == mesh.vertexCount
                         ? MeshColorsToFloat4Array(mesh.colors)
                         : null;

            // Skinning, if SkinnedMeshRenderer-sourced.
            int bpv = 0;
            int[] boneIndices = null;
            float[] weights = null;
            if (smrBones != null && mesh.boneWeights != null && mesh.boneWeights.Length == mesh.vertexCount)
            {
                bpv = 4;  // Unity BoneWeight has 4 fixed slots
                boneIndices = new int[mesh.vertexCount * 4];
                weights = new float[mesh.vertexCount * 4];
                var bws = mesh.boneWeights;
                for (int i = 0; i < bws.Length; ++i)
                {
                    boneIndices[i * 4 + 0] = MapBone(bws[i].boneIndex0, boneIndexInSkeleton);
                    boneIndices[i * 4 + 1] = MapBone(bws[i].boneIndex1, boneIndexInSkeleton);
                    boneIndices[i * 4 + 2] = MapBone(bws[i].boneIndex2, boneIndexInSkeleton);
                    boneIndices[i * 4 + 3] = MapBone(bws[i].boneIndex3, boneIndexInSkeleton);
                    weights[i * 4 + 0] = bws[i].weight0;
                    weights[i * 4 + 1] = bws[i].weight1;
                    weights[i * 4 + 2] = bws[i].weight2;
                    weights[i * 4 + 3] = bws[i].weight3;
                }
            }

            for (int s = 0; s < subCount; ++s)
            {
                var indices = mesh.GetIndices(s);
                if (indices == null || indices.Length == 0) continue;

                var mh = NativeMethods.idtx_mesh_create();
                NativeMethods.idtx_mesh_set_name(mh, $"{baseName}_{s}");
                NativeMethods.idtx_mesh_set_vertices(
                    mh, mesh.vertexCount, positions, normals, uvs, colors);
                NativeMethods.idtx_mesh_set_indices(mh, indices.Length, indices);
                if (bpv > 0)
                    NativeMethods.idtx_mesh_set_skinning(mh, bpv, boneIndices, weights);

                int matIdx = -1;
                if (perSubmeshMaterials != null && s < perSubmeshMaterials.Length)
                    matIdx = AddOrLookupMaterial(avatar, materialCache, perSubmeshMaterials[s]);

                NativeMethods.idtx_avatar_add_mesh(avatar, mh, matIdx);
            }
        }

        private static int MapBone(int boneIndex, int[] indexInSkeleton)
        {
            if (indexInSkeleton == null) return boneIndex;
            if (boneIndex < 0 || boneIndex >= indexInSkeleton.Length) return -1;
            return indexInSkeleton[boneIndex];
        }

        private static float[] MeshToFloat3Array(Vector3[] vs)
        {
            var o = new float[vs.Length * 3];
            for (int i = 0; i < vs.Length; ++i)
            {
                o[i * 3 + 0] = vs[i].x;
                o[i * 3 + 1] = vs[i].y;
                o[i * 3 + 2] = vs[i].z;
            }
            return o;
        }

        private static float[] MeshToFloat2Array(Vector2[] vs)
        {
            var o = new float[vs.Length * 2];
            for (int i = 0; i < vs.Length; ++i)
            {
                o[i * 2 + 0] = vs[i].x;
                o[i * 2 + 1] = vs[i].y;
            }
            return o;
        }

        private static float[] MeshColorsToFloat4Array(Color[] cs)
        {
            var o = new float[cs.Length * 4];
            for (int i = 0; i < cs.Length; ++i)
            {
                o[i * 4 + 0] = cs[i].r;
                o[i * 4 + 1] = cs[i].g;
                o[i * 4 + 2] = cs[i].b;
                o[i * 4 + 3] = cs[i].a;
            }
            return o;
        }

        private static float[] IdentityMatrix16()
        {
            return new float[16]
            {
                1, 0, 0, 0,
                0, 1, 0, 0,
                0, 0, 1, 0,
                0, 0, 0, 1,
            };
        }

        // ---------------------------------------------------------------
        // Reverse direction — idtx_avatar -> GameObject hierarchy.
        // ---------------------------------------------------------------

        /// <summary>
        /// Reconstruct a Unity GameObject hierarchy from an IdtxAvatar.
        /// Creates a root GameObject, bone Transforms (if a skeleton is
        /// attached), and a SkinnedMeshRenderer (skinned meshes) or
        /// MeshFilter+MeshRenderer (static meshes) per idtx_mesh.
        /// Caller owns the returned GameObject.
        /// </summary>
        public static GameObject AvatarToGameObject(IdtxAvatar avatar)
        {
            if (avatar == null) return null;

            var root = new GameObject(string.IsNullOrEmpty(avatar.Name) ? "IdtxAvatar" : avatar.Name);
            var rootXform = avatar.GetRootTransform();
            ApplyMatrix16(root.transform, rootXform);

            // Build bone Transform array from idtx_skeleton (if any).
            Transform[] boneTransforms = null;
            var skelHandle = NativeMethods.idtx_avatar_get_skeleton(avatar.Handle);
            if (skelHandle.IsValid)
            {
                int n = NativeMethods.idtx_skeleton_get_bone_count(skelHandle);
                boneTransforms = new Transform[n];
                for (int i = 0; i < n; ++i)
                {
                    string name = NativeMethods.PtrToUtf8(
                        NativeMethods.idtx_skeleton_get_bone_name(skelHandle, i));
                    var t = new GameObject(name).transform;
                    boneTransforms[i] = t;
                }
                // Second pass: parent up.
                for (int i = 0; i < n; ++i)
                {
                    int parent = NativeMethods.idtx_skeleton_get_bone_parent(skelHandle, i);
                    Transform parentT = (parent < 0 || parent >= n) ? root.transform : boneTransforms[parent];
                    boneTransforms[i].SetParent(parentT, false);

                    var rest = new float[16];
                    NativeMethods.idtx_skeleton_get_bone_rest(skelHandle, i, rest);
                    ApplyMatrix16(boneTransforms[i], rest);
                }
            }

            // Materials (Unity Material instances, kept by index).
            int matCount = avatar.MaterialCount;
            var unityMats = new Material[matCount];
            for (int i = 0; i < matCount; ++i)
            {
                var mh = NativeMethods.idtx_avatar_get_material(avatar.Handle, i);
                unityMats[i] = MaterialFromHandle(mh);
            }

            int meshCount = avatar.MeshCount;
            for (int i = 0; i < meshCount; ++i)
            {
                var mh = NativeMethods.idtx_avatar_get_mesh(avatar.Handle, i);
                if (!mh.IsValid) continue;
                int matIdx = NativeMethods.idtx_avatar_get_mesh_material(avatar.Handle, i);
                Material mat = (matIdx >= 0 && matIdx < matCount) ? unityMats[matIdx] : null;

                var unityMesh = MeshFromHandle(mh);
                if (unityMesh == null) continue;

                bool isSkinned = boneTransforms != null
                    && NativeMethods.idtx_mesh_get_bones_per_vertex(mh) > 0;

                var meshName = NativeMethods.PtrToUtf8(NativeMethods.idtx_mesh_get_name(mh));
                var go = new GameObject(string.IsNullOrEmpty(meshName) ? $"Mesh_{i}" : meshName);
                go.transform.SetParent(root.transform, false);

                if (isSkinned)
                {
                    var smr = go.AddComponent<SkinnedMeshRenderer>();
                    smr.sharedMesh = unityMesh;
                    smr.bones = boneTransforms;
                    smr.rootBone = boneTransforms.Length > 0 ? boneTransforms[0] : root.transform;
                    if (mat != null) smr.sharedMaterial = mat;
                }
                else
                {
                    var mf = go.AddComponent<MeshFilter>();
                    mf.sharedMesh = unityMesh;
                    var mr = go.AddComponent<MeshRenderer>();
                    if (mat != null) mr.sharedMaterial = mat;
                }
            }

            return root;
        }

        private static Material MaterialFromHandle(MaterialHandle mh)
        {
            if (!mh.IsValid) return null;
            // Use the Standard shader by default; consumers can swap to
            // URP-Lit or MToon-equivalent post-construction.
            var standard = Shader.Find("Standard");
            var m = new Material(standard != null ? standard : Shader.Find("Diffuse"));
            m.name = NativeMethods.PtrToUtf8(NativeMethods.idtx_material_get_name(mh));

            var rgba = new float[4];
            NativeMethods.idtx_material_get_base_color(mh, rgba);
            if (m.HasProperty("_Color"))
                m.SetColor("_Color", new Color(rgba[0], rgba[1], rgba[2], rgba[3]));
            else if (m.HasProperty("_BaseColor"))
                m.SetColor("_BaseColor", new Color(rgba[0], rgba[1], rgba[2], rgba[3]));

            if (m.HasProperty("_Metallic"))
                m.SetFloat("_Metallic", NativeMethods.idtx_material_get_metallic(mh));
            if (m.HasProperty("_Glossiness"))
                m.SetFloat("_Glossiness", 1.0f - NativeMethods.idtx_material_get_roughness(mh));
            else if (m.HasProperty("_Smoothness"))
                m.SetFloat("_Smoothness", 1.0f - NativeMethods.idtx_material_get_roughness(mh));

            return m;
        }

        private static Mesh MeshFromHandle(MeshHandle mh)
        {
            int vc = NativeMethods.idtx_mesh_get_vertex_count(mh);
            int ic = NativeMethods.idtx_mesh_get_index_count(mh);
            if (vc <= 0 || ic <= 0) return null;

            var positions = new float[vc * 3];
            NativeMethods.idtx_mesh_get_positions(mh, positions);
            var verts = new Vector3[vc];
            for (int i = 0; i < vc; ++i)
                verts[i] = new Vector3(positions[i * 3], positions[i * 3 + 1], positions[i * 3 + 2]);

            var mesh = new Mesh();
            mesh.name = NativeMethods.PtrToUtf8(NativeMethods.idtx_mesh_get_name(mh));
            if (vc > 65535) mesh.indexFormat = UnityEngine.Rendering.IndexFormat.UInt32;
            mesh.vertices = verts;

            if (NativeMethods.idtx_mesh_has_normals(mh) != 0)
            {
                var n = new float[vc * 3];
                NativeMethods.idtx_mesh_get_normals(mh, n);
                var ns = new Vector3[vc];
                for (int i = 0; i < vc; ++i)
                    ns[i] = new Vector3(n[i * 3], n[i * 3 + 1], n[i * 3 + 2]);
                mesh.normals = ns;
            }
            if (NativeMethods.idtx_mesh_has_uvs(mh) != 0)
            {
                var u = new float[vc * 2];
                NativeMethods.idtx_mesh_get_uvs(mh, u);
                var us = new Vector2[vc];
                for (int i = 0; i < vc; ++i)
                    us[i] = new Vector2(u[i * 2], u[i * 2 + 1]);
                mesh.uv = us;
            }
            if (NativeMethods.idtx_mesh_has_colors(mh) != 0)
            {
                var c = new float[vc * 4];
                NativeMethods.idtx_mesh_get_colors(mh, c);
                var cs = new Color[vc];
                for (int i = 0; i < vc; ++i)
                    cs[i] = new Color(c[i * 4], c[i * 4 + 1], c[i * 4 + 2], c[i * 4 + 3]);
                mesh.colors = cs;
            }

            var idx = new int[ic];
            NativeMethods.idtx_mesh_get_indices(mh, idx);
            mesh.SetTriangles(idx, 0);

            int bpv = NativeMethods.idtx_mesh_get_bones_per_vertex(mh);
            if (bpv == 4)
            {
                var bi = new int[vc * 4];
                var bw = new float[vc * 4];
                NativeMethods.idtx_mesh_get_bone_indices(mh, bi);
                NativeMethods.idtx_mesh_get_weights(mh, bw);
                var bws = new BoneWeight[vc];
                for (int i = 0; i < vc; ++i)
                {
                    bws[i].boneIndex0 = bi[i * 4 + 0];
                    bws[i].boneIndex1 = bi[i * 4 + 1];
                    bws[i].boneIndex2 = bi[i * 4 + 2];
                    bws[i].boneIndex3 = bi[i * 4 + 3];
                    bws[i].weight0    = bw[i * 4 + 0];
                    bws[i].weight1    = bw[i * 4 + 1];
                    bws[i].weight2    = bw[i * 4 + 2];
                    bws[i].weight3    = bw[i * 4 + 3];
                }
                mesh.boneWeights = bws;
            }

            mesh.RecalculateBounds();
            return mesh;
        }

        private static void ApplyMatrix16(Transform t, float[] m)
        {
            // Convert the row-major matrix back into a Unity Matrix4x4
            // (which is column-major in storage), then decompose into
            // localPosition / localRotation / localScale.
            var mat = new Matrix4x4();
            for (int row = 0; row < 4; ++row)
                for (int col = 0; col < 4; ++col)
                    mat[row, col] = m[row * 4 + col];

            var pos = new Vector3(mat.m03, mat.m13, mat.m23);
            // Decompose: rotation from the orthogonalised basis,
            // scale from each basis vector's length.
            var sx = new Vector3(mat.m00, mat.m10, mat.m20).magnitude;
            var sy = new Vector3(mat.m01, mat.m11, mat.m21).magnitude;
            var sz = new Vector3(mat.m02, mat.m12, mat.m22).magnitude;
            var rot = mat.rotation;

            t.localPosition = pos;
            t.localRotation = rot;
            t.localScale = new Vector3(sx, sy, sz);
        }
    }
}
