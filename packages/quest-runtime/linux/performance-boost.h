#ifndef QUEST_PERFORMANCE_BOOST_H
#define QUEST_PERFORMANCE_BOOST_H
#include <fcntl.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
// Same-kernel monotonic deadlines; atomic replacement avoids partial reads.
// A request never waits for Android and adds no resident process.
static inline void quest_request_loading_boost(void){
 const char* path="/run/stearlight/quest/performance-boost.request";
 char temporary[128],value[64];struct timespec now;
 if(clock_gettime(CLOCK_MONOTONIC,&now))return;
 snprintf(temporary,sizeof(temporary),"%s.%ld.next",path,(long)getpid());
 int count=snprintf(value,sizeof(value),"%.6f\n",now.tv_sec+now.tv_nsec/1e9+15);
 int fd=open(temporary,O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC,0600);
 if(fd<0)return;
 ssize_t written=write(fd,value,(size_t)count);int closed=close(fd);
 if(written==count&&!closed&&rename(temporary,path)==0)return;
 unlink(temporary);
}
#endif
