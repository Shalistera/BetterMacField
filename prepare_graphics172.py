#!/usr/bin/env python3
"""Stage prepared shared iOS 1.7.2 rendering exports (no asset extraction)."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil

FOLDERS = ('character172', 'scene172', 'native172')


def inventory(source):
    """Validate referenced programs and return only the allowed package inputs."""
    source = Path(source).resolve()
    files = {}

    def read_file(folder, name, digest=None):
        relative = Path(name)
        path = source/folder/relative
        if (relative.is_absolute() or '..' in relative.parts or path.is_symlink()
                or not path.resolve().is_relative_to(source/folder)
                or path.suffix not in ('.json', '.metal', '.metallib')):
            raise ValueError(f'Unexpected rendering-program path: {folder}/{name}')
        data = path.read_bytes()
        actual = hashlib.sha256(data).hexdigest()
        if digest is not None and actual != digest:
            raise ValueError(f'Rendering-program checksum mismatch: {folder}/{name}')
        files[str(Path(folder)/relative)] = actual
        return data

    catalogs = {}
    for folder in FOLDERS:
        catalog = json.loads(read_file(folder, 'manifest.json'))
        if catalog.get('version') != '1.7.2':
            raise ValueError(f'Expected shared iOS 1.7.2 export: {folder}')
        catalogs[folder] = catalog
    for folder, count in [('character172', 6), ('scene172', 24)]:
        catalog = catalogs[folder]
        if catalog.get('route') != 'library' or len(catalog['sources']) != count:
            raise ValueError(f'Incomplete validated shader catalog: {folder}')
        for item in catalog['sources'].values():
            read_file(folder, item['file'], item['sha256'])
    scene = catalogs['scene172']
    for family, count in [('official-gtao', 16), ('official-contact', 6)]:
        entry = scene['kernels'][family]
        kernels = json.loads(read_file('scene172', entry['file'], entry['sha256']))['kernels']
        if len(kernels) != count:
            raise ValueError(f'Incomplete kernel catalog: {family}')
        for kernel in kernels:
            read_file('scene172', f'{family}/{kernel["name"]}.metallib', kernel['sha256'])
    camera = scene['cameraBindings']
    read_file('scene172', camera['file'], camera['sha256'])
    native = catalogs['native172']
    motion = native['motionDecoder']
    read_file('native172', motion['file'], motion['sha256'])
    # Missing the terrain consumers caused ground material loss with MetalFX.
    terrain = native['terrainDepth']['bindings']
    consumers, shaders = native['depthConsumers'], native.get('depthConsumerShaders', {})
    # 18 named readers were not the whole set; the 1.7.2 package has 74, each mapped to its shader.
    if len(terrain) != 16 or len(consumers) != 74 or shaders.keys() != consumers.keys():
        raise ValueError('Incomplete terrain/decal/visibility depth-consumer catalog')
    if any(native['depthConsumers'].get(key) != value['slot'] for key, value in terrain.items()):
        raise ValueError('Terrain depth slots disagree with the consumer catalog')
    return files


def prepare(source, target):
    source, target = Path(source).resolve(), Path(target).resolve()
    if source == target or source.is_relative_to(target) or target.is_relative_to(source):
        raise ValueError('Source and output directories must be separate')
    files = inventory(source)
    if target.exists():
        shutil.rmtree(target)
    for name in files:
        path = target/name
        path.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source/name, path)
    return files


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('export_directory', type=Path,
                        help='Directory containing character172, scene172 and native172')
    parser.add_argument('--output', type=Path, default=Path(__file__).parent/'build/Graphics172')
    args = parser.parse_args()
    files = prepare(args.export_directory, args.output)
    print(f'Staged {len(files)} verified rendering-program files at {args.output}')
