// Copyright 2026 V-Sekai contributors.
// SPDX-License-Identifier: Apache-2.0 OR MPL-2.0
//
// Headless EditMode smoke test for the IdtxCore Unity plugin. Mirrors the
// manual MCP verification that diagnosed the missing-companion bug: it
// confirms libidtx_core (and every runtime companion the OS loader must
// resolve beside it) actually loads, and that the OpenUSD plugin registry
// is wired so a stage can be created and written to both .usdc and .usdz.
//
// If any companion DLL or USD registry piece is absent, CoreVersion() throws
// DllNotFoundException("idtx_core") and NativeLibraryLoads fails first — the
// same failure mode that silently broke export from the editor.

using System.IO;
using NUnit.Framework;
using IdtxCore;

public class IdtxExportTests
{
    [Test]
    public void NativeLibraryLoads()
    {
        string version = IdtxAvatar.CoreVersion();
        Assert.IsFalse(string.IsNullOrEmpty(version),
            "libidtx_core failed to load — a runtime companion (usd_ms / tbb12 / " +
            "libidtx_usd) is likely missing beside idtx_core.");
    }

    [Test]
    public void ExportsUsdAndUsdz()
    {
        string dir = Path.Combine(Path.GetTempPath(), "idtx_ci_export");
        Directory.CreateDirectory(dir);
        string usdc = Path.Combine(dir, "ci.usdc");
        string usdz = Path.Combine(dir, "ci.usdz");

        using (var avatar = new IdtxAvatar())
        {
            avatar.Name = "CiSmokeTest";
            Assert.AreEqual(0, avatar.ExportToUsd(usdc),
                "OpenUSD crate (.usdc) export returned a nonzero code.");
            Assert.AreEqual(0, avatar.ExportToUsd(usdz),
                "OpenUSD package (.usdz) export returned a nonzero code.");
        }

        Assert.IsTrue(File.Exists(usdc), "Export reported success but no .usdc was written.");
        Assert.IsTrue(File.Exists(usdz), "Export reported success but no .usdz was written.");
        Assert.Greater(new FileInfo(usdz).Length, 0, "The .usdz package is empty.");
    }
}
