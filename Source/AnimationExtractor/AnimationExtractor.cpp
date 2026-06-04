#include "AnimationExtractor.h"

#include "AnimationExtractorStyle.h"
#include "AnimationExtractorCommands.h"
#include "EditorAnimUtils.h"
#include "Misc/MessageDialog.h"
#include "ToolMenus.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimSequence.h"
#include "AssetRegistry/AssetRegistryHelpers.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/ScopedSlowTask.h"
#include "IAssetTools.h"
#include "AssetToolsModule.h"
#include "SeamlessAnimationAction.h"
#include "SeamlessAnimatedSkeletons/SeamlessAnimationData.h"
#include "PostRetargetActionBase.h"
#include "Animation/AnimBlueprintGeneratedClass.h"
#include "EdGraph/EdGraph.h"

DEFINE_LOG_CATEGORY(AnimationExtractor);

class FAssetRegistryModule;

#define LOCTEXT_NAMESPACE "FAnimationExtractorModule"

void FAnimationExtractorModule::StartupModule()
{
	// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module

	FAnimationExtractorStyle::Initialize();
	FAnimationExtractorStyle::ReloadTextures();

	FAnimationExtractorCommands::Register();

	// Expose MeshRetargetData as asset by registering the editor Action.
	IAssetTools& AssetToolsModule =
		FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

	AssetToolsModule.RegisterAssetTypeActions(MakeShareable(new FAssetTypeActions_SeamlessAnimationData));

	PluginCommands = MakeShareable(new FUICommandList);

	PluginCommands->MapAction(
		FAnimationExtractorCommands::Get().RetargetMissingAction,
		FExecuteAction::CreateRaw(this, &FAnimationExtractorModule::RetargetMissingBtnClick),
		FCanExecuteAction());

	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FAnimationExtractorModule::RegisterMenus));
}

void FAnimationExtractorModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.

	UToolMenus::UnRegisterStartupCallback(this);

	UToolMenus::UnregisterOwner(this);

	FAnimationExtractorStyle::Shutdown();

	FAnimationExtractorCommands::Unregister();
}

void FAnimationExtractorModule::RetargetMissingBtnClick()
{
	StartRetarget();
}

void FAnimationExtractorModule::StartRetarget()
{
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));

	TArray<FAssetData> Assets;
	AssetRegistryModule.GetRegistry().GetAssetsByClass(FName(TEXT("SeamlessAnimationData")), Assets);
	for (FAssetData& Asset : Assets)
	{
		USeamlessAnimationData* SeamlessAnimationData = Cast<USeamlessAnimationData>(Asset.GetAsset());
		Retarget(Asset.PackagePath.ToString(), SeamlessAnimationData, AssetRegistryModule);
		UpdateAnimationData(SeamlessAnimationData, AssetRegistryModule);
	}

	FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(TEXT("Retargeting done.")));
}

// Copied from EditorAnimUtils.cpp, since not exposed :/
namespace CopiedEditorAnimUtils
{
/** Helper archive class to find all references, used by the cycle finder **/
class FFindAnimAssetRefs : public FArchiveUObject
{
public:
	/**
	* Constructor
	*
	* @param	Src		the object to serialize which may contain a references
	*/
	FFindAnimAssetRefs(UObject* Src, TArray<UAnimationAsset*>& OutAnimationAssets)
		: AnimationAssets(OutAnimationAssets)
	{
		// use the optimized RefLink to skip over properties which don't contain object references
		ArIsObjectReferenceCollector = true;

		ArIgnoreArchetypeRef = false;
		ArIgnoreOuterRef = true;
		ArIgnoreClassRef = false;

		Src->Serialize(*this);
	}

	virtual FString GetArchiveName() const
	{
		return TEXT("FFindAnimAssetRefs");
	}

private:
	/** Serialize a reference **/
	FArchive& operator<<(class UObject*& Obj)
	{
		if (UAnimationAsset* Anim = Cast<UAnimationAsset>(Obj))
		{
			AnimationAssets.AddUnique(Anim);
		}
		return *this;
	}

