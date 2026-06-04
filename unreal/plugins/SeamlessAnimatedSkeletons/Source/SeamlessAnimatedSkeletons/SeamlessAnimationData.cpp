#include "SeamlessAnimationData.h"

#include "Animation/AnimSequenceBase.h"

#if WITH_EDITOR
void USeamlessAnimationData::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	UObject::PostEditChangeProperty(PropertyChangedEvent);

	if (AnimationsMap.Num() == 0)
	{
		// Nothing to do.
		return;
	}

	// Something changed, make sure the skeletons still match the Animations inside
	// the map, otherwise clear it.

	const UAnimSequenceBase* SampleAnimationSource = AnimationsMap.begin().Key();
	const UAnimSequenceBase* SampleAnimationTarget = AnimationsMap.begin().Value();

	if (SourceSkeleton != SampleAnimationSource->GetSkeleton() || TargetSkeleton != SampleAnimationTarget->GetSkeleton())
	{
		AnimationsMap.Empty();
	}
}
#endif

UAnimSequenceBase* USeamlessAnimationData::GetRetargetedAnimation(const UAnimSequenceBase* SourceAnim) const
{
	checkf(SourceAnim->GetSkeleton() == SourceSkeleton, TEXT("You can't request the retargeted animation to this archive, since it's not handling this skeleton. This is a bug!"));
	UAnimSequenceBase* const* RetargetedAnimE = AnimationsMap.Find(SourceAnim);
	return RetargetedAnimE != nullptr ? *RetargetedAnimE : nullptr;
}
