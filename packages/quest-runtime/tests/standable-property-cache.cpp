// Compile against the patched Ignition common/OpenVR headers; no Steam needed.
#include <quest_property_cache.h>
#include <cstdio>
#include <cstdlib>

#define CHECK(value) do { if (!(value)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #value); std::exit(1); } } while (0)

int main() {
    using Clock = QuestPropertyCache::Clock;
    auto now = Clock::time_point{} + std::chrono::seconds(10);
    QuestPropertyCache cache;
    float battery = 0.5f;
    vr::PropertyWrite_t write{};
    write.prop = vr::Prop_DeviceBatteryPercentage_Float;
    write.writeType = vr::PropertyWrite_Set;
    write.pvBuffer = &battery;
    write.unBufferSize = sizeof(battery);
    write.unTag = vr::k_unFloatPropertyTag;
    unsigned forwarded = 0;
    auto success = [&] { ++forwarded; write.eError = vr::TrackedProp_Success; return vr::TrackedProp_Success; };
    for (unsigned i = 0; i < 1000; ++i) {
        write.eError = vr::TrackedProp_NotYetAvailable;
        CHECK(cache.Write(1, &write, 1, success, now) == vr::TrackedProp_Success);
        CHECK(write.eError == vr::TrackedProp_Success);
    }
    CHECK(forwarded == 1);
    battery = 0.6f;
    cache.Write(1, &write, 1, success, now);
    CHECK(forwarded == 2); // Changed battery is immediate.
    cache.Write(2, &write, 1, success, now);
    CHECK(forwarded == 3); // Never merge different devices.
    cache.Write(1, &write, 1, success, now + std::chrono::seconds(1));
    CHECK(forwarded == 4); // Refresh even an unchanged successful write.
    write.unTag = vr::k_unInt32PropertyTag;
    cache.Write(1, &write, 1, success, now + std::chrono::seconds(1));
    CHECK(forwarded == 5);
    write.writeType = vr::PropertyWrite_Erase;
    cache.Write(1, &write, 1, success, now + std::chrono::seconds(1));
    CHECK(forwarded == 6);
    write.writeType = vr::PropertyWrite_Set;
    cache.Write(1, &write, 1, success, now + std::chrono::seconds(1));
    CHECK(forwarded == 7); // Erasing invalidates the prior value.
    battery = 0.7f;
    auto failure = [&] { ++forwarded; write.eError = vr::TrackedProp_InvalidDevice; return vr::TrackedProp_InvalidDevice; };
    CHECK(cache.Write(1, &write, 1, failure, now) == vr::TrackedProp_InvalidDevice);
    CHECK(write.eError == vr::TrackedProp_InvalidDevice);
    cache.Write(1, &write, 1, success, now);
    CHECK(forwarded == 9); // Retry failed writes.
    vr::PropertyWrite_t batch[2]{write, write};
    auto multi = [&] { ++forwarded; return vr::TrackedProp_Success; };
    cache.Write(1, batch, 2, multi, now);
    cache.Write(1, &write, 1, success, now);
    CHECK(forwarded == 11); // Forward batches and invalidate cached entries.

    QuestContainerCache containers;
    unsigned lookups = 0;
    auto handle = [&]() -> vr::PropertyContainerHandle_t { ++lookups; return 123; };
    for (unsigned i = 0; i < 1000; ++i) CHECK(containers.Get(3, handle, now) == 123);
    CHECK(lookups == 1);
    CHECK(containers.Get(3, handle, now + std::chrono::seconds(1)) == 123);
    CHECK(lookups == 2);
    auto invalid = [&]() -> vr::PropertyContainerHandle_t { ++lookups; return vr::k_ulInvalidPropertyContainer; };
    containers.Get(4, invalid, now);
    containers.Get(4, invalid, now);
    CHECK(lookups == 4); // Invalid/reused slots must not stick.
    CHECK(containers.Get(4, handle, now) == 123);
    CHECK(lookups == 5);
    CHECK(containers.Get(vr::k_unMaxTrackedDeviceCount, handle, now) == 123);
    CHECK(lookups == 6);
    std::puts("PASS: repeated writes/lookups, changed values, expiry, errors, batches and device isolation");
}
