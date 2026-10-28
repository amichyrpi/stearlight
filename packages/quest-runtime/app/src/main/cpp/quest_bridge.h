#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <sys/syscall.h>
#include <linux/futex.h>
#include <unistd.h>

enum { QUEST_BRIDGE_MAGIC=0x53515231, QUEST_BRIDGE_VERSION=2 };
enum QuestButtons { QUEST_A=1,QUEST_B=2,QUEST_X=4,QUEST_Y=8,QUEST_THUMBSTICK=16,QUEST_MENU=32 };
struct QuestPose { float position[3]; float orientation[4]; uint32_t flags; };
// Private source tag; low bits keep the OpenXR location flags and wire size.
constexpr uint32_t QUEST_POSE_HAND = 1u << 16;
constexpr uint32_t QUEST_POSE_TOUCH_PLUS = 1u << 17;
// Private Unix datagram from the OpenVR driver to the OpenXR frame thread.
struct QuestHaptic {
 uint32_t magic,hand;
 float duration_seconds,frequency,amplitude;
};
static_assert(sizeof(QuestHaptic)==20,"Quest haptic wire layout");
struct QuestTracking {
 uint32_t magic,version,sequence,width,height;
 float refresh_hz;
 uint64_t predicted_time_ns;
 uint32_t session_visible,session_focused;
 QuestPose headset,grip[2],aim[2],eyes[2];
 float fov[2][4];
 uint32_t buttons[2],touches[2];
 float trigger[2],squeeze[2],thumbstick[2][2];
 float battery[3];
 uint32_t charging,connected,wifi,bluetooth;
 uint64_t predicted_monotonic_ns;
};

// Sample OpenXR before calling this: Linux readers must not wait behind
// runtime/controller queries while the shared sequence is marked busy.
inline void questPublishTracking(QuestTracking* shared, const QuestTracking& sample) {
 __atomic_add_fetch(&shared->sequence,1,__ATOMIC_ACQ_REL);
 constexpr size_t offset=offsetof(QuestTracking,width);
 memcpy(reinterpret_cast<char*>(shared)+offset,reinterpret_cast<const char*>(&sample)+offset,sizeof(sample)-offset);
 __atomic_add_fetch(&shared->sequence,1,__ATOMIC_RELEASE);
 // Wake the Linux display pulse after publishing the complete OpenXR sample.
 syscall(SYS_futex,&shared->sequence,FUTEX_WAKE,1,nullptr,nullptr,0);
}
