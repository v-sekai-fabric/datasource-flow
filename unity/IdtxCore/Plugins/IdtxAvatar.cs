// Copyright 2026 The openusd-fabric authors / V-Sekai contributors.
// SPDX-License-Identifier: Apache-2.0 OR MPL-2.0

using System;
using IdtxCore.Native;

namespace IdtxCore
{
    /// <summary>
    /// Managed wrapper around an idtx_avatar_t native handle. Owns the
    /// handle; disposing destroys it and cascades destruction to the
    /// attached skeleton / meshes / materials.
    /// </summary>
    public sealed class IdtxAvatar : IDisposable
    {
        public AvatarHandle Handle { get; private set; }
        private bool _disposed;

        public IdtxAvatar()
        {
            Handle = NativeMethods.idtx_avatar_create();
            if (!Handle.IsValid)
            {
                throw new InvalidOperationException(
                    "idtx_avatar_create returned NULL — native library not loaded?");
            }
        }

        // For wrapping a handle returned by an import call.
        private IdtxAvatar(AvatarHandle handle) { Handle = handle; }

        public string Name
        {
            get
            {
                CheckNotDisposed();
                return NativeMethods.PtrToUtf8(NativeMethods.idtx_avatar_get_name(Handle));
            }
            set
            {
                CheckNotDisposed();
                NativeMethods.idtx_avatar_set_name(Handle, value ?? string.Empty);
            }
        }

        public int MeshCount
        {
            get { CheckNotDisposed(); return NativeMethods.idtx_avatar_get_mesh_count(Handle); }
        }

        public int MaterialCount
        {
            get { CheckNotDisposed(); return NativeMethods.idtx_avatar_get_material_count(Handle); }
        }

        public float[] GetRootTransform()
        {
            CheckNotDisposed();
            var m = new float[16];
            NativeMethods.idtx_avatar_get_root_transform(Handle, m);
            return m;
        }

        public void SetRootTransform(float[] matrix16)
        {
            CheckNotDisposed();
            if (matrix16 == null || matrix16.Length != 16)
                throw new ArgumentException("matrix16 must be a 16-element float array", nameof(matrix16));
            NativeMethods.idtx_avatar_set_root_transform(Handle, matrix16);
        }

        // ---------------------------------------------------------------
        // Top-level I/O. Static factories mirror the C ABI surface.
        // ---------------------------------------------------------------

        public static IdtxAvatar ImportFromUsd(string path)
        {
            if (string.IsNullOrEmpty(path)) throw new ArgumentNullException(nameof(path));
            var h = NativeMethods.idtx_core_import_avatar_from_usd(path);
            return h.IsValid ? new IdtxAvatar(h) : null;
        }

        public int ExportToUsd(string path)
        {
            CheckNotDisposed();
            if (string.IsNullOrEmpty(path)) throw new ArgumentNullException(nameof(path));
            return NativeMethods.idtx_core_export_avatar_to_usd(Handle, path);
        }

        public static IdtxAvatar ImportFromVrm(string path)
        {
            if (string.IsNullOrEmpty(path)) throw new ArgumentNullException(nameof(path));
            var h = NativeMethods.idtx_core_import_avatar_from_vrm(path);
            return h.IsValid ? new IdtxAvatar(h) : null;
        }

        public int ExportToVrm(string path)
        {
            CheckNotDisposed();
            if (string.IsNullOrEmpty(path)) throw new ArgumentNullException(nameof(path));
            return NativeMethods.idtx_core_export_avatar_to_vrm(Handle, path);
        }

        public static string CoreVersion()
        {
            return NativeMethods.PtrToUtf8(NativeMethods.idtx_core_version());
        }

        // ---------------------------------------------------------------
        // Disposal — calls idtx_avatar_destroy which cascades to children.
        // ---------------------------------------------------------------

        private void CheckNotDisposed()
        {
            if (_disposed) throw new ObjectDisposedException(nameof(IdtxAvatar));
        }

        public void Dispose()
        {
            if (_disposed) return;
            if (Handle.IsValid) NativeMethods.idtx_avatar_destroy(Handle);
            Handle = new AvatarHandle(IntPtr.Zero);
            _disposed = true;
            GC.SuppressFinalize(this);
        }

        ~IdtxAvatar() { Dispose(); }
    }
}
