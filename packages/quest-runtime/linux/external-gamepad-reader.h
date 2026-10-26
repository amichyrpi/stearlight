#pragma once
#include "external_gamepad.h"
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstdlib>

class ExternalGamepadReader {
 const QuestExternalGamepad* shared=nullptr;uint64_t retry=0;
public:
 ~ExternalGamepadReader(){if(shared)munmap(const_cast<QuestExternalGamepad*>(shared),sizeof(*shared));}
 bool read(QuestExternalGamepad& value,uint64_t now,bool* connected=nullptr){
  if(!shared&&now>=retry){
   retry=now+1000000000;const char* path=getenv("STEARLIGHT_EXTERNAL_GAMEPAD_STATE");
   int fd=open(path?path:"/run/stearlight/quest/external-gamepad",O_RDONLY|O_CLOEXEC|O_NOFOLLOW);
   if(fd>=0){struct stat info{};if(!fstat(fd,&info)&&info.st_size==sizeof(QuestExternalGamepad)){
    void* mapping=mmap(nullptr,sizeof(QuestExternalGamepad),PROT_READ,MAP_SHARED,fd,0);
    if(mapping!=MAP_FAILED)shared=static_cast<const QuestExternalGamepad*>(mapping);
   }close(fd);}
  }
  return shared&&quest::readExternalGamepad(shared,value,now,connected);
 }
};
