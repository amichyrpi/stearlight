"""Exercise APK overlay extraction with and without an installed OS update."""
import io
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import tempfile
import unittest


class RootfsArchiveTest(unittest.TestCase):
    def test_overlay_preserves_updated_runtime_and_extracts_following_entries(self):
        java_home = os.environ.get('JAVA_HOME')
        javac = str(Path(java_home) / 'bin/javac') if java_home else shutil.which('javac')
        java = str(Path(java_home) / 'bin/java') if java_home else shutil.which('java')
        self.assertTrue(javac and java, 'JDK 17 is required')
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            package = root / 'net/stearlight/quest'
            android = root / 'android/system'
            package.mkdir(parents=True)
            android.mkdir(parents=True)
            source = Path(__file__).resolve().parents[1] / 'app/src/main/java/net/stearlight/quest/RootfsArchive.java'
            shutil.copy2(source, package / source.name)
            (android / 'Os.java').write_text('''package android.system;
public class Os {
 public static void chmod(String p,int m) {} 
 public static void symlink(String a,String b) throws Exception {throw new Exception("unexpected symlink");}
 public static void link(String a,String b) throws ErrnoException {throw new ErrnoException();}
 public static Stat stat(String p) {return new Stat();}
 public static class Stat {public int st_mode=0644;}
}''')
            (android / 'ErrnoException.java').write_text('package android.system; public class ErrnoException extends Exception {public int errno;}')
            (android / 'OsConstants.java').write_text('package android.system; public class OsConstants {public static int EACCES=13,EPERM=1;}')
            (package / 'ArchiveTest.java').write_text('''package net.stearlight.quest;
import java.io.*;
public class ArchiveTest {
 public static void main(String[] args) throws Exception {
  RootfsArchive.extract(new FileInputStream(args[0]),new File(args[1]),Boolean.parseBoolean(args[2]));
 }
}''')
            archive = root / 'overlay.tar.gz'
            runtime_name = 'opt/steamvr/resources/' + 'long-path-' * 14 + '/panel.js'
            with tarfile.open(archive, 'w:gz', format=tarfile.PAX_FORMAT) as output:
                for name, data in ((runtime_name, b'bundled runtime' * 37),
                                   ('opt/quest/start-session.sh', b'new launcher')):
                    entry = tarfile.TarInfo(name)
                    entry.size = len(data)
                    output.addfile(entry, io.BytesIO(data))
            subprocess.run([javac, '-d', str(root), *map(str, root.rglob('*.java'))], check=True)
            for preserve in (False, True):
                destination = root / str(preserve)
                runtime = destination / runtime_name
                runtime.parent.mkdir(parents=True)
                runtime.write_bytes(b'installed OS runtime')
                subprocess.run([java, '-cp', str(root), 'net.stearlight.quest.ArchiveTest',
                                str(archive), str(destination), str(preserve).lower()], check=True)
                expected = b'installed OS runtime' if preserve else b'bundled runtime' * 37
                self.assertEqual(runtime.read_bytes(), expected)
                self.assertEqual((destination / 'opt/quest/start-session.sh').read_bytes(), b'new launcher')


if __name__ == '__main__':
    unittest.main()
