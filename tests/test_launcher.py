import hashlib
import json
from pathlib import Path
import plistlib
import tempfile
import unittest
from unittest.mock import patch
import bettermacfield as bmf


class LauncherBoundaries(unittest.TestCase):
    def test_cn_uses_cn_binary_identity(self):
        with tempfile.TemporaryDirectory() as folder:
            app = Path(folder)
            (app/'Info.plist').write_bytes(plistlib.dumps(dict(CFBundleIdentifier=bmf.CLIENTS['cn'], CFBundleShortVersionString='1.6.3')))
            (app/'test-code').write_bytes(b'cn')
            with patch.object(bmf.platform, 'system', return_value='Darwin'), patch.object(bmf.platform, 'machine', return_value='arm64'), patch.object(bmf, 'EXPECTED', {'test-code': hashlib.sha256(b'global').hexdigest()}), patch.object(bmf, 'CN_EXPECTED', {'test-code': hashlib.sha256(b'cn').hexdigest()}), patch.object(bmf.subprocess, 'check_output', return_value='PlayTools.framework/PlayTools'):
                self.assertEqual(bmf.validate(app)['CFBundleIdentifier'], bmf.CLIENTS['cn'])
                (app/'test-code').write_bytes(b'global')
                with self.assertRaisesRegex(RuntimeError, 'identity changed'):
                    bmf.validate(app)

    def test_regions_keep_runtime_and_restore_records_separate(self):
        with tempfile.TemporaryDirectory() as folder, patch.object(bmf.Path, 'home', return_value=Path(folder)), patch.object(bmf, 'STATE', Path(folder)/'state'), patch.object(bmf, 'LOG'), patch.object(bmf, 'COMMAND'), patch.object(bmf, 'STATUS'), patch.object(bmf, 'RESTORE'):
            global_root = bmf.configure_runtime(bmf.CLIENTS['global'])
            global_command, global_restore = bmf.COMMAND, bmf.RESTORE
            cn_root = bmf.configure_runtime(bmf.CLIENTS['cn'])
            self.assertNotEqual(global_root, cn_root)
            self.assertNotEqual(global_command, bmf.COMMAND)
            self.assertNotEqual(global_restore, bmf.RESTORE)
            self.assertIn('/com.hypergryph.endfield/Data/tmp/', str(cn_root))
            with self.assertRaisesRegex(RuntimeError, 'Unknown client'):
                bmf.configure_runtime('unrelated.app')

    def test_changed_binary_stops_before_dependency_inspection(self):
        with tempfile.TemporaryDirectory() as folder:
            app = Path(folder)/'client.app';app.mkdir()
            (app/'Info.plist').write_bytes(plistlib.dumps(dict(CFBundleIdentifier='com.gryphline.endfield.ios', CFBundleShortVersionString='1.6.3')))
            (app/'test-code').write_bytes(b'changed')
            with patch.object(bmf.platform, 'system', return_value='Darwin'), patch.object(bmf.platform, 'machine', return_value='arm64'), patch.object(bmf, 'EXPECTED', {'test-code': hashlib.sha256(b'expected').hexdigest()}), patch.object(bmf.subprocess, 'check_output') as run:
                with self.assertRaisesRegex(RuntimeError, 'identity changed'):
                    bmf.validate(app)
                run.assert_not_called()

    def test_unknown_version_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            app = Path(folder)
            (app/'Info.plist').write_bytes(plistlib.dumps(dict(CFBundleIdentifier='com.gryphline.endfield.ios', CFBundleShortVersionString='9.9.9')))
            with patch.object(bmf.platform, 'system', return_value='Darwin'), patch.object(bmf.platform, 'machine', return_value='arm64'):
                with self.assertRaisesRegex(RuntimeError, '1.6.3 only'):
                    bmf.validate(app)

    def test_single_restore_record_preserves_original_and_later_user_changes(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder);app = root/'Applications/com.gryphline.endfield.ios.app';app.mkdir(parents=True)
            settings = root/'App Settings/com.gryphline.endfield.ios.plist';settings.parent.mkdir()
            settings.write_bytes(plistlib.dumps(dict(customScaler=1.0, keymapping=True, unrelated='keep')))
            state = root/'state';restore = state/'playcover-settings.json'
            with patch.object(bmf, 'STATE', state), patch.object(bmf, 'RESTORE', restore):
                bmf.apply_display(app);original = restore.read_bytes()
                bmf.apply_display(app)
                self.assertEqual(restore.read_bytes(), original)
                current = plistlib.loads(settings.read_bytes());current['resolution'] = 3
                settings.write_bytes(plistlib.dumps(current))
                bmf.restore_display()
                actual = plistlib.loads(settings.read_bytes())
                self.assertEqual(actual, dict(customScaler=1.0, keymapping=True, unrelated='keep', resolution=3))
                self.assertFalse(restore.exists())

    def test_second_installation_cannot_overwrite_restore_record(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder);state = root/'state';state.mkdir();restore = state/'playcover-settings.json'
            original = json.dumps(dict(path='/another-installation/settings.plist', original={}, absent=[]))
            restore.write_text(original)
            with patch.object(bmf, 'STATE', state), patch.object(bmf, 'RESTORE', restore):
                with self.assertRaisesRegex(RuntimeError, 'previous PlayCover settings'):
                    bmf.apply_display(root/'Applications/client.app')
                self.assertEqual(restore.read_text(), original)


if __name__ == '__main__':
    unittest.main()
