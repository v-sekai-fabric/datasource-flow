// Copyright 2026 The openusd-fabric authors / V-Sekai contributors.
// SPDX-License-Identifier: Apache-2.0 OR MPL-2.0

#include "IdtxFlowUnrealModule.h"

#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "HAL/PlatformProcess.h"
#include "ToolMenus.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <windows.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif

#if IDTX_CORE_USE_STUBS
#include "idtx_core_stubs.h" // generated (core/generated) — POSIX dlsym table
#endif

#include "idtx_core/idtx_core.h"

#define LOCTEXT_NAMESPACE "IdtxFlowUnreal"

#ifndef IDTX_CORE_LIB_BASENAME
#define IDTX_CORE_LIB_BASENAME "libidtx_core"
#endif

static bool GIdtxCoreLoaded = false;

bool FIdtxFlowUnrealModule::IsCoreLoaded()
{
	return GIdtxCoreLoaded;
}

void FIdtxFlowUnrealModule::StartupModule()
{
	bCoreLoaded = LoadIdtxCore();
	GIdtxCoreLoaded = bCoreLoaded;
	if (bCoreLoaded)
	{
		// Initialise core (no plugin dir override — core finds its own USD
		// plugins relative to the loaded DLL).
		idtx_core_init(nullptr);
		UE_LOG(LogTemp, Log, TEXT("IdtxFlowUnreal: libidtx_core %s loaded"),
			ANSI_TO_TCHAR(idtx_core_version()));
		UToolMenus::RegisterStartupCallback(
			FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FIdtxFlowUnrealModule::RegisterMenus));
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("IdtxFlowUnreal: failed to load libidtx_core; export disabled."));
	}
}

void FIdtxFlowUnrealModule::ShutdownModule()
{
	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);
#if PLATFORM_WINDOWS
	if (CoreHandle)
	{
		FPlatformProcess::FreeDllHandle(CoreHandle);
		CoreHandle = nullptr;
	}
#endif
	GIdtxCoreLoaded = false;
}

// Directory where the bundled core DLL + deps are staged:
//   <Plugin>/Source/ThirdParty/idtx_core/<Platform>/  (dev tree)
//   <Plugin>/Binaries/<Platform>/                      (staged build — deps
//                                                        land beside the module)
static FString CoreBinaryDir()
{
	TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("IdtxFlowUnreal"));
	if (!Plugin.IsValid())
	{
		return FString();
	}
	const FString Base = Plugin->GetBaseDir();
#if PLATFORM_WINDOWS
	const TCHAR* Platform = TEXT("Win64");
#elif PLATFORM_MAC
	const TCHAR* Platform = TEXT("Mac");
#else
	const TCHAR* Platform = TEXT("Linux");
#endif
	// Prefer the staged Binaries dir (packaged), fall back to the dev ThirdParty dir.
	const FString Staged = FPaths::Combine(Base, TEXT("Binaries"), Platform);
	const FString Dev = FPaths::Combine(Base, TEXT("Source"), TEXT("ThirdParty"), TEXT("idtx_core"), Platform);
	return FPaths::DirectoryExists(Staged) ? Staged : Dev;
}

bool FIdtxFlowUnrealModule::LoadIdtxCore()
{
	const FString Dir = CoreBinaryDir();
	if (Dir.IsEmpty())
	{
		return false;
	}

#if PLATFORM_WINDOWS
	// Add the dir so the core DLL's own deps (usd_ms/tbb12/libidtx_usd) resolve,
	// then load the DLL so the delay-load thunks bind to this bundled copy.
	FPlatformProcess::PushDllDirectory(*Dir);
	const FString DllPath = FPaths::Combine(Dir, TEXT(IDTX_CORE_LIB_BASENAME) TEXT(".dll"));
	CoreHandle = FPlatformProcess::GetDllHandle(*DllPath);
	FPlatformProcess::PopDllDirectory(*Dir);
	return CoreHandle != nullptr;
#elif IDTX_CORE_USE_STUBS
	const TCHAR* Ext =
#if PLATFORM_MAC
		TEXT(".dylib");
#else
		TEXT(".so");
#endif
	const FString LibPath = FPaths::Combine(Dir, TEXT(IDTX_CORE_LIB_BASENAME) + FString(Ext));
	core::StubPathMap Paths;
	Paths[core::kModuleIdtx_core] = { TCHAR_TO_UTF8(*LibPath) };
	return core::InitializeStubs(Paths);
#else
	return false;
#endif
}

void FIdtxFlowUnrealModule::RegisterMenus()
{
	// File -> Export menu entry is provided by the UExporter subclass
	// (UIdtxFlowExporter); this hook is reserved for a dedicated
	// "Export Avatar to USD (idtx)" tool menu action. Left as a registration
	// point so the exporter and an explicit action share one owner.
	FToolMenuOwnerScoped OwnerScoped(this);
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FIdtxFlowUnrealModule, IdtxFlowUnreal)
