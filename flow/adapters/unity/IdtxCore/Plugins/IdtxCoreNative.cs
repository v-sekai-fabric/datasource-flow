// Copyright 2026 V-Sekai contributors.
// SPDX-License-Identifier: Apache-2.0 OR MPL-2.0
//
// Raw P/Invoke declarations for libidtx_core's C ABI.
//
// This file is the contract surface — its function signatures must
// match flow/ports/include/idtx_core/idtx_core.h byte-for-byte. The
// idiomatic C# wrappers (IdtxAvatar, IdtxSkeleton, etc.) build on
// top of these and are what application code should use.
//
// Unity 2022.3 target: API compatibility netstandard2.1, IL2CPP-safe.
//
// On Windows the native library is libidtx_core.windows.x86_64.dll;
// Unity's PluginImporter expects the file to live under
// Assets/Plugins/x86_64/. The DllName constant below is shared by
// every [DllImport] in this file so future platform variants can
// override it in a single place.

using System;
using System.Runtime.InteropServices;

namespace IdtxCore.Native
{
    internal static class Lib
    {
        // Unity native-plugin logical name. The SCons build deploys the core as
        // Plugins/<arch>/idtx_core.<ext> (see scons/idtxcore.py), so Unity's own
        // plugin loader resolves [DllImport("idtx_core")] directly — the same
        // pattern as MantisLOD. No custom resolver (Unity's .NET Framework API
        // level lacks System.Runtime.InteropServices.NativeLibrary).
        internal const string DllName = "idtx_core";
    }

    /// <summary>Opaque skeleton handle. Lifetime owned by idtx_avatar.</summary>
    public readonly struct SkeletonHandle
    {
        public readonly IntPtr Ptr;
        public SkeletonHandle(IntPtr p) { Ptr = p; }
        public bool IsValid => Ptr != IntPtr.Zero;
    }

    /// <summary>Opaque mesh handle. Lifetime owned by idtx_avatar.</summary>
    public readonly struct MeshHandle
    {
        public readonly IntPtr Ptr;
        public MeshHandle(IntPtr p) { Ptr = p; }
        public bool IsValid => Ptr != IntPtr.Zero;
    }

    /// <summary>Opaque material handle. Lifetime owned by idtx_avatar.</summary>
    public readonly struct MaterialHandle
    {
        public readonly IntPtr Ptr;
        public MaterialHandle(IntPtr p) { Ptr = p; }
        public bool IsValid => Ptr != IntPtr.Zero;
    }

    /// <summary>Opaque avatar handle. Caller-owned; pair with Destroy.</summary>
    public readonly struct AvatarHandle
    {
        public readonly IntPtr Ptr;
        public AvatarHandle(IntPtr p) { Ptr = p; }
        public bool IsValid => Ptr != IntPtr.Zero;
    }

    public enum AlphaMode : int
    {
        Opaque = 0,
        Mask   = 1,
        Blend  = 2,
    }

    // ---------------------------------------------------------------
    // P/Invoke declarations — keep names identical to the C ABI.
    // ---------------------------------------------------------------

