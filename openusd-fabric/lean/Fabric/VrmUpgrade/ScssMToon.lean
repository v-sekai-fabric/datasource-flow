-- Copyright 2026 The openusd-fabric authors.
-- SPDX-License-Identifier: MIT
--
-- Bidirectional shader-parameter mapping between Silent's Cel Shading
-- Shader (SCSS, https://gitlab.com/s-ilent/SCSS) and VRM 1.0 MToon.
--
-- This is the first concrete instance of the CHI-252 VRM upgrade tables
-- pattern: a Lean spec captures the parameter correspondences as total
-- functions, native_decide pins the emitted JSON bytes, and the Python
-- (Blender hook, reverse path) and C# (V-Sekai usd-converter-for-vrchat
-- fork) consumers read the same table so they cannot drift.
--
-- Source data:
--   * SCSS: Runtime/Shaders/Flat Lit Toon.shader Properties block.
--   * MToon: VRM 1.0 / vrm-addon-for-blender Mtoon1MaterialPropertyGroup.
--
-- Scope: the parameters that have a clean cross-spec analogue (lit /
-- shade colours, parametric rim, matcap, outline). Lossy / SCSS-only
-- fields (light-ramp LUT, multi-matcap blend, hatching, audiolink,
-- SDF face, detail maps, anisotropic spec) are not in the canonical
-- table — they are stored as SCSS-extension data on the V-Sekai schema
-- side when the bridge needs to round-trip them (planned: a
-- VSekaiMToonScssExtensionAPI sub-schema if demand justifies it).

namespace Fabric.VrmUpgrade.ScssMToon

/-- Subset of the SCSS Flat Lit Toon properties that maps cleanly to
    VRM 1.0 MToon. Values use the same conventions SCSS itself uses
    (Color = RGBA in 0..1 unless [HDR], Range = floats with shader
    annotations). -/
structure ScssParams where
  -- Lit
  colorR : Float := 1.0
  colorG : Float := 1.0
  colorB : Float := 1.0
  colorA : Float := 1.0
  -- Shade (the SCSS shadow-mask path approximates MToon shade colour)
  shadowMaskColorR : Float := 1.0
  shadowMaskColorG : Float := 1.0
  shadowMaskColorB : Float := 1.0
  shadow : Float := 0.5     -- _Shadow, Range 0..1
  shadowLift : Float := 0.0 -- _ShadowLift, Range -1..1
  -- Rim (SCSS Fresnel)
  fresnelTintR : Float := 1.0
  fresnelTintG : Float := 1.0
  fresnelTintB : Float := 1.0
  fresnelWidth : Float := 0.5    -- _FresnelWidth, Range 0..20
  fresnelStrength : Float := 0.5 -- _FresnelStrength, Range 0.01..0.9999
  -- Matcap (slot 1 only -- SCSS supports up to 4 slots; V-Sekai uses #1
  -- as the canonical MToon matcap)
  matcap1TintR : Float := 1.0
  matcap1TintG : Float := 1.0
  matcap1TintB : Float := 1.0
  matcap1Strength : Float := 1.0 -- _Matcap1Strength, Range 0..2
  -- Outline
  outlineMode : Float := 1.0     -- 0=None, 1=Tinted (screen), 2=World
  outlineWidth : Float := 0.1    -- _outline_width
  outlineColorR : Float := 0.5
  outlineColorG : Float := 0.5
  outlineColorB : Float := 0.5
  -- Transparency
  alphaSharp : Float := 0.0      -- 0=Dithered, 1=Sharp/cutout, 2=ZWrite
  deriving Repr, BEq

/-- Subset of VRM 1.0 MToon factors corresponding to ScssParams. Names
    and units follow the spec (see schema/v_sekai_schema.usda). -/
