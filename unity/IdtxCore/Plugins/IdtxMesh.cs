// Copyright 2026 The openusd-fabric authors / V-Sekai contributors.
// SPDX-License-Identifier: Apache-2.0 OR MPL-2.0

using System;
using IdtxCore.Native;

namespace IdtxCore
{
    /// <summary>
    /// Managed wrapper around an idtx_mesh_t native handle. When added
    /// to an IdtxAvatar, ownership transfers (avatar's Dispose frees it).
    /// </summary>
    public sealed class IdtxMesh : IDisposable
    {
        public MeshHandle Handle { get; private set; }
        private bool _disposed;
        private bool _owned = true;

        public IdtxMesh()
        {
            Handle = NativeMethods.idtx_mesh_create();
            if (!Handle.IsValid)
                throw new InvalidOperationException("idtx_mesh_create returned NULL");
        }

        internal IdtxMesh(MeshHandle handle, bool owned)
        {
            Handle = handle;
            _owned = owned;
            _disposed = !handle.IsValid;
        }

        public string Name
        {
            get { CheckNotDisposed(); return NativeMethods.PtrToUtf8(NativeMethods.idtx_mesh_get_name(Handle)); }
            set { CheckNotDisposed(); NativeMethods.idtx_mesh_set_name(Handle, value ?? string.Empty); }
        }

        public int VertexCount     { get { CheckNotDisposed(); return NativeMethods.idtx_mesh_get_vertex_count(Handle); } }
        public int IndexCount      { get { CheckNotDisposed(); return NativeMethods.idtx_mesh_get_index_count(Handle); } }
        public int BonesPerVertex  { get { CheckNotDisposed(); return NativeMethods.idtx_mesh_get_bones_per_vertex(Handle); } }
        public bool HasNormals     { get { CheckNotDisposed(); return NativeMethods.idtx_mesh_has_normals(Handle) != 0; } }
        public bool HasUvs         { get { CheckNotDisposed(); return NativeMethods.idtx_mesh_has_uvs(Handle) != 0; } }
        public bool HasColors      { get { CheckNotDisposed(); return NativeMethods.idtx_mesh_has_colors(Handle) != 0; } }

        /// <summary>
        /// Set vertex attribute arrays. positions is required;
        /// normals (length=vc*3), uvs (length=vc*2), colors (length=vc*4)
        /// are optional — pass null to skip.
        /// </summary>
        public void SetVertices(int vertexCount, float[] positions, float[] normals = null, float[] uvs = null, float[] colors = null)
        {
            CheckNotDisposed();
            if (vertexCount <= 0) throw new ArgumentException("vertexCount must be positive");
            if (positions == null || positions.Length != vertexCount * 3)
                throw new ArgumentException("positions must be float[vertexCount * 3]");
            NativeMethods.idtx_mesh_set_vertices(Handle, vertexCount, positions, normals, uvs, colors);
        }

        public void SetIndices(int[] indices)
        {
            CheckNotDisposed();
            if (indices == null || indices.Length == 0) throw new ArgumentException("indices required");
            NativeMethods.idtx_mesh_set_indices(Handle, indices.Length, indices);
        }

        public void SetSkinning(int bonesPerVertex, int[] boneIndices, float[] weights)
        {
            CheckNotDisposed();
            if (bonesPerVertex <= 0) throw new ArgumentException("bonesPerVertex must be positive");
            if (boneIndices == null || weights == null) throw new ArgumentException("boneIndices and weights required");
            NativeMethods.idtx_mesh_set_skinning(Handle, bonesPerVertex, boneIndices, weights);
        }

        public float[] GetPositions()  { CheckNotDisposed(); var b = new float[VertexCount * 3]; NativeMethods.idtx_mesh_get_positions(Handle, b); return b; }
        public float[] GetNormals()    { CheckNotDisposed(); if (!HasNormals) return null; var b = new float[VertexCount * 3]; NativeMethods.idtx_mesh_get_normals(Handle, b); return b; }
        public float[] GetUvs()        { CheckNotDisposed(); if (!HasUvs) return null; var b = new float[VertexCount * 2]; NativeMethods.idtx_mesh_get_uvs(Handle, b); return b; }
        public float[] GetColors()     { CheckNotDisposed(); if (!HasColors) return null; var b = new float[VertexCount * 4]; NativeMethods.idtx_mesh_get_colors(Handle, b); return b; }
        public int[]   GetIndices()    { CheckNotDisposed(); var b = new int[IndexCount]; NativeMethods.idtx_mesh_get_indices(Handle, b); return b; }
        public int[]   GetBoneIndices(){ CheckNotDisposed(); if (BonesPerVertex <= 0) return null; var b = new int[VertexCount * BonesPerVertex]; NativeMethods.idtx_mesh_get_bone_indices(Handle, b); return b; }
        public float[] GetWeights()    { CheckNotDisposed(); if (BonesPerVertex <= 0) return null; var b = new float[VertexCount * BonesPerVertex]; NativeMethods.idtx_mesh_get_weights(Handle, b); return b; }

        internal void ReleaseOwnership() { _owned = false; }

        private void CheckNotDisposed()
        { if (_disposed) throw new ObjectDisposedException(nameof(IdtxMesh)); }

        public void Dispose()
        {
            if (_disposed) return;
            if (_owned && Handle.IsValid) NativeMethods.idtx_mesh_destroy(Handle);
            Handle = new MeshHandle(IntPtr.Zero);
            _disposed = true;
            GC.SuppressFinalize(this);
        }

        ~IdtxMesh() { Dispose(); }
    }
}
