#pragma once

#include "application_base.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace px
{
	class application : public application_base
	{
	public:
		application()
		{
			VkApplicationInfo app_info{};
			app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
			app_info.pApplicationName = name().c_str();
			app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
			app_info.apiVersion = VK_API_VERSION_1_0;

			if (validate && !layer_support(validation)) {
				throw std::runtime_error("validation layers requested, but not available!");
			}
			auto extensions = required_extensions();

			VkInstanceCreateInfo create_info{};
			create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
			create_info.pApplicationInfo = &app_info;
			create_info.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
			create_info.ppEnabledExtensionNames = extensions.data();
			create_info.enabledLayerCount = static_cast<uint32_t>(validation.size());
			create_info.ppEnabledLayerNames = validation.size() > 0 ? validation.data() : nullptr;

			VkInstance instance;
			if (vkCreateInstance(&create_info, nullptr, &instance) != VK_SUCCESS) {
				throw std::runtime_error("failed to create instance!");
			}

			VkDebugReportCallbackCreateInfoEXT create_debug_info = {};
			create_debug_info.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT;
			create_debug_info.flags = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT;
			create_debug_info.pfnCallback = debug_callback;

			if (validate)
			{
				VkDebugReportCallbackEXT callback;

				if (CreateDebugReportCallbackEXT(instance, &create_debug_info, nullptr, &callback) != VK_SUCCESS) {
					throw std::runtime_error("failed to set up debug callback!");
				}

				DestroyDebugReportCallbackEXT(instance, callback, nullptr);
			}

			vkDestroyInstance(instance, nullptr);
		}

		virtual ~application()
		{
		}

	private:
		bool layer_support(std::vector<const char*> const& validation_list) const
		{
			uint32_t count;
			vkEnumerateInstanceLayerProperties(&count, nullptr);
			std::vector<VkLayerProperties> layers(count);
			vkEnumerateInstanceLayerProperties(&count, layers.data());

			return std::all_of(std::begin(validation_list), std::end(validation_list), [&layers](const char* layer_name) {
				return std::any_of(std::begin(layers), std::end(layers), [name = std::string(layer_name)](auto layer) { return name == layer.layerName; }); });
		}
		std::vector<const char*> required_extensions() const {
			std::vector<const char*> extensions;

			uint32_t count = 0;
			const char** glfwExtensions;
			glfwExtensions = glfwGetRequiredInstanceExtensions(&count);

			for (uint32_t i = 0; i < count; ++i)
			{
				extensions.push_back(glfwExtensions[i]);
			}

			if (validate)
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

			std::cerr << "validation layer: " << msg << " code: " << std::to_string(code) << std::endl;

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
#ifndef _DEBUG
		static const bool validate = false;
		const std::vector<const char*> validation = {};
#else
		static const bool validate = true;
		const std::vector<const char*> validation = {
			"VK_LAYER_LUNARG_standard_validation"
		};
#endif

	};
}