structure MToonParams where
  -- baseColor lives on pbrMetallicRoughness in glTF, but the SCSS→MToon
  -- bridge is the consumer of `_Color`, so we carry it here too.
  baseColorR : Float := 1.0
  baseColorG : Float := 1.0
  baseColorB : Float := 1.0
  baseColorA : Float := 1.0
  -- MToon shade
  shadeColorR : Float := 1.0
  shadeColorG : Float := 1.0
  shadeColorB : Float := 1.0
  shadingShift : Float := 0.0    -- shadingShiftFactor, [-1, 1]
  shadingToony : Float := 0.9    -- shadingToonyFactor, [0, 1]
  -- Parametric rim
  parametricRimR : Float := 0.0
  parametricRimG : Float := 0.0
  parametricRimB : Float := 0.0
  parametricRimFresnelPower : Float := 1.0
  parametricRimLift : Float := 0.0
  rimLightingMix : Float := 0.0
  -- Matcap
  matcapR : Float := 1.0
  matcapG : Float := 1.0
  matcapB : Float := 1.0
  -- Outline
  outlineWidthMode : String := "none" -- "none" / "worldCoordinates" / "screenCoordinates"
  outlineWidth : Float := 0.0
  outlineColorR : Float := 0.0
  outlineColorG : Float := 0.0
  outlineColorB : Float := 0.0
  -- Transparency
  transparentWithZWrite : Bool := false
  deriving Repr, BEq

/-- SCSS `_Shadow` (0..1, where 1 = darker) gates the shade factor
    multiplier. MToon's `shadeColorFactor` is the absolute shade colour;
    we mix toward black as SCSS shadow approaches 1. -/
def applyScssShadowToColour (c : Float) (shadow : Float) : Float :=
  c * (1.0 - shadow * 0.5)

/-- Inverse of applyScssShadowToColour with shadow held at the reverse
    pin (0.5). Clamped to [0, 1] so an MToon shade > 0.75 still produces
    a representable SCSS shadowMaskColor (the surplus would be encoded
    by lowering SCSS `_Shadow` below 0.5, but the reverse pins it). -/
def invertScssShadowToColour (m : Float) : Float :=
  let v := m / 0.75
  if v > 1.0 then 1.0 else v

/-- SCSS `_FresnelWidth` (Range 0..20) is an unbounded SCSS-specific
    "rim spread" knob; MToon's `parametricRimFresnelPowerFactor` is the
    Fresnel exponent. Map by inverse-square heuristic clamped sensibly:
    width 0 -> power 8 (sharp), width 20 -> power 0.25 (broad). -/
def fresnelWidthToPower (w : Float) : Float :=
  if w <= 0.0 then 8.0
  else if w >= 20.0 then 0.25
  else 8.0 / (1.0 + w)

def fresnelPowerToWidth (p : Float) : Float :=
  if p <= 0.25 then 20.0
  else if p >= 8.0 then 0.0
  else (8.0 / p) - 1.0

/-- SCSS `_OutlineMode` is an enum, 0/1/2; MToon outlineWidthMode is a
    string token. -/
def scssOutlineModeToMToon (m : Float) : String :=
  if m < 0.5 then "none"
  else if m < 1.5 then "screenCoordinates"
  else "worldCoordinates"

def mToonOutlineModeToScss (mode : String) : Float :=
  match mode with
  | "none" => 0.0
  | "screenCoordinates" => 1.0
  | "worldCoordinates" => 2.0
  | _ => 1.0

/-- SCSS `_AlphaSharp`: 0 = dithered, 1 = sharp cutout, 2 = z-write
    transparency. MToon has a boolean `transparentWithZWrite`. The
    z-write transparency mode is the only direct MToon analogue. -/
def scssAlphaSharpToZWrite (a : Float) : Bool := a >= 1.5

def zWriteToScssAlphaSharp (z : Bool) : Float := if z then 2.0 else 0.0

