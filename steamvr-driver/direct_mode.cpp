#include <winsock2.h>
#include <ws2tcpip.h>
#include "direct_mode.h"
#include <stearlight_protocol.h>
#include <dxgi1_2.h>
#include <algorithm>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cctype>
#include <random>

using Microsoft::WRL::ComPtr;
static void debug(const char *s){char line[2304];std::snprintf(line,sizeof(line),"SVRT: %s",s);OutputDebugStringA(line);OutputDebugStringA("\n");if(vr::VRDriverLog())vr::VRDriverLog()->Log(line);}
static void debugf(const char *format,...){char message[2048];va_list args;va_start(args,format);std::vsnprintf(message,sizeof(message),format,args);va_end(args);debug(message);}
SvrtDirectMode::SvrtDirectMode()=default;
SvrtDirectMode::~SvrtDirectMode(){Stop();}
bool SvrtDirectMode::EnsureDevice(){std::lock_guard<std::mutex> lifecycle(lifecycle_mutex_);return EnsureDeviceLocked();}
bool SvrtDirectMode::EnsureDeviceLocked(){
  if(device_&&context_) return true;
  UINT flags=D3D11_CREATE_DEVICE_BGRA_SUPPORT;D3D_FEATURE_LEVEL level;
  HRESULT hr=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,flags,nullptr,0,D3D11_SDK_VERSION,&device_,&level,&context_);
  if(FAILED(hr)){debugf("D3D11CreateDevice failed: 0x%08lx",(unsigned long)hr);return false;}
  return true;
}
uint64_t SvrtDirectMode::GraphicsAdapterLuid() const{
  std::lock_guard<std::mutex> lifecycle(lifecycle_mutex_);
  if(!device_) return 0;
  ComPtr<IDXGIDevice> dxgi_device;
  if(FAILED(device_.As(&dxgi_device))) return 0;
  ComPtr<IDXGIAdapter> adapter;
  if(FAILED(dxgi_device->GetAdapter(&adapter))) return 0;
  DXGI_ADAPTER_DESC desc{};
  if(FAILED(adapter->GetDesc(&desc))) return 0;
  uint64_t luid=0;
  static_assert(sizeof(desc.AdapterLuid)==sizeof(luid),"unexpected LUID size");
  std::memcpy(&luid,&desc.AdapterLuid,sizeof(luid));
  return luid;
}
bool SvrtDirectMode::Start(const std::string &host,uint16_t port,unsigned fps,unsigned bitrate,const std::string &ffmpeg,const std::string &encoder){
  std::lock_guard<std::mutex> lifecycle(lifecycle_mutex_);
  if (running_) return true;
  host_=host;port_=port;fps_=fps?fps:60;bitrate_=bitrate?bitrate:8;bitrate_ceiling_=bitrate_;ffmpeg_=ffmpeg.empty()?"ffmpeg.exe":ffmpeg;encoder_=encoder.empty()?"hevc_nvenc":encoder;
  encoder_failed_=false; accepting_=false;if(!EnsureDeviceLocked()){encoder_failed_=true;return false;}debugf("direct mode ready: receiver=%s:%u fps=%u bitrate=%uM encoder=%s",host_.c_str(),port_,fps_,bitrate_,encoder_.c_str());running_=true;accepting_=true;worker_=std::thread(&SvrtDirectMode::EncoderThread,this);return true;
}
void SvrtDirectMode::SetReceiverAvailable(bool available){
  const bool was_available=receiver_available_.exchange(available);
  if(available&&!was_available)encoder_failed_=false;
  if(!available){
    if (running_.load()) SendControl(STEARLIGHT_CONTROL_DISCONNECTED);
    disconnect_requested_=true;
    // Cancel a pipe write immediately when the receiver disappears. Without
    // this, vrserver can wait forever for FFmpeg and make the desktop appear
    // frozen during disconnect or SteamVR shutdown.
    if(worker_.joinable()) CancelSynchronousIo(worker_.native_handle());
  }
  ready_.notify_one();
}
void SvrtDirectMode::Stop(){
  std::unique_lock<std::mutex> lifecycle(lifecycle_mutex_);
  if (!running_.exchange(false)) {
    accepting_ = false;
    return;
  }
  SendControl(receiver_available_.load() ? STEARLIGHT_CONTROL_SHUTDOWN
                                         : STEARLIGHT_CONTROL_DISCONNECTED);
  accepting_=false;
  // A synchronous WriteFile to FFmpeg's stdin can block indefinitely when the
  // receiver disappears.  Stop the reader first: this breaks that write and
  // lets the SteamVR shutdown thread join the worker promptly.
  if(worker_.joinable()) CancelSynchronousIo(worker_.native_handle());
  if(process_) TerminateProcess(process_, 0);
  ready_.notify_all();
  if(worker_.joinable())worker_.join();
  CloseEncoder();
  std::lock_guard<std::mutex> l(mutex_);textures_.clear();slots_.clear();frame_.clear();context_.Reset();device_.Reset();
}
void SvrtDirectMode::CreateSwapTextureSet(uint32_t pid,const SwapTextureSetDesc_t *d,SwapTextureSet_t *out){
  *out={};if(!device_||!d)return;debugf("creating swap textures: pid=%u size=%ux%u format=%u samples=%u",pid,d->nWidth,d->nHeight,d->nFormat,d->nSampleCount);D3D11_TEXTURE2D_DESC td{};td.Width=d->nWidth;td.Height=d->nHeight;td.MipLevels=1;td.ArraySize=1;td.Format=(DXGI_FORMAT)d->nFormat;td.SampleDesc.Count=std::max(1u,d->nSampleCount);td.Usage=D3D11_USAGE_DEFAULT;td.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;td.MiscFlags=D3D11_RESOURCE_MISC_SHARED;
  std::lock_guard<std::mutex> l(mutex_);Texture created[3];uint64_t keys[3]{};for(int i=0;i<3;i++){ComPtr<ID3D11Texture2D> tex;if(FAILED(device_->CreateTexture2D(&td,nullptr,&tex)))return;ComPtr<IDXGIResource> dxgi;if(FAILED(tex.As(&dxgi)))return;HANDLE h=nullptr;if(FAILED(dxgi->GetSharedHandle(&h))||!h)return;keys[i]=(uint64_t)(uintptr_t)h;created[i]=Texture{pid,keys[0],tex};}for(int i=0;i<3;i++){created[i].group=keys[0];textures_[keys[i]]=std::move(created[i]);out->rSharedTextureHandles[i]=(vr::SharedTextureHandle_t)keys[i];}out->unTextureFlags=0;
}
void SvrtDirectMode::DestroySwapTextureSet(vr::SharedTextureHandle_t h){std::lock_guard<std::mutex> l(mutex_);auto it=textures_.find((uint64_t)h);if(it==textures_.end())return;uint64_t group=it->second.group;for(auto p=textures_.begin();p!=textures_.end();){if(p->second.group==group)p=textures_.erase(p);else ++p;}}
void SvrtDirectMode::DestroyAllSwapTextureSets(uint32_t pid){std::lock_guard<std::mutex> l(mutex_);for(auto p=textures_.begin();p!=textures_.end();){if(p->second.pid==pid)p=textures_.erase(p);else ++p;}}
void SvrtDirectMode::GetNextSwapTextureSetIndex(vr::SharedTextureHandle_t[2],uint32_t (*indices)[2]){std::lock_guard<std::mutex> l(mutex_);next_[0]=(next_[0]+1)%3;next_[1]=(next_[1]+1)%3;(*indices)[0]=next_[0];(*indices)[1]=next_[1];}
void SvrtDirectMode::SubmitLayer(const SubmitLayerPerEye_t (&eyes)[2]){std::lock_guard<std::mutex> l(mutex_);submitted_[0]=eyes[0].hTexture;submitted_[1]=eyes[1].hTexture;}
static constexpr unsigned kStagingSlots=3;
bool SvrtDirectMode::EnsureGpuConversion(unsigned w,unsigned h,DXGI_FORMAT fmt){
  if(FAILED(device_.As(&video_device_))||FAILED(context_.As(&video_context_)))return false;
  D3D11_VIDEO_PROCESSOR_CONTENT_DESC content{};
  content.InputFrameFormat=D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
  content.InputWidth=content.OutputWidth=w;content.InputHeight=content.OutputHeight=h;
  content.InputFrameRate={fps_,1};content.OutputFrameRate={fps_,1};
  content.Usage=D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;
  if(FAILED(video_device_->CreateVideoProcessorEnumerator(&content,&video_enumerator_))||
     FAILED(video_device_->CreateVideoProcessor(video_enumerator_.Get(),0,&video_processor_)))return false;
  D3D11_TEXTURE2D_DESC input{};input.Width=w;input.Height=h;input.MipLevels=input.ArraySize=1;
  input.Format=fmt;input.SampleDesc.Count=1;input.Usage=D3D11_USAGE_DEFAULT;
  D3D11_TEXTURE2D_DESC output=input;output.Format=DXGI_FORMAT_NV12;output.BindFlags=D3D11_BIND_RENDER_TARGET;
  D3D11_TEXTURE2D_DESC staging=output;staging.BindFlags=0;staging.Usage=D3D11_USAGE_STAGING;staging.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
  for(unsigned i=0;i<kStagingSlots;i++){
    Slot s;
    if(FAILED(device_->CreateTexture2D(&input,nullptr,&s.input))||
       FAILED(device_->CreateTexture2D(&output,nullptr,&s.converted))||
       FAILED(device_->CreateTexture2D(&staging,nullptr,&s.staging)))return false;
    D3D11_QUERY_DESC query{};query.Query=D3D11_QUERY_EVENT;
    if(FAILED(device_->CreateQuery(&query,&s.source_copied)))return false;
    D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC iv{};iv.ViewDimension=D3D11_VPIV_DIMENSION_TEXTURE2D;iv.Texture2D.MipSlice=0;iv.Texture2D.ArraySlice=0;
    D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC ov{};ov.ViewDimension=D3D11_VPOV_DIMENSION_TEXTURE2D;ov.Texture2D.MipSlice=0;
    if(FAILED(video_device_->CreateVideoProcessorInputView(s.input.Get(),video_enumerator_.Get(),&iv,&s.input_view))||
       FAILED(video_device_->CreateVideoProcessorOutputView(s.converted.Get(),video_enumerator_.Get(),&ov,&s.output_view)))return false;
    slots_.push_back(std::move(s));
  }
  video_context_->VideoProcessorSetStreamFrameFormat(video_processor_.Get(),0,D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE);
  pixel_format_="nv12";gpu_nv12_=true;
  frame_.resize((size_t)w*h*3/2);
  debug("D3D11 GPU BGRA-to-NV12 conversion enabled");
  return true;
}
bool SvrtDirectMode::ConvertSlot(Slot &slot){
  D3D11_VIDEO_PROCESSOR_STREAM stream{};stream.Enable=TRUE;stream.pInputSurface=slot.input_view.Get();
  if(FAILED(video_context_->VideoProcessorBlt(video_processor_.Get(),slot.output_view.Get(),0,1,&stream)))return false;
  context_->CopyResource(slot.staging.Get(),slot.converted.Get());return true;
}
void SvrtDirectMode::SetNetworkStats(uint64_t invalid,uint64_t recovered,uint64_t dropped){
  const auto now=std::chrono::steady_clock::now();
  if(last_network_adjust_.time_since_epoch().count()==0){last_network_adjust_=now;last_invalid_=invalid;last_recovered_=recovered;last_network_dropped_=dropped;return;}
  if(now-last_network_adjust_<std::chrono::seconds(2))return;
  const uint64_t new_invalid=invalid>=last_invalid_?invalid-last_invalid_:invalid,new_recovered=recovered>=last_recovered_?recovered-last_recovered_:recovered,new_dropped=dropped>=last_network_dropped_?dropped-last_network_dropped_:dropped;
  last_invalid_=invalid;last_recovered_=recovered;last_network_dropped_=dropped;last_network_adjust_=now;
  ++network_intervals_;
  if(new_dropped||new_invalid)debugf("network recovery: bitrate=%uM invalid=%llu fec_recovered=%llu unrecoverable_frames=%llu",bitrate_,(unsigned long long)new_invalid,(unsigned long long)new_recovered,(unsigned long long)new_dropped);
}
bool SvrtDirectMode::WaitForSourceCopy(Slot &slot){
  if(!slot.source_copied)return false;
  context_->End(slot.source_copied.Get());
  context_->Flush();
  const auto started=std::chrono::steady_clock::now();
  for(;;){
    const HRESULT ready=context_->GetData(slot.source_copied.Get(),nullptr,0,D3D11_ASYNC_GETDATA_DONOTFLUSH);
    if(ready==S_OK){
      const auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-started).count();
      if(elapsed>20)debugf("virtual display: source copy fence took %lldms",static_cast<long long>(elapsed));
      return true;
    }
    if(ready!=S_FALSE){debugf("virtual display: source copy fence failed: 0x%08lx",static_cast<unsigned long>(ready));return false;}
    if(std::chrono::steady_clock::now()-started>std::chrono::milliseconds(100)){
      debugf("virtual display: source copy fence timed out; device=0x%08lx",static_cast<unsigned long>(device_->GetDeviceRemovedReason()));
      return false;
    }
    Sleep(0);
  }
}
bool SvrtDirectMode::EnsureSlots(unsigned ew,unsigned h,DXGI_FORMAT fmt){if(!slots_.empty())return true;width_=std::min(ew,1440u)*2;height_=std::min(h,1600u);debugf("preparing fixed stream: source=%ux%u output=%ux%u",ew,h,width_,height_);if(!EnsureGpuConversion(width_,height_,fmt)){debug("D3D11 NV12 conversion unavailable");slots_.clear();return false;}return StartEncoder(width_,height_);}
bool SvrtDirectMode::EnsureVirtualSlots(unsigned w,unsigned h,DXGI_FORMAT fmt){if(!slots_.empty())return true;width_=std::min(w,2880u);height_=std::min(h,1600u);debugf("preparing virtual-display stream: %ux%u",width_,height_);if(!EnsureGpuConversion(width_,height_,fmt)){debug("D3D11 NV12 conversion unavailable");slots_.clear();return false;}return StartEncoder(width_,height_);}
void SvrtDirectMode::Present(vr::SharedTextureHandle_t){
  std::lock_guard<std::mutex> lifecycle(lifecycle_mutex_);
  if(!accepting_) return;
  std::unique_lock<std::mutex> l(mutex_);auto a=textures_.find((uint64_t)submitted_[0]),b=textures_.find((uint64_t)submitted_[1]);if(a==textures_.end()||b==textures_.end())return;D3D11_TEXTURE2D_DESC d{};a->second.texture->GetDesc(&d);std::lock_guard<std::mutex> dl(d3d_mutex_);if(!EnsureSlots(d.Width,d.Height,d.Format))return;auto slot=std::find_if(slots_.begin(),slots_.end(),[](const Slot&s){return !s.pending;});if(slot==slots_.end())return;unsigned eye_width=width_/2,copy_width=std::min(d.Width,eye_width),copy_height=std::min(d.Height,height_),source_x=(d.Width-copy_width)/2,source_y=(d.Height-copy_height)/2;D3D11_BOX box{source_x,source_y,0,source_x+copy_width,source_y+copy_height,1};context_->CopySubresourceRegion(slot->input.Get(),0,0,0,0,a->second.texture.Get(),0,&box);context_->CopySubresourceRegion(slot->input.Get(),0,eye_width,0,0,b->second.texture.Get(),0,&box);if(!ConvertSlot(*slot))return;context_->Flush();slot->pending=true;slot->sequence=++sequence_;l.unlock();ready_.notify_one();}
