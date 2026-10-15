"""Exercise the pinned upstream SetBufferData function, rather than a copy."""
import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

REPO = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location("fna_build", REPO / "build_files/quest/build-fna3d.py")
builder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(builder)


class BufferReuseTest(unittest.TestCase):
    @unittest.skipUnless(os.environ.get("TEST_FNA3D_SOURCE"), "Provide the pinned extracted FNA3D source")
    def test_reuses_completed_slots_and_preserves_busy_buffers_and_data(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / "src").mkdir()
            source = root / "src/FNA3D_Driver_Vulkan.c"
            shutil.copyfile(Path(os.environ["TEST_FNA3D_SOURCE"]) / "src/FNA3D_Driver_Vulkan.c", source)
            builder.repair_buffer_reuse(root)
            text = source.read_text()
            builder.repair_buffer_reuse(root)
            self.assertEqual(text, source.read_text())
            start = text.index("static void VULKAN_INTERNAL_SetBufferData(")
            end = text.index("\n}\n", start) + 3
            function = text[start:end]
            harness = r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
#define SDL_memcpy memcpy
#define VK_TRUE 1
typedef int FNA3D_SetDataOptions;
enum { FNA3D_SETDATAOPTIONS_NONE, FNA3D_SETDATAOPTIONS_DISCARD, FNA3D_SETDATAOPTIONS_NOOVERWRITE };
typedef struct { unsigned char *mapPointer; } Allocation;
typedef struct { Allocation *allocation; unsigned resourceOffset; } Region;
typedef struct { int bound; Region *usedRegion; } SubBuffer;
typedef struct { unsigned size; int currentSubBufferIndex,subBufferCount,bound; SubBuffer **subBuffers; } VulkanBuffer;
typedef struct { int bufferDefragInProgress,logicalDevice,defragFence; int (*vkWaitForFences)(int,int,int*,int,uint64_t); } VulkanRenderer;
typedef VulkanRenderer FNA3D_Renderer;
typedef VulkanBuffer FNA3D_Buffer;
static unsigned allocations;
static int VULKAN_INTERNAL_AllocateSubBuffer(VulkanRenderer *r,VulkanBuffer *b){(void)r;++allocations;++b->subBufferCount;return 1;}
static void VULKAN_INTERNAL_FlushCommands(VulkanRenderer *r,int wait){(void)r;(void)wait;assert(0);}
static void FNA3D_LogError(const char *s){(void)s;assert(0);}
'''
            main = r'''
int main(void){
 unsigned char memory[48]={0}; Allocation allocation={memory};
 Region regions[3]={{&allocation,0},{&allocation,16},{&allocation,32}};
 SubBuffer slots[3]={{-1,&regions[0]},{0,&regions[1]},{-1,&regions[2]}};
 SubBuffer *pointers[3]={&slots[0],&slots[1],&slots[2]};
 VulkanBuffer buffer={8,1,2,1,pointers}; VulkanRenderer renderer={0};
 unsigned char data[2]={42,43};
 VULKAN_INTERNAL_SetBufferData(&renderer,&buffer,2,data,2,FNA3D_SETDATAOPTIONS_DISCARD);
 assert(buffer.currentSubBufferIndex==0 && allocations==0 && memory[2]==42);
 memcpy(memory+16,"original",8); memset(memory,0,8); buffer.currentSubBufferIndex=1;
 VULKAN_INTERNAL_SetBufferData(&renderer,&buffer,2,data,2,FNA3D_SETDATAOPTIONS_NONE);
 assert(buffer.currentSubBufferIndex==0 && memory[0]=='o' && memory[7]=='l' && memory[2]==42);
 buffer.currentSubBufferIndex=1;
 VULKAN_INTERNAL_SetBufferData(&renderer,&buffer,0,data,2,FNA3D_SETDATAOPTIONS_NOOVERWRITE);
 assert(buffer.currentSubBufferIndex==1 && allocations==0 && memory[16]==42);
 slots[0].bound=0; slots[1].bound=1; buffer.currentSubBufferIndex=0;
 VULKAN_INTERNAL_SetBufferData(&renderer,&buffer,0,data,2,FNA3D_SETDATAOPTIONS_DISCARD);
 assert(buffer.currentSubBufferIndex==2 && allocations==1 && memory[32]==42);
 return 0;
}
'''
            (root / "test.c").write_text(harness + function + main)
            subprocess.run(["gcc", "-std=gnu99", "-Wall", "-Werror", str(root / "test.c"), "-o", str(root / "test")], check=True)
            subprocess.run([str(root / "test")], check=True)


if __name__ == "__main__":
    unittest.main()
