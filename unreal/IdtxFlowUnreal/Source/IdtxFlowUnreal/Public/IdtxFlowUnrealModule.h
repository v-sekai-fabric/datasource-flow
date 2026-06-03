// Copyright 2026 The openusd-fabric authors / V-Sekai contributors.
// SPDX-License-Identifier: Apache-2.0 OR MPL-2.0

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

// CHI-312 #4: editor module. StartupModule loads libidtx_core via the generated
// dlopen table (delay-load on Windows, dlsym stubs on POSIX) before any
// idtx_core_* call, and registers the export menu action. ShutdownModule frees
// the handle. UE never link-depends on core or its static OpenUSD.
class FIdtxFlowUnrealModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	/** True once libidtx_core is loaded and callable. */
	static bool IsCoreLoaded();

private:
	bool LoadIdtxCore();
	void RegisterMenus();

	void* CoreHandle = nullptr; // Windows: bundled-DLL handle; POSIX: unused
	bool bCoreLoaded = false;
};
