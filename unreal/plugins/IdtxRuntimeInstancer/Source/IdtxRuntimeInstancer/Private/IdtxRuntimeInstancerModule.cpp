// Copyright 2026 The openusd-fabric authors / V-Sekai contributors.
// SPDX-License-Identifier: Apache-2.0 OR MPL-2.0

#include "Modules/ModuleManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "HAL/PlatformProcess.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <windows.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif

#if IDTX_CORE_USE_STUBS
#include "idtx_core_stubs.h"
#endif

#include "idtx_core/idtx_core.h"

#ifndef IDTX_CORE_LIB_BASENAME
#define IDTX_CORE_LIB_BASENAME "libidtx_core"
#endif

// Read by IdtxRuntimeInstancer.cpp before any idtx_core_* call.
bool GIdtxRuntimeCoreLoaded = false;

class FIdtxRuntimeInstancerModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		const FString Dir = CoreDir();
		if (Dir.IsEmpty()) return;
#if PLATFORM_WINDOWS
		FPlatformProcess::PushDllDirectory(*Dir);
		const FString Dll = FPaths::Combine(Dir, TEXT(IDTX_CORE_LIB_BASENAME) TEXT(".dll"));
		Handle = FPlatformProcess::GetDllHandle(*Dll);
		FPlatformProcess::PopDllDirectory(*Dir);
		GIdtxRuntimeCoreLoaded = (Handle != nullptr);
#elif IDTX_CORE_USE_STUBS
		const TCHAR* Ext = PLATFORM_MAC ? TEXT(".dylib") : TEXT(".so");
		const FString Lib = FPaths::Combine(Dir, TEXT(IDTX_CORE_LIB_BASENAME) + FString(Ext));
		core::StubPathMap Paths;
		Paths[core::kModuleIdtx_core] = { TCHAR_TO_UTF8(*Lib) };
		GIdtxRuntimeCoreLoaded = core::InitializeStubs(Paths);
#endif
		if (GIdtxRuntimeCoreLoaded)
		{
			idtx_core_init(nullptr);
			UE_LOG(LogTemp, Log, TEXT("IdtxRuntimeInstancer: libidtx_core %s loaded"),
				ANSI_TO_TCHAR(idtx_core_version()));
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("IdtxRuntimeInstancer: libidtx_core failed to load from %s"), *Dir);
		}
	}

	virtual void ShutdownModule() override
	{
#if PLATFORM_WINDOWS
		if (Handle) { FPlatformProcess::FreeDllHandle(Handle); Handle = nullptr; }
#endif
		GIdtxRuntimeCoreLoaded = false;
	}

private:
	static FString CoreDir()
	{
		TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("IdtxRuntimeInstancer"));
		if (!Plugin.IsValid()) return FString();
#if PLATFORM_WINDOWS
		const TCHAR* P = TEXT("Win64");
#elif PLATFORM_MAC
		const TCHAR* P = TEXT("Mac");
#else
		const TCHAR* P = TEXT("Linux");
#endif
		const FString Base = Plugin->GetBaseDir();
		const FString Staged = FPaths::Combine(Base, TEXT("Binaries"), P);
		const FString Dev = FPaths::Combine(Base, TEXT("Source"), TEXT("ThirdParty"), TEXT("idtx_core"), P);
		return FPaths::DirectoryExists(Staged) ? Staged : Dev;
	}

	void* Handle = nullptr;
};

IMPLEMENT_MODULE(FIdtxRuntimeInstancerModule, IdtxRuntimeInstancer)
