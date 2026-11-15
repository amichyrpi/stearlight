// Exercise concurrent calibration/readout without Steam or the Windows driver.
#include <time_sync.h>
#include <atomic>
#include <cstdio>
#include <thread>

int main() {
    constexpr uint64_t offset=1000000000000ULL, ticks=3000000000ULL;
    TimeSync::SetCalibration(1000000000ULL,offset+100000000000ULL,10000000);
    std::atomic<bool> done{false}, failed{false};
    std::thread writer([&] {
        for(unsigned i=0;i<100000;++i) {
            const uint64_t base=(i&1)?1000000000ULL:2000000000ULL;
            TimeSync::SetCalibration(base,offset+base*100,10000000);
        }
        done=true;
    });
    unsigned reads=0;
    do {
        if(TimeSync::QpcToLinuxTicks(ticks)!=offset+ticks*100)failed=true;
        ++reads;
    } while(!done||reads<100000);
    writer.join();
    if(failed){std::puts("FAIL: mixed clock-calibration fields");return 1;}
    std::puts("PASS: concurrent calibration keeps complete clock snapshots");
}
