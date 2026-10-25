#!/usr/bin/env python3
"""Build native-assets scene vegetation from a local 1.6.3 research export.

The export contains VFS indices, the iOS resource catalog, native component
descriptors and the reviewed instance classification. Game data stays in build/.
Requires lz4; this tool is for maintainers, not for players.
"""
import argparse
from concurrent.futures import ProcessPoolExecutor
import hashlib
import json
from pathlib import Path
import struct

from tools.scene_vegetation import (Flat, read, compress, decode, entities,
    render_references, component_rows, entity_groups, selected_scene,
    with_missing_ownership, with_missing_archetypes, insert_renderers, transplant)


def native_assets(path):
    raw = path.read_bytes()
    p = 4
    n, = struct.unpack_from('<I', raw, p)
    p += 4 + n * 2 + 4
    for _ in range(2):
        n, = struct.unpack_from('<I', raw, p)
        p += 4 + n * 2
    size, = struct.unpack_from('<I', raw, p)
    asset = p + 4
    capacity, = struct.unpack_from('<I', raw, asset)
    return {row[0] for row in struct.iter_unpack('<Qiiii', raw[asset + 4 + capacity * 8:asset + size])}


def build_chunk(task):
    relative, ids, pc_record, mobile_record, descriptors, available, output = task
    mobile, pc = read(mobile_record), read(pc_record)
    incoming = set(ids) - entities(mobile).keys()
    desktop = render_references(pc)
    for eid in incoming:
        keys = {struct.unpack_from('<Q', ref)[0] for flag, ref in zip(desktop[eid][3], desktop[eid][4]) if flag == 15} - {0}
        assert keys <= available, (relative, eid)
    compact = selected_scene(pc, incoming)
    prepared = with_missing_ownership(mobile, compact, incoming)
    if '/InitChunkData_' in relative:
        prepared = with_missing_archetypes(prepared, compact, incoming, set(descriptors))
        result = insert_renderers(prepared, compact, incoming)
    else:
        assert not entity_groups(mobile) and not entity_groups(compact)
        result = transplant(prepared, compact, incoming)
    after = render_references(result)
    assert entities(result).keys() == entities(mobile).keys() | incoming
    assert all(after[eid] == value for eid, value in render_references(mobile).items())
    assert all(after[eid] == desktop[eid] for eid in incoming)
    original_rows, final_rows = component_rows(mobile), component_rows(result)
    assert all(final_rows[eid] == row for eid, row in original_rows.items())
    packed = compress(result)
    assert decode(packed) == result
    target = output / relative
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(packed)
    return dict(iosBundle='streaming/' + relative, fileName=mobile_record['path'],
        payload=relative, originalBytes=mobile_record['size'], originalMD5=mobile_record['md5'],
        bytes=len(packed), sha256=hashlib.sha256(packed).hexdigest(),
        addedEntities=len(incoming), selectedSceneBytes=len(compact))


def prepare(export, output, workers):
    classification = json.loads((export/'world-vegetation/release-classification.json').read_text())
    selection = classification['categories']['ios_assets_pc_scene_data']['chunkEntities']
    descriptors = json.loads((export/'world-vegetation/native-descriptors.json').read_text())
    available = native_assets(export/'graphics-ios-manifest-1.6.3.raw')
    catalogs = {}
    for platform, folder in [('ios', 'Mobile'), ('pc', 'PC')]:
        prefix = 'Data/Streaming/' + folder + '/'
        catalogs[platform] = {row['path'][len(prefix):]: row
            for line in (export/f'graphics-{platform}-vfs-1.6.3.jsonl').open()
            if (row := json.loads(line))['path'].startswith(prefix)}
    tasks = [(path, ids, catalogs['pc'][path], catalogs['ios'][path], descriptors, available, output)
             for relative, ids in sorted(selection.items())
             for path in (relative, relative.replace('InitChunkData_', 'StreamingChunkData_'))]
    output.mkdir(parents=True, exist_ok=True)
    with ProcessPoolExecutor(max_workers=workers) as pool:
        routes = list(pool.map(build_chunk, tasks))
    report = dict(bundleID='com.gryphline.endfield.ios', version='1.6.3',
        instances=classification['summary']['nativeAssetsInstances'],
        pcModels=False, pcTextures=False, pcSceneRecords=True, bundles=routes)
    (output/'manifest.json').write_text(json.dumps(report, indent=2) + '\n')
    expected = {row['payload'] for row in routes} | {'manifest.json'}
    for path in output.rglob('*'):
        if path.is_file() and str(path.relative_to(output)) not in expected:
            path.unlink()
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('export_directory', type=Path)
    parser.add_argument('--output', type=Path, default=Path(__file__).parent/'build/Vegetation')
    parser.add_argument('--workers', type=int, default=4)
    args = parser.parse_args()
    report = prepare(args.export_directory, args.output, args.workers)
    print(f"Prepared {report['instances']:,} instances, {len(report['bundles'])} scene chunks at {args.output}")
