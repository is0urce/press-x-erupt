#pragma once

#include <px/core/basic_application.hpp>
#include <px/vk_instance.hpp>
#include <px/vk_device.hpp>

#pragma warning(push)	// disable for this header only & restore original warning level
#pragma warning(disable:4201) // unions for rgba and xyzw
#include <glm/glm.hpp>
#pragma warning(pop)

#include <algorithm>
#include <array>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace px
{
	class renderer
	{
	public:
		struct queues
		{
			int graphics;
			int presentation;
			explicit operator bool() const noexcept
			{
				return graphics >= 0 && presentation >= 0;
			}
			bool match() const noexcept
			{
				return graphics == presentation;
			}
		};
		struct swapchain_details
		{
			VkSurfaceCapabilitiesKHR capabilities;
			std::vector<VkSurfaceFormatKHR> formats;
			std::vector<VkPresentModeKHR> presentation_modes;
			explicit operator bool() const noexcept
			{
				return !formats.empty() && !presentation_modes.empty();
			}
		};
		struct vertex
		{
			glm::vec2 position;
			glm::vec3 color;
			constexpr static VkVertexInputBindingDescription binding_description()
			{
				return VkVertexInputBindingDescription{ 0 , sizeof(vertex), VK_VERTEX_INPUT_RATE_VERTEX };
			}
			constexpr static std::array<VkVertexInputAttributeDescription, 2> attribute_descriptions()
			{
				return std::array<VkVertexInputAttributeDescription, 2>	{
					VkVertexInputAttributeDescription{ 0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(vertex, position) },
					VkVertexInputAttributeDescription{ 0, 1, VK_FORMAT_R32G32_SFLOAT, offsetof(vertex, color) }
				};
			}
		};

		renderer(basic_application & application)
			: m_physical_device(VK_NULL_HANDLE)
			, m_swapchain(VK_NULL_HANDLE)
			, m_pipeline_layout(VK_NULL_HANDLE)
			, m_pipeline(VK_NULL_HANDLE)
			, m_renderpass(VK_NULL_HANDLE)
			, m_width(application.width())
			, m_height(application.height())
		{

			uint32_t count = 0;
			const char** extensions;
			extensions = glfwGetRequiredInstanceExtensions(&count);
			m_instance.create(count, extensions, validate);
			if (glfwCreateWindowSurface(m_instance, application.window(), nullptr, &m_surface) != VK_SUCCESS)
			{
				throw std::runtime_error("failed to create window surface!");
			}

			select_physical_device();
			create_logical_device();
			create_swapchain();
			create_image_views();
			create_renderpass();
			create_pipeline();
			create_framebuffers();
			create_command_pool();
			create_command_buffers();
			create_semaphores();
		}

		renderer(renderer const&) = delete;
		renderer& operator=(renderer const&) = delete;
		virtual ~renderer()
		{
			vkDeviceWaitIdle(m_device);

			vkDestroySemaphore(m_device, m_image_available, nullptr);
			vkDestroySemaphore(m_device, m_rendering_finished, nullptr);

			vkDestroyCommandPool(m_device, m_command_pool, nullptr);

			for (auto const& framebuffer : m_swapchain_framebuffers)
			{
				vkDestroyFramebuffer(m_device, framebuffer, nullptr);
			}

			vkDestroyPipeline(m_device, m_pipeline, nullptr);
			vkDestroyRenderPass(m_device, m_renderpass, nullptr);
			vkDestroyPipelineLayout(m_device, m_pipeline_layout, nullptr);

			for (auto const& image_view : m_image_views)
			{
				vkDestroyImageView(m_device, image_view, nullptr);
			}
			vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);
			m_device.release();
			vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
			m_instance.release();
		}
		void draw_frame()
		{
			uint32_t image_index;
			VkResult result = vkAcquireNextImageKHR(m_device, m_swapchain, std::numeric_limits<uint64_t>::max(), m_image_available, VK_NULL_HANDLE, &image_index);

			if (result == VK_ERROR_OUT_OF_DATE_KHR)
			{
				reset_swapchain();
				return;
			}
			else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
			{
				throw std::runtime_error("failed to acquire swap chain image!");
			}

			VkSemaphore wait_semaphores[] = { m_image_available };
			VkSemaphore signal_semaphores[] = { m_rendering_finished };
			VkPipelineStageFlags wait_stages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
			VkSubmitInfo submit_info = {};
			submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
			submit_info.waitSemaphoreCount = 1;
			submit_info.pWaitSemaphores = wait_semaphores;
			submit_info.pWaitDstStageMask = wait_stages;
			submit_info.commandBufferCount = 1;
			submit_info.pCommandBuffers = &m_command_buffers[image_index];
			submit_info.signalSemaphoreCount = 1;
			submit_info.pSignalSemaphores = signal_semaphores;

			if (vkQueueSubmit(m_graphics_queue, 1, &submit_info, VK_NULL_HANDLE) != VK_SUCCESS)
			{
				throw std::runtime_error("failed to submit draw command buffer!");
			}

			// submitting the result back to the swap chain to have it eventually show up on the screen
			VkPresentInfoKHR presentInfo = {};
			presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
			presentInfo.waitSemaphoreCount = 1;
			presentInfo.pWaitSemaphores = signal_semaphores;

			VkSwapchainKHR swapChains[] = { m_swapchain };
			presentInfo.swapchainCount = 1;
			presentInfo.pSwapchains = swapChains;
			presentInfo.pImageIndices = &image_index;
			presentInfo.pResults = nullptr;

			result = vkQueuePresentKHR(m_presentation_queue, &presentInfo);

			if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
			{
				reset_swapchain();
			}
			else if (result != VK_SUCCESS)
			{
				throw std::runtime_error("failed to present swap chain image!");
			}
		}
		void resize(int width, int height)
		{
			m_width = width;
			m_height = height;
			reset_swapchain();
		}

	private:
		void select_physical_device()
		{
			uint32_t device_count = 0;
			vkEnumeratePhysicalDevices(m_instance, &device_count, nullptr);
			if (device_count == 0)
			{
				throw std::runtime_error("failed to find GPUs with Vulkan support!");
			}

			std::vector<VkPhysicalDevice> devices(device_count);
			vkEnumeratePhysicalDevices(m_instance, &device_count, devices.data());
			for (auto const& current : devices)
			{
				if (suitable(current))
				{
					m_physical_device = current;
					break;
				}
			}

			if (m_physical_device == VK_NULL_HANDLE)
			{
				throw std::runtime_error("failed to find a suitable GPU!");
			}
		}
		void create_logical_device()
		{
			queues queue_indices = find_queues(m_physical_device);

			m_device.create(m_physical_device, { queue_indices.graphics, queue_indices.presentation }, m_instance.layer_count(), m_instance.layers(), static_cast<uint32_t>(device_extensions.size()), device_extensions.data());

			vkGetDeviceQueue(m_device, queue_indices.graphics, 0, &m_graphics_queue);
			vkGetDeviceQueue(m_device, queue_indices.presentation, 0, &m_presentation_queue);
		}
		void create_swapchain()
		{
			auto details = swapchain_support(m_physical_device);

			VkSurfaceFormatKHR surface_format = choose_swapchain_format(details.formats);
			VkPresentModeKHR mode = choose_swapchain_mode(details.presentation_modes);
			m_extent = choose_swapchain_extent(details.capabilities, m_width, m_height);
			m_format = surface_format.format;

			// A value of 0 for maxImageCount means that there is no limit besides memory requirements, which is why we need to check for that.
			uint32_t image_count = details.capabilities.minImageCount + 1;
			if (details.capabilities.maxImageCount > 0 && image_count > details.capabilities.maxImageCount)
			{
				image_count = details.capabilities.maxImageCount;
			}

			auto queues = find_queues(m_physical_device);
			uint32_t queue_indices[] = { static_cast<uint32_t>(queues.graphics), static_cast<uint32_t>(queues.presentation) };

			VkSwapchainCreateInfoKHR create_info = {};
			create_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
			create_info.surface = m_surface;
			create_info.minImageCount = image_count;
			create_info.imageFormat = m_format;
			create_info.imageColorSpace = surface_format.colorSpace;
			create_info.imageExtent = m_extent;
			create_info.imageArrayLayers = 1;
			create_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

			if (queues.match())
			{
				create_info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
				create_info.queueFamilyIndexCount = 2;
				create_info.pQueueFamilyIndices = queue_indices;
			}
			else
			{
				create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
				create_info.queueFamilyIndexCount = 0;
				create_info.pQueueFamilyIndices = nullptr;
			}

			create_info.preTransform = details.capabilities.currentTransform;
			create_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
			create_info.presentMode = mode;
			create_info.clipped = VK_TRUE;

			VkSwapchainKHR old = m_swapchain; // VK_NULL_HANDLE set in constructor
			create_info.oldSwapchain = old;

			if (vkCreateSwapchainKHR(m_device, &create_info, nullptr, &m_swapchain) != VK_SUCCESS)
			{
				throw std::runtime_error("failed to create swap chain!");
			}
			if (old != VK_NULL_HANDLE)
			{
				vkDestroySwapchainKHR(m_device, old, nullptr);
			}

			// The implementation is allowed to create more images, which is why we need to explicitly query the amount again.
			vkGetSwapchainImagesKHR(m_device, m_swapchain, &image_count, nullptr);
			m_swapchain_images.resize(image_count);
			vkGetSwapchainImagesKHR(m_device, m_swapchain, &image_count, m_swapchain_images.data());
		}
		void create_image_views()
		{
			for (size_t i = 0, size = m_image_views.size(); i != size; ++i)
			{
				vkDestroyImageView(m_device, m_image_views[i], nullptr);
			}

			m_image_views.resize(m_swapchain_images.size());
			for (size_t i = 0, size = m_swapchain_images.size(); i != size; ++i)
			{
				VkImageViewCreateInfo create_info = {};
				create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
				create_info.image = m_swapchain_images[i];
				create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
				create_info.format = m_format;
				create_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
				create_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
				create_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
				create_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
				create_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				create_info.subresourceRange.baseMipLevel = 0;
				create_info.subresourceRange.levelCount = 1;
				create_info.subresourceRange.baseArrayLayer = 0;
				create_info.subresourceRange.layerCount = 1;

				if (vkCreateImageView(m_device, &create_info, nullptr, &m_image_views[i]) != VK_SUCCESS)
				{
					throw std::runtime_error("failed to create image views!");
				}
			}
		}
		void create_pipeline()
		{
			if (m_pipeline != VK_NULL_HANDLE)
			{
				vkDestroyPipeline(m_device, m_pipeline, nullptr);
			}
			if (m_pipeline_layout != VK_NULL_HANDLE)
			{
				vkDestroyPipelineLayout(m_device, m_pipeline_layout, nullptr);
			}

			VkViewport viewport = {};
			viewport.x = 0.0f;
			viewport.y = 0.0f;
			viewport.width = static_cast<float>(m_extent.width);
			viewport.height = static_cast<float>(m_extent.height);
			viewport.minDepth = 0.0f;
			viewport.maxDepth = 1.0f;
			VkRect2D scissor = { { 0, 0 }, m_extent };

			auto vertex = create_shader(read_file("data/shaders/triangle.vert.spv"));
			auto fragment = create_shader(read_file("data/shaders/triangle.frag.spv"));
			VkPipelineShaderStageCreateInfo vertex_info{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
			vertex_info.stage = VK_SHADER_STAGE_VERTEX_BIT;
			vertex_info.module = vertex;
			vertex_info.pName = "main";
			vertex_info.pSpecializationInfo = nullptr;
			VkPipelineShaderStageCreateInfo fragment_info{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
			fragment_info.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
			fragment_info.module = fragment;
			fragment_info.pName = "main";
			fragment_info.pSpecializationInfo = nullptr;
			VkPipelineShaderStageCreateInfo shader_stages[] = { vertex_info, fragment_info };

			VkPipelineVertexInputStateCreateInfo vertex_input_info = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
			vertex_input_info.vertexBindingDescriptionCount = 0;
			vertex_input_info.pVertexBindingDescriptions = nullptr;
			vertex_input_info.vertexAttributeDescriptionCount = 0;
			vertex_input_info.pVertexAttributeDescriptions = nullptr;

			VkPipelineInputAssemblyStateCreateInfo input_assembly_info = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
			input_assembly_info.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
			input_assembly_info.primitiveRestartEnable = VK_FALSE;

			VkPipelineViewportStateCreateInfo viewport_info = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
			viewport_info.viewportCount = 1;
			viewport_info.pViewports = &viewport;
			viewport_info.scissorCount = 1;
			viewport_info.pScissors = &scissor;

			VkPipelineRasterizationStateCreateInfo rasterizer{ VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
			rasterizer.depthClampEnable = VK_FALSE;
			rasterizer.rasterizerDiscardEnable = VK_FALSE;
			rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
			rasterizer.lineWidth = 1.0f;
			rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
			rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
			rasterizer.depthBiasEnable = VK_FALSE;
			rasterizer.depthBiasConstantFactor = 0.0f;
			rasterizer.depthBiasClamp = 0.0f;
			rasterizer.depthBiasSlopeFactor = 0.0f;

			VkPipelineMultisampleStateCreateInfo multisampling{ VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
			multisampling.sampleShadingEnable = VK_FALSE;
			multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
			multisampling.minSampleShading = 1.0f;
			multisampling.pSampleMask = nullptr;
			multisampling.alphaToCoverageEnable = VK_FALSE;
			multisampling.alphaToOneEnable = VK_FALSE;

			VkPipelineColorBlendAttachmentState blend_attachment = {};
			blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
			blend_attachment.blendEnable = VK_FALSE;
			blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
			blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
			blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
			blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
			blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
			blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD;

			VkPipelineColorBlendStateCreateInfo blending{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
			blending.logicOpEnable = VK_FALSE;
			blending.attachmentCount = 1;
			blending.pAttachments = &blend_attachment;
			blending.blendConstants[0] = 0.0f;
			blending.blendConstants[1] = 0.0f;
			blending.blendConstants[2] = 0.0f;
			blending.blendConstants[3] = 0.0f;

			VkPipelineLayoutCreateInfo layout_info{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
			layout_info.setLayoutCount = 0;
			layout_info.pSetLayouts = nullptr;
			layout_info.pushConstantRangeCount = 0;
			layout_info.pPushConstantRanges = 0;

			if (vkCreatePipelineLayout(m_device, &layout_info, nullptr, &m_pipeline_layout) != VK_SUCCESS)
			{
				throw std::runtime_error("failed to create pipeline layout!");
			}

			VkGraphicsPipelineCreateInfo pipeline_info = {};
			pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
			pipeline_info.stageCount = 2;
			pipeline_info.pStages = shader_stages;
			pipeline_info.pVertexInputState = &vertex_input_info;
			pipeline_info.pInputAssemblyState = &input_assembly_info;
			pipeline_info.pViewportState = &viewport_info;
			pipeline_info.pRasterizationState = &rasterizer;
			pipeline_info.pMultisampleState = &multisampling;
			pipeline_info.pDepthStencilState = nullptr;
			pipeline_info.pColorBlendState = &blending;
			pipeline_info.pDynamicState = nullptr;
			pipeline_info.layout = m_pipeline_layout;
			pipeline_info.renderPass = m_renderpass;
			pipeline_info.subpass = 0; // index of pass
			pipeline_info.basePipelineHandle = VK_NULL_HANDLE;

			if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &m_pipeline) != VK_SUCCESS)
			{
				throw std::runtime_error("failed to create graphics pipeline!");
			}

			vkDestroyShaderModule(m_device, vertex, nullptr);
			vkDestroyShaderModule(m_device, fragment, nullptr);
		}
		void create_renderpass()
		{
			if (m_renderpass != VK_NULL_HANDLE)
			{
				vkDestroyRenderPass(m_device, m_renderpass, nullptr);
			}

			VkAttachmentDescription attachment = {};
			attachment.format = m_format;
			attachment.samples = VK_SAMPLE_COUNT_1_BIT;
			attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
			attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
			attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

			VkAttachmentReference attachment_reference = {};
			attachment_reference.attachment = 0;
			attachment_reference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

			VkSubpassDescription subpass = {};
			subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
			subpass.colorAttachmentCount = 1;
			subpass.pColorAttachments = &attachment_reference;

			VkSubpassDependency dependency = {};
			dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
			dependency.dstSubpass = 0;
			dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			dependency.srcAccessMask = 0;
			dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

			VkRenderPassCreateInfo renderpass_info = { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
			renderpass_info.attachmentCount = 1;
			renderpass_info.pAttachments = &attachment;
			renderpass_info.subpassCount = 1;
			renderpass_info.pSubpasses = &subpass;
			renderpass_info.dependencyCount = 1;
			renderpass_info.pDependencies = &dependency;

			if (vkCreateRenderPass(m_device, &renderpass_info, nullptr, &m_renderpass) != VK_SUCCESS)
			{
				throw std::runtime_error("failed to create render pass!");
			}
		}
		void create_framebuffers()
		{
			for (size_t i = 0, size = m_swapchain_framebuffers.size(); i != size; ++i)
			{
				vkDestroyFramebuffer(m_device, m_swapchain_framebuffers[i], nullptr);
			}

			size_t size = m_swapchain_images.size();
			m_swapchain_framebuffers.resize(size);
			for (size_t i = 0; i != size; ++i)
			{
				VkImageView attachments[] = { m_image_views[i] };

				VkFramebufferCreateInfo framebufferInfo{ VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
				framebufferInfo.renderPass = m_renderpass;
				framebufferInfo.attachmentCount = 1;
				framebufferInfo.pAttachments = attachments;
				framebufferInfo.width = m_extent.width;
				framebufferInfo.height = m_extent.height;
				framebufferInfo.layers = 1;

				if (vkCreateFramebuffer(m_device, &framebufferInfo, nullptr, &m_swapchain_framebuffers[i]) != VK_SUCCESS)
				{
					throw std::runtime_error("failed to create framebuffer!");
				}
			}
		}
		void create_command_pool()
		{
			auto queues = find_queues(m_physical_device);

			VkCommandPoolCreateInfo pool_info = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
			pool_info.queueFamilyIndex = queues.graphics;
			pool_info.flags = 0;

			if (vkCreateCommandPool(m_device, &pool_info, nullptr, &m_command_pool) != VK_SUCCESS)
			{
				throw std::runtime_error("failed to create command pool!");
			}
		}
		void create_command_buffers()
		{
			if (m_command_buffers.size() > 0)
			{
				vkFreeCommandBuffers(m_device, m_command_pool, static_cast<uint32_t>(m_command_buffers.size()), m_command_buffers.data());
			}

			auto size = m_swapchain_framebuffers.size();
			m_command_buffers.resize(size);

			VkCommandBufferAllocateInfo info = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
			info.commandPool = m_command_pool;
			info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
			info.commandBufferCount = static_cast<uint32_t>(size);

			if (vkAllocateCommandBuffers(m_device, &info, m_command_buffers.data()) != VK_SUCCESS)
			{
				throw std::runtime_error("failed to allocate command buffers!");
			}

			for (size_t i = 0; i != size; ++i)
			{
				VkCommandBufferBeginInfo begin_info{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
				begin_info.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
				begin_info.pInheritanceInfo = nullptr;

				vkBeginCommandBuffer(m_command_buffers[i], &begin_info);

				VkClearValue clear_color = { 0.0f, 0.0f, 0.0f, 1.0f };

				VkRenderPassBeginInfo renderpass_info{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
				renderpass_info.renderPass = m_renderpass;
				renderpass_info.framebuffer = m_swapchain_framebuffers[i];
				renderpass_info.renderArea.offset = { 0, 0 }; // should match the size of the attachments
				renderpass_info.renderArea.extent = m_extent; // for best performance.
				renderpass_info.clearValueCount = 1;
				renderpass_info.pClearValues = &clear_color;

				vkCmdBeginRenderPass(m_command_buffers[i], &renderpass_info, VK_SUBPASS_CONTENTS_INLINE);
				vkCmdBindPipeline(m_command_buffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
				vkCmdDraw(m_command_buffers[i], 3, 1, 0, 0);
				vkCmdEndRenderPass(m_command_buffers[i]);

				if (vkEndCommandBuffer(m_command_buffers[i]) != VK_SUCCESS)
				{
					throw std::runtime_error("failed to record command buffer!");
				}
			}
		}
		void create_semaphores()
		{
			VkSemaphoreCreateInfo semaphoreInfo{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };

			if (vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &m_image_available) != VK_SUCCESS ||
				vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &m_rendering_finished) != VK_SUCCESS)
			{
				throw std::runtime_error("failed to create semaphores!");
			}
		}
		void reset_swapchain()
		{
			vkDeviceWaitIdle(m_device);

			create_swapchain();
			create_image_views();
			create_renderpass();
			create_pipeline();
			create_framebuffers();
			create_command_buffers();
		}
		std::vector<const char*> required_extensions() const
		{
			std::vector<const char*> extensions;

			uint32_t count = 0;
			const char** glfwExtensions;
			glfwExtensions = glfwGetRequiredInstanceExtensions(&count);

			for (uint32_t i = 0; i != count; ++i)
			{
				extensions.push_back(glfwExtensions[i]);
			}

			if (validate)
			{
				extensions.push_back(VK_EXT_DEBUG_REPORT_EXTENSION_NAME);
			}

			return extensions;
		}
		bool suitable(VkPhysicalDevice device) const
		{
			VkPhysicalDeviceProperties properties;
			VkPhysicalDeviceFeatures features;
			vkGetPhysicalDeviceProperties(device, &properties);
			vkGetPhysicalDeviceFeatures(device, &features);

			return properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU
				&& features.geometryShader
				&& find_queues(device)
				&& support_extensions(device)
				&& swapchain_support(device); // query swapchain support after checking for swapchain extention support
		}
		bool support_extensions(VkPhysicalDevice device) const
		{
			uint32_t count;
			vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr);

			std::vector<VkExtensionProperties> available_extensions(count);
			vkEnumerateDeviceExtensionProperties(device, nullptr, &count, available_extensions.data());

			std::set<std::string> required_extensions(device_extensions.begin(), device_extensions.end());

			for (const auto& extension : available_extensions)
			{
				required_extensions.erase(extension.extensionName);
			}

			return required_extensions.empty();
		}
		queues find_queues(VkPhysicalDevice device) const
		{
			uint32_t queue_family_count = 0;
			vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_family_count, nullptr);
			std::vector<VkQueueFamilyProperties> families(queue_family_count);
			vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_family_count, families.data());

			queues found{ -1, -1 };

			int i = 0;
			for (const auto& family : families)
			{
				VkBool32 presentation = false;
				vkGetPhysicalDeviceSurfaceSupportKHR(device, i, m_surface, &presentation);

				if (family.queueCount > 0 && family.queueFlags & VK_QUEUE_GRAPHICS_BIT)
				{
					found.graphics = i;
				}
				if (family.queueCount > 0 && presentation)
				{
					found.presentation = i;
				}

				if (found) break;

				++i;
			}
			return found;
		}
		swapchain_details swapchain_support(VkPhysicalDevice device) const
		{
			swapchain_details details;

			vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, m_surface, &details.capabilities);

			uint32_t format_count;
			vkGetPhysicalDeviceSurfaceFormatsKHR(device, m_surface, &format_count, nullptr);
			if (format_count != 0)
			{
				details.formats.resize(format_count);
				vkGetPhysicalDeviceSurfaceFormatsKHR(device, m_surface, &format_count, details.formats.data());
			}

			uint32_t mode_count;
			vkGetPhysicalDeviceSurfacePresentModesKHR(device, m_surface, &mode_count, nullptr);
			if (mode_count != 0)
			{
				details.presentation_modes.resize(mode_count);
				vkGetPhysicalDeviceSurfacePresentModesKHR(device, m_surface, &mode_count, details.presentation_modes.data());
			}

			return details;
		}
		VkSurfaceFormatKHR choose_swapchain_format(std::vector<VkSurfaceFormatKHR> const& available_formats) const
		{
			if (available_formats.size() == 1 && available_formats[0].format == VK_FORMAT_UNDEFINED)
			{
				return{ VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR };
			}

			for (auto const& available_format : available_formats)
			{
				if (available_format.format == VK_FORMAT_B8G8R8A8_UNORM && available_format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
				{
					return available_format;
				}
			}

			return available_formats[0];
		}
		VkPresentModeKHR choose_swapchain_mode(std::vector<VkPresentModeKHR> const& modes) const
		{
			VkPresentModeKHR result = VK_PRESENT_MODE_FIFO_KHR;
			for (const auto& mode : modes)
			{
				if (mode == VK_PRESENT_MODE_MAILBOX_KHR)
				{
					result = mode;
					break;
				}
			}
			return result;
		}
		VkExtent2D choose_swapchain_extent(VkSurfaceCapabilitiesKHR const& capabilities, uint32_t width, uint32_t height) const
		{
			VkExtent2D extent{ width, height };

			// by setting the width and height in currentExtent to a special value - the maximum value of uint32_t
			// In that case we'll pick the resolution that best matches the window within the minImageExtent and maxImageExtent bounds
			if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max())
			{
				extent = capabilities.currentExtent;
			}
			else
			{
				extent.width = std::max(capabilities.minImageExtent.width, std::min(capabilities.maxImageExtent.width, extent.width));
				extent.height = std::max(capabilities.minImageExtent.height, std::min(capabilities.maxImageExtent.height, extent.height));
			}

			return extent;
		}

		static std::vector<char> read_file(std::string const& name)
		{
			std::ifstream file(name, std::ios::ate | std::ios::binary);

			if (!file.is_open())
			{
				throw std::runtime_error("px::core::renderer::read_file() - failed to open file!");
			}

			size_t size = static_cast<size_t>(file.tellg());
			std::vector<char> buffer(size);

			file.seekg(0);
			file.read(buffer.data(), size);

			file.close();

			return buffer;
		}
		VkShaderModule create_shader(std::vector<char> const& code) const
		{
			VkShaderModuleCreateInfo create_info = {};
			create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
			create_info.codeSize = code.size();
			create_info.pCode = reinterpret_cast<uint32_t const*>(code.data());

			VkShaderModule shader;
			if (vkCreateShaderModule(m_device, &create_info, nullptr, &shader) != VK_SUCCESS)
			{
				throw std::runtime_error("px::core::renderer::create_shader() - failed to create shader module!");
			}
			return shader;
		}

	private:
		uint32_t m_width;
		uint32_t m_height;

		vk_instance m_instance;
		VkSurfaceKHR m_surface;
		VkPhysicalDevice m_physical_device;
		vk_device m_device;

		VkQueue m_graphics_queue;
		VkQueue m_presentation_queue;

		VkFormat m_format;
		VkExtent2D m_extent;
		VkSwapchainKHR m_swapchain;
		std::vector<VkImage> m_swapchain_images;
		std::vector<VkImageView> m_image_views;

		VkRenderPass m_renderpass;
		VkPipelineLayout m_pipeline_layout;
		VkPipeline m_pipeline;

		std::vector<VkFramebuffer> m_swapchain_framebuffers;

		VkCommandPool m_command_pool;
		std::vector<VkCommandBuffer> m_command_buffers;

		VkSemaphore m_image_available;
		VkSemaphore m_rendering_finished;

		const std::vector<vertex> vertices = {
			{ { 0.0f, -0.5f },{ 1.0f, 0.0f, 0.0f } },
			{ { 0.5f, 0.5f },{ 0.0f, 1.0f, 0.0f } },
			{ { -0.5f, 0.5f },{ 0.0f, 0.0f, 1.0f } }
		};

	private:
#ifndef _DEBUG
		const bool validate = false;
#else
		const bool validate = true;
#endif
		const std::vector<const char*> device_extensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
	};
}