/-- Forward map: SCSS -> MToon. Total over ScssParams. -/
def scssToMToon (s : ScssParams) : MToonParams :=
  { baseColorR := s.colorR
  , baseColorG := s.colorG
  , baseColorB := s.colorB
  , baseColorA := s.colorA
  , shadeColorR := applyScssShadowToColour s.shadowMaskColorR s.shadow
  , shadeColorG := applyScssShadowToColour s.shadowMaskColorG s.shadow
  , shadeColorB := applyScssShadowToColour s.shadowMaskColorB s.shadow
  , shadingShift := s.shadowLift
  , shadingToony := 0.9 -- SCSS has no direct knob; use MToon default
  , parametricRimR := s.fresnelTintR
  , parametricRimG := s.fresnelTintG
  , parametricRimB := s.fresnelTintB
  , parametricRimFresnelPower := fresnelWidthToPower s.fresnelWidth
  , parametricRimLift := s.fresnelStrength
  , rimLightingMix := 0.0 -- SCSS rim is additive; mix stays 0 for fidelity
  , matcapR := s.matcap1TintR * s.matcap1Strength
  , matcapG := s.matcap1TintG * s.matcap1Strength
  , matcapB := s.matcap1TintB * s.matcap1Strength
  , outlineWidthMode := scssOutlineModeToMToon s.outlineMode
  , outlineWidth := s.outlineWidth
  , outlineColorR := s.outlineColorR
  , outlineColorG := s.outlineColorG
  , outlineColorB := s.outlineColorB
  , transparentWithZWrite := scssAlphaSharpToZWrite s.alphaSharp
  }

/-- Reverse map: MToon -> SCSS. Total over MToonParams. Some MToon-only
    fields (shadingToony, rimLightingMix) are dropped; documented as
    lossy in the issue. -/
def mToonToScss (m : MToonParams) : ScssParams :=
  -- Inverse of applyScssShadowToColour with shadow held at 0 — i.e.
  -- when MToon shade differs from base, encode it as shadowMaskColor and
  -- leave SCSS _Shadow at the default 0.5.
  { colorR := m.baseColorR
  , colorG := m.baseColorG
  , colorB := m.baseColorB
  , colorA := m.baseColorA
  , shadowMaskColorR := invertScssShadowToColour m.shadeColorR
  , shadowMaskColorG := invertScssShadowToColour m.shadeColorG
  , shadowMaskColorB := invertScssShadowToColour m.shadeColorB
  , shadow := 0.5
  , shadowLift := m.shadingShift
  , fresnelTintR := m.parametricRimR
  , fresnelTintG := m.parametricRimG
  , fresnelTintB := m.parametricRimB
  , fresnelWidth := fresnelPowerToWidth m.parametricRimFresnelPower
  , fresnelStrength := m.parametricRimLift
  , matcap1TintR := m.matcapR
  , matcap1TintG := m.matcapG
  , matcap1TintB := m.matcapB
  , matcap1Strength := 1.0
  , outlineMode := mToonOutlineModeToScss m.outlineWidthMode
  , outlineWidth := m.outlineWidth
  , outlineColorR := m.outlineColorR
  , outlineColorG := m.outlineColorG
  , outlineColorB := m.outlineColorB
  , alphaSharp := zWriteToScssAlphaSharp m.transparentWithZWrite
  }

/-- **Round-trip is NOT general over MToonParams.** The reverse
    pin `invertScssShadowToColour` clamps SCSS `_Shadow` to 0.5,
    discarding information whenever the source SCSS shadow != 0.5.
    `mToonToScss` also drops `shadingToony` and `rimLightingMix`
    (no SCSS analogue — both default-restored on the way back).

    The recoverable subset is the set of MToonParams whose pre-image
    under `scssToMToon` exists with the reverse-pin constants. The
    example below is one such input — useful as a regression test
    but does not generalise to arbitrary MToon values.

    See `lossyMToonOnly` for the full list of MToon-only fields. -/
def isOnRoundTripSubset (m : MToonParams) : Bool :=
  -- 1. shadingToony pinned to the reverse-trip default 0.9.
  m.shadingToony == 0.9 &&
  -- 2. rimLightingMix pinned to the reverse-trip default 0.0.
  m.rimLightingMix == 0.0 &&
  -- 3. Shade colour within the representable shadow=0.5 band: each
  --    channel <= 0.75 so invertScssShadowToColour does not clamp.
  m.shadeColorR <= 0.75 && m.shadeColorG <= 0.75 && m.shadeColorB <= 0.75

