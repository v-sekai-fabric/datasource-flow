#pragma once

#include "Modules/ModuleManager.h"
#include "AssetRegistry/AssetData.h"
#include "Subsystems/GameInstanceSubsystem.h"

#include "SeamlessAnimatedSkeletons.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogSAS, Log, All);

class UAnimSequenceBase;
class USkeleton;
class USeamlessAnimationData;

class FSeamlessAnimatedSkeletons : public IModuleInterface
{
public: // ---------------------------------------------------- IModuleInterface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};

UCLASS()
class SEAMLESSANIMATEDSKELETONS_API USeamlessAnimatedSkeletonSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

	/// The list of animations maps:
	///		- The first Skeleton is the TargetSkeleton.
	///		- The child one is the source skeleton.
	///		- Then it points to the correct USeamlessAnimationData to extract the
	///		  retargeted animation.
	TMap<const USkeleton*, TMap<const USkeleton*, const USeamlessAnimationData*>> AnimationsMaps;

private: // ------------------------------------------------------ Notifications
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

public: // ----------------------------------------------------------------- API
	/// Search the same animation but retargeted for the given `Skeleton`.
	/// If the animation doesn't exist, returns `nullptr`.
	UAnimSequenceBase* GetAnimationFor(
		UAnimSequenceBase* SourceAnim,
		const USkeleton* TargetSkeleton);
};
