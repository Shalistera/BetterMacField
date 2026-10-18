#!/usr/bin/env python3
"""Stage the verified rendering programs from a local development export.

This is a private packaging input, separate from the MIT implementation.
Only shader source/code and binding metadata are accepted; models, textures,
Unity bundles and MOD manifests never enter the launcher package.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil


def prepare(source, target):
    source, target = Path(source), Path(target)
    if target.exists():
        shutil.rmtree(target)
    target.mkdir(parents=True)
    def copy_program(item):
        path = Path(item['source'])
        if path.parent.resolve() != source.resolve() or path.suffix != '.metal':
            raise ValueError(f'Unexpected shader source: {path}')
        data = path.read_bytes()
        if hashlib.sha256(data).hexdigest() != item['sha256']:
            raise ValueError(f'Shader checksum mismatch: {path.name}')
        (target/path.name).write_bytes(data)
        return dict(item, source='${RUNTIME}/'+path.name)
    patches = json.loads((source/'endfield-ios-ao-patches.json').read_text())
    if len(patches) != 7:
        raise ValueError('Expected the seven validated 1.6.3 lighting variants')
    (target/'endfield-ios-ao-patches.json').write_text(json.dumps({key:copy_program(item) for key,item in patches.items()}, indent=2)+'\n')
    skin = json.loads((source/'endfield-ios-character-skin.json').read_text())
    staged = copy_program(skin)
    staged['variants'] = [copy_program(item) for item in skin['variants']]
    (target/'endfield-ios-character-skin.json').write_text(json.dumps(staged, indent=2)+'\n')
    for name in ['endfield-ios-lighting-source-map.json', 'endfield-ios-lighting-gbuffer-map.json']:
        shutil.copy2(source/name, target/name)
    for name in ['endfield-official-gtao', 'endfield-official-contact']:
        folder=source/name; destination=target/name; destination.mkdir()
        manifest=json.loads((folder/'kernels.json').read_text())
        for kernel in manifest['kernels']:
            path=folder/(kernel['name']+'.metallib'); data=path.read_bytes()
            if hashlib.sha256(data).hexdigest() != kernel['sha256']:
                raise ValueError(f'Kernel checksum mismatch: {path.name}')
            (destination/path.name).write_bytes(data)
        shutil.copy2(folder/'kernels.json', destination/'kernels.json')
        if name.endswith('gtao'):
            shutil.copy2(folder/'camera-bindings.json', destination/'camera-bindings.json')
    return {str(path.relative_to(target)):hashlib.sha256(path.read_bytes()).hexdigest()
            for path in target.rglob('*') if path.is_file()}

if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('export_directory', type=Path)
    parser.add_argument('--output', type=Path, default=Path(__file__).parent/'build/Graphics')
    args=parser.parse_args()
    files=prepare(args.export_directory,args.output)
    print(f'Staged {len(files)} rendering-program files at {args.output}')
