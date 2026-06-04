// Copyright 2026 V-Sekai contributors.
// SPDX-License-Identifier: Apache-2.0 OR MPL-2.0

using System;
using IdtxCore.Native;

namespace IdtxCore
{
    /// <summary>
    /// Managed wrapper around an idtx_skeleton_t native handle. When
    /// attached to an IdtxAvatar via Avatar.SetSkeleton, ownership
    /// transfers; the avatar's Dispose will free this skeleton. Until
    /// then, callers own it and must Dispose to avoid leaks.
    /// </summary>
    public sealed class IdtxSkeleton : IDisposable
    {
        public SkeletonHandle Handle { get; private set; }
        private bool _disposed;
        private bool _owned = true;

        public IdtxSkeleton()
        {
            Handle = NativeMethods.idtx_skeleton_create();
            if (!Handle.IsValid)
                throw new InvalidOperationException("idtx_skeleton_create returned NULL");
        }

        internal IdtxSkeleton(SkeletonHandle handle, bool owned)
        {
            Handle = handle;
            _owned = owned;
            _disposed = !handle.IsValid;
        }

        public string Name
        {
            get { CheckNotDisposed(); return NativeMethods.PtrToUtf8(NativeMethods.idtx_skeleton_get_name(Handle)); }
            set { CheckNotDisposed(); NativeMethods.idtx_skeleton_set_name(Handle, value ?? string.Empty); }
        }

        public int BoneCount
        {
            get { CheckNotDisposed(); return NativeMethods.idtx_skeleton_get_bone_count(Handle); }
        }

        /// <summary>
        /// Append a bone. parentIndex = -1 for root bones. rest/bind
        /// matrices are 16-element row-major. Returns the assigned bone index.
        /// </summary>
        public int AddBone(string name, int parentIndex, float[] restMatrix16, float[] bindMatrix16)
        {
            CheckNotDisposed();
            if (restMatrix16 == null || restMatrix16.Length != 16) throw new ArgumentException("restMatrix16 must be float[16]");
            if (bindMatrix16 == null || bindMatrix16.Length != 16) throw new ArgumentException("bindMatrix16 must be float[16]");
            return NativeMethods.idtx_skeleton_add_bone(Handle, name ?? string.Empty, parentIndex, restMatrix16, bindMatrix16);
        }

        public string GetBoneName(int index)
        { CheckNotDisposed(); return NativeMethods.PtrToUtf8(NativeMethods.idtx_skeleton_get_bone_name(Handle, index)); }

        public int GetBoneParent(int index)
        { CheckNotDisposed(); return NativeMethods.idtx_skeleton_get_bone_parent(Handle, index); }

        public float[] GetBoneRest(int index)
        { CheckNotDisposed(); var m = new float[16]; NativeMethods.idtx_skeleton_get_bone_rest(Handle, index, m); return m; }

        public float[] GetBoneBind(int index)
        { CheckNotDisposed(); var m = new float[16]; NativeMethods.idtx_skeleton_get_bone_bind(Handle, index, m); return m; }

        /// <summary>Marks this wrapper as no longer owning the native
        /// handle. Called internally when the skeleton has been attached
        /// to an avatar that took ownership.</summary>
        internal void ReleaseOwnership() { _owned = false; }

        private void CheckNotDisposed()
        { if (_disposed) throw new ObjectDisposedException(nameof(IdtxSkeleton)); }

        public void Dispose()
        {
            if (_disposed) return;
            if (_owned && Handle.IsValid) NativeMethods.idtx_skeleton_destroy(Handle);
            Handle = new SkeletonHandle(IntPtr.Zero);
            _disposed = true;
            GC.SuppressFinalize(this);
        }

        ~IdtxSkeleton() { Dispose(); }
    }
}
