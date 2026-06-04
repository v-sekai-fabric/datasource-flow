-- Copyright 2026 The openusd-fabric authors.
-- SPDX-License-Identifier: MIT
--
-- Godot .scn binary resource serializer (ART-30 / ART-34).
--
-- Emits a Slang module (`godot_scn.slang`) targeting `slangc -target cpp`.
-- The emitted code is linked into libidtx_core for Godot-independent
-- .scn writing in the zone-baker.
--
-- Pipeline:
--   Lean spec (this file) → LeanSlang AST → Slang source string
--   → EmitArtifacts writes `shaders/godot_scn.slang`
--   → slangc -target cpp → godot_scn.cpp (linked into libidtx_core)

import LeanSlang.Types
import LeanSlang.AST
import LeanSlang.Emit

namespace Fabric.Serialization.GodotScn

open LeanSlang

-- ═══════════════════════════════════════════════════════════════════════════
-- STRUCTS
-- ═══════════════════════════════════════════════════════════════════════════

def headerStruct : SlangStructDecl := {
  name := "GodotResourceHeader"
  fields := [
    { name := "big_endian",     type := .scalar .uint },
    { name := "use_64bit",      type := .scalar .uint },
    { name := "ver_major",      type := .scalar .uint },
    { name := "ver_minor",      type := .scalar .uint },
    { name := "format_version", type := .scalar .uint },
    { name := "flags",          type := .scalar .uint },
    { name := "uid_lo",         type := .scalar .uint },
    { name := "uid_hi",         type := .scalar .uint }
  ]
}

def compressedHeaderStruct : SlangStructDecl := {
  name := "GodotCompressedHeader"
  fields := [
    { name := "compression_mode",        type := .scalar .uint },
    { name := "block_size",              type := .scalar .uint },
    { name := "total_uncompressed_size", type := .scalar .uint }
  ]
}

def lodStruct : SlangStructDecl := {
  name := "GodotMeshLOD"
  fields := [
    { name := "distance",     type := .scalar .float },
    { name := "index_offset", type := .scalar .uint },
    { name := "index_count",  type := .scalar .uint }
  ]
}

-- ═══════════════════════════════════════════════════════════════════════════
-- SERIALIZATION FUNCTIONS
-- ═══════════════════════════════════════════════════════════════════════════

def storeU32Fn : SlangFunctionDecl := {
  name   := "godot_store_u32"
  params := [
    { name := "buf",    type := .rwBuf (.scalar .uint) },
    { name := "offset", type := .scalar .uint, qualifier := .qInOut },
    { name := "value",  type := .scalar .uint }
  ]
  body := [
    .assign (.index (.var "buf") (.bin "/" (.var "offset") (.litUint 4))) (.var "value"),
    .assign (.var "offset") (.bin "+" (.var "offset") (.litUint 4))
  ]
}

def storeU64Fn : SlangFunctionDecl := {
  name   := "godot_store_u64"
  params := [
    { name := "buf",      type := .rwBuf (.scalar .uint) },
    { name := "offset",   type := .scalar .uint, qualifier := .qInOut },
    { name := "value_lo", type := .scalar .uint },
    { name := "value_hi", type := .scalar .uint }
  ]
  body := [
    .assign (.index (.var "buf") (.bin "/" (.var "offset") (.litUint 4))) (.var "value_lo"),
    .assign (.var "offset") (.bin "+" (.var "offset") (.litUint 4)),
    .assign (.index (.var "buf") (.bin "/" (.var "offset") (.litUint 4))) (.var "value_hi"),
    .assign (.var "offset") (.bin "+" (.var "offset") (.litUint 4))
  ]
}

def storeFloatFn : SlangFunctionDecl := {
  name   := "godot_store_float"
  params := [
    { name := "buf",    type := .rwBuf (.scalar .uint) },
    { name := "offset", type := .scalar .uint, qualifier := .qInOut },
    { name := "value",  type := .scalar .float }
  ]
  body := [
    .assign (.index (.var "buf") (.bin "/" (.var "offset") (.litUint 4))) (.call "asuint" [.var "value"]),
    .assign (.var "offset") (.bin "+" (.var "offset") (.litUint 4))
  ]
}

def writeVariantTagFn : SlangFunctionDecl := {
  name   := "godot_write_variant_tag"
  params := [
    { name := "buf",    type := .rwBuf (.scalar .uint) },
    { name := "offset", type := .scalar .uint, qualifier := .qInOut },
    { name := "tag",    type := .scalar .uint }
  ]
  body := [
    .assign (.index (.var "buf") (.bin "/" (.var "offset") (.litUint 4))) (.var "tag"),
    .assign (.var "offset") (.bin "+" (.var "offset") (.litUint 4))
  ]
}

