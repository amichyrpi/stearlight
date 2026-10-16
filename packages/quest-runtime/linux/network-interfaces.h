/* Android's observed interface table replaces its forbidden netlink dump.
 * Addresses, prefix lengths and scope IDs come from LinkProperties. */
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <stdio.h>
static _Atomic(uintptr_t) quest_address_lists[32];
struct quest_address {struct ifaddrs iface;struct sockaddr_storage address,mask,broadcast;char name[IFNAMSIZ];};
struct quest_address_list {struct quest_address entries[64];};
static FILE* interface_file(void){
 const char* registry=getenv("QUEST_SOCKET_REGISTRY");if(!registry)return NULL;
 char path[256];size_t prefix=strlen(registry);while(prefix&&registry[prefix-1]!='/')prefix--;
 if(prefix+sizeof("network-interfaces.tsv")>sizeof(path))return NULL;
 memcpy(path,registry,prefix);strcpy(path+prefix,"network-interfaces.tsv");return fopen(path,"r");
}
int getifaddrs(struct ifaddrs** output){
 FILE* file=interface_file();
 if(file){
  struct quest_address_list* list=calloc(1,sizeof(*list));if(!list){fclose(file);errno=ENOMEM;return -1;}
  unsigned count=0,index,family,prefix;char name[IFNAMSIZ]={0},address[80];
  while(count<64&&quest_fscanf(file,"%15s %u %u %u %79s",name,&index,&family,&prefix,address)==5){
   if(!index||(family!=4&&family!=6)||prefix>(family==4?32:128))continue;
   struct quest_address* entry=&list->entries[count];int domain=family==4?AF_INET:AF_INET6;
   unsigned char* bytes;unsigned char* mask;
   entry->address.ss_family=entry->mask.ss_family=domain;
   if(family==4){bytes=(unsigned char*)&((struct sockaddr_in*)&entry->address)->sin_addr;mask=(unsigned char*)&((struct sockaddr_in*)&entry->mask)->sin_addr;}
   else{bytes=(unsigned char*)&((struct sockaddr_in6*)&entry->address)->sin6_addr;mask=(unsigned char*)&((struct sockaddr_in6*)&entry->mask)->sin6_addr;}
   if(inet_pton(domain,address,bytes)!=1)continue;
   for(unsigned bit=0;bit<prefix;bit++)mask[bit/8]|=(unsigned char)(128>>(bit%8));
   if(family==6&&IN6_IS_ADDR_LINKLOCAL(&((struct sockaddr_in6*)&entry->address)->sin6_addr))((struct sockaddr_in6*)&entry->address)->sin6_scope_id=index;
   memcpy(entry->name,name,sizeof(entry->name));entry->iface.ifa_name=entry->name;entry->iface.ifa_flags=IFF_UP|IFF_RUNNING;
   if(!strcmp(name,"lo"))entry->iface.ifa_flags|=IFF_LOOPBACK;else entry->iface.ifa_flags|=IFF_MULTICAST;
   entry->iface.ifa_addr=(struct sockaddr*)&entry->address;entry->iface.ifa_netmask=(struct sockaddr*)&entry->mask;
   if(family==4&&prefix<31&&strcmp(name,"lo")){
    entry->iface.ifa_flags|=IFF_BROADCAST;entry->broadcast.ss_family=AF_INET;
    struct in_addr* broadcast=&((struct sockaddr_in*)&entry->broadcast)->sin_addr;
    broadcast->s_addr=((struct sockaddr_in*)&entry->address)->sin_addr.s_addr|~((struct sockaddr_in*)&entry->mask)->sin_addr.s_addr;
    entry->iface.ifa_broadaddr=(struct sockaddr*)&entry->broadcast;
   }
   if(count)list->entries[count-1].iface.ifa_next=&entry->iface;
   count++;
  }
  fclose(file);
  if(count)for(unsigned slot=0;slot<32;slot++){
   uintptr_t empty=0;if(atomic_compare_exchange_strong(&quest_address_lists[slot],&empty,(uintptr_t)list)){*output=&list->entries[0].iface;return 0;}
  }
  free(list);
 }
 typedef int (*function)(struct ifaddrs**);function real=(function)dlsym(RTLD_NEXT,"getifaddrs");
 if(!real){errno=ENOSYS;return -1;}return real(output);
}
void freeifaddrs(struct ifaddrs* list){
 if(!list)return;
 for(unsigned slot=0;slot<32;slot++){
  uintptr_t expected=(uintptr_t)list;
  if(atomic_compare_exchange_strong(&quest_address_lists[slot],&expected,0)){free(list);return;}
 }
 typedef void (*function)(struct ifaddrs*);function real=(function)dlsym(RTLD_NEXT,"freeifaddrs");if(real)real(list);
}
unsigned int if_nametoindex(const char* wanted){
 FILE* file=interface_file();if(file){char name[IFNAMSIZ],address[80];unsigned index,family,prefix;
  while(quest_fscanf(file,"%15s %u %u %u %79s",name,&index,&family,&prefix,address)==5)if(!strcmp(name,wanted)){fclose(file);return index;}
  fclose(file);
 }
 typedef unsigned int (*function)(const char*);function real=(function)dlsym(RTLD_NEXT,"if_nametoindex");if(!real){errno=ENOSYS;return 0;}return real(wanted);
}
char* if_indextoname(unsigned int wanted,char output[IF_NAMESIZE]){
 FILE* file=interface_file();if(file){char name[IFNAMSIZ]={0},address[80];unsigned index,family,prefix;
  while(quest_fscanf(file,"%15s %u %u %u %79s",name,&index,&family,&prefix,address)==5)if(index==wanted){memcpy(output,name,IF_NAMESIZE);fclose(file);return output;}
  fclose(file);
 }
 typedef char* (*function)(unsigned int,char*);function real=(function)dlsym(RTLD_NEXT,"if_indextoname");if(!real){errno=ENOSYS;return NULL;}return real(wanted,output);
}
