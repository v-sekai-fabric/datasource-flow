#!/usr/bin/env python3
"""Parse and validate Godot .scn binary format against our spec constants."""
import struct
import sys
from pathlib import Path

def read_string(data, pos):
    """Read a Godot unicode string: uint32 len + UTF-8 bytes (NO padding).
    Length includes the null terminator. No 4-byte alignment padding.
    Source: ResourceLoaderBinary::get_unicode_string() line 864."""
    if pos + 4 > len(data):
        raise ValueError(f"String read out of bounds at pos={pos}, data_len={len(data)}")
    length = struct.unpack_from('<I', data, pos)[0]
    pos += 4
    if length == 0:
        return "", pos
    if pos + length > len(data):
        raise ValueError(f"String overflow: pos={pos}, len={length}, data_len={len(data)}")
    s = data[pos:pos+length].decode('utf-8', errors='replace').rstrip('\x00')
    return s, pos + length

def parse_rsrc(data):
    """Parse an uncompressed RSRC file."""
    results = {}

    # Magic
    magic = data[0:4]
    assert magic == b'RSRC', f"Expected RSRC, got {magic}"
    results['magic'] = 'RSRC'

    # Header fields
    pos = 4
    endian = struct.unpack_from('<I', data, pos)[0]; pos += 4
    use64 = struct.unpack_from('<I', data, pos)[0]; pos += 4
    ver_major = struct.unpack_from('<I', data, pos)[0]; pos += 4
    ver_minor = struct.unpack_from('<I', data, pos)[0]; pos += 4
    fmt_version = struct.unpack_from('<I', data, pos)[0]; pos += 4

    results['endian'] = endian
    results['use64'] = use64
    results['version'] = f"{ver_major}.{ver_minor}"
    results['format_version'] = fmt_version

    # Type string
    type_str, pos = read_string(data, pos)
    results['type'] = type_str

    # Import metadata offset
    import_md_ofs = struct.unpack_from('<Q', data, pos)[0]; pos += 8
    results['import_md_offset'] = import_md_ofs

    # Flags
    flags = struct.unpack_from('<I', data, pos)[0]; pos += 4
    results['flags'] = flags
    results['flag_named_scene_ids'] = bool(flags & 1)
    results['flag_uids'] = bool(flags & 2)
    results['flag_real_is_double'] = bool(flags & 4)
    results['flag_has_script_class'] = bool(flags & 8)

    # UID
    uid = struct.unpack_from('<Q', data, pos)[0]; pos += 8
    results['uid'] = uid

    # Script class (if flag set)
    if flags & 8:
        sc, pos = read_string(data, pos)
        results['script_class'] = sc

    # Reserved fields
    reserved = struct.unpack_from('<' + 'I' * 11, data, pos)
    pos += 44
    results['reserved_all_zero'] = all(r == 0 for r in reserved)

    # String table
    str_count = struct.unpack_from('<I', data, pos)[0]; pos += 4
    results['string_count'] = str_count
    strings = []
    for i in range(str_count):
        s, pos = read_string(data, pos)
        strings.append(s)
    results['strings'] = strings

    # External resources
    ext_count = struct.unpack_from('<I', data, pos)[0]; pos += 4
    results['ext_resource_count'] = ext_count
    ext_resources = []
    for i in range(ext_count):
        ext_type, pos = read_string(data, pos)
        ext_path, pos = read_string(data, pos)
        ext_uid = struct.unpack_from('<Q', data, pos)[0]; pos += 8
        ext_resources.append({'type': ext_type, 'path': ext_path, 'uid': ext_uid})
    results['ext_resources'] = ext_resources

    # Internal resources
    int_count = struct.unpack_from('<I', data, pos)[0]; pos += 4
    results['int_resource_count'] = int_count
    int_resources = []
    for i in range(int_count):
        int_path, pos = read_string(data, pos)
        int_offset = struct.unpack_from('<Q', data, pos)[0]; pos += 8
        int_resources.append({'path': int_path, 'offset': int_offset})
    results['int_resources'] = int_resources

    # Parse each internal resource's type
    for i, ir in enumerate(int_resources):
        rpos = ir['offset']
        rtype, rpos = read_string(data, rpos)
        prop_count = struct.unpack_from('<I', data, rpos)[0]; rpos += 4
        ir['type'] = rtype
        ir['property_count'] = prop_count

    # Footer
    footer = data[-4:]
    results['footer'] = footer.decode('ascii', errors='replace')

    return results

