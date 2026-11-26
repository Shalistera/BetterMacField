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
import prepare_graphics172


VERSION = '0.6.1'
BUILD_NUMBER = '12'


def build_current(resources, include_assets=True):
    source = bmf.BUILD/'Graphics172'
    inputs = prepare_graphics172.inventory(source) if include_assets else {}
    bundle = resources/'BetterMacFieldGraphics.bundle'
    if bundle.exists():
        shutil.rmtree(bundle)
    contents = bundle/'Contents'
    binary = contents/'MacOS/BetterMacFieldGraphics'
    binary.parent.mkdir(parents=True)
    assets = contents/'Resources'
    assets.mkdir(parents=True)
    for name in inputs:
        destination = assets/name
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source/name, destination)
    shutil.copy2(bmf.ROOT/'launcher/features.json', assets/'features.json')
    subprocess.run(['xcrun', 'clang', '-bundle', '-fobjc-arc', '-arch', 'arm64',
        '-mmacosx-version-min=13.0', '-framework', 'AppKit', '-framework', 'Foundation',
        '-framework', 'Accelerate', '-framework', 'Metal', '-weak_framework', 'MetalFX',
        str(bmf.ROOT/'src/v172/graphics172.m'), '-o', str(binary)], check=True)
    (contents/'Info.plist').write_bytes(plistlib.dumps(dict(
        CFBundleIdentifier='io.shalistera.BetterMacField.Graphics',
        CFBundleExecutable=binary.name, CFBundlePackageType='BNDL',
        CFBundleVersion=BUILD_NUMBER, NSPrincipalClass='BMFGraphics172', BMFClients172=bmf.CLIENTS)))
    subprocess.run(['codesign', '--force', '--sign', '-', str(bundle)], check=True)
    entry = resources/'BetterMacField172.dylib'
    subprocess.run(['xcrun', 'clang', '-dynamiclib', '-fobjc-arc',
        '-target', 'arm64-apple-ios16.0-macabi', '-framework', 'Foundation',
        '-Wl,-install_name,@rpath/BetterMacField172.dylib',
        str(bmf.ROOT/'src/v172/entry172.m'), '-o', str(entry)], check=True)
    subprocess.run(['codesign', '--force', '--sign', '-', str(entry)], check=True)
    return bundle, entry


def main():
    # Validate all external inputs before replacing the current release bundle.
    prepare_graphics172.inventory(bmf.BUILD/'Graphics172')
    target = bmf.ROOT/'dist/BetterMacField.app'
    # Rebuild the generated package so retired runtime assets cannot survive.
    if target.exists():
        shutil.rmtree(target)
    contents = target/'Contents'
    resources = contents/'Resources'
    executables = contents/'MacOS'
    resources.mkdir(parents=True, exist_ok=True)
    executables.mkdir(parents=True, exist_ok=True)
    bundle, entry = build_current(resources)
    shutil.copy2(bmf.ROOT/'launcher/features.json', resources/'features.json')
    shutil.copy2(bmf.ROOT/'launcher/AppIcon.icns', resources/'AppIcon.icns')
    payload = {'features.json': hashlib.sha256((resources/'features.json').read_bytes()).hexdigest()}
    payload.update({str(p.relative_to(resources)):hashlib.sha256(p.read_bytes()).hexdigest()
                    for p in bundle.rglob('*') if p.is_file()})
    payload[entry.name] = hashlib.sha256(entry.read_bytes()).hexdigest()
    (resources/'clients.json').write_text(json.dumps(dict(clients=bmf.CLIENTS,
        displaySettings=bmf.DISPLAY_SETTINGS, payload=payload), indent=2)+'\n')
    (contents/'Info.plist').write_bytes(plistlib.dumps(dict(CFBundleIdentifier='io.shalistera.BetterMacField',
        CFBundleName='BetterMacField', CFBundleDisplayName='BetterMacField', CFBundleExecutable='BetterMacField',
        CFBundlePackageType='APPL', CFBundleShortVersionString=VERSION, CFBundleVersion=BUILD_NUMBER, CFBundleIconFile='AppIcon',
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
