/**************************************************************************/
/*  idtx_host_uri_resolver.cpp                                            */
/**************************************************************************/
/* Copyright 2026 The openusd-fabric authors / V-Sekai contributors.      */
/* SPDX-License-Identifier: Apache-2.0 OR MPL-2.0                         */
/**************************************************************************/

// In-core ArResolver for host URI schemes (res://, user://). Ported from the
// extension's former UsdGodotAssetResolver, but engine-agnostic: instead of
// calling Godot's ProjectSettings::globalize_path, it maps the URI to a
// filesystem path through the host-registered idtx_asset_io callback. This is
// how OpenUSD (which lives only in the core now) resolves engine asset paths
// referenced INSIDE a stage, while the engine-specific res:// knowledge stays
// in the host. Registered for the res/user schemes via the sibling plugInfo.json.

#include "idtx_core/idtx_asset_io.h"

#include <cstring>
#include <string>

#include <pxr/base/tf/pathUtils.h>
#include <pxr/base/tf/stringUtils.h>
#include <pxr/base/tf/staticTokens.h>
#include <pxr/usd/ar/asset.h>
#include <pxr/usd/ar/defineResolver.h>
#include <pxr/usd/ar/filesystemAsset.h>
#include <pxr/usd/ar/filesystemWritableAsset.h>
#include <pxr/usd/ar/resolvedPath.h>
#include <pxr/usd/ar/resolver.h>
#include <pxr/usd/ar/writableAsset.h>

PXR_NAMESPACE_OPEN_SCOPE
TF_DEFINE_PRIVATE_TOKENS(_IdtxHostUriTokens,
    ((resScheme,  "res"))
    ((userScheme, "user")));
PXR_NAMESPACE_CLOSE_SCOPE

namespace {
// The host's asset-IO hooks (copied on register). Not thread-safe against
// concurrent stage opens, per the ABI contract.
idtx_asset_io_t g_io = {};
bool g_io_set = false;

// Map a host URI to an absolute filesystem path via the registered callback;
// fall back to the URI as-is (plain path) when no host is registered.
std::string host_globalize(const std::string& uri) {
    if (g_io_set && g_io.globalize_path) {
        char buf[4096] = {0};
        if (g_io.globalize_path(g_io.user, uri.c_str(), buf, static_cast<int32_t>(sizeof(buf))))
            return std::string(buf);
    }
    return uri;
}
}  // namespace

extern "C" IDTX_CORE_API void idtx_core_set_asset_io(const idtx_asset_io_t* io) {
    if (io) {
        g_io = *io;
        g_io_set = true;
    } else {
        g_io = idtx_asset_io_t{};
        g_io_set = false;
    }
}

PXR_NAMESPACE_OPEN_SCOPE

class IdtxHostUriResolver : public ArResolver {
public:
    IdtxHostUriResolver() = default;
    ~IdtxHostUriResolver() override = default;

protected:
    std::string _GetExtension(const std::string& path) const override {
        return TfGetExtension(path);
    }

    // Anchor a relative path under the (scheme-prefixed) anchor; pass through an
    // already-absolute scheme URI. Pure string logic — identical to the prior
    // Godot resolver.
    std::string _CreateIdentifier(const std::string& assetPath,
                                  const ArResolvedPath& anchorAssetPath) const override {
        const std::string resPfx  = _IdtxHostUriTokens->resScheme.GetString()  + "://";
        const std::string userPfx = _IdtxHostUriTokens->userScheme.GetString() + "://";

        if (TfStringStartsWith(assetPath, resPfx) || TfStringStartsWith(assetPath, userPfx))
            return assetPath;  // already absolute

        if (!anchorAssetPath.IsEmpty()) {
            std::string anchorDir = TfGetPathName(anchorAssetPath.GetPathString());
            std::string scheme;
            if (TfStringStartsWith(anchorDir, resPfx)) {
                scheme = resPfx; anchorDir = anchorDir.substr(resPfx.size());
            } else if (TfStringStartsWith(anchorDir, userPfx)) {
                scheme = userPfx; anchorDir = anchorDir.substr(userPfx.size());
            }
            return scheme + TfNormPath(TfStringCatPaths(anchorDir, assetPath));
        }
        return resPfx + assetPath;  // relative w/o anchor -> make res://-absolute
    }

    std::string _CreateIdentifierForNewAsset(const std::string& assetPath,
                                             const ArResolvedPath& anchorAssetPath) const override {
        return _CreateIdentifier(assetPath, anchorAssetPath);
    }

    // Keep the scheme URI as the resolved path; _OpenAsset globalizes it. Return
    // empty for paths that are not ours (so other resolvers handle them).
    ArResolvedPath _Resolve(const std::string& assetPath) const override {
        const std::string resPfx  = _IdtxHostUriTokens->resScheme.GetString()  + "://";
        const std::string userPfx = _IdtxHostUriTokens->userScheme.GetString() + "://";
        if (!TfStringStartsWith(assetPath, resPfx) && !TfStringStartsWith(assetPath, userPfx))
            return ArResolvedPath();
        return ArResolvedPath(assetPath);
    }

    ArResolvedPath _ResolveForNewAsset(const std::string& assetPath) const override {
        return _Resolve(assetPath);
    }

    std::shared_ptr<ArAsset> _OpenAsset(const ArResolvedPath& resolvedPath) const override {
        return ArFilesystemAsset::Open(
            ArResolvedPath(host_globalize(resolvedPath.GetPathString())));
    }

    std::shared_ptr<ArWritableAsset> _OpenAssetForWrite(const ArResolvedPath& resolvedPath,
                                                        WriteMode writeMode) const override {
        return ArFilesystemWritableAsset::Create(
            ArResolvedPath(host_globalize(resolvedPath.GetPathString())), writeMode);
    }
};

AR_DEFINE_RESOLVER(IdtxHostUriResolver, ArResolver);

PXR_NAMESPACE_CLOSE_SCOPE