void SvrtDirectMode::PresentVirtual(vr::SharedTextureHandle_t handle){
  std::lock_guard<std::mutex> lifecycle(lifecycle_mutex_);
  if(!accepting_||!device_||!handle||!receiver_available_||encoder_failed_)return;

  // The handle is only guaranteed to refer to the compositor backbuffer for
  // the duration of Present().  Deferring OpenSharedResource/CopyResource to
  // the encoder thread races SteamVR reusing that texture and produces a
  // perfectly valid stream of stale (usually grey) pixels.  Copy the GPU
  // resource while the compositor owns it, then let the worker perform the
  // asynchronous map/encode.
  if(CopyVirtualFrame(handle)) ready_.notify_one();
}
void SvrtDirectMode::WaitForVirtualPresent(){
  std::lock_guard<std::mutex> lifecycle(lifecycle_mutex_);
  if(!accepting_||!context_) return;
  // Present queues the GPU copy. Mapping the staging resource is deliberately
  // left to the encoder worker; this call only gives SteamVR the required
  // synchronization point without making its compositor thread wait for a
  // full-frame CPU readback.
  std::lock_guard<std::mutex> dl(d3d_mutex_);
  context_->Flush();
}
bool SvrtDirectMode::CopyVirtualFrame(vr::SharedTextureHandle_t handle){
  ComPtr<ID3D11Texture2D> source;
  std::unique_lock<std::mutex> l(mutex_);
  const auto now=std::chrono::steady_clock::now();
  const auto interval=std::chrono::nanoseconds(1000000000ull/std::max(1u,fps_));
  if(next_capture_.time_since_epoch().count()&&now<next_capture_)return false;
  if(!next_capture_.time_since_epoch().count()||now-next_capture_>interval*2)next_capture_=now+interval;
  else next_capture_+=interval;
  if(FAILED(device_->OpenSharedResource(
      reinterpret_cast<HANDLE>(static_cast<uintptr_t>(handle)),
      IID_PPV_ARGS(&source)))||!source){
    debugf("virtual display: OpenSharedResource failed for handle=%p",
           reinterpret_cast<void*>(static_cast<uintptr_t>(handle)));
    return false;
  }

  ComPtr<IDXGIKeyedMutex> keyed;
  const bool has_keyed_mutex=SUCCEEDED(source.As(&keyed));
  if(has_keyed_mutex){
    const HRESULT acquired=keyed->AcquireSync(0,10);
    if(acquired!=S_OK){
      debugf("virtual display: AcquireSync failed: 0x%08lx",
             static_cast<unsigned long>(acquired));
      return false;
    }
  }

  bool copied=false;
  {
    std::lock_guard<std::mutex> dl(d3d_mutex_);
    D3D11_TEXTURE2D_DESC d{};
    source->GetDesc(&d);
    if(d.Width!=logged_virtual_width_||d.Height!=logged_virtual_height_||
       d.Format!=logged_virtual_format_){
      debugf("virtual display source: %ux%u format=%u samples=%u array=%u mips=%u misc=0x%08x",
             d.Width,d.Height,static_cast<unsigned>(d.Format),d.SampleDesc.Count,
             d.ArraySize,d.MipLevels,d.MiscFlags);
      logged_virtual_width_=d.Width;
      logged_virtual_height_=d.Height;
      logged_virtual_format_=d.Format;
    }
    if(d.SampleDesc.Count==1 && EnsureVirtualSlots(d.Width,d.Height,d.Format)){
      auto slot=std::find_if(slots_.begin(),slots_.end(),
                             [](const Slot&s){return !s.pending;});
      if(slot!=slots_.end()){
        // CopyResource has no HRESULT; a device-removal check catches the
        // only asynchronous failure that can invalidate the copy.
        context_->CopyResource(slot->input.Get(),source.Get());
        if(!WaitForSourceCopy(*slot)){
          if(has_keyed_mutex) keyed->ReleaseSync(0);
          return false;
        }
        if(!ConvertSlot(*slot)){
          if(has_keyed_mutex) keyed->ReleaseSync(0);
          return false;
        }
        context_->Flush();
        const HRESULT device_status=device_->GetDeviceRemovedReason();
        if(SUCCEEDED(device_status)){
          slot->pending=true;
          slot->sequence=++sequence_;
          copied=true;
        }else{
          debugf("virtual display: CopyResource device error: 0x%08lx",
                 static_cast<unsigned long>(device_status));
        }
      }
    }else{
      debug("virtual display: unsupported source texture description");
    }
  }
  if(has_keyed_mutex) keyed->ReleaseSync(0);
  return copied;
}
static bool valid_encoder(const std::string &encoder){return encoder=="hevc_nvenc"||encoder=="hevc_qsv"||encoder=="hevc_amf"||encoder=="libx265"||encoder=="hevc_v4l2request";}
static bool valid_host(const std::string &host){if(host.empty()||host.size()>253||host.find('"')!=std::string::npos||host.find(' ')!=std::string::npos)return false;if(host.find(':')!=std::string::npos){int groups=0;size_t group_size=0;bool compressed=false;bool digit=false;for(size_t i=0;i<host.size();++i){const char ch=host[i];if(ch==':'){if(i+1<host.size()&&host[i+1]==':'){if(compressed)return false;compressed=true;++i;group_size=0;continue;}if(!group_size)return false;++groups;group_size=0;continue;}if(std::isxdigit(static_cast<unsigned char>(ch))){if(++group_size>4)return false;digit=true;continue;}if(ch=='.')continue;return false;}if(group_size)++groups;return digit&&compressed?groups<=8:(groups==8);}bool label_start=true;for(char ch:host){if(std::isalnum(static_cast<unsigned char>(ch))||ch=='_'){label_start=false;continue;}if(ch=='.'||ch=='-'){if(label_start)return false;label_start=true;continue;}return false;}return !label_start;}
bool SvrtDirectMode::StartEncoder(unsigned w,unsigned h){
    if(pipe_!=INVALID_HANDLE_VALUE)return true;
    if(w>2880||h>1600){debugf("refusing %ux%u stream: maximum packed size is 2880x1600",w,h);encoder_failed_=true;return false;}
    if(!valid_encoder(encoder_)||!valid_host(host_)||ffmpeg_.empty()||ffmpeg_.find('"')!=std::string::npos){debug("refusing unsafe FFmpeg command values");encoder_failed_=true;return false;}
    if(!OpenVideoSocket()){debug("cannot open Stearlight UDP video socket");encoder_failed_=true;return false;}
    auto launch=[&](const char *cmd,HANDLE &write_pipe,HANDLE &output_pipe,HANDLE &process,DWORD &pid)->bool{
      SECURITY_ATTRIBUTES sa{sizeof(sa),nullptr,TRUE};HANDLE read_pipe=nullptr,output_write=nullptr;
      if(!CreatePipe(&read_pipe,&write_pipe,&sa,1<<20))return false;
      if(!CreatePipe(&output_pipe,&output_write,&sa,1<<20)){CloseHandle(read_pipe);CloseHandle(write_pipe);write_pipe=INVALID_HANDLE_VALUE;return false;}
      SetHandleInformation(write_pipe,HANDLE_FLAG_INHERIT,0);
      SetHandleInformation(output_pipe,HANDLE_FLAG_INHERIT,0);
      STARTUPINFOA si{};si.cb=sizeof(si);si.dwFlags=STARTF_USESTDHANDLES;si.hStdInput=read_pipe;si.hStdOutput=output_write;si.hStdError=GetStdHandle(STD_ERROR_HANDLE);
      PROCESS_INFORMATION pi{};BOOL ok=CreateProcessA(nullptr,const_cast<char*>(cmd),nullptr,nullptr,TRUE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi);
      CloseHandle(read_pipe);CloseHandle(output_write);
      if(!ok){CloseHandle(write_pipe);CloseHandle(output_pipe);write_pipe=output_pipe=INVALID_HANDLE_VALUE;return false;}
      CloseHandle(pi.hThread);process=pi.hProcess;pid=pi.dwProcessId;return true;
    };
    char main_cmd[4096];unsigned encode_fps=fps_,keyint=std::max(15u,fps_/2);
    int command_size=std::snprintf(main_cmd,sizeof(main_cmd),
      "\"%s\" -hide_banner -loglevel warning -fflags nobuffer -flags low_delay "
      "-f rawvideo -pix_fmt %s -video_size %ux%u -framerate %u -i pipe:0 -an -c:v %s -preset p1 -tune ull "
      "-zerolatency 1 -delay 0 -rc-lookahead 0 -rc cbr -b:v %uM -maxrate %uM -bufsize %uM "
      "-g %u -bf 0 -bsf:v hevc_metadata=aud=insert -flush_packets 1 -f hevc pipe:1",
      ffmpeg_.c_str(),pixel_format_.c_str(),w,h,encode_fps,encoder_.c_str(),bitrate_,bitrate_,
      std::max(1u,bitrate_/12),keyint);
    if(command_size<0||static_cast<size_t>(command_size)>=sizeof(main_cmd)){debug("refusing truncated FFmpeg command");encoder_failed_=true;return false;}
    DWORD main_pid=0;if(!launch(main_cmd,pipe_,output_pipe_,process_,main_pid)){encoder_failed_=true;debugf("failed to launch main FFmpeg: error %lu",GetLastError());CloseEncoder();return false;}
    encoded_frame_=0;
    packetizer_=std::thread(&SvrtDirectMode::PacketizerThread,this);
    encoder_failed_=false;debugf("FFmpeg started: pid=%lu raw HEVC -> UDP/FEC stream=%ux%u@%u session=%08x",main_pid,w,h,encode_fps,session_id_);return true;
}
bool SvrtDirectMode::OpenVideoSocket(){
  WSADATA data{};if(WSAStartup(MAKEWORD(2,2),&data))return false;
  char service[16];std::snprintf(service,sizeof(service),"%u",port_);
  addrinfo hints{};hints.ai_family=AF_UNSPEC;hints.ai_socktype=SOCK_DGRAM;hints.ai_protocol=IPPROTO_UDP;
  addrinfo *addresses=nullptr;if(getaddrinfo(host_.c_str(),service,&hints,&addresses)){WSACleanup();return false;}
  SOCKET selected=INVALID_SOCKET;
  for(addrinfo *it=addresses;it;it=it->ai_next){selected=socket(it->ai_family,it->ai_socktype,it->ai_protocol);if(selected==INVALID_SOCKET)continue;if(connect(selected,it->ai_addr,static_cast<int>(it->ai_addrlen))==0)break;closesocket(selected);selected=INVALID_SOCKET;}
  freeaddrinfo(addresses);
  if(selected==INVALID_SOCKET){WSACleanup();return false;}
  int buffer=4*1024*1024;setsockopt(selected,SOL_SOCKET,SO_SNDBUF,reinterpret_cast<const char*>(&buffer),sizeof(buffer));
  video_socket_=static_cast<uintptr_t>(selected);
  std::random_device random;session_id_=(static_cast<uint32_t>(random())^static_cast<uint32_t>(GetTickCount64()));if(!session_id_)session_id_=1;
  return true;
}
void SvrtDirectMode::SendControl(uint32_t code){
  const uintptr_t value=video_socket_.load();
  if(value==~uintptr_t{0}||!session_id_)return;
  stearlight_control_header packet{};
  stearlight_control_info info{};
  info.session_id=session_id_;info.code=code;
  info.timestamp_us=static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
  if(stearlight_control_encode(&packet,&info))return;
  std::lock_guard<std::mutex> lock(packet_mutex_);
  const uintptr_t live=video_socket_.load();
  if(live!=~uintptr_t{0})send(static_cast<SOCKET>(live),reinterpret_cast<const char*>(&packet),sizeof(packet),0);
}
static size_t aud_start(const std::vector<uint8_t>&data,size_t from){
  for(size_t i=from;i+5<data.size();++i){size_t nal=0;if(data[i]==0&&data[i+1]==0&&data[i+2]==1)nal=i+3;else if(data[i]==0&&data[i+1]==0&&data[i+2]==0&&data[i+3]==1)nal=i+4;if(nal&&((data[nal]>>1)&0x3f)==35)return i;}return std::string::npos;
}
void SvrtDirectMode::PacketizerThread(){
  std::vector<uint8_t> encoded;encoded.reserve(2*1024*1024);uint8_t chunk[65536];
  auto is_keyframe=[](const uint8_t *bytes,size_t size){for(size_t i=0;i+5<size;++i){size_t nal=0;if(bytes[i]==0&&bytes[i+1]==0&&bytes[i+2]==1)nal=i+3;else if(i+4<size&&bytes[i]==0&&bytes[i+1]==0&&bytes[i+2]==0&&bytes[i+3]==1)nal=i+4;if(nal){unsigned type=(bytes[nal]>>1)&0x3f;if(type>=16&&type<=21)return true;}}return false;};
  auto send_frame=[&](const uint8_t *bytes,size_t size){
    const uintptr_t socket_value=video_socket_.load();if(!size||size>STEARLIGHT_MAX_FRAME_SIZE||socket_value==~uintptr_t{0})return false;
    const uint32_t frame_id=++encoded_frame_;const uint16_t frame_flags=is_keyframe(bytes,size)?STEARLIGHT_FLAG_KEYFRAME:0;auto next_datagram=std::chrono::steady_clock::now();auto pace=[&]{next_datagram+=std::chrono::microseconds(250);while(std::chrono::steady_clock::now()<next_datagram)YieldProcessor();};auto send_packet=[&](const uint8_t *packet,int length){{std::lock_guard<std::mutex> send_lock(packet_mutex_);const uintptr_t live=video_socket_.load();if(live==~uintptr_t{0})return false;if(send(static_cast<SOCKET>(live),reinterpret_cast<const char*>(packet),length,0)==SOCKET_ERROR)return false;}pace();return true;};const uint64_t timestamp_us=static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
    const unsigned shards=static_cast<unsigned>((size+STEARLIGHT_VIDEO_SHARD_SIZE-1)/STEARLIGHT_VIDEO_SHARD_SIZE);uint8_t datagram[STEARLIGHT_DATAGRAM_SIZE];
    for(unsigned group=0,first=0;first<shards;++group,first+=STEARLIGHT_FEC_DATA_SHARDS){
      const unsigned count=std::min<unsigned>(STEARLIGHT_FEC_DATA_SHARDS,shards-first);uint8_t storage[STEARLIGHT_FEC_DATA_SHARDS][STEARLIGHT_VIDEO_SHARD_SIZE]{};const uint8_t *data[STEARLIGHT_FEC_DATA_SHARDS];
      for(unsigned i=0;i<count;++i){const size_t offset=static_cast<size_t>(first+i)*STEARLIGHT_VIDEO_SHARD_SIZE;const size_t payload=std::min<size_t>(STEARLIGHT_VIDEO_SHARD_SIZE,size-offset);std::memcpy(storage[i],bytes+offset,payload);data[i]=storage[i];stearlight_video_info info{};info.flags=frame_flags;info.session_id=session_id_;info.frame_id=frame_id;info.timestamp_us=timestamp_us;info.frame_size=static_cast<uint32_t>(size);info.fec_group=static_cast<uint16_t>(group);info.shard_id=static_cast<uint8_t>(i);info.data_shards=static_cast<uint8_t>(count);info.parity_shards=STEARLIGHT_FEC_PARITY_SHARDS;info.payload_size=static_cast<uint16_t>(payload);stearlight_video_header header;if(stearlight_video_header_encode(&header,&info))return false;std::memcpy(datagram,&header,sizeof(header));std::memcpy(datagram+sizeof(header),storage[i],payload);if(!send_packet(datagram,static_cast<int>(sizeof(header)+payload)))return false;}
      uint8_t parity[2][STEARLIGHT_VIDEO_SHARD_SIZE];stearlight_fec_encode(data,count,STEARLIGHT_VIDEO_SHARD_SIZE,parity[0],parity[1]);
      for(unsigned p=0;p<2;++p){stearlight_video_info info{};info.flags=frame_flags|STEARLIGHT_FLAG_FEC;info.session_id=session_id_;info.frame_id=frame_id;info.timestamp_us=timestamp_us;info.frame_size=static_cast<uint32_t>(size);info.fec_group=static_cast<uint16_t>(group);info.shard_id=static_cast<uint8_t>(count+p);info.data_shards=static_cast<uint8_t>(count);info.parity_shards=2;info.payload_size=STEARLIGHT_VIDEO_SHARD_SIZE;stearlight_video_header header;if(stearlight_video_header_encode(&header,&info))return false;std::memcpy(datagram,&header,sizeof(header));std::memcpy(datagram+sizeof(header),parity[p],STEARLIGHT_VIDEO_SHARD_SIZE);if(!send_packet(datagram,sizeof(datagram)))return false;}
    }
    if(frame_id==1||frame_id%fps_==0)debugf("UDP video frame=%u bytes=%zu data_shards=%u fec=10+2",frame_id,size,shards);return true;
  };
  while(output_pipe_!=INVALID_HANDLE_VALUE){DWORD read=0;if(!ReadFile(output_pipe_,chunk,sizeof(chunk),&read,nullptr)||!read)break;encoded.insert(encoded.end(),chunk,chunk+read);for(;;){size_t first=aud_start(encoded,0);if(first==std::string::npos)break;size_t second=aud_start(encoded,first+4);if(second==std::string::npos)break;if(!send_frame(encoded.data(),second)){encoder_failed_=true;return;}encoded.erase(encoded.begin(),encoded.begin()+second);}if(encoded.size()>STEARLIGHT_MAX_FRAME_SIZE){debug("invalid HEVC stream: no access-unit boundary within 8 MiB");encoder_failed_=true;break;}}
  if(!encoded.empty()&&!encoder_failed_)send_frame(encoded.data(),encoded.size());
}
void SvrtDirectMode::CloseEncoder(){
  // Invalidate the UDP handle first so PacketizerThread stops selecting it.
  // Keep the SOCKET open until after join: closesocket racing send() is UB.
  const uintptr_t socket_value=video_socket_.exchange(~uintptr_t{0});
  SOCKET socket_fd=INVALID_SOCKET;
  if(socket_value!=~uintptr_t{0}){
    socket_fd=static_cast<SOCKET>(socket_value);
    std::lock_guard<std::mutex> send_lock(packet_mutex_);
    shutdown(socket_fd,SD_BOTH);
  }
  if(pipe_!=INVALID_HANDLE_VALUE){CloseHandle(pipe_);pipe_=INVALID_HANDLE_VALUE;}
  if(process_){
    TerminateProcess(process_,0);
    const DWORD wait=WaitForSingleObject(process_,500);
    if(wait!=WAIT_OBJECT_0){
      if(packetizer_.joinable()) CancelSynchronousIo(packetizer_.native_handle());
      if(output_pipe_!=INVALID_HANDLE_VALUE){
        CancelIoEx(output_pipe_,nullptr);
        CloseHandle(output_pipe_);
        output_pipe_=INVALID_HANDLE_VALUE;
      }
    }
    CloseHandle(process_);process_=nullptr;
  }
  if(packetizer_.joinable()){
    CancelSynchronousIo(packetizer_.native_handle());
    packetizer_.join();
  }
  if(output_pipe_!=INVALID_HANDLE_VALUE){CloseHandle(output_pipe_);output_pipe_=INVALID_HANDLE_VALUE;}
  if(socket_fd!=INVALID_SOCKET){closesocket(socket_fd);WSACleanup();}
}
void SvrtDirectMode::EncoderThread(){uint64_t transmitted=0;while(running_){std::unique_lock<std::mutex> l(mutex_);ready_.wait(l,[this]{return !running_||disconnect_requested_||std::any_of(slots_.begin(),slots_.end(),[](const Slot&s){return s.pending;});});if(!running_)break;if(disconnect_requested_.exchange(false)||!receiver_available_){for(auto &slot:slots_)slot.pending=false;next_capture_={};l.unlock();CloseEncoder();l.lock();slots_.clear();encoder_failed_=false;continue;}auto it=std::max_element(slots_.begin(),slots_.end(),[](const Slot&a,const Slot&b){return (!a.pending?0:a.sequence)<(!b.pending?0:b.sequence);});if(it==slots_.end()||!it->pending)continue;Slot *slot=&*it;for(auto &queued:slots_)if(&queued!=slot&&queued.pending&&queued.sequence<slot->sequence)queued.pending=false;unsigned w=width_,h=height_;uint64_t frame=slot->sequence;l.unlock();D3D11_MAPPED_SUBRESOURCE map{};bool ok=false;bool gpu_busy=false;const auto started=std::chrono::steady_clock::now();
  // The immediate context must be serialized, but the CPU copy itself does
  // not.  Keeping this lock during a full stereo-frame memcpy made Present()
  // wait behind the encoder worker and produced visible compositor hitches.
  {
    std::lock_guard<std::mutex> dl(d3d_mutex_);
    if(process_&&WaitForSingleObject(process_,0)==WAIT_OBJECT_0)encoder_failed_=true;
    const HRESULT mapped=encoder_failed_?E_FAIL:context_->Map(slot->staging.Get(),0,D3D11_MAP_READ,D3D11_MAP_FLAG_DO_NOT_WAIT,&map);
    gpu_busy=mapped==DXGI_ERROR_WAS_STILL_DRAWING;
    ok=SUCCEEDED(mapped);
  }
  if(gpu_busy){Sleep(1);continue;}
  auto mapped_at=std::chrono::steady_clock::now();if(ok){
    if(gpu_nv12_){
      const auto *source=static_cast<const uint8_t*>(map.pData);
      for(unsigned y=0;y<h;y++)std::memcpy(frame_.data()+(size_t)y*w,source+(size_t)y*map.RowPitch,w);
      const auto *uv=source+(size_t)map.RowPitch*h;auto *uv_out=frame_.data()+(size_t)w*h;
      for(unsigned y=0;y<h/2;y++)std::memcpy(uv_out+(size_t)y*w,uv+(size_t)y*map.RowPitch,w);
    }else{
      for(unsigned y=0;y<h;y++)std::memcpy(frame_.data()+(size_t)y*w*4,(uint8_t*)map.pData+(size_t)y*map.RowPitch,(size_t)w*4);
    }
    std::lock_guard<std::mutex> dl(d3d_mutex_);
    context_->Unmap(slot->staging.Get(),0);
  }
  auto copied_at=std::chrono::steady_clock::now();
  auto write_frame=[&](HANDLE target,const std::vector<uint8_t>&bytes){const uint8_t *data=bytes.data();size_t remaining=bytes.size();while(remaining&&running_){DWORD n=0;const DWORD chunk=(DWORD)std::min<size_t>(remaining,0x7fffffffu);if(!WriteFile(target,data,chunk,&n,nullptr)||!n)return false;data+=n;remaining-=n;}return remaining==0;};
  HANDLE pipe=pipe_;if(ok&&pipe!=INVALID_HANDLE_VALUE)ok=write_frame(pipe,frame_);
  const auto main_written_at=std::chrono::steady_clock::now();
  l.lock();slot->pending=false;
  if(ok){transmitted++;if(transmitted==1||transmitted%fps_==0){const auto map_us=std::chrono::duration_cast<std::chrono::microseconds>(mapped_at-started).count(),copy_us=std::chrono::duration_cast<std::chrono::microseconds>(copied_at-mapped_at).count(),main_us=std::chrono::duration_cast<std::chrono::microseconds>(main_written_at-copied_at).count();debugf("transmitted frame=%llu source_sequence=%llu size=%ux%u map=%.2fms copy=%.2fms pipe=%.2fms",(unsigned long long)transmitted,(unsigned long long)frame,w,h,map_us/1000.0,copy_us/1000.0,main_us/1000.0);}}
  if(!ok&&running_&&!gpu_busy){const bool first_failure=!encoder_failed_.exchange(true);l.unlock();if(first_failure)debug("encoder pipe write failed; pausing before retry");CloseEncoder();l.lock();slots_.clear();l.unlock();Sleep(1000);if(running_&&receiver_available_)encoder_failed_=false;l.lock();}}}
