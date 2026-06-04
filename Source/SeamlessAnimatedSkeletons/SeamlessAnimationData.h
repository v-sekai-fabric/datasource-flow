#pragma once

#include "SeamlessAnimationData.generated.h"

class USkeleton;
class UAnimSequenceBase;

/// This is an archive to store the map between the Source Skeleton animations
/// and the target skeleton.
UCLASS(BlueprintType)
class SEAMLESSANIMATEDSKELETONS_API USeamlessAnimationData : public UObject
{
	GENERATED_BODY()

	friend class FAnimationExtractorModule;
	friend class USeamlessAnimatedSkeletonSubsystem;

public:
	/// The source Skeleton used to fetch the animations.
	UPROPERTY(EditAnywhere)
	USkeleton* SourceSkeleton = nullptr;

	/// The target Skeleton used during the retargeting.
	UPROPERTY(EditAnywhere)
	USkeleton* TargetSkeleton = nullptr;

	/// The Prefix used by the AnimationExtractor to remap the animations.
	UPROPERTY(EditAnywhere)
	FString Prefix;

private:
	/// The animations map to easily find the animation counter part.
	UPROPERTY(VisibleAnywhere)
	TMap<const UAnimSequenceBase*, UAnimSequenceBase*> AnimationsMap;

public: // ------------------------------------------------------- Notifications
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private: // ------------------------------------------------------- INTERNAL API
	// This function should never be called manually.
	// Please use `SemlessAnimatedSkeletons` module instead.
	UAnimSequenceBase* GetRetargetedAnimation(const UAnimSequenceBase* SourceAnim) const;
};
