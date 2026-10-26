"""Keep Frame atlas coordinates paired with pixels across UI transitions."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
ADAPTER = ROOT / 'packages/quest-runtime/linux/prepare-frame-controls.sh'
NODE = shutil.which('node') or shutil.which('node.exe')

class FrameControlsTest(unittest.TestCase):
    def source(self, lower, legacy=False):
        first, second = ('f', 'F') if lower else ('F', 'f')
        binding = f'const {first}=this.props.overlay_key,{second}=(0,h.w5)();'
        index = 'this.m_nEmbeddedIndex'
        if legacy:
            binding = binding.replace('(0,h.w5)()', 'document.title==="systemui"?"system.systemui":(0,h.w5)()')
            index = 'document.title==="systemui"?void 0:this.m_nEmbeddedIndex'
        return ('function build(){let y={properties:{}};' + binding +
                f'y.properties.key={first}||{second};' +
                'y.properties["embedded-uv-index"]=' + index + ';' +
                'y.properties.uv_min=this.props.uv_min;y.properties.uv_max=this.props.uv_max;return y.properties;}')

    def prepare(self, path):
        return subprocess.run(['sh', str(ADAPTER)],
                              env=dict(os.environ, STEARLIGHT_STEAMVR_ROOT=str(path)),
                              capture_output=True, text=True)

    def test_stock_and_existing_install_keep_pixel_coordinates_and_external_bounds(self):
        for lower in (False, True):
            for legacy in (False, True):
                with self.subTest(lower=lower, legacy=legacy), tempfile.TemporaryDirectory() as folder:
                    root = Path(folder)
                    panel = root / 'resources/webinterface/dashboard/chunk~1a88854fa.js'
                    panel.parent.mkdir(parents=True)
                    panel.write_text(self.source(lower, legacy))
                    panel.chmod(0o640)
                    result = self.prepare(root)
                    self.assertEqual(result.returncode, 0, result.stderr)
                    first = panel.read_bytes()
                    stat = panel.stat()
                    self.assertEqual(stat.st_mode & 0o777, 0o640)
                    self.assertEqual(self.prepare(root).returncode, 0)
                    self.assertEqual(panel.read_bytes(), first)
                    self.assertEqual(panel.stat().st_ino, stat.st_ino)
                    self.assertEqual(panel.stat().st_mtime_ns, stat.st_mtime_ns)
                    if NODE:
                        code = 'const assert=require("assert");let document={title:"systemui"},h={w5:()=>"native-page"};' + first.decode() + '''
const min={u:.2,v:.3},max={u:.8,v:.9};
const internal=build.call({props:{uv_min:min,uv_max:max},m_nEmbeddedIndex:7});
assert.equal(internal.key,"system.systemui");assert.equal(internal["embedded-uv-index"],7);
const external=build.call({props:{overlay_key:"external-steam",uv_min:min,uv_max:max},m_nEmbeddedIndex:4});
assert.equal(external.key,"external-steam");assert.equal(external["embedded-uv-index"],undefined);
assert.strictEqual(external.uv_min,min);assert.strictEqual(external.uv_max,max);
document.title="controllerbindingui";
const other=build.call({props:{},m_nEmbeddedIndex:3});
assert.equal(other.key,"native-page");assert.equal(other["embedded-uv-index"],3);
'''
                        subprocess.run([NODE, '-e', code], check=True)
                    else:
                        self.assertIn(b'&&this.props.overlay_key?void 0:this.m_nEmbeddedIndex', first)

    def test_unknown_or_ambiguous_runtime_is_not_partially_modified(self):
        for source in ('unknown runtime', self.source(True) * 2):
            with self.subTest(source=source[:30]), tempfile.TemporaryDirectory() as folder:
                root = Path(folder)
                panel = root / 'resources/webinterface/dashboard/chunk~1a88854fa.js'
                panel.parent.mkdir(parents=True)
                panel.write_text(source)
                self.assertNotEqual(self.prepare(root).returncode, 0)
                self.assertEqual(panel.read_text(), source)
                self.assertFalse(Path(str(panel) + '.quest-controls').exists())

if __name__ == '__main__':
    unittest.main()
