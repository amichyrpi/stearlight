/* Focused queue-enumeration test; no Vulkan device is required. */
#include "../driver/vulkan_wsi_shim.c"
#include <assert.h>

static uint32_t test_family_count = 1;

static void test_queue_properties(void *physical, uint32_t *count, void *data) {
  (void)physical;
  if (!data) { *count = test_family_count; return; }
  uint32_t written = *count < test_family_count ? *count : test_family_count;
  VkQueueFamilyPropertiesCompat *properties = data;
  for (uint32_t i = 0; i < written; ++i) {
    memset(&properties[i], 0, sizeof(properties[i]));
    properties[i].queueFlags = VK_QUEUE_GRAPHICS_BIT_COMPAT |
                              VK_QUEUE_COMPUTE_BIT_COMPAT |
                              VK_QUEUE_TRANSFER_BIT_COMPAT;
    properties[i].queueCount = 1;
  }
  *count = written;
}

static void test_queue_properties2(void *physical, uint32_t *count, void *data) {
  if (!data) { *count = test_family_count; return; }
  uint32_t written = *count < test_family_count ? *count : test_family_count;
  VkQueueFamilyProperties2Compat *properties = data;
  for (uint32_t i = 0; i < written; ++i) {
    uint32_t one = 1;
    test_queue_properties(physical, &one, &properties[i].queueFamilyProperties);
  }
  *count = written;
}

static void *test_instance_proc(void *instance, const char *name) {
  (void)instance;
  if (!strcmp(name, "vkGetPhysicalDeviceQueueFamilyProperties"))
    return (void *)(uintptr_t)&test_queue_properties;
  if (!strcmp(name, "vkGetPhysicalDeviceQueueFamilyProperties2") ||
      !strcmp(name, "vkGetPhysicalDeviceQueueFamilyProperties2KHR"))
    return (void *)(uintptr_t)&test_queue_properties2;
  return NULL;
}

int main(void) {
  void *physical = (void *)(uintptr_t)2;
  active_vulkan_instance = (void *)(uintptr_t)1;
  real_get_instance_proc_addr = &test_instance_proc;
  real_get_queue_family_properties = &test_queue_properties;
  real_get_queue_family_properties2 = &test_queue_properties2;
  const uint32_t combined = VK_QUEUE_GRAPHICS_BIT_COMPAT |
                            VK_QUEUE_COMPUTE_BIT_COMPAT |
                            VK_QUEUE_TRANSFER_BIT_COMPAT;
  uint32_t count = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, NULL);
  assert(count == 2);
  VkQueueFamilyPropertiesCompat properties[2] = {0};
  vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, properties);
  assert(count == 2 && properties[0].queueFlags == combined);
  assert(!(properties[1].queueFlags & VK_QUEUE_GRAPHICS_BIT_COMPAT));
  assert(properties[1].queueFlags & VK_QUEUE_COMPUTE_BIT_COMPAT);
  count = 1;
  vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, properties);
  assert(count == 1 && properties[0].queueFlags == combined);
  VkQueueFamilyProperties2Compat properties2[2] = {0};
  properties2[0].pNext = (void *)(uintptr_t)3;
  count = 2;
  vkGetPhysicalDeviceQueueFamilyProperties2(physical, &count, properties2);
  assert(count == 2 && properties2[0].queueFamilyProperties.queueFlags == combined);
  assert(properties2[0].pNext == (void *)(uintptr_t)3);
  assert(!(properties2[1].queueFamilyProperties.queueFlags & VK_QUEUE_GRAPHICS_BIT_COMPAT));
  count = 1;
  vkGetPhysicalDeviceQueueFamilyProperties2KHR(physical, &count, properties2);
  assert(count == 1 && properties2[0].queueFamilyProperties.queueFlags == combined);
  test_family_count = 2;
  count = 2;
  vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, properties);
  assert(count == 2 && properties[0].queueFlags == combined && properties[1].queueFlags == combined);
  puts("PASS: combined queue capabilities, compute alias, truncated queries and multiple families");
  return 0;
}