def writeVector3Fn : SlangFunctionDecl := {
  name   := "godot_write_vector3"
  params := [
    { name := "buf",    type := .rwBuf (.scalar .uint) },
    { name := "offset", type := .scalar .uint, qualifier := .qInOut },
    { name := "v",      type := .vec .float 3 }
  ]
  body := [
    -- tag = 12 (VARIANT_VECTOR3)
    .assign (.index (.var "buf") (.bin "/" (.var "offset") (.litUint 4))) (.litUint 12),
    .assign (.var "offset") (.bin "+" (.var "offset") (.litUint 4)),
    -- x
    .assign (.index (.var "buf") (.bin "/" (.var "offset") (.litUint 4))) (.call "asuint" [.member (.var "v") "x"]),
    .assign (.var "offset") (.bin "+" (.var "offset") (.litUint 4)),
    -- y
    .assign (.index (.var "buf") (.bin "/" (.var "offset") (.litUint 4))) (.call "asuint" [.member (.var "v") "y"]),
    .assign (.var "offset") (.bin "+" (.var "offset") (.litUint 4)),
    -- z
    .assign (.index (.var "buf") (.bin "/" (.var "offset") (.litUint 4))) (.call "asuint" [.member (.var "v") "z"]),
    .assign (.var "offset") (.bin "+" (.var "offset") (.litUint 4))
  ]
}

def writeTransform3DFn : SlangFunctionDecl := {
  name   := "godot_write_transform3d"
  params := [
    { name := "buf",    type := .rwBuf (.scalar .uint) },
    { name := "offset", type := .scalar .uint, qualifier := .qInOut },
    { name := "basis",  type := .vec .float 3 },  -- placeholder: caller passes rows
    { name := "origin", type := .vec .float 3 }
  ]
  body := [
    -- tag = 17 (VARIANT_TRANSFORM3D)
    .assign (.index (.var "buf") (.bin "/" (.var "offset") (.litUint 4))) (.litUint 17),
    .assign (.var "offset") (.bin "+" (.var "offset") (.litUint 4)),
    -- origin x/y/z (basis rows emitted by caller in a loop)
    .assign (.index (.var "buf") (.bin "/" (.var "offset") (.litUint 4))) (.call "asuint" [.member (.var "origin") "x"]),
    .assign (.var "offset") (.bin "+" (.var "offset") (.litUint 4)),
    .assign (.index (.var "buf") (.bin "/" (.var "offset") (.litUint 4))) (.call "asuint" [.member (.var "origin") "y"]),
    .assign (.var "offset") (.bin "+" (.var "offset") (.litUint 4)),
    .assign (.index (.var "buf") (.bin "/" (.var "offset") (.litUint 4))) (.call "asuint" [.member (.var "origin") "z"]),
    .assign (.var "offset") (.bin "+" (.var "offset") (.litUint 4))
  ]
}

def writeHeaderFn : SlangFunctionDecl := {
  name   := "godot_write_header"
  params := [
    { name := "buf",    type := .rwBuf (.scalar .uint) },
    { name := "offset", type := .scalar .uint, qualifier := .qInOut },
    { name := "hdr",    type := .named "GodotResourceHeader" }
  ]
  body := [
    -- Magic "RSRC" = 0x43525352 (LE)
    .assign (.index (.var "buf") (.bin "/" (.var "offset") (.litUint 4))) (.litUint 0x43525352),
    .assign (.var "offset") (.bin "+" (.var "offset") (.litUint 4)),
    -- big_endian
    .assign (.index (.var "buf") (.bin "/" (.var "offset") (.litUint 4))) (.member (.var "hdr") "big_endian"),
    .assign (.var "offset") (.bin "+" (.var "offset") (.litUint 4)),
    -- use_64bit
    .assign (.index (.var "buf") (.bin "/" (.var "offset") (.litUint 4))) (.member (.var "hdr") "use_64bit"),
    .assign (.var "offset") (.bin "+" (.var "offset") (.litUint 4)),
    -- ver_major
    .assign (.index (.var "buf") (.bin "/" (.var "offset") (.litUint 4))) (.member (.var "hdr") "ver_major"),
    .assign (.var "offset") (.bin "+" (.var "offset") (.litUint 4)),
    -- ver_minor
    .assign (.index (.var "buf") (.bin "/" (.var "offset") (.litUint 4))) (.member (.var "hdr") "ver_minor"),
    .assign (.var "offset") (.bin "+" (.var "offset") (.litUint 4)),
    -- format_version
    .assign (.index (.var "buf") (.bin "/" (.var "offset") (.litUint 4))) (.member (.var "hdr") "format_version"),
    .assign (.var "offset") (.bin "+" (.var "offset") (.litUint 4))
  ]
}

-- ═══════════════════════════════════════════════════════════════════════════
-- STRING WRITER
-- ═══════════════════════════════════════════════════════════════════════════

/-- write_string: uint32 length + raw UTF-8 bytes (NO padding).
    Length includes null terminator. Caller passes packed uint32 words
    and the byte length (including null). No alignment padding.
    Source: save_unicode_string stores utf8.length()+1 bytes raw. -/
