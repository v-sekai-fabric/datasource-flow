// Copyright 2026 The openusd-fabric authors / V-Sekai contributors.
// SPDX-License-Identifier: Apache-2.0 OR MPL-2.0

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "IdtxRuntimeInstancer.generated.h"

class AActor;
class USkeletalMeshComponent;

// CHI-312 Unreal runtime-import adapter. Reads a USD avatar through
// libidtx_core (the .sigs dlopen table) and instances it as a live
// USkeletalMeshComponent via RuntimeSkeletalMeshGenerator — no editor import,
// no cooked asset. This is the runtime counterpart to the Godot/Blender USD
// import legs; it closes the last spoke of the USD interop matrix.
UCLASS()
class IDTXRUNTIMEINSTANCER_API UIdtxRuntimeInstancer : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Read the avatar at UsdPath and instance it as a USkeletalMeshComponent
	 * attached to Owner. Returns the component (transient), or nullptr on
	 * failure. Runs the whole idtx_avatar -> FMeshSurface[] + USkeleton ->
	 * GenerateSkeletalMeshComponent pipeline.
	 */
	UFUNCTION(BlueprintCallable, Category = "IDTX")
	static USkeletalMeshComponent* InstanceAvatarFromUSD(AActor* Owner, const FString& UsdPath);

	/** Whether libidtx_core loaded successfully (call after module startup). */
	UFUNCTION(BlueprintCallable, Category = "IDTX")
	static bool IsCoreLoaded();
};
