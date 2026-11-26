"""Validate current-client identity, packaged payloads, and reversible settings."""
import hashlib
import json
from pathlib import Path
import platform
import plistlib
import shutil
import struct
import subprocess
import tempfile
import unittest
import uuid

ROOT = Path(__file__).resolve().parents[1]
UNITY_UUID = uuid.UUID('0f92770a-6938-397f-a0f2-272e994ba286')
BUNDLE_ID = 'com.gryphline.endfield.ios'
CN_UUID = uuid.UUID('5e01e569-4b4d-35aa-b5d1-8f3f97c5278a')
CN_ID = 'com.hypergryph.endfield'


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
        common = ['xcrun', 'clang', '-fobjc-arc', '-arch', 'arm64', '-mmacosx-version-min=13.0',
                  '-Wno-deprecated-declarations', '-Wno-incompatible-pointer-types',
                  '-framework', 'AppKit', '-framework', 'Foundation']
        cls.home = cls.root/'home'
        source = cls.root/'launcher.m'
        source.write_text('#import <AppKit/AppKit.h>\n#define NSHomeDirectory() @'
                          + json.dumps(str(cls.home)) + '\n#define main launcher_main\n#include '
                          + json.dumps(str(ROOT/'launcher/main.m')) + '\n#undef main\n' + r'''
int main(int argc,const char **argv) {
    if (argc > 1 && !strcmp(argv[1], "--fixture-restore")) {
        @autoreleasepool { @try {
            selectRegion172(NSProcessInfo.processInfo.arguments,NO);
            displaySettings(clientPath(),restoreRecord(),@{},YES);
            return 0;
        } @catch(NSException *e) { fprintf(stderr,"%s\n",e.reason.UTF8String); return 1; } }
    }
    return launcher_main(argc,argv);
}
''')
        result = subprocess.run(common + [str(source), '-o', str(cls.executable)],
                                capture_output=True, text=True)
        if result.returncode:
            raise RuntimeError(result.stderr)
        # Exercise the production restore function against temporary files only.
        harness = cls.root/'restore.m'
        harness.write_text('#define main launcher_main\n#include '
                           + json.dumps(str(ROOT/'launcher/main.m')) + '\n#undef main\n' + r'''
int main(int argc,const char **argv) {
    @autoreleasepool {
        @try {
            displaySettings(@(argv[1]), @(argv[2]), readJSON(@(argv[3])), !strcmp(argv[4], "restore"));
            return 0;
        } @catch(NSException *e) { fprintf(stderr,"%s\n",e.reason.UTF8String);return 1; }
    }
}
''')
        cls.restore_executable = cls.root/'restore'
        result = subprocess.run(common + [str(harness), '-o', str(cls.restore_executable)],
                                capture_output=True, text=True)
        if result.returncode:
            raise RuntimeError(result.stderr)

    def setUp(self):
        self.app = self.root/'CustomerGame.app'
        self.app.mkdir(exist_ok=True)
        self.info = dict(CFBundleIdentifier=BUNDLE_ID, CFBundleShortVersionString='1.7.2',
                         CFBundleVersion='0', CFBundleExecutable='Endfield')
        self.unity = self.app/'Frameworks/UnityFramework.framework/UnityFramework'
        self.unity.parent.mkdir(parents=True, exist_ok=True)
        self.code = code = b'test instructions'
        self.cn_code = b'CN!! instructions'
        cmdsize = 24 + 72 + 80
        offset = 32 + cmdsize
        uuid_cmd = struct.pack('<II16s', 0x1b, 24, UNITY_UUID.bytes)
        segment = struct.pack('<II16sQQQQiiII', 0x19, 152, b'__TEXT', 0, offset+len(code),
                              0, offset+len(code), 5, 5, 1, 0)
        section = struct.pack('<16s16sQQ8I', b'__text', b'__TEXT', offset, len(code), offset, *([0]*7))
        self.unity.write_bytes(struct.pack('<IiiIIIII', 0xfeedfacf, 0x100000c, 0, 6, 2, cmdsize, 0, 0)
                               + uuid_cmd + segment + section + code)
        self.metadata = self.app/'Data/Managed/Metadata/global-metadata.dat'
        self.metadata.parent.mkdir(parents=True, exist_ok=True)
        self.metadata.write_bytes(b'metadata fixture')
        self.payload = {
            'features.json': b'{"fields":[]}',
            'BetterMacField172.dylib': b'entry fixture',
            'BetterMacFieldGraphics.bundle/Contents/Info.plist': plistlib.dumps(
                dict(CFBundleExecutable='BetterMacFieldGraphics')),
            'BetterMacFieldGraphics.bundle/Contents/MacOS/BetterMacFieldGraphics': b'graphics fixture',
        }
        for name, data in self.payload.items():
            path = self.resources/name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        self.package = dict(
            clients={region: dict(bundleID=bundle, version='1.7.2', build='0', unityUUID=str(uid),
                                  metadataSHA256=digest(self.metadata.read_bytes()), textSHA256=digest(text))
                     for region, bundle, uid, text in [('global', BUNDLE_ID, UNITY_UUID, code),
                                                       ('cn', CN_ID, CN_UUID, self.cn_code)]},
            payload={name: digest(data) for name, data in self.payload.items()})
        self.save_package()
        self.restore_root = self.root/'restore-fixture'
        shutil.rmtree(self.restore_root, ignore_errors=True)
        self.restore_app = self.restore_root/'Applications/Game.app'
        self.restore_app.mkdir(parents=True)
        self.settings_path = self.restore_root/'App Settings/Game.plist'
        self.settings_path.parent.mkdir()
        self.record = self.restore_root/'restore.json'

    def save_package(self):
        (self.resources/'clients.json').write_text(json.dumps(self.package))

    def check(self, *extra):
        (self.app/'Info.plist').write_bytes(plistlib.dumps(self.info))
        before = {str(path.relative_to(self.app)): digest(path.read_bytes())
                  for path in self.app.rglob('*') if path.is_file()}
        result = subprocess.run([str(self.executable), '--check', '--app', str(self.app), *extra],
                                capture_output=True, text=True)
        after = {str(path.relative_to(self.app)): digest(path.read_bytes())
                 for path in self.app.rglob('*') if path.is_file()}
        self.assertEqual(before, after, 'validation must not modify the client')
        return result

    def display(self, desired, restore=False, app=None):
        config = self.restore_root/'desired.json'
        config.write_text(json.dumps(desired))
        return subprocess.run([str(self.restore_executable), str(app or self.restore_app),
                               str(self.record), str(config), 'restore' if restore else 'apply'],
                              capture_output=True, text=True)

    def test_default_and_explicit_global_pass(self):
        for extra in ((), ('--region', 'global')):
            with self.subTest(extra=extra):
                result = self.check(*extra)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn('Verified global 1.7.2', result.stdout)

    def test_cn_requires_its_own_unity_code(self):
        self.info['CFBundleIdentifier'] = CN_ID
        self.assertNotEqual(self.check('--region', 'cn').returncode, 0)
        self.unity.write_bytes(self.unity.read_bytes().replace(UNITY_UUID.bytes, CN_UUID.bytes))
        self.assertNotEqual(self.check('--region', 'cn').returncode, 0, 'the CN UUID alone is insufficient')
        self.unity.write_bytes(self.unity.read_bytes().replace(self.code, self.cn_code))
        for extra in ((), ('--region', 'cn')):
            result = self.check(*extra)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn('Verified cn 1.7.2', result.stdout)
        self.assertNotEqual(self.check('--region', 'global').returncode, 0)

    def test_region_selection_and_restore_use_the_selected_install(self):
        playcover = self.home/'Library/Containers/io.playcover.PlayCover'
        state = self.home/'Library/Application Support/BetterMacField'
        state.mkdir(parents=True)
        installs = playcover/'Applications'
        settings = playcover/'App Settings'
        settings.mkdir(parents=True)
        def run(*args):
            return subprocess.run([str(self.executable), *args], capture_output=True, text=True)

        self.assertNotEqual(run('--check').returncode, 0)
        for bundle, region, record_name in [(CN_ID, 'cn', 'playcover-settings-cn.json'),
                                            (BUNDLE_ID, 'global', 'playcover-settings.json')]:
            app = installs/(bundle+'.app')
            shutil.copytree(self.app, app)
            (app/'Info.plist').write_bytes(plistlib.dumps(dict(self.info, CFBundleIdentifier=bundle)))
            if region == 'cn':
                framework = app/self.unity.relative_to(self.app)
                framework.write_bytes(framework.read_bytes().replace(UNITY_UUID.bytes, CN_UUID.bytes)
                                      .replace(self.code, self.cn_code))
                self.assertIn('Verified cn', run('--check').stdout)
            plist = settings/(bundle+'.plist')
            plist.write_bytes(plistlib.dumps(dict(keymapping=False)))
            (state/record_name).write_text(json.dumps(dict(path=str(plist), original=dict(keymapping=True),
                                                          absent=[], desired=dict(keymapping=False))))
        self.assertIn('Both clients', run('--check').stderr)
        self.assertIn('Verified global', run('--check', '--region', 'global').stdout)
        global_plist, cn_plist = (settings/(bundle+'.plist') for bundle in (BUNDLE_ID, CN_ID))
        global_record = state/'playcover-settings.json'
        before = global_plist.read_bytes(), global_record.read_bytes()
        result = run('--fixture-restore', '--region', 'cn')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(plistlib.loads(cn_plist.read_bytes()), dict(keymapping=True))
        self.assertFalse((state/'playcover-settings-cn.json').exists())
        self.assertEqual((global_plist.read_bytes(), global_record.read_bytes()), before)

    def test_current_identity_survives_signature_changes(self):
        thin = self.unity.read_bytes()
        for signature in (b'first signature', b'another signing identity'):
            with self.subTest(signature=signature):
                self.unity.write_bytes(thin + signature)
                result = self.check()
                self.assertEqual(result.returncode, 0, result.stderr)

    def test_current_universal_macho_identity(self):
        thin = self.unity.read_bytes()
        fat = struct.pack('>IIiiIII', 0xcafebabe, 1, 0x100000c, 0, 64, len(thin), 4)
        self.unity.write_bytes(fat.ljust(64, b'\0') + thin)
        result = self.check()
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_metadata_uuid_and_malformed_commands_are_rejected(self):
        original = self.unity.read_bytes()
        for changed in ('metadata', 'uuid', 'text', 'malformed'):
            with self.subTest(changed=changed):
                self.unity.write_bytes(original)
                self.metadata.write_bytes(b'metadata fixture')
                if changed == 'metadata':
                    self.metadata.write_bytes(b'new game metadata')
                else:
                    value = bytearray(original)
                    if changed == 'uuid':
                        value[40:56] = uuid.uuid4().bytes
                    elif changed == 'text':
                        value[-1] ^= 1
                    else:
                        value[36:40] = struct.pack('<I', 0xffffffff)
                    self.unity.write_bytes(value)
                result = self.check()
                self.assertEqual(result.returncode, 1)
                self.assertIn('代码', result.stderr)

    def test_unadapted_versions_regions_and_force_are_rejected(self):
        for version in ('1.6.3', '1.6.4', '1.7.1', '1.7.3'):
            with self.subTest(version=version):
                self.info['CFBundleShortVersionString'] = version
                result = self.check()
                self.assertEqual(result.returncode, 1)
                self.assertIn('尚未适配', result.stderr)
        self.info['CFBundleShortVersionString'] = '1.7.2'
        for extra in (('--region', 'cn'), ('--region', 'unknown'), ('--force',)):
            with self.subTest(extra=extra):
                self.assertEqual(self.check(*extra).returncode, 1)
        self.info['CFBundleIdentifier'] = 'com.hypergryph.endfield'
        self.assertEqual(self.check().returncode, 1)

    def test_changed_build_is_rejected(self):
        self.info['CFBundleVersion'] = '1'
        result = self.check()
        self.assertEqual(result.returncode, 1)
        self.assertIn('代码', result.stderr)

    def test_each_packaged_payload_is_checked(self):
        for name, data in self.payload.items():
            with self.subTest(name=name):
                path = self.resources/name
                path.write_bytes(b'{"fields":[],"changed":true}' if name == 'features.json'
                                 else data + b'changed')
                try:
                    result = self.check()
                    self.assertEqual(result.returncode, 1)
                    self.assertIn('画质组件', result.stderr)
                finally:
                    path.write_bytes(data)

    def test_incomplete_payload_manifest_is_rejected(self):
        self.package['payload'].pop('BetterMacField172.dylib')
        self.save_package()
        result = self.check()
        self.assertEqual(result.returncode, 1)
        self.assertIn('校验清单', result.stderr)

    def test_restore_preserves_manual_edits_and_boolean_types(self):
        original = dict(customScaler=1.0, keymapping=True, BlockSleepSpamming=False)
        self.settings_path.write_bytes(plistlib.dumps(original))
        desired = dict(customScaler=2.0, keymapping=False, metalHUD=False)
        result = self.display(desired)
        self.assertEqual(result.returncode, 0, result.stderr)
        current = plistlib.loads(self.settings_path.read_bytes())
        self.assertIs(current['BlockSleepSpamming'], False)
        current['keymapping'] = True  # A subsequent manual change must survive restore.
        self.settings_path.write_bytes(plistlib.dumps(current))
        result = self.display(desired, restore=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(plistlib.loads(self.settings_path.read_bytes()), original)
        self.assertFalse(self.record.exists())

    def test_restore_captures_new_options_without_overwriting_prior_originals(self):
        original = dict(customScaler=1.0, metalHUD=True)
        self.settings_path.write_bytes(plistlib.dumps(original))
        self.assertEqual(self.display(dict(customScaler=2.0)).returncode, 0)
        self.assertEqual(self.display(dict(customScaler=3.0, metalHUD=False)).returncode, 0)
        self.assertEqual(self.display({}, restore=True).returncode, 0)
        self.assertEqual(plistlib.loads(self.settings_path.read_bytes()), original)

    def test_restore_rejects_another_install_and_leaves_files_unchanged(self):
        self.settings_path.write_bytes(plistlib.dumps(dict(customScaler=1.0)))
        self.assertEqual(self.display(dict(customScaler=2.0)).returncode, 0)
        before, record = self.settings_path.read_bytes(), self.record.read_bytes()
        result = self.display({}, restore=True, app=self.restore_root/'Applications/Other.app')
        self.assertEqual(result.returncode, 1)
        self.assertIn('另一份安装', result.stderr)
        self.assertEqual(self.settings_path.read_bytes(), before)
        self.assertEqual(self.record.read_bytes(), record)

    def test_restore_without_a_record_does_not_create_settings(self):
        self.assertEqual(self.display({}, restore=True).returncode, 0)
        self.assertFalse(self.settings_path.exists())


if __name__ == '__main__':
    unittest.main()