def writeStringFn : SlangFunctionDecl := {
  name   := "godot_write_string"
  params := [
    { name := "buf",      type := .rwBuf (.scalar .uint) },
    { name := "offset",   type := .scalar .uint, qualifier := .qInOut },
    { name := "str_data", type := .roBuf (.scalar .uint) },
    { name := "str_len",  type := .scalar .uint }
  ]
  body := [
    -- write length prefix (includes null terminator in count)
    .assign (.index (.var "buf") (.bin "/" (.var "offset") (.litUint 4))) (.var "str_len"),
    .assign (.var "offset") (.bin "+" (.var "offset") (.litUint 4)),
    -- copy packed uint32 words: ceil(str_len / 4) words
    .declInit (.scalar .uint) "word_count"
        (.bin "/" (.bin "+" (.var "str_len") (.litUint 3)) (.litUint 4)),
    .forCount "w" (.litUint 0) (.var "word_count") [
      .assign (.index (.var "buf") (.bin "+" (.bin "/" (.var "offset") (.litUint 4))
          (.var "w")))
        (.index (.var "str_data") (.var "w"))
    ],
    -- advance offset by exact byte count (NO padding — Godot does not pad strings)
    .assign (.var "offset") (.bin "+" (.var "offset") (.var "str_len"))
  ]
}

-- ═══════════════════════════════════════════════════════════════════════════
-- DICTIONARY / ARRAY / PACKED ARRAY WRITERS
-- ═══════════════════════════════════════════════════════════════════════════

/-- write_dict_header: tag(26) + uint32 count. Caller writes key/value pairs after. -/
def writeDictHeaderFn : SlangFunctionDecl := {
  name   := "godot_write_dict_header"
  params := [
    { name := "buf",    type := .rwBuf (.scalar .uint) },
    { name := "offset", type := .scalar .uint, qualifier := .qInOut },
    { name := "count",  type := .scalar .uint }
  ]
  body := [
    .assign (.index (.var "buf") (.bin "/" (.var "offset") (.litUint 4))) (.litUint 26),
    .assign (.var "offset") (.bin "+" (.var "offset") (.litUint 4)),
    .assign (.index (.var "buf") (.bin "/" (.var "offset") (.litUint 4))) (.var "count"),
    .assign (.var "offset") (.bin "+" (.var "offset") (.litUint 4))
  ]
}

/-- write_array_header: tag(30) + uint32 count. Caller writes elements after. -/
def writeArrayHeaderFn : SlangFunctionDecl := {
  name   := "godot_write_array_header"
  params := [
    { name := "buf",    type := .rwBuf (.scalar .uint) },
    { name := "offset", type := .scalar .uint, qualifier := .qInOut },
    { name := "count",  type := .scalar .uint }
  ]
  body := [
    .assign (.index (.var "buf") (.bin "/" (.var "offset") (.litUint 4))) (.litUint 30),
    .assign (.var "offset") (.bin "+" (.var "offset") (.litUint 4)),
    .assign (.index (.var "buf") (.bin "/" (.var "offset") (.litUint 4))) (.var "count"),
    .assign (.var "offset") (.bin "+" (.var "offset") (.litUint 4))
  ]
}

/-- write_packed_int32_array: tag(32) + count + count×int32.
    Data passed as a StructuredBuffer<uint>. -/
def writePackedInt32ArrayFn : SlangFunctionDecl := {
  name   := "godot_write_packed_int32_array"
  params := [
    { name := "buf",    type := .rwBuf (.scalar .uint) },
    { name := "offset", type := .scalar .uint, qualifier := .qInOut },
    { name := "data",   type := .roBuf (.scalar .uint) },
    { name := "count",  type := .scalar .uint }
  ]
  body := [
    .assign (.index (.var "buf") (.bin "/" (.var "offset") (.litUint 4))) (.litUint 32),
    .assign (.var "offset") (.bin "+" (.var "offset") (.litUint 4)),
    .assign (.index (.var "buf") (.bin "/" (.var "offset") (.litUint 4))) (.var "count"),
    .assign (.var "offset") (.bin "+" (.var "offset") (.litUint 4)),
    .forCount "i" (.litUint 0) (.var "count") [
      .assign (.index (.var "buf") (.bin "+" (.bin "/" (.var "offset") (.litUint 4))
          (.var "i")))
        (.index (.var "data") (.var "i"))
    ],
    .assign (.var "offset") (.bin "+" (.var "offset")
        (.bin "*" (.var "count") (.litUint 4)))
  ]
}

/-- write_packed_string_array: tag(34) + count, then count×string.
    Each string is stored via godot_write_string. This is a declaration only —
    the body calls godot_write_string in a loop (modeled as a tag+count header;
    actual per-string calls happen in the C++ host). -/
def writePackedStringArrayHeaderFn : SlangFunctionDecl := {
  name   := "godot_write_packed_string_array_header"
  params := [
    { name := "buf",    type := .rwBuf (.scalar .uint) },
    { name := "offset", type := .scalar .uint, qualifier := .qInOut },
    { name := "count",  type := .scalar .uint }
  ]
  body := [
    .assign (.index (.var "buf") (.bin "/" (.var "offset") (.litUint 4))) (.litUint 34),
    .assign (.var "offset") (.bin "+" (.var "offset") (.litUint 4)),
    .assign (.index (.var "buf") (.bin "/" (.var "offset") (.litUint 4))) (.var "count"),
    .assign (.var "offset") (.bin "+" (.var "offset") (.litUint 4))
  ]
}

