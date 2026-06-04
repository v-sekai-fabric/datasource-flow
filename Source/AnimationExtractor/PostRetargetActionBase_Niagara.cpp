#include "PostRetargetActionBase_Niagara.h"

#include "Animation/AnimSequenceBase.h"
#include "AnimNotify_PlayNiagaraEffect.h"
#include "AnimationExtractor.h"

void UPostRetargetAction_NiagaraEffect::ExecuteAction(USkeleton* SourceSkeleton, UObject* SourceAsset, USkeleton* TargetSkeleton, UObject* RetargetedAsset)
{
	if (SourceSkeleton->GetRig() != TargetSkeleton->GetRig())
	{
		// Nothing to do, the rigs are different.
		UE_LOG(AnimationExtractor, Warning, TEXT("The two skeletons rigs are different, this is not supposed to happen."));
		return;
	}

	if (RetargetedAsset->IsA<UAnimSequenceBase>())
	{
		UAnimSequenceBase* Sequence = static_cast<UAnimSequenceBase*>(RetargetedAsset);
		for (FAnimNotifyEvent& Notify : Sequence->Notifies)
		{
			if (Notify.Notify != nullptr && Notify.Notify->IsA<UAnimNotify_PlayNiagaraEffect>())
			{
				UAnimNotify_PlayNiagaraEffect* Effect = static_cast<UAnimNotify_PlayNiagaraEffect*>(Notify.Notify);
				const FName NodeName = SourceSkeleton->GetRigNodeNameFromBoneName(Effect->SocketName);
				if (NodeName != NAME_None)
				{
					const FName TargetBone = TargetSkeleton->GetRigBoneMapping(NodeName);
					Effect->SocketName = TargetBone;
				}
				else
				{
					UE_LOG(AnimationExtractor, Warning, TEXT("The Bone name `%s` on the source skeleton was not found on the Retarget bone mapping. This asset AnimNotify is impossible to retarget: `%s`."), *Effect->SocketName.ToString(), *RetargetedAsset->GetName());
				}
			}
		}
	}
}
