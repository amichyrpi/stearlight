#pragma once
#include <openvr_driver.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <atomic>
#include <cstdint>
#include <condition_variable>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

class SvrtDirectMode final : public vr::IVRDriverDirectModeComponent {
 public:
  SvrtDirectMode(); ~SvrtDirectMode();
  bool Start(const std::string &host, uint16_t port, unsigned fps,
             unsigned bitrate_mbps, const std::string &ffmpeg,
             const std::string &encoder);
  bool EnsureDevice();
  void Stop();
  void SetReceiverAvailable(bool available);
  void SetNetworkStats(uint64_t invalid_packets,uint64_t recovered_shards,uint64_t dropped_frames);
  bool ReceiverAvailable() const { return receiver_available_.load(); }
  bool IsRunning() const { return running_.load(); }
  // LUID of the adapter that owns the D3D device. SteamVR uses this on a
  // display-redirect device to route the compositor backbuffer to the same
  // GPU that the driver opens with OpenSharedResource.
  uint64_t GraphicsAdapterLuid() const;
  bool EncoderFailed() const { return encoder_failed_.load(); }
  void CreateSwapTextureSet(uint32_t pid,const SwapTextureSetDesc_t *desc,SwapTextureSet_t *out) override;
  void DestroySwapTextureSet(vr::SharedTextureHandle_t handle) override;
  void DestroyAllSwapTextureSets(uint32_t pid) override;
  void GetNextSwapTextureSetIndex(vr::SharedTextureHandle_t handles[2],uint32_t (*indices)[2]) override;
  void SubmitLayer(const SubmitLayerPerEye_t (&eyes)[2]) override;
  void Present(vr::SharedTextureHandle_t sync) override;
  // IVRVirtualDisplay supplies the final compositor backbuffer through this
  // path. Unlike driver-direct mode, it preserves SteamVR's mirror/VR View.
  void PresentVirtual(vr::SharedTextureHandle_t backbuffer);
  void WaitForVirtualPresent();
  void PostPresent(const Throttling_t *) override {}
  void GetFrameTiming(vr::DriverDirectMode_FrameTiming *timing) override { timing->m_nReprojectionFlags=0; }
 private:
  struct Texture { uint32_t pid=0; uint64_t group=0; Microsoft::WRL::ComPtr<ID3D11Texture2D> texture; };
  struct Slot {
    Microsoft::WRL::ComPtr<ID3D11Texture2D> input, converted, staging;
    Microsoft::WRL::ComPtr<ID3D11VideoProcessorInputView> input_view;
    Microsoft::WRL::ComPtr<ID3D11VideoProcessorOutputView> output_view;
    Microsoft::WRL::ComPtr<ID3D11Query> source_copied;
    bool pending=false; uint64_t sequence=0;
  };
  bool EnsureSlots(unsigned eye_width,unsigned height,DXGI_FORMAT format);
  bool EnsureDeviceLocked();
  bool EnsureVirtualSlots(unsigned width,unsigned height,DXGI_FORMAT format);
  bool EnsureGpuConversion(unsigned width,unsigned height,DXGI_FORMAT format);
  bool ConvertSlot(Slot &slot);
  bool WaitForSourceCopy(Slot &slot);
  bool CopyVirtualFrame(vr::SharedTextureHandle_t handle);
  bool StartEncoder(unsigned width,unsigned height);
  void EncoderThread(); void PacketizerThread(); void CloseEncoder();
  bool OpenVideoSocket();
  void SendControl(uint32_t code);
  Microsoft::WRL::ComPtr<ID3D11Device> device_; Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
  Microsoft::WRL::ComPtr<ID3D11VideoDevice> video_device_;
  Microsoft::WRL::ComPtr<ID3D11VideoContext> video_context_;
  Microsoft::WRL::ComPtr<ID3D11VideoProcessorEnumerator> video_enumerator_;
  Microsoft::WRL::ComPtr<ID3D11VideoProcessor> video_processor_;
  std::unordered_map<uint64_t,Texture> textures_; std::vector<Slot> slots_;
  vr::SharedTextureHandle_t submitted_[2]{}; uint32_t next_[2]{};
  // lifecycle_mutex_ serializes the short callback-side access to the D3D
  // objects with Stop().  OpenVR may deliver a final Present while the
  // provider is being cleaned up, so checking a raw ComPtr without this gate
  // is a use-after-reset race.
  mutable std::mutex lifecycle_mutex_;
  std::mutex mutex_,d3d_mutex_,packet_mutex_; std::condition_variable ready_; std::thread worker_;
  std::vector<uint8_t> frame_;
  bool gpu_nv12_=false;
  std::atomic<bool> running_{false},accepting_{false},encoder_failed_{false},receiver_available_{false},disconnect_requested_{false};
  uint64_t sequence_=0; unsigned width_=0,height_=0,fps_=60,bitrate_=8,bitrate_ceiling_=8;
  uint64_t last_invalid_=0,last_recovered_=0,last_network_dropped_=0;
  std::chrono::steady_clock::time_point last_network_adjust_{};
  unsigned stable_intervals_=0;
  unsigned congested_intervals_=0;
  unsigned network_intervals_=0;
  std::chrono::steady_clock::time_point next_capture_{};
  unsigned logged_virtual_width_=0,logged_virtual_height_=0;
  DXGI_FORMAT logged_virtual_format_=DXGI_FORMAT_UNKNOWN;
  std::string host_,ffmpeg_,encoder_,pixel_format_="bgra"; uint16_t port_=9944;
  HANDLE pipe_=INVALID_HANDLE_VALUE,output_pipe_=INVALID_HANDLE_VALUE,process_=nullptr;
  std::atomic<uintptr_t> video_socket_{~uintptr_t{0}};
  std::thread packetizer_;
  uint32_t session_id_=0;
  std::atomic<uint32_t> encoded_frame_{0};
};
