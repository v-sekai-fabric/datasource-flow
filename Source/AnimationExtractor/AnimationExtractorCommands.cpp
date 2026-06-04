#include "AnimationExtractorCommands.h"

#define LOCTEXT_NAMESPACE "FAnimationExtractorModule"

void FAnimationExtractorCommands::RegisterCommands()
{
	UI_COMMAND(RetargetMissingAction, "Retarget seamless Skeletons.", "Retarget seamless Skeletons.", EUserInterfaceActionType::Button, FInputGesture());
}

#undef LOCTEXT_NAMESPACE
