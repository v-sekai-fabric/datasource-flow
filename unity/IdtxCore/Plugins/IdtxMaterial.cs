// Copyright 2026 The openusd-fabric authors / V-Sekai contributors.
// SPDX-License-Identifier: Apache-2.0 OR MPL-2.0

using System;
using IdtxCore.Native;

namespace IdtxCore
{
    /// <summary>
    /// Managed wrapper around an idtx_material_t native handle. When
    /// added to an IdtxAvatar, ownership transfers.
    /// </summary>
    public sealed class IdtxMaterial : IDisposable
    {
        public MaterialHandle Handle { get; private set; }
        private bool _disposed;
        private bool _owned = true;

        public IdtxMaterial()
        {
            Handle = NativeMethods.idtx_material_create();
            if (!Handle.IsValid)
                throw new InvalidOperationException("idtx_material_create returned NULL");
        }

        internal IdtxMaterial(MaterialHandle handle, bool owned)
        {
            Handle = handle;
            _owned = owned;
            _disposed = !handle.IsValid;
        }

        public string Name
        {
            get { CheckNotDisposed(); return NativeMethods.PtrToUtf8(NativeMethods.idtx_material_get_name(Handle)); }
            set { CheckNotDisposed(); NativeMethods.idtx_material_set_name(Handle, value ?? string.Empty); }
        }

        public void SetBaseColor(float r, float g, float b, float a)
        { CheckNotDisposed(); NativeMethods.idtx_material_set_base_color(Handle, r, g, b, a); }

        public float[] GetBaseColor()
        { CheckNotDisposed(); var c = new float[4]; NativeMethods.idtx_material_get_base_color(Handle, c); return c; }

        public float Metallic
        {
            get { CheckNotDisposed(); return NativeMethods.idtx_material_get_metallic(Handle); }
            set { CheckNotDisposed(); NativeMethods.idtx_material_set_metallic(Handle, value); }
        }
        public float Roughness
        {
            get { CheckNotDisposed(); return NativeMethods.idtx_material_get_roughness(Handle); }
            set { CheckNotDisposed(); NativeMethods.idtx_material_set_roughness(Handle, value); }
        }
        public AlphaMode AlphaMode
        {
            get { CheckNotDisposed(); return NativeMethods.idtx_material_get_alpha_mode(Handle); }
            set { CheckNotDisposed(); NativeMethods.idtx_material_set_alpha_mode(Handle, value); }
        }
        public float AlphaCutoff
        {
            get { CheckNotDisposed(); return NativeMethods.idtx_material_get_alpha_cutoff(Handle); }
            set { CheckNotDisposed(); NativeMethods.idtx_material_set_alpha_cutoff(Handle, value); }
        }
        public string BaseColorTexture
        {
            get { CheckNotDisposed(); return NativeMethods.PtrToUtf8(NativeMethods.idtx_material_get_base_color_texture(Handle)); }
            set { CheckNotDisposed(); NativeMethods.idtx_material_set_base_color_texture(Handle, value ?? string.Empty); }
        }
        public string NormalTexture
        {
            get { CheckNotDisposed(); return NativeMethods.PtrToUtf8(NativeMethods.idtx_material_get_normal_texture(Handle)); }
            set { CheckNotDisposed(); NativeMethods.idtx_material_set_normal_texture(Handle, value ?? string.Empty); }
        }

        public bool IsMToon
        { get { CheckNotDisposed(); return NativeMethods.idtx_material_is_mtoon(Handle) != 0; } }

        public void SetMToonShadeColor(float r, float g, float b)
        { CheckNotDisposed(); NativeMethods.idtx_material_set_mtoon_shade_color(Handle, r, g, b); }
        public void SetMToonRimColor(float r, float g, float b)
        { CheckNotDisposed(); NativeMethods.idtx_material_set_mtoon_rim_color(Handle, r, g, b); }
        public float MToonOutlineWidth
        {
            get { CheckNotDisposed(); return NativeMethods.idtx_material_get_mtoon_outline_width(Handle); }
            set { CheckNotDisposed(); NativeMethods.idtx_material_set_mtoon_outline_width(Handle, value); }
        }
        public float[] GetMToonShadeColor() { CheckNotDisposed(); var c = new float[3]; NativeMethods.idtx_material_get_mtoon_shade_color(Handle, c); return c; }
        public float[] GetMToonRimColor()   { CheckNotDisposed(); var c = new float[3]; NativeMethods.idtx_material_get_mtoon_rim_color(Handle, c); return c; }

        internal void ReleaseOwnership() { _owned = false; }

        private void CheckNotDisposed()
        { if (_disposed) throw new ObjectDisposedException(nameof(IdtxMaterial)); }

        public void Dispose()
        {
            if (_disposed) return;
            if (_owned && Handle.IsValid) NativeMethods.idtx_material_destroy(Handle);
            Handle = new MaterialHandle(IntPtr.Zero);
            _disposed = true;
            GC.SuppressFinalize(this);
        }

        ~IdtxMaterial() { Dispose(); }
    }
}