/-- Round-trip identity on the recoverable subset. -/
example :
    let m : MToonParams := {
      baseColorR := 0.4, baseColorG := 0.5, baseColorB := 0.6, baseColorA := 1.0
      shadeColorR := 0.2, shadeColorG := 0.3, shadeColorB := 0.4
      shadingShift := -0.2
      shadingToony := 0.9
      parametricRimR := 0.1, parametricRimG := 0.1, parametricRimB := 0.2
      parametricRimFresnelPower := 2.0
      parametricRimLift := 0.3
      rimLightingMix := 0.0
      matcapR := 0.5, matcapG := 0.5, matcapB := 0.5
      outlineWidthMode := "screenCoordinates"
      outlineWidth := 0.002
      outlineColorR := 0.1, outlineColorG := 0.1, outlineColorB := 0.1
      transparentWithZWrite := false }
    -- The input IS on the recoverable subset.
    isOnRoundTripSubset m = true ∧
    -- And on that subset the round-trip recovers it exactly.
    scssToMToon (mToonToScss m) == m := by native_decide

/-- Counter-example: an MToon with `shadingToony` != 0.9 falls
    outside the recoverable subset (and the round-trip drops the
    custom toony value). Documents the limit explicitly. -/
example :
    let m : MToonParams := { shadingToony := 0.5 }
    isOnRoundTripSubset m = false := by native_decide

/-- One entry of the SCSS<->MToon mapping table. Used by `bridgeRows`
    (the single source of truth) and `genRowJson` (emission). -/
structure BridgeRow where
  scss        : String
  mtoon       : String
  xform       : String
  lossless    : Bool
  lossyReason : Option String := none

/-- All 11 SCSS<->MToon parameter correspondences. The Python + C#
    bridges read this list (post-JSON-emission) and apply the named
    `xform` by id. Adding a new pair here propagates to both
    consumers without touching either implementation. -/
def bridgeRows : List BridgeRow :=
  [ { scss := "_Color",            mtoon := "baseColorFactor"
    , xform := "color4_identity",   lossless := true }
  , { scss := "_ShadowMaskColor",  mtoon := "shadeColorFactor"
    , xform := "color3_via_shadow", lossless := false
    , lossyReason := some "SCSS _Shadow gates the shade colour multiplicatively; MToon shadeColorFactor is absolute. Round-trip pins _Shadow=0.5." }
  , { scss := "_ShadowLift",       mtoon := "shadingShiftFactor"
    , xform := "float_identity",   lossless := true }
  , { scss := "_FresnelTint",      mtoon := "parametricRimColorFactor"
    , xform := "color3_identity",  lossless := true }
  , { scss := "_FresnelWidth",     mtoon := "parametricRimFresnelPowerFactor"
    , xform := "fresnel_width_to_power", lossless := false
    , lossyReason := some "SCSS Range 0..20 mapped via 8/(1+w); MToon power range typically 0..8. Outside the bracket clamps." }
  , { scss := "_FresnelStrength",  mtoon := "parametricRimLiftFactor"
    , xform := "float_identity",   lossless := true }
  , { scss := "_Matcap1Tint",      mtoon := "matcapFactor"
    , xform := "color3_times_strength", lossless := false
    , lossyReason := some "SCSS supports 4 matcap slots; only slot 1 round-trips. _Matcap1Strength is folded into matcapFactor." }
  , { scss := "_OutlineMode",      mtoon := "outlineWidthMode"
    , xform := "outline_mode_enum", lossless := true }
  , { scss := "_outline_width",    mtoon := "outlineWidthFactor"
    , xform := "float_identity",   lossless := true }
  , { scss := "_outline_color",    mtoon := "outlineColorFactor"
    , xform := "color3_identity",  lossless := true }
  , { scss := "_AlphaSharp",       mtoon := "transparentWithZWrite"
    , xform := "alpha_sharp_to_zwrite", lossless := false
    , lossyReason := some "SCSS has 3 transparency modes (dithered / sharp cutout / z-write); MToon transparentWithZWrite is a single bool. Only z-write mode round-trips precisely." }
  ]

