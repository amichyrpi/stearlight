#pragma once
#include <stdint.h>
// Private, same-uid socket. Packets contain Annex-B H.264/HEVC access units.
enum { QUEST_VIDEO_MAGIC=0x53515631, QUEST_VIDEO_CONFIGURE=1,
       QUEST_VIDEO_PACKET=2, QUEST_VIDEO_POLL=3, QUEST_VIDEO_SHARED_OUTPUT=4, QUEST_VIDEO_MAX_PACKET=8*1024*1024,
       QUEST_VIDEO_MAX_DIMENSION=8192, QUEST_VIDEO_MAX_PIXELS=4096*4096,
       QUEST_VIDEO_MAX_FRAME=QUEST_VIDEO_MAX_PIXELS*3/2 };
struct QuestVideoRequest { uint32_t magic,operation,bytes,width,height,codec;uint64_t timestamp; };
enum { QUEST_VIDEO_FRAME_READY=0, QUEST_VIDEO_ACCEPTED=1, QUEST_VIDEO_FORMAT_CHANGED=2 };
// A format notification has dimensions but no pixel payload or frame tag.
// timestamp is an opaque guest frame tag, not an Android presentation clock.
// Negative status reports a decoder failure.
struct QuestVideoResponse { uint32_t magic;int32_t status;uint32_t bytes,width,height,color;uint64_t timestamp; };
static_assert(sizeof(QuestVideoRequest)==32 && sizeof(QuestVideoResponse)==32,"video wire format");
