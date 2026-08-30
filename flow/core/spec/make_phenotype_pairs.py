"""
Build per-phenotype USD/GLB pairs for the correspondence spec.

Copyright 2026 V-Sekai contributors.
SPDX-License-Identifier: Apache-2.0 OR MPL-2.0

One case per phenotype axis at 1.0 plus the default body — no combinations.
Three stages because three environments own the libraries:

    anny env:  python make_phenotype_pairs.py verts <out_dir>
    usd env:   python make_phenotype_pairs.py usd   <out_dir>
    any+trimesh: python make_phenotype_pairs.py glb <out_dir>

The GLB is written in glTF's Y-up frame (x, z, -y of the Z-up ANNY verts), so a
correct USD import and the GLB agree coordinate-for-coordinate.
"""
import pathlib
import sys

import numpy as np


def stage_verts(out):
    import torch
    CORPUS = pathlib.Path(r"C:\weftspun-keypoints\6-datasource\anny-render-corpus")
    sys.path.insert(0, str(CORPUS))
    import anny_rig
    model = anny_rig.build_corpus_model(dtype=torch.float64)
    ident = anny_rig._identity_pose(model)
    faces = np.asarray(model.faces)
    cases = {"default": {}}
    for label in model.phenotype_labels:
        cases["ph_" + label] = {label: 1.0}
    for name, kw in cases.items():
        with torch.no_grad():
            v = model(pose_parameters=ident, phenotype_kwargs=kw)["vertices"][0].numpy()
        np.savez(out / f"{name}.npz", verts=v, faces=faces)
        print(name, v.shape[0], "verts")


def stage_usd(out):
    from pxr import Usd, UsdGeom, Vt
    for npz in sorted(out.glob("*.npz")):
        d = np.load(npz)
        stage = Usd.Stage.CreateNew(str(npz.with_suffix(".usda")))
        UsdGeom.SetStageUpAxis(stage, UsdGeom.Tokens.z)
        UsdGeom.SetStageMetersPerUnit(stage, 1.0)
        mesh = UsdGeom.Mesh.Define(stage, "/Body")
        stage.SetDefaultPrim(mesh.GetPrim())
        mesh.CreatePointsAttr(Vt.Vec3fArray.FromNumpy(d["verts"].astype(np.float32)))
        mesh.CreateFaceVertexIndicesAttr(
            Vt.IntArray.FromNumpy(d["faces"].reshape(-1).astype(np.int32)))
        mesh.CreateFaceVertexCountsAttr(
            Vt.IntArray.FromNumpy(np.full(len(d["faces"]), 3, np.int32)))
        mesh.CreateSubdivisionSchemeAttr(UsdGeom.Tokens.none)
        stage.Save()
        print(npz.stem, "usda")


def stage_glb(out):
    import trimesh
    for npz in sorted(out.glob("*.npz")):
        d = np.load(npz)
        v = d["verts"].astype(np.float32)
        yup = np.stack([v[:, 0], v[:, 2], -v[:, 1]], axis=1)
        trimesh.Trimesh(vertices=yup, faces=d["faces"], process=False).export(
            str(npz.with_suffix(".glb")))
        print(npz.stem, "glb")


if __name__ == "__main__":
    stage, out = sys.argv[1], pathlib.Path(sys.argv[2])
    out.mkdir(parents=True, exist_ok=True)
    {"verts": stage_verts, "usd": stage_usd, "glb": stage_glb}[stage](out)
