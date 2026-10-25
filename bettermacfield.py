#!/usr/bin/env python3
"""Build and launch BetterMacField against an explicitly supported PlayCover client."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import plistlib
import shutil
import signal
import subprocess
import sys
import time
import uuid

ROOT = Path(__file__).resolve().parent
DEFAULT_APP = Path.home() / 'Library/Containers/io.playcover.PlayCover/Applications/com.gryphline.endfield.ios.app'
CLIENTS = {'global': 'com.gryphline.endfield.ios', 'cn': 'com.hypergryph.endfield'}
BUILD = ROOT / 'build'
LIBRARY = BUILD / 'libBetterMacField.dylib'
STATE = Path.home() / 'Library/Application Support/BetterMacField'
RESTORE = STATE / 'playcover-settings.json'
LOG = Path('/private/tmp/bettermacfield-runtime.log')
COMMAND = Path('/private/tmp/bettermacfield-command.json')
STATUS = Path('/private/tmp/bettermacfield-status.json')
DISPLAY_SETTINGS = dict(customScaler=2.0, resolution=6, resizableAspectRatioWidth=16,
                        resizableAspectRatioHeight=9, keymapping=False)


def validate(app):
    if platform.system() != 'Darwin' or platform.machine() != 'arm64':
        raise RuntimeError('This release requires an Apple Silicon Mac and native arm64 Python.')
    info = plistlib.loads((app/'Info.plist').read_bytes())
    bundle = info.get('CFBundleIdentifier')
    if bundle not in CLIENTS.values() or info.get('CFBundleShortVersionString') != '1.6.3':
        raise RuntimeError('Supported clients: global/CN iOS 1.6.3 only.')
    dependencies = subprocess.check_output(['xcrun', 'otool', '-L', str(app/'Endfield')], text=True)
    if 'PlayTools.framework/PlayTools' not in dependencies:
        raise RuntimeError('Install this client through PlayCover with PlayTools first.')
    return info


def configure_runtime(bundle):
    global LOG, COMMAND, STATUS, RESTORE
    if bundle not in CLIENTS.values():
        raise RuntimeError('Unknown client bundle identifier.')
    runtime = Path.home() / 'Library/Containers' / bundle / 'Data/tmp/BetterMacField'
    LOG = runtime/'bettermacfield-runtime.log'
    COMMAND = runtime/'bettermacfield-command.json'
    STATUS = runtime/'bettermacfield-status.json'
    RESTORE = STATE/('playcover-settings-cn.json' if bundle == CLIENTS['cn'] else 'playcover-settings.json')
    return runtime


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


def restore_vegetation(runtime):
    if runtime.name == 'BetterMacField' and runtime.parent.name == 'tmp':
        runtime = runtime.parent.parent/'Library/Application Support/BetterMacField'
    journal = runtime/'vegetation-files.plist'
    if not journal.exists():
        return
    for entry in reversed(plistlib.loads(journal.read_bytes())):
        path, backup = Path(entry['path']), Path(entry['backup'])
        current = hashlib.sha256(path.read_bytes()).hexdigest() if path.exists() else None
        if not backup.exists() and current == entry.get('previous'):
            continue
        if current and current != entry['installed']:
            raise RuntimeError(f'Scene file changed after installation; keep the recovery journal: {path}')
        if entry.get('previous') and not backup.exists():
            raise RuntimeError('Vegetation backup is missing; keep the recovery journal.')
        path.unlink(missing_ok=True)
        if backup.exists():
            backup.replace(path)
    journal.unlink()
    shutil.rmtree(runtime/'VegetationBackup', ignore_errors=True)


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


def stage_graphics(runtime, bundle):
    restore_vegetation(runtime)
    source = BUILD/'Graphics'
    if not (source/'endfield-ios-ao-patches.json').is_file():
        raise RuntimeError('Missing local graphics export. Run prepare_graphics.py before launching the full build.')
    runtime.mkdir(parents=True, exist_ok=True)
    for path in source.rglob('*'):
        if not path.is_file():
            continue
        target = runtime/path.relative_to(source)
        target.parent.mkdir(parents=True, exist_ok=True)
        if path.suffix == '.json':
            prefix = json.dumps(str(runtime), ensure_ascii=False)[1:-1]
            target.write_text(path.read_text().replace('${RUNTIME}', prefix))
        else:
            shutil.copy2(path, target)
    shutil.copy2(ROOT/'launcher/features.json', runtime/'features.json')
    if (BUILD/'Vegetation/manifest.json').is_file():
        shutil.copytree(BUILD/'Vegetation', runtime/'Vegetation', dirs_exist_ok=True)
    fields = json.loads((ROOT/'launcher/features.json').read_text())['fields']
    settings = {f['key']: f['default'] for f in fields}
    region = next(key for key, value in CLIENTS.items() if value == bundle)
    saved = STATE/f'settings-{region}.json'
    if saved.exists():
        settings.update(json.loads(saved.read_text()))
    (runtime/'bettermacfield-settings.json').write_text(json.dumps(settings, ensure_ascii=False))
    return settings


def launch(app, restart):
    info = validate(app)
    runtime = configure_runtime(info['CFBundleIdentifier'])
    if running(app) and not restart:
        raise RuntimeError('Game is running. Close it or use launch --restart.')
    build()
    if not (BUILD/'Graphics/endfield-ios-ao-patches.json').exists():
        raise RuntimeError('Prepare the local graphics export before launching.')
    stop(app)
    apply_display(app)
    settings = stage_graphics(runtime, info['CFBundleIdentifier'])
    loaded_library = runtime/LIBRARY.name
    shutil.copy2(LIBRARY, loaded_library)
    COMMAND.unlink(missing_ok=True);STATUS.unlink(missing_ok=True)
    previous = LOG.stat().st_mtime_ns if LOG.exists() else 0
    subprocess.run(['open', '-n', '--env', f'DYLD_INSERT_LIBRARIES={loaded_library}',
                    '--env', f'BMF_RUNTIME_DIR={runtime}',
                    '--env', f'ENDFIELD_AO_PATCHES_PATH={runtime}/endfield-ios-ao-patches.json',
                    '--env', f'ENDFIELD_CHARACTER_SKIN_PATH={runtime}/endfield-ios-character-skin.json',
                    '--env', f'ENDFIELD_OFFICIAL_GTAO_PATH={runtime}/endfield-official-gtao',
                    '--env', 'MTL_HUD_ENABLED='+('1' if settings['metalHUD'] else '0'), '--env', 'MTL_HUD_DISABLE_MENU_BAR=0',
                    str(app), '--args', '-MetalForceHudEnabled', 'YES' if settings['metalHUD'] else 'NO'], check=True)
    deadline = time.monotonic()+150
    while time.monotonic() < deadline:
        if LOG.exists() and LOG.stat().st_mtime_ns != previous and 'core=ready' in LOG.read_text():
            print('BetterMacField ready. Enter the scene to use the graphics menu.');return
        time.sleep(.5)
    raise RuntimeError(f'Runtime not ready; inspect {LOG}.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--app', type=Path, help='Exact PlayCover app bundle')
    parser.add_argument('--region', choices=tuple(CLIENTS), default='cn')
    sub = parser.add_subparsers(dest='action', required=True)
    sub.add_parser('check');sub.add_parser('build');sub.add_parser('status')
    sub.add_parser('restore-settings', help='Restore only settings changed by this launcher')
    sub.add_parser('launch').add_argument('--restart', action='store_true')
    args = parser.parse_args()
    app = (args.app or DEFAULT_APP.with_name(CLIENTS[args.region]+'.app')).expanduser().resolve()
    try:
        bundle = plistlib.loads((app/'Info.plist').read_bytes()).get('CFBundleIdentifier') if (app/'Info.plist').exists() else CLIENTS[args.region]
        configure_runtime(bundle)
        if args.action == 'check':
            validate(app);print('Supported 1.6.3 client and PlayTools verified.')
        elif args.action == 'build':build()
        elif args.action == 'launch':launch(app, args.restart)
        elif args.action == 'status':print(json.dumps(command(dict(op='status')), indent=2, ensure_ascii=False))
        elif args.action == 'restore-settings':
            if running(app):raise RuntimeError('Close the game before restoring its PlayCover settings.')
            restore_display()
            restore_vegetation(LOG.parent)
    except (RuntimeError, OSError, subprocess.CalledProcessError, ValueError) as error:
        print(f'Error: {error}', file=sys.stderr);return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