/-- write_packed_float32_array: tag(33) + count + count×float32. -/
def writePackedFloat32ArrayFn : SlangFunctionDecl := {
  name   := "godot_write_packed_float32_array"
  params := [
    { name := "buf",    type := .rwBuf (.scalar .uint) },
    { name := "offset", type := .scalar .uint, qualifier := .qInOut },
    { name := "data",   type := .roBuf (.scalar .float) },
    { name := "count",  type := .scalar .uint }
  ]
  body := [
    .assign (.index (.var "buf") (.bin "/" (.var "offset") (.litUint 4))) (.litUint 33),
    .assign (.var "offset") (.bin "+" (.var "offset") (.litUint 4)),
    .assign (.index (.var "buf") (.bin "/" (.var "offset") (.litUint 4))) (.var "count"),
    .assign (.var "offset") (.bin "+" (.var "offset") (.litUint 4)),
    .forCount "i" (.litUint 0) (.var "count") [
      .assign (.index (.var "buf") (.bin "+" (.bin "/" (.var "offset") (.litUint 4))
          (.var "i")))
        (.call "asuint" [.index (.var "data") (.var "i")])
    ],
    .assign (.var "offset") (.bin "+" (.var "offset")
        (.bin "*" (.var "count") (.litUint 4)))
  ]
}

-- ═══════════════════════════════════════════════════════════════════════════
-- OBJECT REFERENCE WRITERS
-- ═══════════════════════════════════════════════════════════════════════════

/-- write_object_internal: tag(24) + subtype(2) + string path. -/
def writeObjectInternalFn : SlangFunctionDecl := {
  name   := "godot_write_object_internal"
  params := [
    { name := "buf",      type := .rwBuf (.scalar .uint) },
    { name := "offset",   type := .scalar .uint, qualifier := .qInOut },
    { name := "path_data", type := .roBuf (.scalar .uint) },
    { name := "path_len", type := .scalar .uint }
  ]
  body := [
    -- VARIANT_OBJECT tag
    .assign (.index (.var "buf") (.bin "/" (.var "offset") (.litUint 4))) (.litUint 24),
    .assign (.var "offset") (.bin "+" (.var "offset") (.litUint 4)),
    -- OBJECT_INTERNAL_RESOURCE subtype = 2
    .assign (.index (.var "buf") (.bin "/" (.var "offset") (.litUint 4))) (.litUint 2),
    .assign (.var "offset") (.bin "+" (.var "offset") (.litUint 4)),
    -- path string (inline — same logic as godot_write_string but without tag)
    .assign (.index (.var "buf") (.bin "/" (.var "offset") (.litUint 4))) (.var "path_len"),
    .assign (.var "offset") (.bin "+" (.var "offset") (.litUint 4)),
    .declInit (.scalar .uint) "wc"
        (.bin "/" (.bin "+" (.var "path_len") (.litUint 3)) (.litUint 4)),
    .forCount "w" (.litUint 0) (.var "wc") [
      .assign (.index (.var "buf") (.bin "+" (.bin "/" (.var "offset") (.litUint 4))
          (.var "w")))
        (.index (.var "path_data") (.var "w"))
    ],
    .assign (.var "offset") (.bin "+" (.var "offset")
        (.bin "*" (.var "wc") (.litUint 4)))
  ]
}

/-- write_object_ext_index: tag(24) + subtype(3) + uint32 index. -/
def writeObjectExtIndexFn : SlangFunctionDecl := {
  name   := "godot_write_object_ext_index"
  params := [
    { name := "buf",    type := .rwBuf (.scalar .uint) },
    { name := "offset", type := .scalar .uint, qualifier := .qInOut },
    { name := "idx",    type := .scalar .uint }
  ]
  body := [
    .assign (.index (.var "buf") (.bin "/" (.var "offset") (.litUint 4))) (.litUint 24),
    .assign (.var "offset") (.bin "+" (.var "offset") (.litUint 4)),
    .assign (.index (.var "buf") (.bin "/" (.var "offset") (.litUint 4))) (.litUint 3),
    .assign (.var "offset") (.bin "+" (.var "offset") (.litUint 4)),
    .assign (.index (.var "buf") (.bin "/" (.var "offset") (.litUint 4))) (.var "idx"),
    .assign (.var "offset") (.bin "+" (.var "offset") (.litUint 4))
  ]
}

/-- write_nil: just the tag(1). -/
def writeNilFn : SlangFunctionDecl := {
  name   := "godot_write_nil"
  params := [
    { name := "buf",    type := .rwBuf (.scalar .uint) },
    { name := "offset", type := .scalar .uint, qualifier := .qInOut }
  ]
  body := [
    .assign (.index (.var "buf") (.bin "/" (.var "offset") (.litUint 4))) (.litUint 1),
    .assign (.var "offset") (.bin "+" (.var "offset") (.litUint 4))
  ]
}

