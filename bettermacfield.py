#!/usr/bin/env python3
"""Build and launch BetterMacField against an explicitly supported PlayCover client."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import plistlib
import signal
import subprocess
import sys
import time
import uuid

ROOT = Path(__file__).resolve().parent
DEFAULT_APP = Path.home() / 'Library/Containers/io.playcover.PlayCover/Applications/com.gryphline.endfield.ios.app'
BUILD = ROOT / 'build'
LIBRARY = BUILD / 'libBetterMacField.dylib'
STATE = Path.home() / 'Library/Application Support/BetterMacField'
RESTORE = STATE / 'playcover-settings.json'
LOG = Path('/private/tmp/bettermacfield-runtime.log')
COMMAND = Path('/private/tmp/bettermacfield-command.json')
STATUS = Path('/private/tmp/bettermacfield-status.json')
EXPECTED = {
    'Frameworks/UnityFramework.framework/UnityFramework': '767616ee9093e7ce8a588f30ed4beef3929d4adb0fcfb8ca100e7b8a77d4cb13',
    'Data/Managed/Metadata/global-metadata.dat': '701d8556af6268bcb489bb7460e220f04b779a4002a9c8bf28de839a33ba5409',
}
DISPLAY_SETTINGS = dict(customScaler=2.0, resolution=6, resizableAspectRatioWidth=16,
                        resizableAspectRatioHeight=9, keymapping=False)


def sha256(path):
    h = hashlib.sha256()
    with path.open('rb') as f:
        for block in iter(lambda: f.read(4*1024*1024), b''):
            h.update(block)
    return h.hexdigest()


def validate(app):
    if platform.system() != 'Darwin' or platform.machine() != 'arm64':
        raise RuntimeError('This release requires an Apple Silicon Mac and native arm64 Python.')
    info = plistlib.loads((app/'Info.plist').read_bytes())
    if info.get('CFBundleIdentifier') != 'com.gryphline.endfield.ios' or info.get('CFBundleShortVersionString') != '1.6.3':
        raise RuntimeError('Supported client: com.gryphline.endfield.ios 1.6.3 only.')
    for relative, expected in EXPECTED.items():
        if sha256(app/relative) != expected:
            raise RuntimeError(f'Client build identity changed: {relative}. Update support before launching.')
    dependencies = subprocess.check_output(['xcrun', 'otool', '-L', str(app/'Endfield')], text=True)
    if 'PlayTools.framework/PlayTools' not in dependencies:
        raise RuntimeError('Install this client through PlayCover with PlayTools first.')
    return info


def build():
    BUILD.mkdir(exist_ok=True)
    pending = BUILD/'libBetterMacField-next.dylib'
    subprocess.run(['xcrun', 'clang', '-dynamiclib', '-fobjc-arc', '-arch', 'arm64',
                    '-mmacosx-version-min=13.0', '-framework', 'Foundation', '-framework', 'AppKit',
                    '-framework', 'Metal', '-framework', 'MetalFX', '-framework', 'QuartzCore',
                    '-framework', 'CoreGraphics', '-o', str(pending), str(ROOT/'src/core.m')], check=True)
    subprocess.run(['codesign', '--force', '--sign', '-', str(pending)], check=True)
    pending.replace(LIBRARY)
    print('Built', LIBRARY)


def running(app):
    rows = subprocess.check_output(['ps', '-axo', 'pid=,comm='], text=True).splitlines()
    executable = str(app/'Endfield')
    return [int(p[0]) for row in rows if len(p := row.strip().split(None, 1)) == 2 and p[1] == executable]


def stop(app):
    for pid in running(app):
        os.kill(pid, signal.SIGTERM)
        deadline = time.monotonic()+10
        while time.monotonic() < deadline:
            try:
                os.kill(pid, 0)
            except ProcessLookupError:
                break
            time.sleep(.1)
        else:
            raise RuntimeError('Game did not exit; close it before launching another instance.')


def apply_display(app):
    path = app.parent.parent/'App Settings'/f'{app.stem}.plist'
    current = plistlib.loads(path.read_bytes()) if path.exists() else {}
    if RESTORE.exists():
        saved = json.loads(RESTORE.read_text())
        if saved['path'] != str(path):
            raise RuntimeError('Restore the previous PlayCover settings before selecting another installation.')
    else:
        STATE.mkdir(parents=True, exist_ok=True)
        saved = dict(path=str(path), original={k: current[k] for k in DISPLAY_SETTINGS if k in current},
                     absent=[k for k in DISPLAY_SETTINGS if k not in current])
        RESTORE.write_text(json.dumps(saved, indent=2)+'\n')
    current.update(DISPLAY_SETTINGS)
    path.parent.mkdir(parents=True, exist_ok=True)
    pending = path.with_suffix('.bmf-pending')
    pending.write_bytes(plistlib.dumps(current));pending.replace(path)


def restore_display():
    if not RESTORE.exists():
        print('No saved PlayCover settings.');return
    saved = json.loads(RESTORE.read_text());path = Path(saved['path'])
    current = plistlib.loads(path.read_bytes()) if path.exists() else {}
    for key in DISPLAY_SETTINGS:
        # Keep a setting the user explicitly changed after our launch.
        if current.get(key) != DISPLAY_SETTINGS[key]:
            continue
        if key in saved['absent']:
            current.pop(key, None)
        else:
            current[key] = saved['original'][key]
    pending = path.with_suffix('.bmf-pending')
    pending.write_bytes(plistlib.dumps(current));pending.replace(path)
    RESTORE.unlink()
    print('Restored saved PlayCover display/key-mapping settings.')


def command(payload, timeout=15):
    identifier = uuid.uuid4().hex
    pending = COMMAND.with_suffix('.pending')
    pending.write_text(json.dumps(dict(payload, id=identifier)));pending.replace(COMMAND)
    deadline = time.monotonic()+timeout
    while time.monotonic() < deadline:
        if STATUS.exists():
            result = json.loads(STATUS.read_text())
            if result.get('id') == identifier:
                return result
        time.sleep(.2)
    raise RuntimeError('Runtime did not respond. Launch with BetterMacField and wait for the game menu.')


def launch(app, restart):
    validate(app)
    if running(app) and not restart:
        raise RuntimeError('Game is running. Close it or use launch --restart.')
    build()
    stop(app)
    apply_display(app)
    COMMAND.unlink(missing_ok=True);STATUS.unlink(missing_ok=True)
    previous = LOG.stat().st_mtime_ns if LOG.exists() else 0
    subprocess.run(['open', '-n', '--env', f'DYLD_INSERT_LIBRARIES={LIBRARY}',
                    '--env', 'MTL_HUD_ENABLED=1', '--env', 'MTL_HUD_DISABLE_MENU_BAR=0',
                    str(app), '--args', '-MetalForceHudEnabled', 'YES'], check=True)
    deadline = time.monotonic()+150
    while time.monotonic() < deadline:
        if LOG.exists() and LOG.stat().st_mtime_ns != previous and 'core=ready' in LOG.read_text():
            command(dict(op='settings', components=True, reveal=True, resolutionBridge=True))
            print('BetterMacField ready. Enter the scene to use the graphics menu.');return
        time.sleep(.5)
    raise RuntimeError(f'Runtime not ready; inspect {LOG}.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--app', type=Path, default=DEFAULT_APP, help='Exact PlayCover app bundle')
    sub = parser.add_subparsers(dest='action', required=True)
    sub.add_parser('check');sub.add_parser('build');sub.add_parser('status')
    sub.add_parser('restore-settings', help='Restore only settings changed by this launcher')
    sub.add_parser('launch').add_argument('--restart', action='store_true')
    args = parser.parse_args();app = args.app.expanduser().resolve()
    try:
        if args.action == 'check':
            validate(app);print('Supported 1.6.3 client and PlayTools verified.')
        elif args.action == 'build':build()
        elif args.action == 'launch':launch(app, args.restart)
        elif args.action == 'status':print(json.dumps(command(dict(op='status')), indent=2, ensure_ascii=False))
        elif args.action == 'restore-settings':
            if running(app):raise RuntimeError('Close the game before restoring its PlayCover settings.')
            restore_display()
    except (RuntimeError, OSError, subprocess.CalledProcessError, ValueError) as error:
        print(f'Error: {error}', file=sys.stderr);return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