    public static class NativeMethods
    {
        // Core
        [DllImport(Lib.DllName, EntryPoint = "idtx_core_version", CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr idtx_core_version();

        // Skeleton
        [DllImport(Lib.DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern SkeletonHandle idtx_skeleton_create();

        [DllImport(Lib.DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void idtx_skeleton_destroy(SkeletonHandle skel);

        [DllImport(Lib.DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void idtx_skeleton_set_name(SkeletonHandle skel, [MarshalAs(UnmanagedType.LPUTF8Str)] string name);

        [DllImport(Lib.DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr idtx_skeleton_get_name(SkeletonHandle skel);

        [DllImport(Lib.DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int idtx_skeleton_add_bone(
            SkeletonHandle skel,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string name,
            int parentIndex,
            [In] float[] restMatrix,
            [In] float[] bindMatrix);

        [DllImport(Lib.DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int idtx_skeleton_get_bone_count(SkeletonHandle skel);

        [DllImport(Lib.DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr idtx_skeleton_get_bone_name(SkeletonHandle skel, int index);

        [DllImport(Lib.DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int idtx_skeleton_get_bone_parent(SkeletonHandle skel, int index);

        [DllImport(Lib.DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void idtx_skeleton_get_bone_rest(SkeletonHandle skel, int index, [Out] float[] outMatrix);

        [DllImport(Lib.DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void idtx_skeleton_get_bone_bind(SkeletonHandle skel, int index, [Out] float[] outMatrix);

        // Mesh
        [DllImport(Lib.DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern MeshHandle idtx_mesh_create();

        [DllImport(Lib.DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void idtx_mesh_destroy(MeshHandle mesh);

        [DllImport(Lib.DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void idtx_mesh_set_name(MeshHandle mesh, [MarshalAs(UnmanagedType.LPUTF8Str)] string name);

        [DllImport(Lib.DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr idtx_mesh_get_name(MeshHandle mesh);

        [DllImport(Lib.DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void idtx_mesh_set_vertices(
            MeshHandle mesh, int vertexCount,
            [In] float[] positions,
            [In] float[] normals,
            [In] float[] uvs,
            [In] float[] colors);

        [DllImport(Lib.DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void idtx_mesh_set_indices(MeshHandle mesh, int indexCount, [In] int[] indices);

        [DllImport(Lib.DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void idtx_mesh_set_skinning(
            MeshHandle mesh, int bonesPerVertex,
            [In] int[] boneIndices,
            [In] float[] weights);

        [DllImport(Lib.DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void idtx_mesh_add_blendshape(
            MeshHandle mesh,
            [MarshalAs(UnmanagedType.LPUTF8Str)] string name,
            float weight,
            [In] float[] positionDeltas,
            [In] float[] normalDeltas);

        [DllImport(Lib.DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int idtx_mesh_get_vertex_count(MeshHandle mesh);

        [DllImport(Lib.DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int idtx_mesh_get_index_count(MeshHandle mesh);

        [DllImport(Lib.DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int idtx_mesh_get_bones_per_vertex(MeshHandle mesh);

        [DllImport(Lib.DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void idtx_mesh_get_positions(MeshHandle mesh, [Out] float[] outPositions);

        [DllImport(Lib.DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void idtx_mesh_get_normals(MeshHandle mesh, [Out] float[] outNormals);

        [DllImport(Lib.DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void idtx_mesh_get_uvs(MeshHandle mesh, [Out] float[] outUvs);

        [DllImport(Lib.DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void idtx_mesh_get_colors(MeshHandle mesh, [Out] float[] outColors);

        [DllImport(Lib.DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void idtx_mesh_get_indices(MeshHandle mesh, [Out] int[] outIndices);

        [DllImport(Lib.DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void idtx_mesh_get_bone_indices(MeshHandle mesh, [Out] int[] outBoneIndices);

        [DllImport(Lib.DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void idtx_mesh_get_weights(MeshHandle mesh, [Out] float[] outWeights);

        [DllImport(Lib.DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int idtx_mesh_has_normals(MeshHandle mesh);

        [DllImport(Lib.DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int idtx_mesh_has_uvs(MeshHandle mesh);

        [DllImport(Lib.DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int idtx_mesh_has_colors(MeshHandle mesh);

        // Material
        [DllImport(Lib.DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern MaterialHandle idtx_material_create();

        [DllImport(Lib.DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void idtx_material_destroy(MaterialHandle mat);

        [DllImport(Lib.DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void idtx_material_set_name(MaterialHandle mat, [MarshalAs(UnmanagedType.LPUTF8Str)] string name);

        [DllImport(Lib.DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr idtx_material_get_name(MaterialHandle mat);

        [DllImport(Lib.DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void idtx_material_set_base_color(MaterialHandle mat, float r, float g, float b, float a);

        [DllImport(Lib.DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void idtx_material_get_base_color(MaterialHandle mat, [Out] float[] outRgba);

        [DllImport(Lib.DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void idtx_material_set_metallic(MaterialHandle mat, float metallic);

        [DllImport(Lib.DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void idtx_material_set_roughness(MaterialHandle mat, float roughness);

        [DllImport(Lib.DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern float idtx_material_get_metallic(MaterialHandle mat);

        [DllImport(Lib.DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern float idtx_material_get_roughness(MaterialHandle mat);

        [DllImport(Lib.DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void idtx_material_set_alpha_mode(MaterialHandle mat, AlphaMode mode);

        [DllImport(Lib.DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern AlphaMode idtx_material_get_alpha_mode(MaterialHandle mat);

        [DllImport(Lib.DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void idtx_material_set_alpha_cutoff(MaterialHandle mat, float cutoff);

        [DllImport(Lib.DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern float idtx_material_get_alpha_cutoff(MaterialHandle mat);

        [DllImport(Lib.DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void idtx_material_set_base_color_texture(MaterialHandle mat, [MarshalAs(UnmanagedType.LPUTF8Str)] string path);

        [DllImport(Lib.DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr idtx_material_get_base_color_texture(MaterialHandle mat);

        [DllImport(Lib.DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void idtx_material_set_normal_texture(MaterialHandle mat, [MarshalAs(UnmanagedType.LPUTF8Str)] string path);

        [DllImport(Lib.DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr idtx_material_get_normal_texture(MaterialHandle mat);

        [DllImport(Lib.DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void idtx_material_set_mtoon_shade_color(MaterialHandle mat, float r, float g, float b);

        [DllImport(Lib.DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void idtx_material_set_mtoon_rim_color(MaterialHandle mat, float r, float g, float b);

        [DllImport(Lib.DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void idtx_material_set_mtoon_outline_width(MaterialHandle mat, float width);

        [DllImport(Lib.DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int idtx_material_is_mtoon(MaterialHandle mat);

        [DllImport(Lib.DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void idtx_material_get_mtoon_shade_color(MaterialHandle mat, [Out] float[] outRgb);

        [DllImport(Lib.DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void idtx_material_get_mtoon_rim_color(MaterialHandle mat, [Out] float[] outRgb);

        [DllImport(Lib.DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern float idtx_material_get_mtoon_outline_width(MaterialHandle mat);

        // Avatar
        [DllImport(Lib.DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern AvatarHandle idtx_avatar_create();

        [DllImport(Lib.DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void idtx_avatar_destroy(AvatarHandle avatar);

        [DllImport(Lib.DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void idtx_avatar_set_name(AvatarHandle avatar, [MarshalAs(UnmanagedType.LPUTF8Str)] string name);

        [DllImport(Lib.DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr idtx_avatar_get_name(AvatarHandle avatar);

        [DllImport(Lib.DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void idtx_avatar_set_root_transform(AvatarHandle avatar, [In] float[] matrix);

        [DllImport(Lib.DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void idtx_avatar_get_root_transform(AvatarHandle avatar, [Out] float[] outMatrix);

        [DllImport(Lib.DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern void idtx_avatar_set_skeleton(AvatarHandle avatar, SkeletonHandle skel);

        [DllImport(Lib.DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern SkeletonHandle idtx_avatar_get_skeleton(AvatarHandle avatar);

        [DllImport(Lib.DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int idtx_avatar_add_mesh(AvatarHandle avatar, MeshHandle mesh, int materialIndex);

        [DllImport(Lib.DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int idtx_avatar_get_mesh_count(AvatarHandle avatar);

        [DllImport(Lib.DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern MeshHandle idtx_avatar_get_mesh(AvatarHandle avatar, int index);

        [DllImport(Lib.DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int idtx_avatar_get_mesh_material(AvatarHandle avatar, int meshIndex);

        [DllImport(Lib.DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int idtx_avatar_add_material(AvatarHandle avatar, MaterialHandle mat);

        [DllImport(Lib.DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int idtx_avatar_get_material_count(AvatarHandle avatar);

        [DllImport(Lib.DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern MaterialHandle idtx_avatar_get_material(AvatarHandle avatar, int index);

        // Top-level I/O
        [DllImport(Lib.DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int idtx_core_export_avatar_to_usd(
            AvatarHandle avatar, [MarshalAs(UnmanagedType.LPUTF8Str)] string path);

        [DllImport(Lib.DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern AvatarHandle idtx_core_import_avatar_from_usd(
            [MarshalAs(UnmanagedType.LPUTF8Str)] string path);

        [DllImport(Lib.DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int idtx_core_export_avatar_to_vrm(
            AvatarHandle avatar, [MarshalAs(UnmanagedType.LPUTF8Str)] string path);

        [DllImport(Lib.DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern AvatarHandle idtx_core_import_avatar_from_vrm(
            [MarshalAs(UnmanagedType.LPUTF8Str)] string path);

        // Helper for the IntPtr-returning string accessors.
        public static string PtrToUtf8(IntPtr ptr)
        {
            if (ptr == IntPtr.Zero) return string.Empty;
            return Marshal.PtrToStringUTF8(ptr);
        }
    }
}
