import os,shutil,subprocess,tempfile,unittest
from pathlib import Path
REPO=Path(__file__).resolve().parents[3]
LINUX=REPO/'packages/quest-runtime/linux'
class ClientUpdateTest(unittest.TestCase):
 def test_update_restores_adapters_without_rewriting_unchanged_files(self):
  with tempfile.TemporaryDirectory() as folder:
   root=Path(folder)/'steam';quest=Path(folder)/'quest'
   (root/'steamrtarm64').mkdir(parents=True);(root/'steamui').mkdir();quest.mkdir()
   for name in ['prepare-store-browser.sh','store-browser-bridge.js','steamwebhelper-launcher.sh','steam-channel-args.sh']:
    shutil.copyfile(LINUX/name,quest/name)
   for name in ['prepare-cef.sh','frame-steam-ipc','prepare-streaming-client.sh']:(quest/name).write_text('exit 0\n')
   launcher=root/'steamrtarm64/steamwebhelper.sh';runner=root/'RUNSTEAM.sh';library=root/'steamui/library.js'
   source='export LD_LIBRARY_PATH="${STEAMROOT}/${STEAM_RT_ARM64}"\nSTEAM_COMMAND=(\n true\n)\n'
   env=dict(os.environ,STEARLIGHT_STEAM_ROOT=str(root),STEARLIGHT_QUEST_ROOT=str(quest))
   def prepare():subprocess.run(['sh',str(LINUX/'prepare-steam-client.sh')],env=env,check=True)
   for revision in ['first','updated']:
    launcher.write_text('Valve launcher '+revision);runner.write_text(source);library.write_text('Valve UI '+revision)
    prepare()
    self.assertEqual(launcher.read_bytes(),(quest/'steamwebhelper-launcher.sh').read_bytes())
    self.assertIn('/libs:',runner.read_text());self.assertEqual(runner.read_text().count('# STEARLIGHT_STEAM_CHANNEL'),1)
    self.assertTrue(library.read_text().endswith('Valve UI '+revision))
    timestamps=[p.stat().st_mtime_ns for p in [launcher,runner,library]]
    prepare()
    self.assertEqual(timestamps,[p.stat().st_mtime_ns for p in [launcher,runner,library]])
if __name__=='__main__':unittest.main()
