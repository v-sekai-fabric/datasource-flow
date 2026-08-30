"""
Build canonical-ANNY fixtures across phenotypes and poses for the import spec.

Copyright 2026 V-Sekai contributors.
SPDX-License-Identifier: Apache-2.0 OR MPL-2.0

Runs in anny-render-corpus's `anny` pixi env (torch + anny, no pxr): one npz +
names.json per (phenotype, pose) variant, in mesh_to_usda.py's schema. The
canonical model comes from anny_rig.build_corpus_model -- "every stage builds
from here" -- so the fixtures inherit its twist fix.

    .pixi/envs/anny/python.exe make_anny_variants.py <out_dir>
"""
import json
import pathlib
import sys

import numpy as np
import torch

CORPUS = pathlib.Path(r"C:\weftspun-keypoints\6-datasource\anny-render-corpus")
sys.path.insert(0, str(CORPUS))
import anny_rig  # noqa: E402

PHENOTYPES = {
    "default": {},
    "heavy": {"weight": 1.0},
    "tall_muscular": {"height": 1.0, "muscle": 1.0},
}


def find_bone(model, *needles):
    labels = list(model.bone_labels)
    for needle in needles:
        for i, name in enumerate(labels):
            if needle in name:
                return i, name
    raise SystemExit(f"no bone matching {needles} in {labels[:8]}...")


def poses_for(model):
    ident = anny_rig._identity_pose(model)

    elbow = ident.clone()
    i, name = find_bone(model, "lowerarm01.L", "lowerarm", "forearm")
    elbow[0, i, :3, :3] = torch.tensor(anny_rig.rotation([1.0, 0.0, 0.0], 90.0),
                                       dtype=elbow.dtype)

    twist = anny_rig.disperse_wrist_roll(model, ident.clone(), "L", 60.0)
    return {"identity": ident, "elbow90": elbow, "twist60": twist}


def main(out_dir):
    out = pathlib.Path(out_dir)
    out.mkdir(parents=True, exist_ok=True)
    model = anny_rig.build_corpus_model(dtype=torch.float64)
    names = json.dumps(list(model.bone_labels))
    for pheno, kw in PHENOTYPES.items():
        for pose_name, pose in poses_for(model).items():
            with torch.no_grad():
                r = model(pose_parameters=pose, phenotype_kwargs=kw)
            tag = f"{pheno}__{pose_name}"
            np.savez(out / f"{tag}.npz",
                     verts=r["vertices"][0].numpy(),
                     faces=np.asarray(model.faces),
                     bone_poses=r["bone_poses"][0].numpy(),
                     parents=np.asarray(model.bone_parents))
            (out / f"{tag}.names.json").write_text(names, encoding="utf-8")
            print(f"{tag}: {r['vertices'].shape[1]} verts")
    print("done:", out)


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "anny_variants")
