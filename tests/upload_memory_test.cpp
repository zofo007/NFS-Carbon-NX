// Exercise the installed VMA policy, not a copy of its selection algorithm.
#define VMA_IMPLEMENTATION
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 0
#include <vk_mem_alloc.h>
#include <cassert>

#ifdef NDEBUG
#error These tests require enabled assertions.
#endif

int main() {
  VmaAllocationCreateInfo info{};
  info.usage = VMA_MEMORY_USAGE_AUTO;
  VkMemoryPropertyFlags required, preferred, avoided;
  const VmaBufferImageUsage usage(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT |
                                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                 VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                                 VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
  info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
               VMA_ALLOCATION_CREATE_MAPPED_BIT;
  assert(FindMemoryPreferences(true, info, usage, required, preferred, avoided));
  assert(required & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
  assert(avoided & VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
  info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT |
               VMA_ALLOCATION_CREATE_MAPPED_BIT;
  assert(FindMemoryPreferences(true, info, usage, required, preferred, avoided));
  assert(required & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
  assert(preferred & VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
  assert(!(required & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT));
  assert(!(required & VK_MEMORY_PROPERTY_HOST_CACHED_BIT));  // Coherent fallback allowed.
  const VkMemoryPropertyFlags cached = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
  const VkMemoryPropertyFlags coherent = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
  assert((cached & required) == required && (coherent & required) == required);
  assert((cached & preferred) != (coherent & preferred));
}