	TArray<UAnimationAsset*>& AnimationAssets;
};

void GetAllAnimationSequencesReferredInBlueprint(UAnimBlueprint* AnimBlueprint, TArray<UAnimationAsset*>& AnimationAssets)
{
	UObject* DefaultObject = AnimBlueprint->GetAnimBlueprintGeneratedClass()->GetDefaultObject();
	FFindAnimAssetRefs AnimRefFinderObject(DefaultObject, AnimationAssets);

	// For assets referenced in the event graph (either pin default values or variable-get nodes)
	// we need to serialize the nodes in that graph
	for (UEdGraph* GraphPage : AnimBlueprint->UbergraphPages)
	{
		for (UEdGraphNode* Node : GraphPage->Nodes)
		{
			FFindAnimAssetRefs AnimRefFinderBlueprint(Node, AnimationAssets);
		}
	}

	// Gather references in functions
	for (UEdGraph* GraphPage : AnimBlueprint->FunctionGraphs)
	{
		for (UEdGraphNode* Node : GraphPage->Nodes)
		{
			FFindAnimAssetRefs AnimRefFinderBlueprint(Node, AnimationAssets);
		}
	}
}
}

void FAnimationExtractorModule::Retarget(const FString& FolderPath, USeamlessAnimationData* SeamlessAnimation, FAssetRegistryModule& AssetRegistryModule)
{
	USkeleton* SourceSkeleton = SeamlessAnimation->SourceSkeleton;
	USkeleton* TargetSkeleton = SeamlessAnimation->TargetSkeleton;
	const FString& Prefix = SeamlessAnimation->Prefix;

	if (SourceSkeleton == nullptr || TargetSkeleton == nullptr)
	{
		// Nothing to do.
		return;
	};

	TArray<FString> AnimationNamesRetargeted;
	TArray<FAssetData> AssetsToRetargetData;

	// Filter the animations to retarget. This step is needed to show a progress bar.
	{
		TArray<FAssetData> AssetData;
		FetchAssets(AssetData, AssetRegistryModule);
		AssetsToRetargetData.Reserve(AssetData.Num());

		// Fetch the animation names already retargeted.
		for (auto Asset : AssetData)
		{
			if (IsUsingSkeleton(Asset, TargetSkeleton))
			{
				AnimationNamesRetargeted.Push(GetAnimResourceName(Asset));
			}
		}

		// Fetch the animations to retarget, and skip the one already retargeted.
		for (auto Asset : AssetData)
		{
			if (IsUsingSkeleton(Asset, SourceSkeleton))
			{
				const FString NewAssetName = Prefix + GetAnimResourceName(Asset);
				const int NameIndex = AnimationNamesRetargeted.Find(NewAssetName);
				if (NameIndex == INDEX_NONE)
				{
					AssetsToRetargetData.Push(Asset);
				}
			}
		}
	}

	if (AssetsToRetargetData.Num() == 0)
	{
		const FString Msg = TEXT("No new animations to retarget.");
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Msg));
		return;
	}
	else
	{
		// Make sure the User want's to retarget these Animations:
		FString Msg = FString::Printf(TEXT("You are about to start the Animation Retargeting from %s Skeleton to %s Skeleton.\n"), *SourceSkeleton->GetName(), *TargetSkeleton->GetName());
		Msg += FString::Printf(TEXT("The destination folder is `%s` and the prefix used to store the new animation is `%s`.\n\n"), *FolderPath, *Prefix);
		Msg += TEXT("Do you want to retarget the following animations?\n\n");
		for (auto Asset : AssetsToRetargetData)
		{
			Msg += TEXT("- ") + GetAnimResourceName(Asset) + TEXT("\n");
		}
		const EAppReturnType::Type Res = FMessageDialog::Open(
			EAppMsgType::YesNo,
			FText::FromString(Msg));

		if (EAppReturnType::Yes != Res)
		{
			// Doesn't want to proceed.
			return;
		}
	}

	FScopedSlowTask ProgressBarDialog(AssetsToRetargetData.Num(), LOCTEXT("SeamlessSkeletonsRetargeting", "Seamless Skeletons Retargeting"));
	ProgressBarDialog.Initialize();
	ProgressBarDialog.MakeDialog();

	// Original Asset: Retargeted Asset
	TMap<UAnimationAsset*, UAnimationAsset*> RetargetedAnimAssets;

	for (auto Asset : AssetsToRetargetData)
	{
		if (ProgressBarDialog.ShouldCancel())
		{
			// Early return, the user canceled.
			break;
		}

		ProgressBarDialog.EnterProgressFrame(1.0f, FText::Format(LOCTEXT("SeamlessSkeletonsRetargetingPerc", "Retargeting the animation: {0}"), FText::FromString(GetAnimResourceName(Asset))));

		// It's doing one retargeting at a time, since we need to set the right
		// FolderPath, and avoid changing Path.
		TArray<FAssetData> RetargetAssets;
		RetargetAssets.Push(Asset);

		EditorAnimUtils::FNameDuplicationRule NameRule;
		NameRule.Prefix = Prefix;
		NameRule.FolderPath = FolderPath;

		const bool bRetargetReferredAssets = true;
		const bool bConvertSpace = true;

		EditorAnimUtils::FAnimationRetargetContext RetargetContext(RetargetAssets, bRetargetReferredAssets, bConvertSpace);

		// Add the already retargeted assets, to avoid duplicates.
		for (const TPair<UAnimationAsset*, UAnimationAsset*>& E : RetargetedAnimAssets)
		{
			RetargetContext.AddRemappedAsset(E.Key, E.Value);
		}

		EditorAnimUtils::RetargetAnimations(
			SourceSkeleton,
			TargetSkeleton,
			RetargetContext,
			bRetargetReferredAssets,
			&NameRule);

		TArray<UObject*> SourceAssetsToRetarget;

		// Fetch all the sub animations used by this BP anim.
		if (Asset.GetAsset()->IsA<UAnimBlueprint>())
		{
			TArray<UAnimationAsset*> ReferredAnimations;
			CopiedEditorAnimUtils::GetAllAnimationSequencesReferredInBlueprint(static_cast<UAnimBlueprint*>(Asset.GetAsset()), ReferredAnimations);
			SourceAssetsToRetarget.Append(ReferredAnimations);
		}

		// Put the original asset
		SourceAssetsToRetarget.AddUnique(Asset.GetAsset());

		// Fetch the Retargeted assets to execute the post actions.
		TArray<UObject*> RetargetedObjects;
		for (UObject* SourceAsset : SourceAssetsToRetarget)
		{
			UObject* Retargeted = RetargetContext.GetDuplicate(SourceAsset);
			if (Retargeted != nullptr)
			{
				// Post action.
				ExecutePostRetargetActions(SourceSkeleton, SourceAsset, TargetSkeleton, Retargeted);

				// Store the assets inside the map so those are not retargeted again.
				if (SourceAsset->IsA<UAnimationAsset>())
				{
					checkf(Retargeted->IsA<UAnimationAsset>(), TEXT("An UAnimationAsset can't be retargeted to something else."));
					RetargetedAnimAssets.Add(
						static_cast<UAnimationAsset*>(SourceAsset),
						static_cast<UAnimationAsset*>(Retargeted));
				}
			}
		}
	}

	ProgressBarDialog.Destroy();
}

