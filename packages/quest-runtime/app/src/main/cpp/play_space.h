#pragma once
#include "quest_bridge.h"
#include <cmath>
#include <cstdio>
#include <string>

// Raw Quest poses use OpenXR's floor-level STAGE space. SteamVR also needs
// an explicit seated origin; otherwise seated games receive invalid poses.
// Horizon retains its own Guardian. A stage rectangle is not a wall polygon.
inline std::string questPlaySpace(const QuestPose& head, float width, float depth,
                                  const char* timestamp) {
 if ((head.flags & 3) != 3) return {};
 for (float value : head.position) if (!std::isfinite(value)) return {};
 float norm = 0;
 for (float value : head.orientation) {
  if (!std::isfinite(value)) return {};
  norm += value * value;
 }
 if (norm < .9f || norm > 1.1f) return {};
 const auto& q = head.orientation;
 float yaw = std::atan2(2 * (q[3] * q[1] + q[0] * q[2]),
                        1 - 2 * (q[0] * q[0] + q[1] * q[1]));
 char seated[256];
 // SteamVR stores the negative raw-space offset in its chaperone JSON;
 // GetSeatedZeroPoseToStandingAbsoluteTrackingPose returns its inverse.
 std::snprintf(seated, sizeof(seated),
  "\"seated\":{\"translation\":[%.9g,%.9g,%.9g],\"yaw\":%.9g}",
  -head.position[0], -head.position[1], -head.position[2], yaw);
 std::string area;
 if (std::isfinite(width) && std::isfinite(depth) && width > 0 && depth > 0) {
  char buffer[128];
  std::snprintf(buffer, sizeof(buffer), ",\"play_area\":[%.9g,%.9g]", width, depth);
  area = buffer;
 }
 return std::string("{\"jsonid\":\"chaperone_info\",\"version\":5,\"time\":\"") +
  timestamp + "\",\"universes\":[{\"universeID\":2,\"collision_bounds\":[]," +
  seated + ",\"standing\":{\"translation\":[0,0,0],\"yaw\":0}" + area + "}]}\n";
}
