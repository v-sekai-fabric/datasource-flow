#pragma once

#include "AssetTypeActions_Base.h"

/// The action responsible to create the asset `USeamlessAnimationData`
/// on the editor.
class FAssetTypeActions_SeamlessAnimationData : public FAssetTypeActions_Base
{
public:
	virtual FText GetName() const override;
	virtual FColor GetTypeColor() const override;
	virtual UClass* GetSupportedClass() const override;
	virtual uint32 GetCategories() override;
};
