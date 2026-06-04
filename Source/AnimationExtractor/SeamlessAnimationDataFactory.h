#pragma once

#include "Factories/Factory.h"

#include "SeamlessAnimationDataFactory.generated.h"

/// The factory to show the option to create the `USeamlessAnimationData`.
UCLASS(HideCategories=(Object))
class USeamlessAnimationDataFactory : public UFactory
{
	GENERATED_BODY()

public:
	USeamlessAnimationDataFactory(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	UObject* FactoryCreateNew(
		UClass* InClass,
		UObject* InParent,
		FName InName,
		EObjectFlags Flags,
		UObject* Context,
		FFeedbackContext* Warn) override;

	bool ShouldShowInNewMenu() const override;
};
