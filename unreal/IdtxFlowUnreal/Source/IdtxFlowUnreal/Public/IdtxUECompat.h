// Copyright 2026 The openusd-fabric authors / V-Sekai contributors.
// SPDX-License-Identifier: Apache-2.0 OR MPL-2.0
//
// CHI-312 #4 — the thin UE4.27 <-> UE5 accessor shim. ~90% of the avatar
// builder is shared; the differences are getter vs direct-member access and a
// couple of renamed APIs. All of them funnel through this header so the
// builder reads identically on both engines.

#pragma once

#include "CoreMinimal.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Rendering/SkeletalMeshRenderData.h"

namespace IdtxUE
{
	// --- SkeletalMesh accessors ------------------------------------------
	// UE4.27 and UE5 both expose getters (direct member access deprecated);
	// <=4.26 needs the public members. ENGINE_MAJOR_VERSION / _MINOR_VERSION
	// come from Runtime/Launch/Resources/Version.h (always in scope in UE).

	FORCEINLINE const FReferenceSkeleton& GetRefSkeleton(const USkeletalMesh* Mesh)
	{
#if ENGINE_MAJOR_VERSION >= 5 || (ENGINE_MAJOR_VERSION == 4 && ENGINE_MINOR_VERSION >= 27)
		return Mesh->GetRefSkeleton();
#else
		return Mesh->RefSkeleton;
#endif
	}

	FORCEINLINE FSkeletalMeshRenderData* GetSkeletalRenderData(const USkeletalMesh* Mesh)
	{
#if ENGINE_MAJOR_VERSION >= 5 || (ENGINE_MAJOR_VERSION == 4 && ENGINE_MINOR_VERSION >= 27)
		return Mesh->GetResourceForRendering();
#else
		return Mesh->GetResourceForRendering(); // stable since 4.19
#endif
	}

	FORCEINLINE const TArray<FSkeletalMaterial>& GetMaterials(const USkeletalMesh* Mesh)
	{
#if ENGINE_MAJOR_VERSION >= 5 || (ENGINE_MAJOR_VERSION == 4 && ENGINE_MINOR_VERSION >= 27)
		return Mesh->GetMaterials();
#else
		return Mesh->Materials;
#endif
	}

	// --- StaticMesh accessors --------------------------------------------

	FORCEINLINE const FStaticMeshRenderData* GetStaticRenderData(const UStaticMesh* Mesh)
	{
#if ENGINE_MAJOR_VERSION >= 5
		return Mesh->GetRenderData();
#elif (ENGINE_MAJOR_VERSION == 4 && ENGINE_MINOR_VERSION >= 27)
		return Mesh->GetRenderData();
#else
		return Mesh->RenderData.Get();
#endif
	}

	// --- Coordinate conversion (UE -> USD/idtx) --------------------------
	// UE: left-handed, Z-up, centimetres. USD/idtx: right-handed, Y-up, metres.
	// Convert host-side, mirroring UE's own USD exporter (metersPerUnit=0.01,
	// upAxis=Y). The basis change maps UE (X fwd, Y right, Z up) to USD
	// (X right, Y up, Z back) with the cm->m scale, and flips handedness.
	//
	// NOTE (must-verify, per CHI-312): the exact axis signs depend on how
	// idtx_core interprets the root matrix vs UsdGeomSetStageUpAxis. This is
	// the single place to tune; validated by the round-trip commandlet
	// (usdchecker + re-import equivalence) before shipping.
	FORCEINLINE void FillUEToUSDRootMatrix(float OutMatrix[16])
	{
		// Row-major 4x4. Scale 0.01 (cm->m) on the basis-swap rows.
		const float S = 0.01f;
		// USD.x =  UE.x ; USD.y = UE.z ; USD.z = -UE.y  (Z-up -> Y-up, LH->RH)
		const float M[16] = {
			   S, 0.0f, 0.0f, 0.0f,
			0.0f, 0.0f,    S, 0.0f,
			0.0f,   -S, 0.0f, 0.0f,
			0.0f, 0.0f, 0.0f, 1.0f,
		};
		for (int i = 0; i < 16; ++i)
		{
			OutMatrix[i] = M[i];
		}
	}
}
