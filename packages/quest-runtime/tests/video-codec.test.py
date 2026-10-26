"""Exercise Android decoder backpressure using the actual bridge and a mock codec."""
from pathlib import Path
import subprocess
import tempfile

source = Path(__file__).resolve().parents[1] / 'app/src/main/cpp/video_bridge.cpp'
with tempfile.TemporaryDirectory(prefix='quest-codec-') as folder:
    folder = Path(folder)
    (folder / 'media').mkdir()
    (folder / 'android').mkdir()
    (folder / 'android/log.h').write_text('#define ANDROID_LOG_INFO 4\ninline int __android_log_print(int,const char*,const char*,...){return 0;}\n')
    (folder / 'media/NdkMediaFormat.h').write_text(r'''
#pragma once
#include <cstdint>
struct AMediaFormat {};
#define AMEDIAFORMAT_KEY_MIME "mime"
#define AMEDIAFORMAT_KEY_WIDTH "width"
#define AMEDIAFORMAT_KEY_HEIGHT "height"
#define AMEDIAFORMAT_KEY_COLOR_FORMAT "color-format"
#define AMEDIAFORMAT_KEY_MAX_INPUT_SIZE "max-input-size"
inline AMediaFormat* AMediaFormat_new(){return new AMediaFormat;}
inline void AMediaFormat_delete(AMediaFormat* f){delete f;}
inline void AMediaFormat_setString(AMediaFormat*,const char*,const char*){}
inline void AMediaFormat_setInt32(AMediaFormat*,const char*,int32_t){}
inline bool AMediaFormat_getInt32(AMediaFormat*,const char*,int32_t*){return false;}
inline const char* AMediaFormat_toString(AMediaFormat*){return "mock";}
''')
    (folder / 'media/NdkMediaCodec.h').write_text(r'''
#pragma once
#include "NdkMediaFormat.h"
#include <sys/types.h>
#include <chrono>
#include <thread>
#include <vector>
constexpr int AMEDIA_OK=0,AMEDIACODEC_INFO_TRY_AGAIN_LATER=-1,AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED=-2;
struct AMediaCodecBufferInfo{int32_t offset=0,size=0;int64_t presentationTimeUs=0;uint32_t flags=0;};
struct AMediaCodec{
 bool outputHeld=true,broken=false,formatChanged=false;unsigned inputCalls=0,releases=0,queued=0;int64_t lastPts=42;
 std::vector<uint8_t> pixels=std::vector<uint8_t>(128*128*3/2,64),input=std::vector<uint8_t>(4096);
};
inline AMediaCodec* AMediaCodec_createDecoderByType(const char*){return new AMediaCodec;}
inline int AMediaCodec_stop(AMediaCodec*){return 0;}
inline int AMediaCodec_delete(AMediaCodec* c){delete c;return 0;}
inline int AMediaCodec_configure(AMediaCodec*,AMediaFormat*,void*,void*,uint32_t){return 0;}
inline int AMediaCodec_start(AMediaCodec*){return 0;}
inline int AMediaCodec_getName(AMediaCodec*,char**){return -1;}
inline void AMediaCodec_releaseName(AMediaCodec*,char*){}
inline ssize_t AMediaCodec_dequeueInputBuffer(AMediaCodec* c,int64_t timeout){
 ++c->inputCalls;
 // A wait for input cannot help while this thread owns the output buffer.
 if(timeout)return -1;
 return c->outputHeld||c->broken?-1:0;
}
inline uint8_t* AMediaCodec_getInputBuffer(AMediaCodec* c,size_t,size_t* size){*size=c->input.size();return c->input.data();}
inline int AMediaCodec_queueInputBuffer(AMediaCodec* c,size_t,size_t,size_t,uint64_t pts,uint32_t){c->lastPts=pts;++c->queued;return 0;}
inline ssize_t AMediaCodec_dequeueOutputBuffer(AMediaCodec* c,AMediaCodecBufferInfo* info,int64_t){
 if(c->formatChanged){c->formatChanged=false;return AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED;}
 if(c->broken||!c->outputHeld){std::this_thread::sleep_for(std::chrono::milliseconds(5));return -1;}
 info->presentationTimeUs=c->lastPts;return 0;
}
inline AMediaFormat* AMediaCodec_getOutputFormat(AMediaCodec*){return new AMediaFormat;}
inline uint8_t* AMediaCodec_getOutputBuffer(AMediaCodec* c,size_t,size_t* size){*size=c->pixels.size();return c->pixels.data();}
inline int AMediaCodec_releaseOutputBuffer(AMediaCodec* c,size_t,bool){c->outputHeld=false;++c->releases;return 0;}
''')
    (folder / 'test.cpp').write_text('#include "' + str(source) + '"\n' + r'''
#include <cassert>
int main(){
 Decoder d;d.codec=new AMediaCodec;d.width=d.height=d.stride=d.slice=128;d.color=21;
 d.frameTags.emplace(42,9223372036854ULL);
 QuestVideoRequest request{};request.timestamp=99;
 QuestVideoResponse response{};response.status=1;std::vector<unsigned char> frame,packet(100,7);
 assert(d.submit(request,packet,response,frame));
 assert(d.codec->queued==1&&d.codec->releases==1&&d.codec->inputCalls==2);
 assert(response.status==0&&response.timestamp==9223372036854ULL&&frame.size()==128*128*3/2);
 assert(d.codec->lastPts>0&&d.codec->lastPts!=99); // Android sees clock time, not a frame identifier.
 int64_t firstPts=d.codec->lastPts;
 assert(d.codec->input[0]==7); // Encoded packet retained, decoded frame returned.
 response.status=1;assert(d.submit(request,packet,response,frame));
 assert(d.codec->queued==2&&d.codec->releases==1); // Normal path needs no drain.
 assert(d.codec->lastPts>firstPts);
 // Restore the exact guest tag on a decoded picture, including repeated tags.
 d.codec->outputHeld=true;response.status=1;assert(d.output(response,frame));
 assert(response.status==0&&response.timestamp==99);
 // Announce dimensions before producing the next picture.
 d.codec->formatChanged=true;response={};response.status=1;frame.clear();assert(d.output(response,frame));
 assert(response.status==QUEST_VIDEO_FORMAT_CHANGED&&response.width==128&&response.height==128&&response.bytes==0);
 d.codec->broken=true;auto start=std::chrono::steady_clock::now();
 assert(!d.submit(request,packet,response,frame));
 assert(std::chrono::steady_clock::now()-start<std::chrono::seconds(1));
 assert(d.codec->queued==2); // Permanent failures remain bounded.
 d.codec->broken=false;response.status=1;
 for(int i=0;i<300;i++)assert(d.submit(request,packet,response,frame));
 assert(d.frameTags.size()<=256); // Dropped codec outputs cannot grow memory indefinitely.
 d.reset();assert(d.frameTags.empty()&&d.lastCodecPts==0);
}
''')
    subprocess.run(['g++', '-O2', '-std=c++17', '-pthread', '-I' + str(folder),
                    str(folder / 'test.cpp'), '-o', str(folder / 'test')], check=True)
    subprocess.run([str(folder / 'test')], timeout=5, check=True)
print('Android decoder: input backpressure drains output; packets and timestamps preserved; failure bounded')
