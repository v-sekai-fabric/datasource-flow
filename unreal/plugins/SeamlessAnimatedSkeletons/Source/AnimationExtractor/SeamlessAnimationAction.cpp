#include "SeamlessAnimationAction.h"

#include "SeamlessAnimatedSkeletons/SeamlessAnimationData.h"

#define LOCTEXT_NAMESPACE "FSeamlessAnimatedSkeletons"

FText FAssetTypeActions_SeamlessAnimationData::GetName() const
{
	return LOCTEXT("SeamlessAnimationData", "Seamless Animation Data");
}

FColor FAssetTypeActions_SeamlessAnimationData::GetTypeColor() const
{
	return FColor::FromHex(TEXT("0a9396"));
}

UClass* FAssetTypeActions_SeamlessAnimationData::GetSupportedClass() const
{
	return USeamlessAnimationData::StaticClass();
}

uint32 FAssetTypeActions_SeamlessAnimationData::GetCategories()
{
	return EAssetTypeCategories::Animation;
}

#undef LOCTEXT_NAMESPACE
