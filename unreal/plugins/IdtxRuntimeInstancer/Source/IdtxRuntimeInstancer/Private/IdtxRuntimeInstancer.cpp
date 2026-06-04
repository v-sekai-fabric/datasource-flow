// Copyright 2026 The openusd-fabric authors / V-Sekai contributors.
// SPDX-License-Identifier: Apache-2.0 OR MPL-2.0

#include "IdtxRuntimeInstancer.h"

#include "GameFramework/Actor.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/Skeleton.h"
#include "ReferenceSkeleton.h"
#include "Materials/Material.h"
#include "UObject/UObjectGlobals.h"

#include "RuntimeSkeletalMeshGenerator.h" // FMeshSurface, FRawBoneInfluence, FRuntimeSkeletalMeshGenerator

#include "idtx_core/idtx_core.h"

// Forward from the module (loads libidtx_core via the dlopen table).
extern bool GIdtxRuntimeCoreLoaded;

bool UIdtxRuntimeInstancer::IsCoreLoaded()
{
	return GIdtxRuntimeCoreLoaded;
}

namespace
{
	// Row-major float[16] (idtx/USD) -> FMatrix.
	FMatrix ToFMatrix(const float M[16])
	{
		FMatrix R;
		for (int r = 0; r < 4; ++r)
			for (int c = 0; c < 4; ++c)
				R.M[r][c] = M[r * 4 + c];
		return R;
	}

	// USD (Y-up, metres, right-handed) -> UE (Z-up, centimetres, left-handed).
	// Inverse of the UE->USD export conversion. MUST-VERIFY against usdchecker
	// round-trip — this is the single coordinate-conversion knob on the import
	// side (mirror of IdtxUECompat::FillUEToUSDRootMatrix on export).
	FVector UsdToUE(float x, float y, float z)
	{
		const float S = 100.0f; // m -> cm
		return FVector(x * S, z * S, y * S);
	}
}

