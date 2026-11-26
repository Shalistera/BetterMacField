"""Run the public current-client protocol and input logic without loading a game."""
from pathlib import Path
import platform
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


@unittest.skipUnless(platform.system() == 'Darwin' and platform.machine() == 'arm64'
                     and shutil.which('xcrun'), 'requires macOS arm64 and Xcode command line tools')
class CurrentRuntimeProtocol(unittest.TestCase):
    def test_public_settings_output_ownership_and_zipline_queries(self):
        with tempfile.TemporaryDirectory(prefix='bmf-current-runtime-test-') as directory:
            executable = Path(directory) / 'runtime-check'
            result = subprocess.run([
                'xcrun', 'clang', '-fobjc-arc', '-fblocks', '-target', 'arm64-apple-macos13.0',
                '-framework', 'AppKit', '-framework', 'Foundation', '-framework', 'Metal',
                '-weak_framework', 'MetalFX', '-framework', 'CoreGraphics',
                str(ROOT / 'tests/current_runtime172.m'), '-o', str(executable),
            ], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([str(executable)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn('PASS: public settings and commands', result.stdout)


if __name__ == '__main__':
    unittest.main()
