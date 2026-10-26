"""Exercise ELF global lookup without requiring a host SDL3 installation."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]


class SDL3ResolverTest(unittest.TestCase):
    def test_global_and_library_lookup_call_the_sdk_not_the_wrapper(self):
        with tempfile.TemporaryDirectory() as folder:
            folder = Path(folder)
            adapter = folder / 'libquest_android_gamepad.so'
            sdk = folder / 'sdk.so'
            probe = folder / 'probe'
            (folder / 'sdk.cpp').write_text('''
#include <stdint.h>
extern "C" bool SDL_Init(uint32_t flags){return flags==0x2000;}
extern "C" uint32_t* SDL_GetJoysticks(int* count){*count=0;return nullptr;}
''')
            (folder / 'probe.cpp').write_text('''
#include <cassert>
#include <dlfcn.h>
#include <stdint.h>
#include <unistd.h>
int main(int argc,char** argv){
 assert(getpid()>0);
 if(argc==4){assert(dlopen(argv[3],RTLD_NOW|RTLD_GLOBAL));}
 void* sdk=dlopen(argv[1],RTLD_NOW|RTLD_GLOBAL);assert(sdk);
 if(argc==4){auto legacy=reinterpret_cast<int(*)(uint32_t)>(dlsym(RTLD_DEFAULT,"SDL_Init"));assert(legacy&&legacy(0x2000)==42);}
 auto init=reinterpret_cast<bool(*)(uint32_t)>(dlsym(argc==3?RTLD_DEFAULT:sdk,"SDL_Init"));
 assert(init&&init(0x2000)&&!init(0));
}
''')
            subprocess.run(['g++', '-std=c++17', '-O2', '-shared', '-fPIC', '-Wl,-Bsymbolic-functions',
                            '-I', str(ROOT / 'packages/quest-runtime/app/src/main/cpp'),
                            str(ROOT / 'packages/quest-runtime/linux/sdl3-gamepad.cpp'), '-ldl', '-o', str(adapter)], check=True)
            subprocess.run(['g++', '-shared', '-fPIC', str(folder / 'sdk.cpp'), '-o', str(sdk)], check=True)
            (folder / 'legacy.cpp').write_text('extern "C" int SDL_Init(unsigned){return 42;}')
            legacy = folder / 'legacy.so'
            subprocess.run(['g++', '-shared', '-fPIC', str(folder / 'legacy.cpp'), '-o', str(legacy)], check=True)
            (folder / 'interposer.cpp').write_text('''
#include <dlfcn.h>
#include <unistd.h>
extern "C" pid_t getpid(){
 static auto next=reinterpret_cast<pid_t(*)()>(dlsym(RTLD_NEXT,"getpid"));
 return next();
}
''')
            interposer = folder / 'interposer.so'
            subprocess.run(['g++', '-shared', '-fPIC', str(folder / 'interposer.cpp'), '-ldl', '-o', str(interposer)], check=True)
            subprocess.run(['g++', str(folder / 'probe.cpp'), '-ldl', '-o', str(probe)], check=True)
            for scope in ([], ['global'], ['mixed', str(legacy)]):
                subprocess.run([str(probe), str(sdk), *scope],
                               env=dict(os.environ, LD_PRELOAD=f'{interposer}:{adapter}'), check=True, timeout=5)


if __name__ == '__main__':
    unittest.main()
