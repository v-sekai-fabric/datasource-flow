// Copyright 2026 The openusd-fabric authors / V-Sekai contributors.
// SPDX-License-Identifier: Apache-2.0 OR MPL-2.0

#include "IdtxUnrealAvatarBuilder.h"
#include "IdtxUECompat.h"

#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "StaticMeshResources.h"
#include "ReferenceSkeleton.h"
#include "Materials/MaterialInterface.h"

#include "idtx_core/idtx_core.h"

namespace
{
	// UE FMatrix is row-major; pack to the C ABI float[16] (row-major).
	void PackMatrix(const FMatrix& M, float Out[16])
	{
		for (int r = 0; r < 4; ++r)
			for (int c = 0; c < 4; ++c)
				Out[r * 4 + c] = static_cast<float>(M.M[r][c]);
	}

	// Skeleton: bone names, parents, rest (component-space) + inverse-bind.
	void BuildSkeleton(void* Avatar, const FReferenceSkeleton& Ref)
	{
		void* Skel = idtx_skeleton_create();
		const int32 Num = Ref.GetNum();

		// Component-space rest from the local-space ref pose, accumulated down
		// the parent chain (UsdSkel wants bind/rest in a consistent space).
		TArray<FMatrix> ComponentSpace;
		ComponentSpace.SetNum(Num);
		const TArray<FTransform>& Local = Ref.GetRefBonePose();
		for (int32 i = 0; i < Num; ++i)
		{
			const int32 Parent = Ref.GetParentIndex(i);
			const FMatrix L = Local[i].ToMatrixWithScale();
			ComponentSpace[i] = (Parent >= 0) ? (L * ComponentSpace[Parent]) : L;
		}

		for (int32 i = 0; i < Num; ++i)
		{
			const FName BoneName = Ref.GetBoneName(i);
			const int32 Parent = Ref.GetParentIndex(i);
			float Rest[16];
			float Bind[16];
			PackMatrix(ComponentSpace[i], Rest);
			PackMatrix(ComponentSpace[i].Inverse(), Bind); // inverse-bind
			idtx_skeleton_add_bone(Skel, TCHAR_TO_UTF8(*BoneName.ToString()),
				Parent, Rest, Bind);
		}
		idtx_avatar_set_skeleton(Avatar, Skel);
	}

	// Materials: base color from the material's BaseColor parameter, falling
	// back to a neutral default. Returns the avatar-side material index.
	int32 AddMaterial(void* Avatar, const UMaterialInterface* MatIf)
	{
		void* Mat = idtx_material_create();
		FLinearColor Base(0.8f, 0.8f, 0.8f, 1.0f);
		if (MatIf)
		{
			FMaterialParameterInfo Info(TEXT("BaseColor"));
			FLinearColor Found;
			if (MatIf->GetVectorParameterValue(Info, Found))
			{
				Base = Found;
			}
		}
		idtx_material_set_base_color(Mat, Base.R, Base.G, Base.B, Base.A);
		return idtx_avatar_add_material(Avatar, Mat);
	}
}

