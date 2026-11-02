Vulkan headers for vinput's runtime-resolved Vulkan capture (VK_NO_PROTOTYPES; vulkan-1.dll is never linked).

vk_platform.h, vulkan_core.h, vulkan_win32.h, vulkan.h: unmodified Khronos Vulkan-Headers, header version 1.4.356
(VK_HEADER_VERSION 356), Apache-2.0 OR MIT - see LICENSE.txt. Upstream: https://github.com/KhronosGroup/Vulkan-Headers

vk_video\*.h: project STUBS, not Khronos files. Opaque int placeholders for the StdVideo* names vulkan_core.h
mentions; any video struct that embeds one has the wrong layout. vinput and its tests never use Vulkan video.
