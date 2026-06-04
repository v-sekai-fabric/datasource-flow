#include "SeamlessAnimatedSkeletons.h"

#include "Animation/AnimSequenceBase.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "SeamlessAnimationData.h"

DEFINE_LOG_CATEGORY(LogSAS);

void FSeamlessAnimatedSkeletons::StartupModule()
{
}

void FSeamlessAnimatedSkeletons::ShutdownModule()
{
}

IMPLEMENT_MODULE(FSeamlessAnimatedSkeletons, SeamlessAnimatedSkeletons)

void USeamlessAnimatedSkeletonSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	// Load the USeamlessAnimationData to easily transition between retargeted
	// animations.
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));

	TArray<FAssetData> Assets;
	AssetRegistryModule.GetRegistry().GetAssetsByClass(FName(TEXT("SeamlessAnimationData")), Assets);

	for (FAssetData& Asset : Assets)
	{
		USeamlessAnimationData* SeamlessAnimationData = Cast<USeamlessAnimationData>(Asset.GetAsset());
		checkf(SeamlessAnimationData!=nullptr, TEXT("This is not supposed to happen since we use a filter to fetch this asset."));

		if (SeamlessAnimationData->TargetSkeleton == nullptr ||
		    SeamlessAnimationData->SourceSkeleton == nullptr ||
		    SeamlessAnimationData->AnimationsMap.Num() == 0)
		{
			// Nothing to do.
			continue;
		}

		TMap<const USkeleton*, const USeamlessAnimationData*>& BySource_Maps = AnimationsMaps.FindOrAdd(SeamlessAnimationData->TargetSkeleton);
		BySource_Maps.Emplace(SeamlessAnimationData->SourceSkeleton, SeamlessAnimationData);
	}
}

UAnimSequenceBase* USeamlessAnimatedSkeletonSubsystem::GetAnimationFor(
	UAnimSequenceBase* SourceAnim,
	const USkeleton* TargetSkeleton)
{
	if (SourceAnim == nullptr || SourceAnim->GetSkeleton() == nullptr || TargetSkeleton == nullptr)
	{
		// There is nothing to search.
		return nullptr;
	}

	if (SourceAnim->GetSkeleton() == TargetSkeleton)
	{
		// Nothing special, same skeleton.
		return SourceAnim;
	}

	const TMap<const USkeleton*, const USeamlessAnimationData*>* BySources_Maps = AnimationsMaps.Find(TargetSkeleton);
	if (BySources_Maps != nullptr)
	{
		const USeamlessAnimationData* const* Map = BySources_Maps->Find(SourceAnim->GetSkeleton());
		if (Map != nullptr)
		{
			return (*Map)->GetRetargetedAnimation(SourceAnim);
		}
	}
	return nullptr;
}