/-- write_bool: tag(2) + uint32 (0 or 1). -/
def writeBoolFn : SlangFunctionDecl := {
  name   := "godot_write_bool"
  params := [
    { name := "buf",    type := .rwBuf (.scalar .uint) },
    { name := "offset", type := .scalar .uint, qualifier := .qInOut },
    { name := "value",  type := .scalar .uint }
  ]
  body := [
    .assign (.index (.var "buf") (.bin "/" (.var "offset") (.litUint 4))) (.litUint 2),
    .assign (.var "offset") (.bin "+" (.var "offset") (.litUint 4)),
    .assign (.index (.var "buf") (.bin "/" (.var "offset") (.litUint 4))) (.var "value"),
    .assign (.var "offset") (.bin "+" (.var "offset") (.litUint 4))
  ]
}

/-- write_int: tag(3) + int32. -/
def writeIntFn : SlangFunctionDecl := {
  name   := "godot_write_int"
  params := [
    { name := "buf",    type := .rwBuf (.scalar .uint) },
    { name := "offset", type := .scalar .uint, qualifier := .qInOut },
    { name := "value",  type := .scalar .int }
  ]
  body := [
    .assign (.index (.var "buf") (.bin "/" (.var "offset") (.litUint 4))) (.litUint 3),
    .assign (.var "offset") (.bin "+" (.var "offset") (.litUint 4)),
    .assign (.index (.var "buf") (.bin "/" (.var "offset") (.litUint 4))) (.call "asuint" [.var "value"]),
    .assign (.var "offset") (.bin "+" (.var "offset") (.litUint 4))
  ]
}

/-- write_color: tag(20) + 4×float32 (r,g,b,a). -/
def writeColorFn : SlangFunctionDecl := {
  name   := "godot_write_color"
  params := [
    { name := "buf",    type := .rwBuf (.scalar .uint) },
    { name := "offset", type := .scalar .uint, qualifier := .qInOut },
    { name := "c",      type := .vec .float 4 }
  ]
  body := [
    .assign (.index (.var "buf") (.bin "/" (.var "offset") (.litUint 4))) (.litUint 20),
    .assign (.var "offset") (.bin "+" (.var "offset") (.litUint 4)),
    .assign (.index (.var "buf") (.bin "/" (.var "offset") (.litUint 4))) (.call "asuint" [.member (.var "c") "x"]),
    .assign (.var "offset") (.bin "+" (.var "offset") (.litUint 4)),
    .assign (.index (.var "buf") (.bin "/" (.var "offset") (.litUint 4))) (.call "asuint" [.member (.var "c") "y"]),
    .assign (.var "offset") (.bin "+" (.var "offset") (.litUint 4)),
    .assign (.index (.var "buf") (.bin "/" (.var "offset") (.litUint 4))) (.call "asuint" [.member (.var "c") "z"]),
    .assign (.var "offset") (.bin "+" (.var "offset") (.litUint 4)),
    .assign (.index (.var "buf") (.bin "/" (.var "offset") (.litUint 4))) (.call "asuint" [.member (.var "c") "w"]),
    .assign (.var "offset") (.bin "+" (.var "offset") (.litUint 4))
  ]
}

/-- write_quaternion: tag(14) + 4×float32 (x,y,z,w). -/
def writeQuaternionFn : SlangFunctionDecl := {
  name   := "godot_write_quaternion"
  params := [
    { name := "buf",    type := .rwBuf (.scalar .uint) },
    { name := "offset", type := .scalar .uint, qualifier := .qInOut },
    { name := "q",      type := .vec .float 4 }
  ]
  body := [
    .assign (.index (.var "buf") (.bin "/" (.var "offset") (.litUint 4))) (.litUint 14),
    .assign (.var "offset") (.bin "+" (.var "offset") (.litUint 4)),
    .assign (.index (.var "buf") (.bin "/" (.var "offset") (.litUint 4))) (.call "asuint" [.member (.var "q") "x"]),
    .assign (.var "offset") (.bin "+" (.var "offset") (.litUint 4)),
    .assign (.index (.var "buf") (.bin "/" (.var "offset") (.litUint 4))) (.call "asuint" [.member (.var "q") "y"]),
    .assign (.var "offset") (.bin "+" (.var "offset") (.litUint 4)),
    .assign (.index (.var "buf") (.bin "/" (.var "offset") (.litUint 4))) (.call "asuint" [.member (.var "q") "z"]),
    .assign (.var "offset") (.bin "+" (.var "offset") (.litUint 4)),
    .assign (.index (.var "buf") (.bin "/" (.var "offset") (.litUint 4))) (.call "asuint" [.member (.var "q") "w"]),
    .assign (.var "offset") (.bin "+" (.var "offset") (.litUint 4))
  ]
}