USkeletalMeshComponent* UIdtxRuntimeInstancer::InstanceAvatarFromUSD(AActor* Owner, const FString& UsdPath)
{
	if (!GIdtxRuntimeCoreLoaded || Owner == nullptr)
	{
		return nullptr;
	}

	idtx_avatar_t* Avatar = idtx_core_import_avatar_from_usd(TCHAR_TO_UTF8(*UsdPath));
	if (Avatar == nullptr)
	{
		UE_LOG(LogTemp, Error, TEXT("IdtxRuntimeInstancer: failed to import %s"), *UsdPath);
		return nullptr;
	}

	// --- USkeleton from idtx_skeleton --------------------------------------
	// idtx stores bone rest matrices in COMPONENT space; FReferenceSkeletonModifier
	// wants LOCAL (parent-relative) transforms, so divide out the parent.
	idtx_skeleton_t* Skel = idtx_avatar_get_skeleton(Avatar);
	USkeletalMesh* SkelMesh = NewObject<USkeletalMesh>(GetTransientPackage());
	USkeleton* Skeleton = NewObject<USkeleton>(GetTransientPackage());

	const int32 IdtxBoneCount = (Skel != nullptr) ? idtx_skeleton_get_bone_count(Skel) : 0;
	{
		FReferenceSkeleton RefSkel;
		FReferenceSkeletonModifier Modifier(RefSkel, Skeleton);
		if (IdtxBoneCount > 0)
		{
			TArray<FMatrix> Component; Component.SetNum(IdtxBoneCount);
			for (int32 b = 0; b < IdtxBoneCount; ++b)
			{
				float Rest[16];
				idtx_skeleton_get_bone_rest(Skel, b, Rest);
				Component[b] = ToFMatrix(Rest);
				const int32 Parent = idtx_skeleton_get_bone_parent(Skel, b);
				const FMatrix Local = (Parent >= 0) ? (Component[b] * Component[Parent].Inverse()) : Component[b];
				const FName BoneName(UTF8_TO_TCHAR(idtx_skeleton_get_bone_name(Skel, b)));
				Modifier.Add(FMeshBoneInfo(BoneName, BoneName.ToString(), Parent), FTransform(Local));
			}
		}
		else
		{
			// No skeleton (a "regular"/static mesh): a single root bone lets the
			// same skeletal path carry rigid geometry — every vertex binds to
			// bone 0 below, so it renders static. This is how
			// GenerateSkeletalMeshComponent handles non-skinned meshes.
			Modifier.Add(FMeshBoneInfo(TEXT("Root"), TEXT("Root"), INDEX_NONE), FTransform::Identity);
		}
		// MUST-VERIFY: assigning a runtime-built RefSkeleton + merging to the
		// USkeleton bone tree is the version-sensitive step (SetRefSkeleton is
		// editor-only in some engine versions; on a packaged runtime build use
		// the FReferenceSkeleton overload that GenerateSkeletalMesh accepts, or
		// the USkeletalMesh setter exposed in the target engine).
		SkelMesh->SetRefSkeleton(RefSkel);
		Skeleton->MergeAllBonesToBoneTree(SkelMesh);
	}

	// --- FMeshSurface[] from idtx meshes -----------------------------------
	const int32 MeshCount = idtx_avatar_get_mesh_count(Avatar);
	TArray<FMeshSurface> Surfaces;
	TArray<UMaterialInterface*> SurfaceMaterials;
	UMaterialInterface* Default = UMaterial::GetDefaultMaterial(MD_Surface);

	for (int32 i = 0; i < MeshCount; ++i)
	{
		idtx_mesh_t* Mesh = idtx_avatar_get_mesh(Avatar, i);
		if (Mesh == nullptr) continue;
		const int32 VC = idtx_mesh_get_vertex_count(Mesh);
		const int32 IC = idtx_mesh_get_index_count(Mesh);
		if (VC <= 0 || IC <= 0) continue;

		FMeshSurface Surface;
		Surface.MaterialIndex = i;

		// Positions (+ optional normals).
		TArray<float> Pos; Pos.SetNumUninitialized(VC * 3);
		idtx_mesh_get_positions(Mesh, Pos.GetData());
		Surface.Vertices.SetNumUninitialized(VC);
		for (int32 v = 0; v < VC; ++v)
			Surface.Vertices[v] = UsdToUE(Pos[v * 3], Pos[v * 3 + 1], Pos[v * 3 + 2]);

		Surface.Normals.SetNumUninitialized(VC);
		TArray<float> Nrm; Nrm.SetNumUninitialized(VC * 3);
		idtx_mesh_get_normals(Mesh, Nrm.GetData());
		for (int32 v = 0; v < VC; ++v)
			Surface.Normals[v] = UsdToUE(Nrm[v * 3], Nrm[v * 3 + 1], Nrm[v * 3 + 2]).GetSafeNormal();

		// Indices.
		TArray<int32> Idx; Idx.SetNumUninitialized(IC);
		idtx_mesh_get_indices(Mesh, Idx.GetData());
		Surface.Indices.SetNumUninitialized(IC);
		for (int32 k = 0; k < IC; ++k) Surface.Indices[k] = (uint32)Idx[k];

		// Skinning -> per-vertex FRawBoneInfluence list. Every vertex must have
		// at least one influence or the skeletal build rejects it.
		const int32 BPV = (IdtxBoneCount > 0) ? idtx_mesh_get_bones_per_vertex(Mesh) : 0;
		Surface.BoneInfluences.SetNum(VC);
		if (BPV > 0)
		{
			TArray<int32> BoneIdx; BoneIdx.SetNumUninitialized(VC * BPV);
			TArray<float> Weights; Weights.SetNumUninitialized(VC * BPV);
			idtx_mesh_get_bone_indices(Mesh, BoneIdx.GetData());
			idtx_mesh_get_weights(Mesh, Weights.GetData());
			for (int32 v = 0; v < VC; ++v)
			{
				for (int32 w = 0; w < BPV; ++w)
				{
					const float Wt = Weights[v * BPV + w];
					if (Wt > 0.0f)
						Surface.BoneInfluences[v].Add(FRawBoneInfluence(v, BoneIdx[v * BPV + w], Wt));
				}
				// Degenerate (all-zero) weights -> pin to root so the vertex is valid.
				if (Surface.BoneInfluences[v].Num() == 0)
					Surface.BoneInfluences[v].Add(FRawBoneInfluence(v, 0, 1.0f));
			}
		}
		else
		{
			// Regular / unskinned mesh: bind every vertex rigidly to the single
			// root bone (weight 1) so GenerateSkeletalMeshComponent renders it
			// as static geometry.
			for (int32 v = 0; v < VC; ++v)
				Surface.BoneInfluences[v].Add(FRawBoneInfluence(v, 0, 1.0f));
		}

		Surfaces.Add(MoveTemp(Surface));
		SurfaceMaterials.Add(Default);
	}

	idtx_avatar_destroy(Avatar);

	if (Surfaces.Num() == 0)
	{
		return nullptr;
	}

	// --- Runtime build -----------------------------------------------------
	USkeletalMeshComponent* Component = FRuntimeSkeletalMeshGenerator::GenerateSkeletalMeshComponent(
		Owner, Skeleton, Surfaces, SurfaceMaterials, /*bNeedCPUAccess*/ false);
	if (Component)
	{
		UE_LOG(LogTemp, Log, TEXT("IdtxRuntimeInstancer: instanced %d surface(s) from %s"),
			Surfaces.Num(), *UsdPath);
	}
	return Component;
}