/-- MToon-only fields that have no SCSS counterpart. -/
def lossyMToonOnly : List String :=
  [ "shadingToonyFactor (SCSS uses a light ramp LUT instead of a single toony scalar)"
  , "rimLightingMixFactor (SCSS rim is always additive)"
  , "shadingShiftTexture / shadingShiftTextureScale (no SCSS analogue)"
  , "outlineLightingMixFactor (SCSS outline always uses outline_color)"
  , "renderQueueOffsetNumber (Unity material renderQueue handles this on the SCSS side)"
  ]

/-- SCSS-only fields that have no MToon counterpart. -/
def lossyScssOnly : List String :=
  [ "_Ramp (light ramp LUT — represent as MToon shadingShiftTexture if possible, or drop)"
  , "_Matcap2/3/4* (SCSS multi-matcap slots beyond #1)"
  , "_HatchingTex and family (SCSS hatching has no MToon equivalent)"
  , "_UseAlphaFresnel / _AlphaFresnel* (SCSS alpha-fresnel)"
  , "_UseEmissiveAudiolink / _AudiolinkIntensity (SCSS Audiolink integration)"
  , "_SDFMode + _SDF* (SCSS SDF-face anime eyes)"
  , "_DetailMap1..4 and family (SCSS detail maps)"
  ]

/-- JSON emission for a single bridge row. Single-line entry. -/
def genRowJson (r : BridgeRow) : String :=
  let core :=
    "    { \"scss\": \"" ++ r.scss ++ "\"" ++
    ", \"mtoon\": \"" ++ r.mtoon ++ "\"" ++
    ", \"xform\": \"" ++ r.xform ++ "\"" ++
    ", \"lossless\": " ++ (if r.lossless then "true" else "false")
  match r.lossyReason with
  | none => core ++ " }"
  | some reason => core ++ ", \"lossy_reason\": \"" ++ reason ++ "\" }"

/-- JSON emission for a string list (lossy_*_only sections). -/
def genStringList (items : List String) : String :=
  String.intercalate ",\n" <|
    items.map fun s => "    \"" ++ s ++ "\""

/-- Generator: derive the full JSON document from `bridgeRows` +
    `lossyMToonOnly` + `lossyScssOnly`. -/
def genJson : String :=
  let header := "{
  \"version\": 1,
  \"comment\": \"AUTO-GENERATED by lean/EmitArtifacts.lean from Fabric.VrmUpgrade.ScssMToon. DO NOT EDIT.\",
  \"entries\": [
"
  let entries := String.intercalate ",\n" (bridgeRows.map genRowJson)
  let mid1 := "
  ],
  \"lossy_mtoon_only\": [
"
  let mtoonList := genStringList lossyMToonOnly
  let mid2 := "
  ],
  \"lossy_scss_only\": [
"
  let scssList := genStringList lossyScssOnly
  let footer := "
  ]
}
"
  header ++ entries ++ mid1 ++ mtoonList ++ mid2 ++ scssList ++ footer

/-- Canonical JSON serialisation of the mapping table the host bridges
    consume. Each entry is one parameter correspondence: the SCSS name,
    the MToon name, the value-transform identifier, and whether the
    transform is lossless or lossy.

    Defined as `genJson` so the spec drives the wire format. The
    CHI-254 byte-pin lives in `committedJsonBytes_matches_genJson`. -/
def jsonSource : String := genJson

/-- Bit-identical copy of `maps/scss_mtoon_map.json` as it sits on
    disk. Preserved by the byte-pin proof below. -/