/-- write_footer: "RSRC" magic at end = 0x43525352 LE. -/
def writeFooterFn : SlangFunctionDecl := {
  name   := "godot_write_footer"
  params := [
    { name := "buf",    type := .rwBuf (.scalar .uint) },
    { name := "offset", type := .scalar .uint, qualifier := .qInOut }
  ]
  body := [
    .assign (.index (.var "buf") (.bin "/" (.var "offset") (.litUint 4))) (.litUint 0x43525352),
    .assign (.var "offset") (.bin "+" (.var "offset") (.litUint 4))
  ]
}

-- ═══════════════════════════════════════════════════════════════════════════
-- RSCC COMPRESSED ENVELOPE
-- ═══════════════════════════════════════════════════════════════════════════

/-- write_rscc_header: magic "RSCC" + mode + block_size + total_size.
    Block sizes array and compressed blocks are written by the C++ host
    (zstd compression cannot be expressed in Slang). This function writes
    the fixed envelope prefix. -/
def writeRsccHeaderFn : SlangFunctionDecl := {
  name   := "godot_write_rscc_header"
  params := [
    { name := "buf",        type := .rwBuf (.scalar .uint) },
    { name := "offset",     type := .scalar .uint, qualifier := .qInOut },
    { name := "mode",       type := .scalar .uint },
    { name := "block_size", type := .scalar .uint },
    { name := "total_size", type := .scalar .uint }
  ]
  body := [
    -- Magic "RSCC" = 0x43435352 LE
    .assign (.index (.var "buf") (.bin "/" (.var "offset") (.litUint 4))) (.litUint 0x43435352),
    .assign (.var "offset") (.bin "+" (.var "offset") (.litUint 4)),
    .assign (.index (.var "buf") (.bin "/" (.var "offset") (.litUint 4))) (.var "mode"),
    .assign (.var "offset") (.bin "+" (.var "offset") (.litUint 4)),
    .assign (.index (.var "buf") (.bin "/" (.var "offset") (.litUint 4))) (.var "block_size"),
    .assign (.var "offset") (.bin "+" (.var "offset") (.litUint 4)),
    .assign (.index (.var "buf") (.bin "/" (.var "offset") (.litUint 4))) (.var "total_size"),
    .assign (.var "offset") (.bin "+" (.var "offset") (.litUint 4))
  ]
}

/-- write_rscc_footer: "RSCC" magic repeated at EOF. -/
def writeRsccFooterFn : SlangFunctionDecl := {
  name   := "godot_write_rscc_footer"
  params := [
    { name := "buf",    type := .rwBuf (.scalar .uint) },
    { name := "offset", type := .scalar .uint, qualifier := .qInOut }
  ]
  body := [
    .assign (.index (.var "buf") (.bin "/" (.var "offset") (.litUint 4))) (.litUint 0x43435352),
    .assign (.var "offset") (.bin "+" (.var "offset") (.litUint 4))
  ]
}

-- ═══════════════════════════════════════════════════════════════════════════
-- RESOURCE TABLE WRITERS
-- ═══════════════════════════════════════════════════════════════════════════

/-- write_string_table_header: uint32 count of strings. -/
def writeStringTableHeaderFn : SlangFunctionDecl := {
  name   := "godot_write_string_table_header"
  params := [
    { name := "buf",    type := .rwBuf (.scalar .uint) },
    { name := "offset", type := .scalar .uint, qualifier := .qInOut },
    { name := "count",  type := .scalar .uint }
  ]
  body := [
    .assign (.index (.var "buf") (.bin "/" (.var "offset") (.litUint 4))) (.var "count"),
    .assign (.var "offset") (.bin "+" (.var "offset") (.litUint 4))
  ]
}

/-- write_ext_resource_count: uint32 count of external resources. -/
def writeExtResourceCountFn : SlangFunctionDecl := {
  name   := "godot_write_ext_resource_count"
  params := [
    { name := "buf",    type := .rwBuf (.scalar .uint) },
    { name := "offset", type := .scalar .uint, qualifier := .qInOut },
    { name := "count",  type := .scalar .uint }
  ]
  body := [
    .assign (.index (.var "buf") (.bin "/" (.var "offset") (.litUint 4))) (.var "count"),
    .assign (.var "offset") (.bin "+" (.var "offset") (.litUint 4))
  ]
}

/-- write_int_resource_count: uint32 count of internal resources. -/
def writeIntResourceCountFn : SlangFunctionDecl := {
  name   := "godot_write_int_resource_count"
  params := [
    { name := "buf",    type := .rwBuf (.scalar .uint) },
    { name := "offset", type := .scalar .uint, qualifier := .qInOut },
    { name := "count",  type := .scalar .uint }
  ]
  body := [
    .assign (.index (.var "buf") (.bin "/" (.var "offset") (.litUint 4))) (.var "count"),
    .assign (.var "offset") (.bin "+" (.var "offset") (.litUint 4))
  ]
}

/-- write_resource_data_header: type string + property count.
    Type string written via godot_write_string by the host;
    this writes just the property count that follows. -/
