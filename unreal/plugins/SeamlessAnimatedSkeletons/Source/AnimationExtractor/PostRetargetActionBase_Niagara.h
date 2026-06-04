#pragma once

#include "PostRetargetActionBase.h"

#include "PostRetargetActionBase_Niagara.generated.h"

/// Retargets the NiagaraEffect Anim Notify, so it uses the correct BoneName on the target skeleton.
UCLASS()
class UPostRetargetAction_NiagaraEffect : public UPostRetargetActionBase
{
	GENERATED_BODY()

public:
	virtual void ExecuteAction(USkeleton* SourceSkeleton, UObject* SourceAsset, USkeleton* TargetSkeleton, UObject* RetargetedAsset) override;
};