def committedJsonBytes : String :=
"{
  \"version\": 1,
  \"comment\": \"AUTO-GENERATED by lean/EmitArtifacts.lean from Fabric.VrmUpgrade.ScssMToon. DO NOT EDIT.\",
  \"entries\": [
    { \"scss\": \"_Color\", \"mtoon\": \"baseColorFactor\", \"xform\": \"color4_identity\", \"lossless\": true },
    { \"scss\": \"_ShadowMaskColor\", \"mtoon\": \"shadeColorFactor\", \"xform\": \"color3_via_shadow\", \"lossless\": false, \"lossy_reason\": \"SCSS _Shadow gates the shade colour multiplicatively; MToon shadeColorFactor is absolute. Round-trip pins _Shadow=0.5.\" },
    { \"scss\": \"_ShadowLift\", \"mtoon\": \"shadingShiftFactor\", \"xform\": \"float_identity\", \"lossless\": true },
    { \"scss\": \"_FresnelTint\", \"mtoon\": \"parametricRimColorFactor\", \"xform\": \"color3_identity\", \"lossless\": true },
    { \"scss\": \"_FresnelWidth\", \"mtoon\": \"parametricRimFresnelPowerFactor\", \"xform\": \"fresnel_width_to_power\", \"lossless\": false, \"lossy_reason\": \"SCSS Range 0..20 mapped via 8/(1+w); MToon power range typically 0..8. Outside the bracket clamps.\" },
    { \"scss\": \"_FresnelStrength\", \"mtoon\": \"parametricRimLiftFactor\", \"xform\": \"float_identity\", \"lossless\": true },
    { \"scss\": \"_Matcap1Tint\", \"mtoon\": \"matcapFactor\", \"xform\": \"color3_times_strength\", \"lossless\": false, \"lossy_reason\": \"SCSS supports 4 matcap slots; only slot 1 round-trips. _Matcap1Strength is folded into matcapFactor.\" },
    { \"scss\": \"_OutlineMode\", \"mtoon\": \"outlineWidthMode\", \"xform\": \"outline_mode_enum\", \"lossless\": true },
    { \"scss\": \"_outline_width\", \"mtoon\": \"outlineWidthFactor\", \"xform\": \"float_identity\", \"lossless\": true },
    { \"scss\": \"_outline_color\", \"mtoon\": \"outlineColorFactor\", \"xform\": \"color3_identity\", \"lossless\": true },
    { \"scss\": \"_AlphaSharp\", \"mtoon\": \"transparentWithZWrite\", \"xform\": \"alpha_sharp_to_zwrite\", \"lossless\": false, \"lossy_reason\": \"SCSS has 3 transparency modes (dithered / sharp cutout / z-write); MToon transparentWithZWrite is a single bool. Only z-write mode round-trips precisely.\" }
  ],
  \"lossy_mtoon_only\": [
    \"shadingToonyFactor (SCSS uses a light ramp LUT instead of a single toony scalar)\",
    \"rimLightingMixFactor (SCSS rim is always additive)\",
    \"shadingShiftTexture / shadingShiftTextureScale (no SCSS analogue)\",
    \"outlineLightingMixFactor (SCSS outline always uses outline_color)\",
    \"renderQueueOffsetNumber (Unity material renderQueue handles this on the SCSS side)\"
  ],
  \"lossy_scss_only\": [
    \"_Ramp (light ramp LUT — represent as MToon shadingShiftTexture if possible, or drop)\",
    \"_Matcap2/3/4* (SCSS multi-matcap slots beyond #1)\",
    \"_HatchingTex and family (SCSS hatching has no MToon equivalent)\",
    \"_UseAlphaFresnel / _AlphaFresnel* (SCSS alpha-fresnel)\",
    \"_UseEmissiveAudiolink / _AudiolinkIntensity (SCSS Audiolink integration)\",
    \"_SDFMode + _SDF* (SCSS SDF-face anime eyes)\",
    \"_DetailMap1..4 and family (SCSS detail maps)\"
  ]
}
"

/-- CHI-254 byte-pin (severity-1 gap closed for ScssMToon). The
    literal `committedJsonBytes` and the spec-derived `genJson`
    must coincide bit-for-bit; any drift fails the Lake build. -/
theorem committedJsonBytes_matches_genJson :
    committedJsonBytes = genJson := by native_decide

end Fabric.VrmUpgrade.ScssMToon
