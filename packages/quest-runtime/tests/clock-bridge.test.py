"""Exercise Android tzdata extraction and Linux timezone updates with a real JVM."""
import io
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import unittest
from datetime import datetime, timedelta
from zoneinfo import ZoneInfo


class ClockBridgeTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.classes = Path(cls.temporary.name)
        java_home = os.environ.get('JAVA_HOME')
        javac = str(Path(java_home) / 'bin/javac') if java_home else shutil.which('javac')
        cls.java = str(Path(java_home) / 'bin/java') if java_home else shutil.which('java')
        if not javac or not cls.java:
            raise RuntimeError('JDK 17 is required')
        harness = cls.classes / 'ClockTest.java'
        harness.write_text('''package net.stearlight.quest;
import java.io.*;
public class ClockTest {
 public static void main(String[] args)throws Exception {
  File root=new File(args[0]);
  ClockBridge bridge=new ClockBridge(root,new File[]{new File(args[1])});
  bridge.sync(args[2]);
  if(args.length>3){
   if(args[3].equals("repeat")){
    File rules=new File(root,"usr/share/zoneinfo/"+args[2]);
    if(!rules.setLastModified(1000))throw new AssertionError("mtime");
    bridge.sync(args[2]);
    if(rules.lastModified()!=1000)throw new AssertionError("Unchanged timezone rewritten");
   }else bridge.sync(args[3]);
  }
 }
}''')
        source = Path(__file__).resolve().parents[1] / 'app/src/main/java/net/stearlight/quest/ClockBridge.java'
        subprocess.run([javac, '-d', str(cls.classes), str(source), str(harness)], check=True)

    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.folder = Path(temporary.name)
        self.root = self.folder / 'root'
        self.root.mkdir()
        (self.root / '.stearlight-ready').touch()
        self.database = self.folder / 'tzdata'
        self.rules = {}
        for name, offset, abbreviation in [('Europe/Paris', 3600, b'CET\0'), ('Asia/Tokyo', 32400, b'JST\0')]:
            self.rules[name] = (b'TZif\0' + bytes(15) + struct.pack('>6I', 0, 0, 0, 0, 1, len(abbreviation))
                                + struct.pack('>iBB', offset, 0, 0) + abbreviation)
        data_offset = 24 + 52 * len(self.rules)
        index, data = bytearray(), bytearray()
        for name, rules in self.rules.items():
            index += name.encode().ljust(40, b'\0') + struct.pack('>3I', len(data), len(rules), 0)
            data += rules
        self.database.write_bytes(b'tzdata2023a\0' + struct.pack('>3I', 24, data_offset, data_offset + len(data)) + index + data)

    def run_bridge(self, *zones, check=True):
        result = subprocess.run([self.java, '-cp', str(self.classes), 'net.stearlight.quest.ClockTest',
                                 str(self.root), str(self.database), *zones], capture_output=True, text=True)
        if check:
            self.assertEqual(result.returncode, 0, result.stderr)
        return result

    def test_rules_localtime_link_and_unchanged_zone(self):
        self.run_bridge('Europe/Paris', 'repeat')
        local = self.root / 'etc/localtime'
        self.assertEqual(os.readlink(local), '../usr/share/zoneinfo/Europe/Paris')
        self.assertEqual(local.read_bytes(), self.rules['Europe/Paris'])
        self.assertEqual((self.root / 'etc/timezone').read_text(), 'Europe/Paris\n')
        zone = ZoneInfo.from_file(io.BytesIO(local.read_bytes()))
        self.assertEqual(datetime(2026, 1, 1, tzinfo=zone).utcoffset(), timedelta(hours=1))

    def test_timezone_change_replaces_link_and_keeps_old_rules(self):
        self.run_bridge('Europe/Paris', 'Asia/Tokyo')
        self.assertEqual((self.root / 'etc/localtime').read_bytes(), self.rules['Asia/Tokyo'])
        self.assertEqual((self.root / 'etc/timezone').read_text(), 'Asia/Tokyo\n')
        self.assertEqual((self.root / 'usr/share/zoneinfo/Europe/Paris').read_bytes(), self.rules['Europe/Paris'])

    def test_invalid_or_missing_zone_preserves_current_configuration(self):
        self.run_bridge('Europe/Paris')
        for zone in ('../outside', 'Unknown/Zone'):
            self.assertNotEqual(self.run_bridge(zone, check=False).returncode, 0)
            self.assertEqual((self.root / 'etc/localtime').read_bytes(), self.rules['Europe/Paris'])

    def test_corrupt_or_truncated_database_is_rejected(self):
        original = self.database.read_bytes()
        for blob in (b'bad', original[:24], original[:64], original[:12] + struct.pack('>3I', 24, 25, 25)):
            self.database.write_bytes(blob)
            self.assertNotEqual(self.run_bridge('Europe/Paris', check=False).returncode, 0)
            self.assertFalse((self.root / 'etc/localtime').exists())

    def test_waits_until_rootfs_installation_finishes(self):
        (self.root / '.stearlight-ready').unlink()
        self.run_bridge('Europe/Paris')
        self.assertFalse((self.root / 'etc').exists())


if __name__ == '__main__':
    unittest.main()
