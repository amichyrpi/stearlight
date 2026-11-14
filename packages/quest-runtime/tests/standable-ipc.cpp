// Cross-architecture IPC check for the optional Ignition Quest adapter.
#include <mapped_event_circular_buffer_c.h>
#include <chrono>
#include <thread>
#include <cstdio>
#include <cstring>
#include <string>

int main(int argc,char **argv) {
    if(argc<3||argc>4)return 2;
    if(argc==4&&!std::freopen(argv[3],"w",stdout))return 3;
    std::setvbuf(stdout,nullptr,_IONBF,0);
    bool server=std::strcmp(argv[1],"server")==0;
    if(!server&&std::strcmp(argv[1],"client"))return 2;
    std::string outgoing=std::string(argv[2])+(server?"_a":"_b");
    std::string incoming=std::string(argv[2])+(server?"_b":"_a");
    CircularBuffer *out=nullptr,*in=nullptr;
    if(server) {
        out=mapped_event_circular_buffer_create_shm(outgoing.c_str(),4096);
        in=mapped_event_circular_buffer_create_shm(incoming.c_str(),4096);
    } else {
        auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);
        do {
            if(!out)out=mapped_event_circular_buffer_open_shm(outgoing.c_str(),4096);
            if(!in)in=mapped_event_circular_buffer_open_shm(incoming.c_str(),4096);
            if(out&&in)break;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        } while(std::chrono::steady_clock::now()<deadline);
    }
    if(!out||!in){std::puts("FAIL: shared rings unavailable");return 4;}
    for(unsigned count=0;count<64;++count) {
        char expected[257],actual[300];
        for(unsigned i=0;i<sizeof(expected);++i)expected[i]=static_cast<char>(count+i);
        if(server&&!mapped_event_circular_buffer_write(out,expected,sizeof(expected)))return 5;
        size_t size=sizeof(actual);
        if(!mapped_event_circular_buffer_wait_and_read(in,actual,&size,120000)) {
            std::printf("FAIL: receive timeout at message %u\n",count);return 6;
        }
        if(size!=sizeof(expected)||std::memcmp(actual,expected,size)) {
            std::printf("FAIL: message mismatch at %u\n",count);return 7;
        }
        if(!server&&!mapped_event_circular_buffer_write(out,actual,size))return 8;
    }
    mapped_event_circular_buffer_destroy(out);
    mapped_event_circular_buffer_destroy(in);
    std::puts("PASS: 64 cross-architecture messages, embedded zeros and ring wraparound");
    return 0;
}