void* FIdtxUnrealAvatarBuilder::BuildFromSkeletalMesh(const USkeletalMesh* Mesh)
{
	if (!Mesh)
	{
		return nullptr;
	}
	void* Avatar = idtx_avatar_create();
	idtx_avatar_set_name(Avatar, TCHAR_TO_UTF8(*Mesh->GetName()));

	// UE -> USD/idtx coordinate conversion baked into the root transform.
	float Root[16];
	IdtxUE::FillUEToUSDRootMatrix(Root);
	idtx_avatar_set_root_transform(Avatar, Root);

	BuildSkeleton(Avatar, IdtxUE::GetRefSkeleton(Mesh));

	// Materials (index map mirrors the asset's material slots).
	TArray<int32> MatIndex;
	for (const FSkeletalMaterial& M : IdtxUE::GetMaterials(Mesh))
	{
		MatIndex.Add(AddMaterial(Avatar, M.MaterialInterface));
	}

	// Geometry from LOD0 render data.
	FSkeletalMeshRenderData* RD = IdtxUE::GetSkeletalRenderData(Mesh);
	if (RD && RD->LODRenderData.Num() > 0)
	{
		const FSkeletalMeshLODRenderData& LOD = RD->LODRenderData[0];
		const FPositionVertexBuffer& Pos = LOD.StaticVertexBuffers.PositionVertexBuffer;
		const uint32 NumVerts = Pos.GetNumVertices();

		TArray<float> Positions;
		Positions.SetNumUninitialized(NumVerts * 3);
		for (uint32 v = 0; v < NumVerts; ++v)
		{
			const FVector P = (FVector)Pos.VertexPosition(v);
			Positions[v * 3 + 0] = (float)P.X;
			Positions[v * 3 + 1] = (float)P.Y;
			Positions[v * 3 + 2] = (float)P.Z;
		}

		TArray<uint32> Indices;
		LOD.MultiSizeIndexContainer.GetIndexBuffer(Indices);
		TArray<int32> Idx32;
		Idx32.SetNumUninitialized(Indices.Num());
		for (int32 i = 0; i < Indices.Num(); ++i)
		{
			Idx32[i] = (int32)Indices[i];
		}

		void* MeshH = idtx_mesh_create();
		idtx_mesh_set_vertices(MeshH, (int32)NumVerts, Positions.GetData(), nullptr, nullptr, nullptr);
		idtx_mesh_set_indices(MeshH, Idx32.Num(), Idx32.GetData());
		// NOTE (CHI-312 risk): per-section bone-map remap + skin weights are the
		// next slice — LOD.RenderSections[s].BoneMap maps section-local indices
		// to global skeleton bones; idtx_mesh_set_skinning consumes the global
		// indices. Geometry + skeleton land first; skinning follows once the
		// round-trip commandlet is green on the unskinned path.
		const int32 Mat0 = MatIndex.Num() > 0 ? MatIndex[0] : -1;
		idtx_avatar_add_mesh(Avatar, MeshH, Mat0);
	}

	return Avatar;
}

void* FIdtxUnrealAvatarBuilder::BuildFromStaticMesh(const UStaticMesh* Mesh)
{
	if (!Mesh)
	{
		return nullptr;
	}
	void* Avatar = idtx_avatar_create();
	idtx_avatar_set_name(Avatar, TCHAR_TO_UTF8(*Mesh->GetName()));

	float Root[16];
	IdtxUE::FillUEToUSDRootMatrix(Root);
	idtx_avatar_set_root_transform(Avatar, Root);

	const FStaticMeshRenderData* RD = IdtxUE::GetStaticRenderData(Mesh);
	if (RD && RD->LODResources.Num() > 0)
	{
		const FStaticMeshLODResources& LOD = RD->LODResources[0];
		const FPositionVertexBuffer& Pos = LOD.VertexBuffers.PositionVertexBuffer;
		const uint32 NumVerts = Pos.GetNumVertices();

		TArray<float> Positions;
		Positions.SetNumUninitialized(NumVerts * 3);
		for (uint32 v = 0; v < NumVerts; ++v)
		{
			const FVector P = (FVector)Pos.VertexPosition(v);
			Positions[v * 3 + 0] = (float)P.X;
			Positions[v * 3 + 1] = (float)P.Y;
			Positions[v * 3 + 2] = (float)P.Z;
		}

		FIndexArrayView IndexView = LOD.IndexBuffer.GetArrayView();
		TArray<int32> Idx32;
		Idx32.SetNumUninitialized(IndexView.Num());
		for (int32 i = 0; i < IndexView.Num(); ++i)
		{
			Idx32[i] = (int32)IndexView[i];
		}

		void* MeshH = idtx_mesh_create();
		idtx_mesh_set_vertices(MeshH, (int32)NumVerts, Positions.GetData(), nullptr, nullptr, nullptr);
		idtx_mesh_set_indices(MeshH, Idx32.Num(), Idx32.GetData());
		idtx_avatar_add_mesh(Avatar, MeshH, -1);
	}

	return Avatar;
}
