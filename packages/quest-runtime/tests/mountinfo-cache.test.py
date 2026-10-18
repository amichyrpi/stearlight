"""Check mount snapshots, namespace isolation, invalidation and bounded lifetime."""
from pathlib import Path
import subprocess
import tempfile
import unittest

HARNESS = r'''
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <talloc.h>
#include <unistd.h>
struct quest_mountinfo_cache;
typedef struct bindings {
 unsigned long long quest_mount_generation;
 struct quest_mountinfo_cache *quest_mount_cache;
} Bindings;
#include "SOURCE"
static int remove_file(char *path) { return unlink(path); }
static void generate(void *reader, Bindings *b, int source, const char *text, const char *directory) {
 struct quest_mountinfo_cache *cache = quest_mountinfo_begin(b, source);
 assert(cache);
 char temporary[4096]; snprintf(temporary, sizeof(temporary), "%s/mount-XXXXXX", directory);
 int fd=mkstemp(temporary); assert(fd>=0);
 assert(write(fd,text,strlen(text))==(ssize_t)strlen(text)); close(fd);
 char *path=talloc_strdup(cache,temporary); assert(path);
 talloc_set_destructor(path,remove_file);
 quest_mountinfo_publish(reader,b,cache,path);
}
static void content(const char *path, const char *expected) {
 char data[64]={0}; FILE *file=fopen(path,"r"); assert(file);
 assert(fread(data,1,sizeof(data)-1,file)==strlen(expected)); fclose(file);
 assert(!strcmp(data,expected));
}
int main(int argc,char **argv) {
 assert(argc==2);
 Bindings *b=talloc_zero(NULL,Bindings),*other=talloc_zero(NULL,Bindings);
 void *first=talloc_new(NULL),*second=talloc_new(NULL);
 int source=open("/proc/self/mountinfo",O_RDONLY|O_CLOEXEC); assert(source>=0);
 unsetenv("PROOT_QUEST_ROBUST"); assert(!quest_mountinfo_begin(b,source));
 setenv("PROOT_QUEST_ROBUST","1",1);
 generate(first,b,source,"original",argv[1]);
 const char *old=quest_mountinfo_cached(first,b); assert(old); content(old,"original");
 char saved[4096]; strcpy(saved,old);
 b->quest_mount_generation++;
 assert(!quest_mountinfo_cached(second,b));
 /* A failed refresh must not make the previous binding generation valid. */
 assert(!quest_mountinfo_cached(second,b));
 generate(second,b,source,"updated",argv[1]);
 content(quest_mountinfo_cached(second,b),"updated");
 /* An in-flight translated open retains the old pathname until it opens. */
 content(saved,"original");
 int opened=open(saved,O_RDONLY); assert(opened>=0);
 talloc_free(first); assert(access(saved,F_OK)<0);
 char data[16]={0}; assert(read(opened,data,sizeof(data))==8); close(opened);
 assert(!strcmp(data,"original"));
 assert(!quest_mountinfo_cached(second,other)); // Distinct binding namespaces.
 generate(second,other,source,"isolated",argv[1]);
 content(quest_mountinfo_cached(second,other),"isolated");
 content(quest_mountinfo_cached(second,b),"updated");
 size_t blocks=talloc_total_blocks(b);
 for(int i=0;i<1000;i++) {
  void *reader=talloc_new(NULL);
  assert(quest_mountinfo_cached(reader,b)); talloc_free(reader);
 }
 assert(talloc_total_blocks(b)==blocks);
 /* Repeated refreshes must release superseded files and source descriptors. */
 for(int i=0;i<100;i++) {
  void *reader=talloc_new(NULL);
  b->quest_mount_generation++;
  generate(reader,b,source,"updated",argv[1]);
  talloc_free(reader);
 }
 assert(talloc_total_blocks(b)==blocks);
 /* A kernel mount event/error invalidates permanently, even if regeneration
  * fails and a subsequent poll no longer reports the consumed event. */
 int channel[2]; assert(!pipe(channel));
 generate(second,b,channel[0],"before event",argv[1]);
 assert(quest_mountinfo_cached(second,b));
 close(channel[1]); assert(!quest_mountinfo_cached(second,b));
 assert(dup2(source,b->quest_mount_cache->mount_fd)>=0);
 assert(!quest_mountinfo_cached(second,b));
 close(channel[0]);
 generate(second,b,source,"after event",argv[1]);
 content(quest_mountinfo_cached(second,b),"after event");
 char latest[4096]; strcpy(latest,b->quest_mount_cache->path);
 talloc_free(second); talloc_free(b); talloc_free(other); close(source);
 assert(access(latest,F_OK)<0);
 return 0;
}
'''


class MountInfoCacheTest(unittest.TestCase):
    def test_snapshots_invalidation_and_lifetime(self):
        header = Path(__file__).resolve().parents[1] / 'proot/mountinfo-cache.h'
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            code = root / 'mountinfo.c'
            code.write_text(HARNESS.replace('SOURCE', str(header)))
            flags = subprocess.check_output(['pkg-config', '--cflags', '--libs', 'talloc'], text=True).split()
            binary = root / 'mountinfo-test'
            subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                            str(code), *flags, '-o', str(binary)], check=True)
            subprocess.run([str(binary), directory], check=True, timeout=15)
            self.assertEqual(sorted(p.name for p in root.iterdir()), ['mountinfo-test', 'mountinfo.c'])


if __name__ == '__main__':
    unittest.main()
