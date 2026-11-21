import importlib.util
from pathlib import Path
import subprocess
import unittest

repo = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location('payload', repo / 'build_files/quest/prepare-payload.py')
payload = importlib.util.module_from_spec(spec)
spec.loader.exec_module(payload)


class FloorTest(unittest.TestCase):
    def test_all_native_floor_branches_follow_room_view(self):
        branches = [
            b'o&&ne.JJ.isTheaterMode&&r.createElement(s.B4o)',
            b'o&&c.H.m_bShowFloor&&(l||ne.JJ.isTheaterMode)&&r.createElement(s.B4o)',
            b'o&&c.H.m_bShowFloorFar&&!ne.JJ.isTheaterMode&&!l&&r.createElement(s.B4o)',
        ]
        latest = [b'r&&de.JJ.isTheaterMode&&n.createElement(s.B4o)',
                  b'r&&c.H.m_bShowFloor&&n.createElement(s.B4o)',
                  b'r&&c.H.m_bShowFloorFar&&!de.JJ.isTheaterMode&&!l&&n.createElement(s.B4o)']
        script = '''
const assert=require('node:assert/strict');
const o=true,s={B4o:0},r={createElement:()=>true};
const ne={JJ:{isTheaterMode:false}},c={H:{m_bShowFloor:true,m_bShowFloorFar:true}};
const n=r,de=ne;
let l=false;
globalThis.RoomViewStore={roomViewShowing:false,roomViewEnabled:false};
const evaluate=()=>[EXPRESSIONS];
for(const theater of [false,true]){
 ne.JJ.isTheaterMode=theater;
 for(const showing of [false,true])for(const enabled of [false,true]){
  Object.assign(RoomViewStore,{roomViewShowing:showing,roomViewEnabled:enabled});
  const floors=evaluate();
  assert.equal(floors.some(Boolean),!(showing||enabled));
  assert.deepEqual(c.H,{m_bShowFloor:true,m_bShowFloorFar:true});
 }
}
'''
        for revision in (branches, latest):
            patched = payload.roomview_system_ui(b';'.join(revision)).decode()
            expressions = patched.replace(';', ',')
            subprocess.run(['node', '-e', script.replace('EXPRESSIONS', expressions)], check=True)

    def test_unknown_runtime_is_rejected(self):
        with self.assertRaises(RuntimeError):
            payload.roomview_system_ui(b'unknown runtime')


if __name__ == '__main__':
    unittest.main()
