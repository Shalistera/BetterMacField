#!/usr/bin/env python3
"""Maintainer CLI for the global iOS 1.7.2 native launcher."""
import argparse
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parent
BUILD = ROOT/'build'
CLIENTS = {'global': 'com.gryphline.endfield.ios'}
DEFAULT_APP = Path.home()/'Library/Containers/io.playcover.PlayCover/Applications/com.gryphline.endfield.ios.app'
DISPLAY_SETTINGS = dict(customScaler=2.0, resolution=6, resizableAspectRatioWidth=16,
                        resizableAspectRatioHeight=9, keymapping=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--app', type=Path, default=DEFAULT_APP, help='Exact PlayCover app bundle')
    parser.add_argument('--region', choices=['global'], default='global')
    actions = parser.add_subparsers(dest='action', required=True)
    for name in ['check', 'build', 'status', 'restore-settings']:
        actions.add_parser(name)
    actions.add_parser('launch').add_argument('--restart', action='store_true')
    args = parser.parse_args()
    try:
        if args.action == 'build':
            from build_release import build_current
            build_current(BUILD, include_assets=False)
            print('Built 1.7.2 runtime components at', BUILD)
            return 0
        executable = ROOT/'dist/BetterMacField.app/Contents/MacOS/BetterMacField'
        if not executable.is_file():
            raise RuntimeError('Prepare the rendering export, then run build_release.py first.')
        command = [str(executable), '--'+args.action, '--region', 'global',
                   '--app', str(args.app.expanduser().resolve())]
        if getattr(args, 'restart', False):
            command.append('--restart')
        subprocess.run(command, check=True)
    except (RuntimeError, OSError, subprocess.CalledProcessError, ValueError) as error:
        print(f'Error: {error}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
