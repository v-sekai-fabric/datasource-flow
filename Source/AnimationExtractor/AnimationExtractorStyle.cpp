#include "AnimationExtractorStyle.h"
#include "AnimationExtractor.h"
#include "Framework/Application/SlateApplication.h"
#include "Styling/SlateStyleRegistry.h"
#include "Slate/SlateGameResources.h"
#include "Interfaces/IPluginManager.h"

TSharedPtr<FSlateStyleSet> FAnimationExtractorStyle::StyleInstance = NULL;

void FAnimationExtractorStyle::Initialize()
{
	if (!StyleInstance.IsValid())
	{
		StyleInstance = Create();
		FSlateStyleRegistry::RegisterSlateStyle(*StyleInstance);
	}
}

void FAnimationExtractorStyle::Shutdown()
{
	FSlateStyleRegistry::UnRegisterSlateStyle(*StyleInstance);
	ensure(StyleInstance.IsUnique());
	StyleInstance.Reset();
}

FName FAnimationExtractorStyle::GetStyleSetName()
{
	static FName StyleSetName(TEXT("AnimationExtractorStyle"));
	return StyleSetName;
}

#define IMAGE_BRUSH( RelativePath, ... ) FSlateImageBrush( Style->RootToContentDir( RelativePath, TEXT(".png") ), __VA_ARGS__ )
#define BOX_BRUSH( RelativePath, ... ) FSlateBoxBrush( Style->RootToContentDir( RelativePath, TEXT(".png") ), __VA_ARGS__ )
#define BORDER_BRUSH( RelativePath, ... ) FSlateBorderBrush( Style->RootToContentDir( RelativePath, TEXT(".png") ), __VA_ARGS__ )
#define TTF_FONT( RelativePath, ... ) FSlateFontInfo( Style->RootToContentDir( RelativePath, TEXT(".ttf") ), __VA_ARGS__ )
#define OTF_FONT( RelativePath, ... ) FSlateFontInfo( Style->RootToContentDir( RelativePath, TEXT(".otf") ), __VA_ARGS__ )

const FVector2D Icon16x16(16.0f, 16.0f);
const FVector2D Icon20x20(20.0f, 20.0f);
const FVector2D Icon40x40(40.0f, 40.0f);

TSharedRef<FSlateStyleSet> FAnimationExtractorStyle::Create()
{
	TSharedRef<FSlateStyleSet> Style = MakeShareable(new FSlateStyleSet(GetStyleSetName()));
	Style->SetContentRoot(IPluginManager::Get().FindPlugin("SeamlessAnimatedSkeletons")->GetBaseDir() / TEXT("Resources"));

	{
		// Create a brush from the icon for `RetargetNewIcon`.
		FSlateImageBrush* ThumbnailBrush = new FSlateImageBrush(
			Style->RootToContentDir(TEXT("RetargetIcon"), TEXT(".png")),
			Icon40x40);

		// Bind the Thumbnail to the `AnimationExtractor`.
		Style->Set("AnimationExtractor.Retarget", ThumbnailBrush);
	}
	{
		// Create a brush from the icon for `RetargetNewIcon`.
		FSlateImageBrush* ThumbnailBrush = new FSlateImageBrush(
			Style->RootToContentDir(TEXT("RetargetIcon"), TEXT(".png")),
			FVector2D(24.f, 24.f));

		// Bind the Thumbnail to the `AnimationExtractor`.
		Style->Set("AnimationExtractor.Retarget.Small", ThumbnailBrush);
	}

	return Style;
}

#undef IMAGE_BRUSH
#undef BOX_BRUSH
#undef BORDER_BRUSH
#undef TTF_FONT
#undef OTF_FONT

void FAnimationExtractorStyle::ReloadTextures()
{
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().GetRenderer()->ReloadTextureResources();
	}
}

const ISlateStyle& FAnimationExtractorStyle::Get()
{
	return *StyleInstance;
}
