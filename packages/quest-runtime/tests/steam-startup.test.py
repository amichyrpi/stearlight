"""Exercise the native loader adapter's real exec, IPC and ownership paths."""
import os
from pathlib import Path
import signal
import socket
import subprocess
import tempfile
import time
import unittest

SOURCE = Path(__file__).resolve().parents[1] / 'linux/steam-update-ui.cpp'


class SteamStartupTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory()
        cls.root = Path(cls.temporary.name)
        def compile(name, source, *options):
            path = cls.root / (name + '.cpp');path.write_text(source)
            subprocess.run(['g++', '-std=c++17', str(path), *options, '-o', str(cls.root / name)], check=True)
        compile('compositor', r'''
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
int main(){
 if(getenv("FAIL_COMPOSITOR"))return 3;
 FILE* f=fopen(getenv("TEST_PID"),"w");fprintf(f,"%d",getpid());fclose(f);
 fprintf(stderr,"Running compositor on wayland display 'gamescope-test'\nStarting Xwayland on :9\n");
 for(;;)pause();
}
''')
        compile('libX11.so.6', 'extern "C" void* XOpenDisplay(const char*){return (void*)1;}\n'
                'extern "C" int XCloseDisplay(void*){return 0;}\n', '-shared', '-fPIC')
        compile('steam', r'''
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
int main(int n,char** a){
 if(n==1){puts(getenv("GAMESCOPE_WAYLAND_DISPLAY")?:"unset");return 0;}
 return execv(a[1],a+1);
}
''')
        compile('probe', r'''
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cassert>
int main(int n,char** a){
 if(n>3){char value=0;assert(read(atoi(a[3]),&value,1)==1&&value=='X');}
 printf("DISPLAY=%s\nWAYLAND=%s\nICD=%s\nPRELOAD=%s\n",getenv("DISPLAY"),
 getenv("GAMESCOPE_WAYLAND_DISPLAY")?:"unset",getenv("VK_ICD_FILENAMES"),getenv("LD_PRELOAD")?:"unset");
}
''')
        cls.library = cls.root / 'libquest_steam_update_ui.so'
        subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-shared', '-fPIC', str(SOURCE),
            '-DQUEST_UPDATE_LOADER="' + str(cls.root / 'compositor') + '"',
            '-DQUEST_UPDATE_LOG="' + str(cls.root / 'updater.log') + '"', '-DQUEST_UPDATE_TIMEOUT=500',
            '-ldl', '-o', str(cls.library)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def environment(self, **extra):
        result = dict(os.environ, LD_PRELOAD=str(self.library), LD_LIBRARY_PATH=str(self.root),
                      DISPLAY=':0', VK_ICD_FILENAMES='original-icd', TEST_PID=str(self.root / 'pid'))
        result.pop('GAMESCOPE_WAYLAND_DISPLAY', None)
        result.update(extra)
        return result

    def run_child(self, **extra):
        first, second = socket.socketpair()
        with first, second:
            first.sendall(b'X')
            return subprocess.check_output([str(self.root / 'steam'), str(self.root / 'probe'),
                '-child-update-ui', '-child-update-ui-socket', str(second.fileno())],
                pass_fds=(second.fileno(),), env=self.environment(**extra), text=True, timeout=5)

    def test_native_child_keeps_real_progress_socket_and_gets_its_own_display(self):
        output = self.run_child()
        self.assertIn('DISPLAY=:9\nWAYLAND=gamescope-test\n', output)
        self.assertIn('ICD=/opt/quest/turnip_icd.json\nPRELOAD=unset', output)
        pid = int((self.root / 'pid').read_text())
        for _ in range(50):
            try:
                state = Path('/proc', str(pid), 'stat').read_text().rsplit(')', 1)[1].split()[0]
                if state == 'Z':
                    break
            except FileNotFoundError:
                break
            time.sleep(.01)
        else:
            os.kill(pid, signal.SIGKILL)
            self.fail('The compositor survived its updater owner')

    def test_failed_compositor_preserves_the_original_updater_and_socket(self):
        output = self.run_child(FAIL_COMPOSITOR='1')
        self.assertIn('DISPLAY=:0\nWAYLAND=unset\nICD=original-icd', output)

    def test_ordinary_exec_does_not_change_display_or_gpu_driver(self):
        output = subprocess.check_output([str(self.root / 'steam'), str(self.root / 'probe')],
                                        env=self.environment(), text=True)
        self.assertIn('DISPLAY=:0\nWAYLAND=unset\nICD=original-icd', output)

    def test_graphical_loader_hint_is_scoped_to_bootstrapper_executable(self):
        output = subprocess.check_output([str(self.root / 'steam')], env=self.environment(), text=True)
        self.assertEqual(output.strip(), 'gamescope-0')
        output = subprocess.check_output([str(self.root / 'probe')], env=self.environment(), text=True)
        self.assertIn('WAYLAND=unset', output)


if __name__ == '__main__':
    unittest.main()
