#pragma once
#include <stdint.h>
// Private, same-uid socket. Packets contain Annex-B H.264/HEVC access units.
enum { QUEST_VIDEO_MAGIC=0x53515631, QUEST_VIDEO_CONFIGURE=1,
       QUEST_VIDEO_PACKET=2, QUEST_VIDEO_POLL=3, QUEST_VIDEO_MAX_PACKET=8*1024*1024,
       QUEST_VIDEO_MAX_DIMENSION=8192, QUEST_VIDEO_MAX_PIXELS=4096*4096,
       QUEST_VIDEO_MAX_FRAME=QUEST_VIDEO_MAX_PIXELS*3/2 };
struct QuestVideoRequest { uint32_t magic,operation,bytes,width,height,codec;uint64_t timestamp; };
// status: 0 = NV12 frame, 1 = accepted/no frame, negative = decoder failure.
struct QuestVideoResponse { uint32_t magic;int32_t status;uint32_t bytes,width,height,color;uint64_t timestamp; };
static_assert(sizeof(QuestVideoRequest)==32 && sizeof(QuestVideoResponse)==32,"video wire format");
