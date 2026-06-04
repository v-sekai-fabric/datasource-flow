#pragma once

#include "UObject/Object.h"

#include "PostRetargetActionBase.generated.h"

class USkeleton;

/// You can override this class to add an action executed on each retargeted asset.
///
/// This feature is extremely useful to retarget custom AnimNotify.
/// You can find a usage example of this feature inside the file:
/// `PostRetargetActionBase_Niagara.h`
UCLASS(Abstract)
class ANIMATIONEXTRACTOR_API UPostRetargetActionBase : public UObject
{
	GENERATED_BODY()

public:
	virtual void ExecuteAction(USkeleton* SourceSkeleton, UObject* SourceAsset, USkeleton* TargetSkeleton, UObject* RetargetedAsset);
};
