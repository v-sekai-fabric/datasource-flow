#pragma once

#include "CoreMinimal.h"
#include "Framework/Commands/Commands.h"
#include "AnimationExtractorStyle.h"

class FAnimationExtractorCommands : public TCommands<FAnimationExtractorCommands>
{
public:
	FAnimationExtractorCommands()
		: TCommands<FAnimationExtractorCommands>(TEXT("AnimationExtractor"), NSLOCTEXT("Contexts", "AnimationExtractor", "AnimationExtractor Plugin"), NAME_None, FAnimationExtractorStyle::GetStyleSetName())
	{
	}

	virtual void RegisterCommands() override;

public:
	TSharedPtr<FUICommandInfo> RetargetMissingAction;
};