void FAnimationExtractorModule::UpdateAnimationData(USeamlessAnimationData* SeamlessAnimation, FAssetRegistryModule& AssetRegistryModule)
{
	SeamlessAnimation->AnimationsMap.Empty();

	USkeleton* SourceSkeleton = SeamlessAnimation->SourceSkeleton;
	USkeleton* TargetSkeleton = SeamlessAnimation->TargetSkeleton;
	const FString& Prefix = SeamlessAnimation->Prefix;

	TArray<FAssetData> AssetData;
	FetchAssets(AssetData, AssetRegistryModule);

	TMap<FString, FAssetData> AnimationsRetargeted;

	// Fetch the animation names already retargeted.
	for (auto Asset : AssetData)
	{
		if (IsUsingSkeleton(Asset, TargetSkeleton))
		{
			AnimationsRetargeted.Emplace(GetAnimResourceName(Asset), Asset);
		}
	}

	// Fetch the animations to retarget, and skip the one already retargeted.
	for (auto Asset : AssetData)
	{
		if (IsUsingSkeleton(Asset, SourceSkeleton))
		{
			const FString NewAssetName = Prefix + GetAnimResourceName(Asset);
			const FAssetData* TargetAsset = AnimationsRetargeted.Find(NewAssetName);
			if (TargetAsset != nullptr)
			{
				if (TargetAsset->GetAsset()->IsA<UAnimSequenceBase>())
				{
					// Add to the map the AnimSequence
					SeamlessAnimation->AnimationsMap.Emplace(
						static_cast<const UAnimSequenceBase*>(Asset.GetAsset()),
						static_cast<UAnimSequenceBase*>(TargetAsset->GetAsset()));
				}
			}
		}
	}
}

