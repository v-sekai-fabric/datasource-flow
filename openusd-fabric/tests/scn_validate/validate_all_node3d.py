#!/usr/bin/env python3
"""Validate that the comprehensive .scn contains all expected Node3D types and resources."""
import struct
from pathlib import Path

def read_string(data, pos):
    length = struct.unpack_from('<I', data, pos)[0]
    pos += 4
    if length == 0:
        return '', pos
    s = data[pos:pos+length].decode('utf-8', errors='replace').rstrip('\x00')
    return s, pos + length

def main():
    data = Path('test_all_node3d.scn').read_bytes()
    pos = 4 + 4 + 4 + 4 + 4 + 4  # skip magic+endian+use64+major+minor+format
    type_str, pos = read_string(data, pos)
    pos += 8 + 4 + 8 + 44  # import_md+flags+uid+reserved

    str_count = struct.unpack_from('<I', data, pos)[0]; pos += 4
    strings = []
    for i in range(str_count):
        s, pos = read_string(data, pos)
        strings.append(s)

    ext_count = struct.unpack_from('<I', data, pos)[0]; pos += 4
    for i in range(ext_count):
        _, pos = read_string(data, pos)
        _, pos = read_string(data, pos)
        pos += 8

    int_count = struct.unpack_from('<I', data, pos)[0]; pos += 4
    print(f'File: {len(data)} bytes, type={type_str}')
    print(f'Strings: {str_count}, Ext: {ext_count}, Int: {int_count}')
    print()

    resources = []
    for i in range(int_count):
        path, pos = read_string(data, pos)
        offset = struct.unpack_from('<Q', data, pos)[0]; pos += 8
        rtype, _ = read_string(data, offset)
        resources.append((path, rtype))

    print('Internal resources:')
    for path, rtype in resources:
        print(f'  {rtype:40s} {path}')

    # Check node types in string table (these are what _bundled "names" references)
    print()
    print('Node type names in string table:')
    node_types = ['Node3D', 'MeshInstance3D', 'Skeleton3D', 'AnimationPlayer',
                  'Camera3D', 'DirectionalLight3D', 'StaticBody3D', 'CollisionShape3D']
    all_found = True
    for nt in node_types:
        found = nt in strings
        status = 'FOUND' if found else 'MISSING'
        print(f'  {nt:25s} {status}')
        if not found:
            all_found = False

    # Check resource types
    print()
    print('Resource types:')
    resource_types = [r[1] for r in resources]
    checks = ['PortableCompressedTexture2D', 'StandardMaterial3D', 'ArrayMesh',
              'BoxShape3D', 'Animation', 'AnimationLibrary', 'PackedScene']
    for c in checks:
        found = c in resource_types
        status = 'FOUND' if found else 'MISSING'
        print(f'  {c:35s} {status}')
        if not found:
            all_found = False

    # Check key property strings exist
    print()
    print('Key property strings:')
    props = ['transform', 'mesh', 'shape', 'fov', 'light_energy',
             'albedo_texture', 'metallic', 'roughness', 'bone_count']
    for p in props:
        found = p in strings
        status = 'FOUND' if found else 'missing (may be in long concatenated string)'
        print(f'  {p:25s} {status}')

    print()
    if all_found:
        print('ALL NODE TYPES AND RESOURCES VALIDATED')
    else:
        print('WARNING: Some expected types missing')
        return 1
    return 0

if __name__ == '__main__':
    exit(main())
