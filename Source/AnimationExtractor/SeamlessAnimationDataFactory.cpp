#include "SeamlessAnimationDataFactory.h"

#include "SeamlessAnimatedSkeletons/SeamlessAnimationData.h"

USeamlessAnimationDataFactory::USeamlessAnimationDataFactory(const FObjectInitializer& ObjectInitializer)
{
	SupportedClass = USeamlessAnimationData::StaticClass();
	bCreateNew = true;
	bEditAfterNew = true;
}

UObject* USeamlessAnimationDataFactory::FactoryCreateNew(
	UClass* InClass,
	UObject* InParent,
	FName InName,
	EObjectFlags Flags,
	UObject* Context,
	FFeedbackContext* Warn)
{
	return NewObject<USeamlessAnimationData>(InParent, InClass, InName, Flags);
}

bool USeamlessAnimationDataFactory::ShouldShowInNewMenu() const
{
	// Return always true since we want to show the creation menu in any case.
	return true;
}
