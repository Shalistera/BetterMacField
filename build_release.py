#!/usr/bin/env python3
"""Build the native graphics launcher package; end users need no Python/Xcode."""
import hashlib
import json
from pathlib import Path
import plistlib
import shutil
import subprocess
import zipfile
import bettermacfield as bmf


def main():
    bmf.build()
    target = bmf.ROOT/'dist/BetterMacField.app'
    contents = target/'Contents'
    resources = contents/'Resources'
    executables = contents/'MacOS'
    resources.mkdir(parents=True, exist_ok=True)
    executables.mkdir(parents=True, exist_ok=True)
    # Keep one current package and build in place; no versioned backup copies.
    library = resources/'libBetterMacField.dylib'
    shutil.copy2(bmf.LIBRARY, library)
    graphics = bmf.BUILD/'Graphics'
    if not (graphics/'endfield-ios-ao-patches.json').is_file():
        raise RuntimeError('Prepare the verified local graphics export with prepare_graphics.py first.')
    if (resources/'Graphics').exists():
        shutil.rmtree(resources/'Graphics')
    shutil.copytree(graphics, resources/'Graphics')
    shutil.copy2(bmf.ROOT/'launcher/features.json', resources/'features.json')
    payload = {str(p.relative_to(resources)):hashlib.sha256(p.read_bytes()).hexdigest() for p in (resources/'Graphics').rglob('*') if p.is_file()}
    payload['features.json'] = hashlib.sha256((resources/'features.json').read_bytes()).hexdigest()
    profiles = {region: dict(bundleID=bundle, version='1.6.3', files=bmf.CN_EXPECTED if region == 'cn' else bmf.EXPECTED)
                for region, bundle in bmf.CLIENTS.items()}
    (resources/'clients.json').write_text(json.dumps(dict(clients=profiles, displaySettings=bmf.DISPLAY_SETTINGS,
        runtimeSHA256=hashlib.sha256(library.read_bytes()).hexdigest(), payload=payload), indent=2)+'\n')
    (contents/'Info.plist').write_bytes(plistlib.dumps(dict(CFBundleIdentifier='io.shalistera.BetterMacField',
        CFBundleName='BetterMacField', CFBundleDisplayName='BetterMacField', CFBundleExecutable='BetterMacField',
        CFBundlePackageType='APPL', CFBundleShortVersionString='0.3.3', CFBundleVersion='6',
        LSMinimumSystemVersion='13.0', NSHighResolutionCapable=True)))
    subprocess.run(['xcrun', 'clang', '-fobjc-arc', '-arch', 'arm64', '-mmacosx-version-min=13.0',
        '-Wno-deprecated-declarations', '-framework', 'AppKit', '-framework', 'Foundation',
        str(bmf.ROOT/'launcher/main.m'), '-o', str(executables/'BetterMacField')], check=True)
    subprocess.run(['codesign', '--force', '--sign', '-', str(target)], check=True)
    subprocess.run(['codesign', '--verify', '--deep', '--strict', str(target)], check=True)
    destination = bmf.ROOT/'dist/BetterMacField-macOS-arm64.zip'
    subprocess.run(['ditto', '-c', '-k', '--sequesterRsrc', '--keepParent', str(target), str(destination)], check=True)
    with zipfile.ZipFile(destination, 'a', compression=zipfile.ZIP_DEFLATED) as archive:
        archive.write(bmf.ROOT/'docs/quickstart.md', '使用说明.txt')
        archive.write(bmf.ROOT/'LICENSE', 'LICENSE')
        archive.write(bmf.ROOT/'NOTICE', 'NOTICE')
    digest = hashlib.sha256(destination.read_bytes()).hexdigest()
    (destination.parent/'SHA256SUMS.txt').write_text(f'{digest}  {destination.name}\n')
    print(destination)


if __name__ == '__main__':
    main()
