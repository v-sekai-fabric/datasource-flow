#include "PostRetargetActionBase.h"

void UPostRetargetActionBase::ExecuteAction(USkeleton* SourceSkeleton, UObject* SourceAsset, USkeleton* TargetSkeleton, UObject* RetargetedAsset)
{
	ensureAlwaysMsgf(false, TEXT("Please override this function or remove this class."));
}