def writeResourcePropertyCountFn : SlangFunctionDecl := {
  name   := "godot_write_resource_property_count"
  params := [
    { name := "buf",    type := .rwBuf (.scalar .uint) },
    { name := "offset", type := .scalar .uint, qualifier := .qInOut },
    { name := "count",  type := .scalar .uint }
  ]
  body := [
    .assign (.index (.var "buf") (.bin "/" (.var "offset") (.litUint 4))) (.var "count"),
    .assign (.var "offset") (.bin "+" (.var "offset") (.litUint 4))
  ]
}

/-- write_property_name_idx: uint32 string table index for property name. -/
def writePropertyNameIdxFn : SlangFunctionDecl := {
  name   := "godot_write_property_name_idx"
  params := [
    { name := "buf",    type := .rwBuf (.scalar .uint) },
    { name := "offset", type := .scalar .uint, qualifier := .qInOut },
    { name := "idx",    type := .scalar .uint }
  ]
  body := [
    .assign (.index (.var "buf") (.bin "/" (.var "offset") (.litUint 4))) (.var "idx"),
    .assign (.var "offset") (.bin "+" (.var "offset") (.litUint 4))
  ]
}

/-- write_reserved_fields: 11× uint32 zeros. -/
def writeReservedFieldsFn : SlangFunctionDecl := {
  name   := "godot_write_reserved_fields"
  params := [
    { name := "buf",    type := .rwBuf (.scalar .uint) },
    { name := "offset", type := .scalar .uint, qualifier := .qInOut }
  ]
  body := [
    .forCount "i" (.litUint 0) (.litUint 11) [
      .assign (.index (.var "buf") (.bin "+" (.bin "/" (.var "offset") (.litUint 4))
          (.var "i"))) (.litUint 0)
    ],
    .assign (.var "offset") (.bin "+" (.var "offset") (.litUint 44))
  ]
}

-- ═══════════════════════════════════════════════════════════════════════════
-- MODULE ASSEMBLY
-- ═══════════════════════════════════════════════════════════════════════════

-- ═══════════════════════════════════════════════════════════════════════════
-- ENTRY POINT
-- ═══════════════════════════════════════════════════════════════════════════
--
-- Slangc emits code only for functions transitively reachable from a
-- `[shader(...)]` entry point. A library of helper functions without an
-- entry point compiles to an empty file on every target (Metal: just
-- `#include <metal_stdlib>`; SPIR-V: `error[E57004]: no exported
-- symbols`; CPU: just the prelude include).
--
-- To make slangc produce real `.metal` / `.spv` / `.cpp` outputs that
-- the C++ host can link against, the module exports a single compute
-- entry `bake_scn_kernel`. It calls every writer at least once so the
-- whole library survives dead-code elimination. The kernel runs in a
-- degenerate (1,1,1) grid — thread 0 alone produces the serialized
-- bytes into `buf`; other invocations are no-ops. Inputs come from
-- additional read-only buffers (`str_data`, `int_data`, `float_data`)
-- bound by the host.
--
-- This is the same shape as Google's Wuffs codec DSL: declare the
-- entry points the host calls, the compiler emits a linkable
-- per-target translation unit, the helpers stay alive via reachability.

/-- Compute entry point. Calls every writer in the module so slangc
    keeps the whole library reachable across `-target metal`, `-target
    spirv`, and `-target cpp` lowerings. -/
