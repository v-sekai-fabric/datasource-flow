// Copyright 2026 The openusd-fabric authors / V-Sekai contributors.
// SPDX-License-Identifier: Apache-2.0 OR MPL-2.0

#pragma once

#include "CoreMinimal.h"

class USkeletalMesh;
class UStaticMesh;

// CHI-312 #4: walks Unreal mesh assets into an idtx_avatar_t via the flat C
// ABI, mirroring source/exporter/GodotAvatarBuilder.cpp but with UE render
// data + the dlopen table. The returned handle is caller-owned: pair with
// idtx_avatar_destroy (Export() does this for you).
class IDTXFLOWUNREAL_API FIdtxUnrealAvatarBuilder
{
public:
	// Build an avatar from a skeletal mesh (skeleton + skinned geometry +
	// materials). Returns an idtx_avatar_t* as void* (opaque), or nullptr.
	static void* BuildFromSkeletalMesh(const USkeletalMesh* Mesh);

	// Build an avatar from a static mesh (geometry + materials, no skeleton).
	static void* BuildFromStaticMesh(const UStaticMesh* Mesh);
};
