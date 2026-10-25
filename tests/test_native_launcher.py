"""Exercise client signature independence and version overrides with temporary clients."""
import hashlib
import json
from pathlib import Path
import platform
import plistlib
import shutil
import subprocess
import tempfile
import unittest

import bettermacfield as bmf


def digest(data):
    return hashlib.sha256(data).hexdigest()


@unittest.skipUnless(platform.system() == 'Darwin' and platform.machine() == 'arm64'
                     and shutil.which('xcrun'), 'requires macOS arm64 and Xcode command line tools')
class NativeLauncherCompatibility(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix='bmf-native-test-')
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.root = Path(cls.temporary.name)
        contents = cls.root/'BetterMacField.app/Contents'
        cls.resources = contents/'Resources'
        cls.resources.mkdir(parents=True)
        cls.executable = contents/'MacOS/BetterMacField'
        cls.executable.parent.mkdir()
        (contents/'Info.plist').write_bytes(plistlib.dumps(dict(
            CFBundleIdentifier='test.bmf.launcher', CFBundleExecutable='BetterMacField',
            CFBundlePackageType='APPL', CFBundleShortVersionString='test', CFBundleVersion='1')))
        result = subprocess.run([
            'xcrun', 'clang', '-fobjc-arc', '-arch', 'arm64', '-mmacosx-version-min=13.0',
            '-Wno-deprecated-declarations', '-Wno-incompatible-pointer-types',
            '-framework', 'AppKit', '-framework', 'Foundation',
            str(bmf.ROOT/'launcher/main.m'), '-o', str(cls.executable),
        ], capture_output=True, text=True)
        if result.returncode:
            raise RuntimeError(result.stderr)

    def setUp(self):
        self.app = self.root/'CustomerGame.app'
        self.app.mkdir(exist_ok=True)
        self.info = dict(CFBundleIdentifier='test.bmf.game.cn',
                         CFBundleShortVersionString='1.6.3', CFBundleVersion='fixture')
        self.client_files = {
            'Frameworks/UnityFramework.framework/UnityFramework': b'game fixture',
            'Data/Managed/Metadata/global-metadata.dat': b'metadata fixture',
        }
        for relative, data in self.client_files.items():
            path = self.app/relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        (self.resources/'features.json').write_text('{"fields":[]}')
        (self.resources/'libBetterMacField.dylib').write_bytes(b'runtime fixture')
        package = dict(
            # Legacy file hashes must also be ignored when a client is re-signed.
            clients={region: dict(bundleID='test.bmf.game.'+region, version='1.6.3',
                                  files={name: digest(data) for name, data in self.client_files.items()})
                     for region in ('cn', 'global')},
            runtimeSHA256=digest(b'runtime fixture'),
            payload={'features.json': digest((self.resources/'features.json').read_bytes())})
        (self.resources/'clients.json').write_text(json.dumps(package))

    def check(self, force=False, region='cn'):
        (self.app/'Info.plist').write_bytes(plistlib.dumps(self.info))
        before = {str(path.relative_to(self.app)): digest(path.read_bytes())
                  for path in self.app.rglob('*') if path.is_file()}
        arguments = [str(self.executable), '--check', '--region', region, '--app', str(self.app)]
        result = subprocess.run(arguments + (['--force'] if force else []),
                                capture_output=True, text=True)
        after = {str(path.relative_to(self.app)): digest(path.read_bytes())
                 for path in self.app.rglob('*') if path.is_file()}
        self.assertEqual(before, after, 'validation must not modify the client')
        return result

    def test_supported_clients_pass(self):
        for region in ('cn', 'global'):
            with self.subTest(region=region):
                self.info['CFBundleIdentifier'] = 'test.bmf.game.'+region
                result = self.check(region=region)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn('Verified', result.stdout)

    def test_client_file_differences_pass_without_override(self):
        for relative in self.client_files:
            (self.app/relative).write_bytes(b'different file')
        for region in ('cn', 'global'):
            with self.subTest(region=region):
                self.info['CFBundleIdentifier'] = 'test.bmf.game.'+region
                result = self.check(region=region)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn('Verified', result.stdout)
                self.assertEqual(result.stderr, '')

    def test_client_profiles_need_no_hash_manifest(self):
        path = self.resources/'clients.json'
        package = json.loads(path.read_text())
        for client in package['clients'].values():
            client.pop('files')
        path.write_text(json.dumps(package))
        result = self.check()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('Verified', result.stdout)

    def test_force_accepts_older_and_newer_versions(self):
        for version in ('1.6.2', '1.6.4'):
            with self.subTest(version=version):
                self.info['CFBundleShortVersionString'] = version
                result = self.check()
                self.assertEqual(result.returncode, 1)
                self.assertIn(version, result.stderr)
                self.assertIn('仍然启动', result.stderr)
                result = self.check(force=True)
                self.assertEqual(result.returncode, 0, result.stderr)

    def test_force_still_checks_packaged_runtime(self):
        (self.resources/'libBetterMacField.dylib').write_bytes(b'incomplete runtime')
        result = self.check(force=True)
        self.assertEqual(result.returncode, 1)
        self.assertIn('增强组件', result.stderr)

    def test_force_still_checks_packaged_graphics(self):
        (self.resources/'features.json').write_text('{"fields":[],"changed":true}')
        result = self.check(force=True)
        self.assertEqual(result.returncode, 1)
        self.assertIn('画质组件', result.stderr)


if __name__ == '__main__':
    unittest.main()
