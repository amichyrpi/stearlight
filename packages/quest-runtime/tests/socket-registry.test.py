#!/usr/bin/env python3
"""Check the real socket/credential bridge on Linux, including close cleanup."""
from pathlib import Path
import os,socket,struct,subprocess,tempfile,time
ROOT=Path(__file__).resolve().parents[3]
with tempfile.TemporaryDirectory(prefix='quest-sockets-') as directory:
 d=Path(directory);(d/'android').mkdir();(d/'state/proc-net').mkdir(parents=True)
 (d/'jni.h').write_text('''#define JNIEXPORT
#define JNICALL
using jobject=void*;using jstring=const char*;
struct JNIEnv{const char* GetStringUTFChars(const char* s,void*){return s;}void ReleaseStringUTFChars(const char*,const char*){}};
''')
 (d/'android/log.h').write_text('#define ANDROID_LOG_ERROR 6\ninline int __android_log_print(int,const char*,const char*,...){return 0;}\n')
 (d/'registry.cpp').write_text('#include "'+str(ROOT/'packages/quest-runtime/app/src/main/cpp/socket_registry.cpp')+'"\nint main(int argc,char** argv){JNIEnv env;Java_net_stearlight_quest_QuestActivity_startSocketRegistry(&env,nullptr,argv[1]);getchar();Java_net_stearlight_quest_QuestActivity_stopSocketRegistry(&env,nullptr);}\n')
 subprocess.run(['g++','-std=c++17','-O2','-pthread','-I'+str(d),d/'registry.cpp','-o',d/'registry'],check=True)
 subprocess.run(['gcc','-shared','-fPIC','-O2','-Wall','-Wextra','-Werror',ROOT/'packages/quest-runtime/linux/socket-report.c','-ldl','-o',d/'report.so'],check=True)
 registry=subprocess.Popen([d/'registry',d/'state'],stdin=subprocess.PIPE)
 try:
  for _ in range(100):
   if (d/'state/socket-registry.sock').exists():break
   time.sleep(.01)
  env=os.environ|{'LD_PRELOAD':str(d/'report.so'),'QUEST_SOCKET_REGISTRY':str(d/'state/socket-registry.sock')}
  client=subprocess.Popen(['python3','-c',"import socket,sys; s=socket.socket();s.bind(('127.0.0.1',0));s.listen();c=socket.create_connection(s.getsockname());a,p=s.accept();print(s.getsockname()[1],c.getsockname()[1],flush=True);sys.stdin.readline()"],env=env,stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True)
  try:
   server_port,client_port=map(int,client.stdout.readline().split());rows=[]
   for _ in range(100):
    rows=(d/'state/proc-net/tcp').read_text().splitlines()[1:]
    if len(rows)==3:break
    time.sleep(.02)
   assert len(rows)==3,rows
   seen=set()
   for row in rows:
    fields=row.split();local,peer=fields[1:3];uid=int(fields[7]);inode=int(fields[9])
    assert uid==os.getuid();assert inode>0
    assert any(os.readlink(f)==f'socket:[{inode}]' for f in Path(f'/proc/{client.pid}/fd').iterdir()),row
    seen.add((local,peer,fields[3]))
   assert (f'0100007F:{server_port:04X}','00000000:0000','0A') in seen,seen
   assert (f'0100007F:{server_port:04X}',f'0100007F:{client_port:04X}','01') in seen,seen
   assert (f'0100007F:{client_port:04X}',f'0100007F:{server_port:04X}','01') in seen,seen
  finally:
   client.communicate('\n',timeout=3)
  for _ in range(100):
   if len((d/'state/proc-net/tcp').read_text().splitlines())==1:break
   time.sleep(.02)
  assert len((d/'state/proc-net/tcp').read_text().splitlines())==1,'Closed sockets remain registered'
  print('Socket registry: actual endpoint pairs, inode, UID and close cleanup verified')
 finally:registry.communicate(b'\n',timeout=3)