def parse_rscc(data):
    """Parse compressed RSCC header (don't decompress, just validate envelope)."""
    results = {}
    magic = data[0:4]
    assert magic == b'RSCC', f"Expected RSCC, got {magic}"
    results['magic'] = 'RSCC'

    mode = struct.unpack_from('<I', data, 4)[0]
    block_size = struct.unpack_from('<I', data, 8)[0]
    total_size = struct.unpack_from('<I', data, 12)[0]

    results['compression_mode'] = mode
    results['block_size'] = block_size
    results['total_uncompressed_size'] = total_size
    results['block_count'] = (total_size // block_size) + 1

    footer = data[-4:]
    results['footer'] = footer.decode('ascii', errors='replace')

    return results

def main():
    test_dir = Path(__file__).parent

    print("=" * 60)
    print("GODOT .SCN BINARY FORMAT VALIDATION")
    print("=" * 60)

    # Test uncompressed
    scn_path = test_dir / "test_mesh_basisu.scn"
    if scn_path.exists():
        data = scn_path.read_bytes()
        r = parse_rsrc(data)

        print(f"\n--- {scn_path.name} ({len(data)} bytes) ---")
        print(f"Magic: {r['magic']}")
        print(f"Version: {r['version']}, format={r['format_version']}")
        print(f"Type: '{r['type']}'")
        print(f"Flags: {r['flags']} (named_ids={r['flag_named_scene_ids']}, uids={r['flag_uids']}, double={r['flag_real_is_double']}, script={r['flag_has_script_class']})")
        print(f"Reserved all zero: {r['reserved_all_zero']}")
        print(f"String table: {r['string_count']} entries")
        print(f"External resources: {r['ext_resource_count']}")
        print(f"Internal resources: {r['int_resource_count']}")
        print(f"Footer: '{r['footer']}'")

        print(f"\nInternal resource types:")
        for ir in r['int_resources']:
            print(f"  {ir['path']} → {ir['type']} ({ir['property_count']} props)")

        # Find mesh/texture related strings
        mesh_strings = [s for s in r['strings'] if any(k in s.lower() for k in ['mesh', 'surface', 'array', 'vertex', 'texture', 'basisu', 'basis', 'compress', 'format', 'lod'])]
        if mesh_strings:
            print(f"\nMesh/texture related strings:")
            for s in mesh_strings[:20]:
                print(f"  '{s}'")

        # Assertions
        assert r['format_version'] == 6, f"FORMAT_VERSION should be 6, got {r['format_version']}"
        assert r['flags'] == 3, f"Flags should be 3, got {r['flags']}"
        assert r['reserved_all_zero'], "Reserved fields should all be zero"
        assert r['footer'] == 'RSRC', f"Footer should be RSRC, got {r['footer']}"
        assert r['type'] == 'PackedScene', f"Type should be PackedScene, got {r['type']}"

        # Check we have mesh types
        resource_types = [ir['type'] for ir in r['int_resources']]
        print(f"\nResource types present: {resource_types}")
        assert 'ArrayMesh' in resource_types, "Should contain ArrayMesh"
        assert 'StandardMaterial3D' in resource_types, "Should contain StandardMaterial3D"
        has_texture = any('Texture' in t or 'texture' in t for t in resource_types)
        assert has_texture, f"Should contain a texture resource, got: {resource_types}"

        print("\n✓ All format assertions PASSED")

    # Test compressed
    scn_c_path = test_dir / "test_mesh_basisu_compressed.scn"
    if scn_c_path.exists():
        data = scn_c_path.read_bytes()
        r = parse_rscc(data)

        print(f"\n--- {scn_c_path.name} ({len(data)} bytes) ---")
        print(f"Magic: {r['magic']}")
        print(f"Compression mode: {r['compression_mode']} (2=zstd)")
        print(f"Block size: {r['block_size']}")
        print(f"Total uncompressed: {r['total_uncompressed_size']}")
        print(f"Block count: {r['block_count']}")
        print(f"Footer: '{r['footer']}'")

        assert r['compression_mode'] == 2, f"Should be zstd (2), got {r['compression_mode']}"
        assert r['block_size'] == 4096, f"Block size should be 4096, got {r['block_size']}"
        assert r['footer'] == 'RSCC', f"Footer should be RSCC, got {r['footer']}"

        print("✓ Compressed envelope assertions PASSED")

    # Test simple scene
    scn_simple = test_dir / "test_output.scn"
    if scn_simple.exists():
        data = scn_simple.read_bytes()
        r = parse_rsrc(data)
        print(f"\n--- {scn_simple.name} ({len(data)} bytes) ---")
        print(f"Internal resources: {[(ir['path'], ir['type']) for ir in r['int_resources']]}")
        assert r['format_version'] == 6
        print("✓ Simple scene PASSED")

    print("\n" + "=" * 60)
    print("ALL VALIDATIONS PASSED")
    print("=" * 60)

if __name__ == '__main__':
    main()