void FAnimationExtractorModule::RegisterMenus()
{
	// Owner will be used for cleanup in call to UToolMenus::UnregisterOwner
	FToolMenuOwnerScoped OwnerScoped(this);

	{
		UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Window");
		{
			FToolMenuSection& Section = Menu->FindOrAddSection("WindowLayout");
			Section.AddMenuEntryWithCommandList(FAnimationExtractorCommands::Get().RetargetMissingAction, PluginCommands);
		}
	}

	{
		UToolMenu* ToolbarMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.LevelEditorToolBar");
		{
			FToolMenuSection& Section = ToolbarMenu->FindOrAddSection("Settings");
			{
				{
					FToolMenuEntry& Entry = Section.AddEntry(FToolMenuEntry::InitToolBarButton(
						FAnimationExtractorCommands::Get().RetargetMissingAction,
						TAttribute<FText>(),
						TAttribute<FText>(),
						FSlateIcon(
							FAnimationExtractorStyle::GetStyleSetName(),
							FName(TEXT("AnimationExtractor.Retarget")),
							FName(TEXT("AnimationExtractor.Retarget.Small")))
						));
					Entry.SetCommandList(PluginCommands);
				}
			}
		}
	}
}

void FAnimationExtractorModule::FetchAssets(TArray<FAssetData>& OutAssetData, FAssetRegistryModule& AssetRegistryModule)
{
	AssetRegistryModule.GetRegistry().GetAssetsByClass(FName(TEXT("AnimSequence")), OutAssetData);
	AssetRegistryModule.GetRegistry().GetAssetsByClass(FName(TEXT("BlendSpace1D")), OutAssetData);
	AssetRegistryModule.GetRegistry().GetAssetsByClass(FName(TEXT("BlendSpace")), OutAssetData);
	AssetRegistryModule.GetRegistry().GetAssetsByClass(FName(TEXT("AnimMontage")), OutAssetData);
	AssetRegistryModule.GetRegistry().GetAssetsByClass(FName(TEXT("AnimBlueprint")), OutAssetData);
}

bool FAnimationExtractorModule::IsUsingSkeleton(const FAssetData& Asset, const USkeleton* Skeleton)
{
	if (const UAnimationAsset* Anim = Cast<UAnimationAsset>(Asset.GetAsset()))
	{
		return Anim->GetSkeleton() == Skeleton;
	}
	else if (const UAnimBlueprint* AnimBp = Cast<UAnimBlueprint>(Asset.GetAsset()))
	{
		return AnimBp->TargetSkeleton == Skeleton;
	}
	else
	{
		checkf(false, TEXT("The type of this asset is not handled. Make sure to properly handle it!"));
	}
	return false;
}

FString FAnimationExtractorModule::GetAnimResourceName(const FAssetData& Asset)
{
	if (const UAnimationAsset* Anim = Cast<UAnimationAsset>(Asset.GetAsset()))
	{
		return Anim->GetName();
	}
	else if (const UAnimBlueprint* AnimBp = Cast<UAnimBlueprint>(Asset.GetAsset()))
	{
		return AnimBp->GetName();
	}
	else
	{
		checkf(false, TEXT("The type of this asset is not handled. Make sure to properly handle it!"));
	}
	return FString();
}

// Retarget the Notifies
void FAnimationExtractorModule::ExecutePostRetargetActions(USkeleton* SourceSkeleton, UObject* SourceAsset, USkeleton* TargetSkeleton, UObject* RetargetedAsset)
{
	if (PostRetargetActionsLoaded == false)
	{
		// Fetch all the `UClasses` that derives from `UPostRetargetActionBase`.
		for (TObjectIterator<UClass> It; It; ++It)
		{
			if (It->IsChildOf(UPostRetargetActionBase::StaticClass()) && !It->HasAnyClassFlags(CLASS_Abstract))
			{
				UPostRetargetActionBase* Action = NewObject<UPostRetargetActionBase>(GetTransientPackage(), *It);
				checkf(Action != nullptr, TEXT("This can't fail considering how it's fetching the UClasses."));
				PostRetargetActions.Add(TStrongObjectPtr<UPostRetargetActionBase>(Action));
			}
		}
		PostRetargetActionsLoaded = true;
	}

	for (TStrongObjectPtr<UPostRetargetActionBase>& Action : PostRetargetActions)
	{
		Action->ExecuteAction(SourceSkeleton, SourceAsset, TargetSkeleton, RetargetedAsset);
	}
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FAnimationExtractorModule, AnimationExtractor)
