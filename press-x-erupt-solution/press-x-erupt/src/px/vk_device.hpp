// name: vk_device
// type: c++ header
// desc: wrapper class for vulkan logical device
// auth: is0urce

#pragma once

#include <vulkan/vulkan.hpp>

#include "vk_instance.hpp"

#include <set>

namespace px
{
	class vk_device final
	{
	public:
		operator VkDevice() const noexcept
		{
			return m_device;
		}
		void release() noexcept
		{
			if (m_device != VK_NULL_HANDLE)
			{
				vkDestroyDevice(m_device, nullptr);
				m_device = VK_NULL_HANDLE;
			}
		}
		void create(VkPhysicalDevice physical, std::vector<int> const& queues, uint32_t layer_count, const char* const* layers, uint32_t extension_count, const char* const* extensions)
		{
			release();

			std::vector<VkDeviceQueueCreateInfo> queue_create_infos;
			float queue_priority = 1.0f;
			for (int family : std::set<int>{ std::begin(queues), std::end(queues) })
			{
				VkDeviceQueueCreateInfo queue_create_info = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
				queue_create_info.queueFamilyIndex = family;
				queue_create_info.queueCount = 1;
				queue_create_info.pQueuePriorities = &queue_priority;

				queue_create_infos.push_back(queue_create_info);
			}

			VkPhysicalDeviceFeatures device_features{}; // request no additional features

			VkDeviceCreateInfo create_info{ VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
			create_info.pQueueCreateInfos = queue_create_infos.data();
			create_info.queueCreateInfoCount = static_cast<uint32_t>(queue_create_infos.size());
			create_info.pEnabledFeatures = &device_features;
			create_info.enabledLayerCount = layer_count;
			create_info.ppEnabledLayerNames = layers;
			create_info.enabledExtensionCount = extension_count;
			create_info.ppEnabledExtensionNames = extensions;

			if (vkCreateDevice(physical, &create_info, nullptr, &m_device) != VK_SUCCESS)
			{
				throw std::runtime_error("failed to create logical device!");
			}
		}

	public:
		vk_device() noexcept
			: m_device(VK_NULL_HANDLE)
		{
		}
		vk_device(VkPhysicalDevice physical, std::vector<int> const& queues, uint32_t layer_count, const char* const* layers, uint32_t extension_count, const char* const* extensions)
			: vk_device()
		{
			create(physical, queues, layer_count, layers, extension_count, extensions);
		}
		vk_device(vk_device const&) = delete;
		vk_device& operator=(vk_device const&) = delete;
		vk_device(vk_device && device) noexcept
			: vk_device()
		{
			std::swap(m_device, device.m_device);
		}
		vk_device& operator=(vk_device && device) noexcept
		{
			std::swap(m_device, device.m_device);
			return *this;
		}
		~vk_device()
		{
			release();
		}

	private:
		VkDevice m_device;
	};
}