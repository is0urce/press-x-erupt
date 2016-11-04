// name: vk_instance
// type: c++ header
// desc: wrapper class for vulkan instance
// auth: is0urce

#pragma once

// move assignable and supports implicit cast to VkInstance
// default construction not creates handle

#include <vulkan/vulkan.hpp>

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace px
{
	class vk_instance final
	{
	public:
		operator VkInstance() const noexcept
		{
			return m_instance;
		}
		uint32_t layer_count() const noexcept
		{
			return static_cast<uint32_t>(m_layers.size());
		}
		const char* const* layers() const noexcept
		{
			return m_layers.size() != 0 ? m_layers.data() : nullptr;
		}
		void create(uint32_t count, const char** extension_names, bool enable_debug)
		{
			release();

			if (enable_debug)
			{
				m_layers.push_back("VK_LAYER_LUNARG_standard_validation");
				if (!support_layers(m_layers))
				{
					throw std::runtime_error("px::vk_instance::vk_instance() - validation layers requested, but not available");
				}
			}

			auto extensions = required_extensions(count, extension_names, enable_debug);

			VkApplicationInfo application_info{ VK_STRUCTURE_TYPE_APPLICATION_INFO };
			application_info.pApplicationName = "renderer";
			application_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
			application_info.apiVersion = VK_API_VERSION_1_0;

			VkInstanceCreateInfo instance_info{ VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
			instance_info.pApplicationInfo = &application_info;
			instance_info.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
			instance_info.ppEnabledExtensionNames = extensions.empty() ? nullptr : extensions.data();
			instance_info.enabledLayerCount = layer_count();
			instance_info.ppEnabledLayerNames = layers();

			if (vkCreateInstance(&instance_info, nullptr, &m_instance) != VK_SUCCESS)
			{
				throw std::runtime_error("px::vk_instance::create_instance - failed to create instance");
			}

			if (enable_debug)
			{
				start_debug();
			}
		}
		void start_debug()
		{
			stop_debug();

			if (m_instance != VK_NULL_HANDLE)
			{
				VkDebugReportCallbackCreateInfoEXT info{ VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT };
				info.flags = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT;
				info.pfnCallback = debug_callback;

				if (CreateDebugReportCallbackEXT(m_instance, &info, nullptr, &m_debug_callback) != VK_SUCCESS)
				{
					throw std::runtime_error("px::vk_device::setup_debug() - failed to set up debug callback!");
				}
			}
		}
		void stop_debug()
		{
			if (m_debug_callback != VK_NULL_HANDLE)
			{
				DestroyDebugReportCallbackEXT(m_instance, m_debug_callback, nullptr);
				m_debug_callback = VK_NULL_HANDLE;
			}
		}
		void release()
		{
			stop_debug();
			if (m_instance != VK_NULL_HANDLE)
			{
				vkDestroyInstance(m_instance, nullptr);
				m_instance = VK_NULL_HANDLE;
			}
		}

	public:
		vk_instance() noexcept
			: m_instance(VK_NULL_HANDLE)
			, m_debug_callback(VK_NULL_HANDLE)
		{
		}
		vk_instance(uint32_t count, const char** extension_names, bool enable_debug)
			: vk_instance()
		{
			create(count, extension_names, enable_debug);
		}
		vk_instance(vk_instance const&) = delete;
		vk_instance& operator=(vk_instance const&) = delete;
		vk_instance(vk_instance && instance) noexcept
			: vk_instance()
		{
			std::swap(m_instance, instance.m_instance);
			std::swap(m_debug_callback, instance.m_debug_callback);
			std::swap(m_layers, instance.m_layers);
		}
		vk_instance& operator=(vk_instance && instance) noexcept
		{
			std::swap(m_instance, instance.m_instance);
			std::swap(m_debug_callback, instance.m_debug_callback);
			std::swap(m_layers, instance.m_layers);
			return *this;
		}
		~vk_instance()
		{
			release();
		}

	private:
		bool support_layers(std::vector<const char*> const& validation_list) const
		{
			uint32_t count;
			vkEnumerateInstanceLayerProperties(&count, nullptr);
			std::vector<VkLayerProperties> layers(count);
			vkEnumerateInstanceLayerProperties(&count, layers.data());

			// every layer has his support
			return std::all_of(std::begin(validation_list), std::end(validation_list), [&layers](const char* layer_name) {
				return std::any_of(std::begin(layers), std::end(layers), [name = std::string(layer_name)](auto layer) { return name == layer.layerName; }); });
		}
		std::vector<const char*> required_extensions(uint32_t count, const char** names, bool add_debug) const
		{
			std::vector<const char*> extensions;

			for (uint32_t i = 0; i != count; ++i)
			{
				extensions.push_back(names[i]);
			}

			if (add_debug)
			{
				extensions.push_back(VK_EXT_DEBUG_REPORT_EXTENSION_NAME);
			}

			return extensions;
		}

		static VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(
			VkDebugReportFlagsEXT /*flags*/,
			VkDebugReportObjectTypeEXT /*object_type*/,
			uint64_t /*obj*/,
			size_t /*location*/,
			int32_t code,
			const char* /*prefix*/,
			const char* msg,
			void* /*userData*/)
		{
			std::cerr << "validation layer: " << msg << " code: " << code << std::endl;
			return VK_FALSE;
		}
		static VkResult CreateDebugReportCallbackEXT(VkInstance instance, const VkDebugReportCallbackCreateInfoEXT* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDebugReportCallbackEXT* pCallback)
		{
			auto func = (PFN_vkCreateDebugReportCallbackEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugReportCallbackEXT");
			if (func != nullptr)
			{
				return func(instance, pCreateInfo, pAllocator, pCallback);
			}
			else
			{
				return VK_ERROR_EXTENSION_NOT_PRESENT;
			}
		}
		static void DestroyDebugReportCallbackEXT(VkInstance instance, VkDebugReportCallbackEXT callback, const VkAllocationCallbacks* pAllocator)
		{
			auto func = (PFN_vkDestroyDebugReportCallbackEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugReportCallbackEXT");
			if (func != nullptr)
			{
				func(instance, callback, pAllocator);
			}
		}

	private:
		VkInstance m_instance;
		VkDebugReportCallbackEXT m_debug_callback;

		std::vector<const char*> m_layers;
	};
}