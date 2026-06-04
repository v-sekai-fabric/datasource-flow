// Copyright 2026 The openusd-fabric authors.
// SPDX-License-Identifier: MIT
//
// Hydra plugin registration. TfType lookup table is shipped via
// plugInfo.json next to the built shared library; this file's job is
// just to provide the typed factory that Hd uses to instantiate the
// task when the scene delegate references it.

#include "outlineJFATask.h"

#include "pxr/base/tf/type.h"
#include "pxr/imaging/hd/sceneDelegate.h"
#include "pxr/imaging/hd/task.h"

PXR_NAMESPACE_OPEN_SCOPE

TF_REGISTRY_FUNCTION(TfType)
{
    TfType::Define<HdVSekaiOutlineJFATask, TfType::Bases<HdTask>>();
}

PXR_NAMESPACE_CLOSE_SCOPE
