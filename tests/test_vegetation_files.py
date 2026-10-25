"""Exercise file recovery independently of the game and its resource data."""
from pathlib import Path
import platform
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


@unittest.skipUnless(platform.system() == 'Darwin', 'Foundation recovery helper')
class VegetationFilesTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build = tempfile.TemporaryDirectory()
        source = Path(cls.build.name)/'recovery.m'
        source.write_text('''#import <Foundation/Foundation.h>
#include <CommonCrypto/CommonDigest.h>
#include "vegetation_files.inc"
int main(int argc, const char **argv) { @autoreleasepool {
    NSString *root=@(argv[2]), *error;
    if(!strcmp(argv[1],"stage")) error=vegetation_file_stage(root,@(argv[3]),[@"replacement" dataUsingEncoding:NSUTF8StringEncoding]);
    else error=vegetation_files_restore(root);
    if(error){fprintf(stderr,"%s",error.UTF8String);return 1;}
    return 0;
}}
''')
        cls.exe = Path(cls.build.name)/'recovery'
        subprocess.run(['xcrun', 'clang', '-fobjc-arc', '-Wno-deprecated-declarations',
                        '-framework', 'Foundation', '-I', str(ROOT/'src'),
                        str(source), '-o', str(cls.exe)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.build.cleanup()

    def run_helper(self, *args):
        return subprocess.run([str(self.exe), *map(str, args)], capture_output=True, text=True)

    def test_original_and_absent_files_recover_after_restart(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)/'Data/tmp/BetterMacField'
            root.mkdir(parents=True)
            recovery = Path(folder)/'Data/Library/Application Support/BetterMacField'
            old, new = Path(folder)/'old.bytes', Path(folder)/'new.bytes'
            old.write_bytes(b'previous overlay')
            for path in (old, new):
                result = self.run_helper('stage', root, path)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(path.read_bytes(), b'replacement')
            shutil.rmtree(root.parent)
            result = self.run_helper('restore', root)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(old.read_bytes(), b'previous overlay')
            self.assertFalse(new.exists())
            self.assertFalse((recovery/'vegetation-files.plist').exists())
            self.assertEqual(self.run_helper('restore', root).returncode, 0)

    def test_later_changes_keep_recovery_journal(self):
        with tempfile.TemporaryDirectory() as folder:
            root, path = Path(folder)/'runtime', Path(folder)/'chunk.bytes'
            path.write_bytes(b'previous overlay')
            self.assertEqual(self.run_helper('stage', root, path).returncode, 0)
            path.write_bytes(b'later edit')
            self.assertEqual(self.run_helper('restore', root).returncode, 1)
            self.assertEqual(path.read_bytes(), b'later edit')
            self.assertTrue((root/'vegetation-files.plist').exists())
            self.assertEqual((root/'VegetationBackup/0').read_bytes(), b'previous overlay')