def bakeScnKernelFn : SlangFunctionDecl := {
  attrs   := [.shaderCompute, .numthreads 1 1 1]
  name    := "bake_scn_kernel"
  params  := [
    { name := "tid", type := .vec .uint 3, semantic := .svDispatchThreadId }
  ]
  body := [
    -- Single-thread guard. Real bake kernels would parallelize over
    -- mesh surfaces / nodes; for now the entry point's only job is
    -- to keep the writer library alive in slangc's output.
    .ifNoElse (.bin "==" (.member (.var "tid") "x") (.litUint 0)) [
      .declInit (.scalar .uint) "offset" (.litUint 0),

      -- RSRC header.
      .declare (.named "GodotResourceHeader") "hdr" none,
      .assign (.member (.var "hdr") "big_endian")     (.litUint 0),
      .assign (.member (.var "hdr") "use_64bit")      (.litUint 0),
      .assign (.member (.var "hdr") "ver_major")      (.litUint 4),
      .assign (.member (.var "hdr") "ver_minor")      (.litUint 4),
      .assign (.member (.var "hdr") "format_version") (.litUint 6),
      .assign (.member (.var "hdr") "flags")          (.litUint 0),
      .assign (.member (.var "hdr") "uid_lo")         (.litUint 0),
      .assign (.member (.var "hdr") "uid_hi")         (.litUint 0),
      .expr (.call "godot_write_header" [.var "buf", .var "offset", .var "hdr"]),

      -- Reserved fields + table headers + resource counts.
      .expr (.call "godot_write_reserved_fields"     [.var "buf", .var "offset"]),
      .expr (.call "godot_write_string_table_header" [.var "buf", .var "offset", .litUint 0]),
      .expr (.call "godot_write_ext_resource_count"  [.var "buf", .var "offset", .litUint 0]),
      .expr (.call "godot_write_int_resource_count"  [.var "buf", .var "offset", .litUint 0]),

      -- Primitive stores.
      .expr (.call "godot_store_u32"   [.var "buf", .var "offset", .litUint 0]),
      .expr (.call "godot_store_u64"   [.var "buf", .var "offset", .litUint 0, .litUint 0]),
      .expr (.call "godot_store_float" [.var "buf", .var "offset", .litFloat 0.0]),

      -- Variant scalars.
      .expr (.call "godot_write_variant_tag" [.var "buf", .var "offset", .litUint 0]),
      .expr (.call "godot_write_nil"         [.var "buf", .var "offset"]),
      .expr (.call "godot_write_bool"        [.var "buf", .var "offset", .litUint 0]),
      .expr (.call "godot_write_int"         [.var "buf", .var "offset", .litUint 0]),

      -- Vector / quaternion / color / transform.
      .declare (.vec .float 3) "v3" none,
      .declare (.vec .float 4) "v4" none,
      .expr (.call "godot_write_vector3"     [.var "buf", .var "offset", .var "v3"]),
      .expr (.call "godot_write_quaternion"  [.var "buf", .var "offset", .var "v4"]),
      .expr (.call "godot_write_color"       [.var "buf", .var "offset", .var "v4"]),
      .expr (.call "godot_write_transform3d" [.var "buf", .var "offset", .var "v3", .var "v3"]),

      -- Dict / array container headers.
      .expr (.call "godot_write_dict_header"                [.var "buf", .var "offset", .litUint 0]),
      .expr (.call "godot_write_array_header"               [.var "buf", .var "offset", .litUint 0]),
      .expr (.call "godot_write_packed_string_array_header" [.var "buf", .var "offset", .litUint 0]),

      -- Packed arrays (consume the bound read-only buffers).
      .expr (.call "godot_write_packed_int32_array"   [.var "buf", .var "offset", .var "int_data",   .litUint 0]),
      .expr (.call "godot_write_packed_float32_array" [.var "buf", .var "offset", .var "float_data", .litUint 0]),

      -- Strings + object references.
      .expr (.call "godot_write_string"           [.var "buf", .var "offset", .var "str_data", .litUint 0]),
      .expr (.call "godot_write_object_internal"  [.var "buf", .var "offset", .var "str_data", .litUint 0]),
      .expr (.call "godot_write_object_ext_index" [.var "buf", .var "offset", .litUint 0]),

      -- Resource property metadata.
      .expr (.call "godot_write_resource_property_count" [.var "buf", .var "offset", .litUint 0]),
      .expr (.call "godot_write_property_name_idx"       [.var "buf", .var "offset", .litUint 0]),

      -- Footer.
      .expr (.call "godot_write_footer" [.var "buf", .var "offset"]),

      -- RSCC envelope (compressed variant).
      .expr (.call "godot_write_rscc_header" [.var "buf", .var "offset", .litUint 2, .litUint 4096, .litUint 0]),
      .expr (.call "godot_write_rscc_footer" [.var "buf", .var "offset"])
    ]
  ]
}

def godotScnModule : SlangShaderModule := {
  structs := [headerStruct, compressedHeaderStruct, lodStruct]
  globals := [
    { name := "buf",        type := .rwBuf (.scalar .uint),  binding := some 0, space := some 0 },
    { name := "str_data",   type := .roBuf (.scalar .uint),  binding := some 1, space := some 0 },
    { name := "int_data",   type := .roBuf (.scalar .uint),  binding := some 2, space := some 0 },
    { name := "float_data", type := .roBuf (.scalar .float), binding := some 3, space := some 0 }
  ]
  functions := [
    storeU32Fn,
    storeU64Fn,
    storeFloatFn,
    writeVariantTagFn,
    writeNilFn,
    writeBoolFn,
    writeIntFn,
    writeVector3Fn,
    writeQuaternionFn,
    writeColorFn,
    writeTransform3DFn,
    writeStringFn,
    writeDictHeaderFn,
    writeArrayHeaderFn,
    writePackedInt32ArrayFn,
    writePackedFloat32ArrayFn,
    writePackedStringArrayHeaderFn,
    writeObjectInternalFn,
    writeObjectExtIndexFn,
    writeHeaderFn,
    writeFooterFn,
    writeRsccHeaderFn,
    writeRsccFooterFn,
    writeStringTableHeaderFn,
    writeExtResourceCountFn,
    writeIntResourceCountFn,
    writeResourcePropertyCountFn,
    writePropertyNameIdxFn,
    writeReservedFieldsFn,
    bakeScnKernelFn
  ]
}

def slangSource : String := LeanSlang.emit godotScnModule

-- ═══════════════════════════════════════════════════════════════════════════
-- VERIFICATION
-- ═══════════════════════════════════════════════════════════════════════════

#eval! do
  IO.println "=== godot_scn.slang (preview) ==="
  IO.println slangSource
  IO.println s!"Total length: {slangSource.length} chars"

end Fabric.Serialization.GodotScn
