/**
 * The only purpose of this file is to provide the required HttpAssetResolver registration/glue code that could not be
 * part of the header files in the shared code.
 **/
#include <pxr/usd/ar/defineResolver.h>
#include <pxr/usd/ar/resolver.h>

#include <idtxflow/resolver/HttpResolver.h>

using namespace pxr;

AR_DEFINE_RESOLVER(UsdHttpAssetResolver, ArResolver);
