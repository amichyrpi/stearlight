from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[3]
with tempfile.TemporaryDirectory() as folder:
    folder = Path(folder)
    (folder / 'network-interfaces.tsv').write_text('wlan0 12 4 24 192.0.2.8\nusb0 19 6 64 fe80::abcd\nlo 1 4 8 127.0.0.1\n')
    code = r'''
#include <ifaddrs.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <assert.h>
#include <string.h>
int main(){
 for(int i=0;i<1000;i++){
  struct ifaddrs* list=0;assert(getifaddrs(&list)==0);unsigned count=0;
  for(struct ifaddrs* p=list;p;p=p->ifa_next){count++;
   if(!strcmp(p->ifa_name,"usb0")){assert(p->ifa_addr->sa_family==AF_INET6);assert(((struct sockaddr_in6*)p->ifa_addr)->sin6_scope_id==19);}
   if(!strcmp(p->ifa_name,"wlan0")){assert(ntohl(((struct sockaddr_in*)p->ifa_netmask)->sin_addr.s_addr)==0xffffff00);assert(ntohl(((struct sockaddr_in*)p->ifa_broadaddr)->sin_addr.s_addr)==0xc00002ff);}
  }assert(count==3);freeifaddrs(list);
 }
 assert(if_nametoindex("usb0")==19);char name[IF_NAMESIZE];assert(if_indextoname(19,name)&&!strcmp(name,"usb0"));
}
'''
    (folder / 'check.c').write_text(code)
    subprocess.run(['gcc', '-shared', '-fPIC', '-O2', '-Wall', '-Wextra', '-Werror', str(root / 'packages/quest-runtime/linux/socket-report.c'), '-ldl', '-o', str(folder / 'report.so')], check=True)
    symbols = subprocess.check_output(['readelf', '--dyn-syms', '--wide', str(folder / 'report.so')], text=True)
    assert not any(name in symbols for name in ('__isoc23_', '__strcpy_chk', '__memcpy_chk')), 'Preload must also load in Alpine musl programs'
    subprocess.run(['gcc', str(folder / 'check.c'), '-o', str(folder / 'check')], check=True)
    subprocess.run([str(folder / 'check')], env={**os.environ, 'LD_PRELOAD': str(folder / 'report.so'), 'QUEST_SOCKET_REGISTRY': str(folder / 'socket-registry.sock')}, check=True)
    print('Actual interface snapshot, masks, IPv6 scopes and repeated allocation/free passed')
