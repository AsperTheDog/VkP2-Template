#include "engine.hpp"

#include <array>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>
#include <imgui_internal.h>
#include <vulkan/vk_enum_string_helper.h>

#include <glm/glm.hpp>

#include "vkp2/image.hpp"
#include "spdlog/spdlog.h"
#include "vkp2/device.hpp"
#include "vkp2/instance.hpp"
#include "vkp2/pipeline.hpp"
#include "vkp2/shader.hpp"
#include "vkp2/sync.hpp"
#include "vkp2/extra/window.hpp"

constexpr bool g_AssertOnError = false;

static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(const VkDebugUtilsMessageSeverityFlagBitsEXT p_MessageSeverity, const VkDebugUtilsMessageTypeFlagsEXT p_MessageType, const VkDebugUtilsMessengerCallbackDataEXT* p_CallbackData, void*)
{
    if (p_MessageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT)
    {
        spdlog::info("validation layer: {}", p_CallbackData->pMessage);
    }
    else if (p_MessageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
    {
        spdlog::warn("validation layer: {}", p_CallbackData->pMessage);
    }
    else if (p_MessageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
    {
        spdlog::error("validation layer ({}): \n{}", string_VkDebugUtilsMessageTypeFlagsEXT(p_MessageType), p_CallbackData->pMessage);
		if (g_AssertOnError)
    		__debugbreak();
    }

    return VK_FALSE;
}

void Engine::init()
{
	{
		m_Window.initMaximized("Vulkan App");
		spdlog::debug("Initialized window with size: {}x{}", m_Window.getSize().width, m_Window.getSize().height);
		volkInitialize();

#ifndef NDEBUG
		spdlog::debug("Initialized volk");
#endif

		vkp::InstanceBuilder l_Builder{};
#ifndef NDEBUG
		l_Builder.enableValidationLayers(debugCallback);
#endif
		l_Builder.addExtensions(m_Window.getRequiredInstanceExtensions());
		auto [l_Instance, l_DebugMessenger] = l_Builder.build();
		m_Instance = l_Instance;
		m_DebugUtils = l_DebugMessenger;

        m_Window.createSurface(m_Instance);

#ifndef NDEBUG
		spdlog::debug("Created Vulkan instance: {}", fmt::ptr(m_Instance));
#endif
	}

	{
		auto l_DeviceEval = vkp::device::LeanModern();
		const std::optional<VkPhysicalDevice> l_ChosenDevice = vkp::device::chooseBestPhysicalDevice(m_Instance, m_Window.getSurface(), &l_DeviceEval);
		if (!l_ChosenDevice)
		{
			spdlog::error("No suitable physical device found!");
			return;
		}

#ifndef NDEBUG
		VkPhysicalDeviceProperties l_Properties{};
		vkGetPhysicalDeviceProperties(l_ChosenDevice.value(), &l_Properties);
		spdlog::debug("Chosen physical device: {} (type: {}, API version: {}.{}.{}), driver version: {}, vendor ID: {}, device ID: {}",
			l_Properties.deviceName,
			string_VkPhysicalDeviceType(l_Properties.deviceType),
			VK_VERSION_MAJOR(l_Properties.apiVersion),
			VK_VERSION_MINOR(l_Properties.apiVersion),
			VK_VERSION_PATCH(l_Properties.apiVersion),
			l_Properties.driverVersion,
			l_Properties.vendorID,
			l_Properties.deviceID);
#endif

		m_DeviceData.physicalDevice = l_ChosenDevice.value();

		const vkp::device::DeviceReturn l_Return = vkp::device::buildFromEval(m_DeviceData.physicalDevice, &l_DeviceEval);
		m_DeviceData.device = l_Return.device;

		volkLoadDevice(m_DeviceData.device);
		volkLoadDeviceTable(&m_DeviceData.deviceTable, m_DeviceData.device);
#ifndef NDEBUG
		spdlog::debug("Loaded device: {}", fmt::ptr(m_DeviceData.device));
#endif

		m_DeviceData.allocator = vkp::device::createVmaAllocator(m_Instance, m_DeviceData);

		m_DeviceData->vkGetDeviceQueue(m_DeviceData.device, l_Return.queues[0].queueFamilyIndex, 0, &m_GraphicsQueue);
		m_DeviceData->vkGetDeviceQueue(m_DeviceData.device, l_Return.queues[0].queueFamilyIndex, 1, &m_TransferQueue);
		m_QueueFamilyIndex = l_Return.queues[0].queueFamilyIndex;

#ifndef NDEBUG
		spdlog::debug("Retrieved graphics queue: {} (family index: {})", fmt::ptr(m_GraphicsQueue), l_Return.queues[0].queueFamilyIndex);
		spdlog::debug("Retrieved transfer queue: {} (family index: {})", fmt::ptr(m_TransferQueue), l_Return.queues[0].queueFamilyIndex);
#endif
	}

	{
		m_TimelineSemaphore = vkp::createTimelineSemaphore(m_DeviceData.device);

#ifndef NDEBUG
		spdlog::debug("Created timeline semaphore: {}", fmt::ptr(m_TimelineSemaphore));
#endif
	}

	{
		m_Swapchain = vkp::Swapchain(m_DeviceData, m_Window.getSurface(), 3, m_Window.getSize().toVkExtent2D(), VK_PRESENT_MODE_FIFO_KHR);
		m_Swapchain.recreate(m_DeviceData, m_Window.getSurface(), m_Window.getSize().toVkExtent2D());
		m_Window.getOnPixelResize().connect(this, &Engine::recreateSwapchain);

#ifndef NDEBUG
		spdlog::debug("Created swapchain with {} frames in flight, format: {}, extent: {}x{}, present mode: {}",
			m_Swapchain.properties.framesInFlight,
			string_VkFormat(m_Swapchain.properties.format.format),
			m_Swapchain.properties.extent.width,
			m_Swapchain.properties.extent.height,
			string_VkPresentModeKHR(m_Swapchain.properties.presentMode));
#endif
	}

	{
		ensureFrameSlots(static_cast<uint32_t>(m_Swapchain.images.size()));

#ifndef NDEBUG
		spdlog::debug("Created {} command pools / buffers, {} semaphores and timeline for {} swapchain images", m_CommandPools.size(), m_ImageAvailableSemaphores.size(), m_Swapchain.images.size());
#endif
	}

	{
		constexpr VmaAllocationCreateInfo l_AllocInfo{
			.flags = 0,
			.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
		};

		m_DepthBuffer = vkp::createDepthBuffer(m_DeviceData, m_Swapchain, l_AllocInfo);
		m_DepthBufferView = vkp::createImageView(m_DeviceData, m_DepthBuffer.image, VK_FORMAT_D32_SFLOAT, VK_IMAGE_ASPECT_DEPTH_BIT);

#ifndef NDEBUG
		spdlog::debug("Created depth buffer image: {} with allocation: {}", fmt::ptr(m_DepthBuffer.image), fmt::ptr(m_DepthBuffer.alloc));
		spdlog::debug("Depth buffer allocation info: size: {}, memoryType: {}, mappedData: {}", m_DepthBuffer.info.size, m_DepthBuffer.info.memoryType, fmt::ptr(m_DepthBuffer.info.pMappedData));
		spdlog::debug("Created depth buffer view: {}", fmt::ptr(m_DepthBufferView));
#endif
	}

	{
		vkp::shader::CompileOptions l_Opts;
		l_Opts.cacheFolder = "cache/spv";

		m_TriangleShader = vkp::shader::compileFromFile<true>("shaders/triangle.slang", "triangle", l_Opts);

		VkShaderModule l_Vert = m_TriangleShader.createModule(m_DeviceData, VK_SHADER_STAGE_VERTEX_BIT);
		VkShaderModule l_Frag = m_TriangleShader.createModule(m_DeviceData, VK_SHADER_STAGE_FRAGMENT_BIT);

		const VkFormat l_ColorFormats[]{ m_Swapchain.properties.format.format };

		vkp::pipeline::PipelineBuilder l_Builder;
		l_Builder.useReflection(m_TriangleShader)
			.setPipelineCacheFolder("cache/pipeline")
			.setColorFormats(l_ColorFormats)
			.addShaderStage(l_Vert, VK_SHADER_STAGE_VERTEX_BIT)
			.addShaderStage(l_Frag, VK_SHADER_STAGE_FRAGMENT_BIT);

		m_TrianglePipeline = l_Builder.buildGraphics(m_DeviceData);

		m_DeviceData->vkDestroyShaderModule(m_DeviceData.device, l_Vert, nullptr);
		m_DeviceData->vkDestroyShaderModule(m_DeviceData.device, l_Frag, nullptr);

#ifndef NDEBUG
		spdlog::debug("Built triangle pipeline (layout: {}, descriptor set layouts: {})", fmt::ptr(m_TrianglePipeline.layout), m_TrianglePipeline.descriptorSetLayouts.size());
#endif
	}

	{
		struct Vertex
		{
			float position[3];
			float color[4];
		};

		constexpr Vertex l_Vertices[]{
			{.position = { -0.5f, -0.5f, 0.0f }, .color = { 1.0f, 0.0f, 0.0f, 1.0f }},
			{.position = { 0.5f, -0.5f, 0.0f }, .color = { 0.0f, 1.0f, 0.0f, 1.0f }},
			{.position = { 0.0f, 0.5f, 0.0f }, .color = { 0.0f, 0.0f, 1.0f, 1.0f }},
		};

		constexpr VmaAllocationCreateInfo l_AllocInfo{
			.flags = 0,
			.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
		};

		m_TriangleVertexBuffer = vkp::createBuffer(m_DeviceData, sizeof(l_Vertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, l_AllocInfo);
		vkp::uploadBuffer(m_DeviceData, m_CommandPools[0].handle, m_GraphicsQueue, m_TriangleVertexBuffer, l_Vertices, sizeof(l_Vertices));

#ifndef NDEBUG
		spdlog::debug("Created triangle vertex buffer ({} bytes, stride 28, device-local)", sizeof(l_Vertices));
#endif
	}

	{
		initImgui();
		m_Window.getOnEventCaptured().connect([](const SDL_Event* p_Event)
		{
			ImGui_ImplSDL3_ProcessEvent(p_Event);
		});
	}
}

void Engine::run()
{
	while (!m_Window.shouldClose())
	{
		m_Window.pollEvents();
		imguiDraw();
		drawFrame();
	}
}

void Engine::ensureFrameSlots(const uint32_t p_Count)
{
	while (m_CommandPools.size() < p_Count)
	{
		vkp::cmd::CommandPool l_Pool;
		l_Pool.init(m_DeviceData.device, 0);
		m_CommandPools.push_back(l_Pool);
		m_CommandBuffers.push_back(m_CommandPools.back().allocate(m_DeviceData.device, VK_COMMAND_BUFFER_LEVEL_PRIMARY));
		m_ImageAvailableSemaphores.push_back(vkp::createSemaphore(m_DeviceData.device));
		m_RenderFinishedSemaphores.push_back(vkp::createSemaphore(m_DeviceData.device));
		m_SlotTimelineValues.push_back(0);
	}
}

void Engine::drawFrame()
{
	const uint32_t l_Frames = static_cast<uint32_t>(m_ImageAvailableSemaphores.size());
	const VkSemaphore l_ImageAvailable = m_ImageAvailableSemaphores[m_CurrentFrame];
	VkSemaphore l_RenderFinished = m_RenderFinishedSemaphores[m_CurrentFrame];
	const VkCommandBuffer l_Cb = m_CommandBuffers[m_CurrentFrame];

	if (m_SlotTimelineValues[m_CurrentFrame] != 0)
	{
		vkp::cmd::waitTimeline(m_DeviceData, m_TimelineSemaphore, m_SlotTimelineValues[m_CurrentFrame]);
	}

	uint32_t l_ImageIndex = 0;
	const VkResult l_AcquireResult = m_DeviceData->vkAcquireNextImageKHR(m_DeviceData.device, m_Swapchain.swapchain, UINT64_MAX, l_ImageAvailable, VK_NULL_HANDLE, &l_ImageIndex);
	if (l_AcquireResult == VK_ERROR_OUT_OF_DATE_KHR)
	{
		return;
	}

	m_DeviceData->vkResetCommandPool(m_DeviceData.device, m_CommandPools[m_CurrentFrame].handle, 0);

	vkp::cmd::recordingScope(m_DeviceData, l_Cb, true, [&](const VkCommandBuffer p_Cb)
	{
#ifndef NDEBUG
		constexpr float l_LabelColor[4]{ 1.0f, 1.0f, 1.0f, 1.0f };
		vkp::cmd::debugScope(p_Cb, "triangle", l_LabelColor, [&](const VkCommandBuffer)
		{
#endif
			VkClearValue l_ClearColor{};
			l_ClearColor.color.float32[0] = 0.15f;
			l_ClearColor.color.float32[1] = 0.15f;
			l_ClearColor.color.float32[2] = 0.2f;
			l_ClearColor.color.float32[3] = 1.0f;

			const vkp::cmd::AttachmentSpec l_Attachments[]{
				{
					.view = m_Swapchain.imageViews[l_ImageIndex],
					.image = m_Swapchain.images[l_ImageIndex],
					.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
					.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
					.clearValue = l_ClearColor,
				},
			};

			const vkp::cmd::FrameSpec l_Frame{
				.colors = l_Attachments,
				.depth = nullptr,
				.extent = m_Swapchain.properties.extent,
			};

			vkp::cmd::frameRenderScope(m_DeviceData, p_Cb, l_Frame, [&](const VkCommandBuffer p_Cb)
			{
				m_DeviceData->vkCmdBindPipeline(p_Cb, VK_PIPELINE_BIND_POINT_GRAPHICS, m_TrianglePipeline.pipeline);

				vkp::cmd::pushConstants(m_DeviceData, p_Cb, m_TrianglePipeline.layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(m_ImguiTint), &m_ImguiTint);

				constexpr VkDeviceSize l_Offset = 0;
				m_DeviceData->vkCmdBindVertexBuffers(p_Cb, 0, 1, &m_TriangleVertexBuffer.buffer, &l_Offset);
				m_DeviceData->vkCmdDraw(p_Cb, 3, 1, 0, 0);

				ImDrawData* l_DrawData = ImGui::GetDrawData();
				ImGui_ImplVulkan_RenderDrawData(l_DrawData, p_Cb);
			});
#ifndef NDEBUG
		});
#endif
	});

	++m_TimelineValue;

	constexpr VkPipelineStageFlags2 l_WaitStage2 = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
	const VkCommandBuffer l_CommandBuffers[]{ l_Cb };
	const vkp::cmd::SemaphoreSubmit l_Waits[]{
		{ .semaphore = l_ImageAvailable, .stageMask = l_WaitStage2, .value = 0 },
	};
	const vkp::cmd::SemaphoreSubmit l_Signals[]{
		{ .semaphore = l_RenderFinished, .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, .value = 0 },
		{ .semaphore = m_TimelineSemaphore, .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, .value = m_TimelineValue },
	};
	vkp::cmd::submit2(m_DeviceData, m_GraphicsQueue, l_CommandBuffers, l_Waits, l_Signals, VK_NULL_HANDLE);
	m_SlotTimelineValues[m_CurrentFrame] = m_TimelineValue;

	const VkPresentInfoKHR l_PresentInfo{
		.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
		.pNext = nullptr,
		.waitSemaphoreCount = 1,
		.pWaitSemaphores = &l_RenderFinished,
		.swapchainCount = 1,
		.pSwapchains = &m_Swapchain.swapchain,
		.pImageIndices = &l_ImageIndex,
		.pResults = nullptr,
	};
	const VkResult l_PresentResult = m_DeviceData->vkQueuePresentKHR(m_GraphicsQueue, &l_PresentInfo);
	if (l_PresentResult == VK_ERROR_OUT_OF_DATE_KHR || l_PresentResult == VK_SUBOPTIMAL_KHR)
	{
		const VkSemaphoreSubmitInfo l_Wait{
			.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
			.pNext = nullptr,
			.semaphore = l_RenderFinished,
			.value = 0,
			.stageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
			.deviceIndex = 0,
		};
		const VkSubmitInfo2 l_Consume{
			.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
			.pNext = nullptr,
			.flags = 0,
			.waitSemaphoreInfoCount = 1,
			.pWaitSemaphoreInfos = &l_Wait,
			.commandBufferInfoCount = 0,
			.pCommandBufferInfos = nullptr,
			.signalSemaphoreInfoCount = 0,
			.pSignalSemaphoreInfos = nullptr,
		};
		m_DeviceData->vkQueueSubmit2(m_GraphicsQueue, 1, &l_Consume, VK_NULL_HANDLE);
		m_DeviceData->vkQueueWaitIdle(m_GraphicsQueue);
	}

	m_CurrentFrame = (m_CurrentFrame + 1) % l_Frames;
}

void Engine::destroy()
{

#ifndef NDEBUG
	spdlog::debug("Destroying engine...");
#endif
	m_DeviceData->vkDeviceWaitIdle(m_DeviceData.device);

	ImGui_ImplVulkan_Shutdown();
	ImGui_ImplSDL3_Shutdown();
	ImGui::DestroyContext();

	if (m_ImguiDescriptorPool)
	{
		m_DeviceData->vkDestroyDescriptorPool(m_DeviceData.device, m_ImguiDescriptorPool, nullptr);
	}

	vkp::destroyBuffer(m_DeviceData, m_TriangleVertexBuffer);

	for (const VkSemaphore l_Semaphore : m_RenderFinishedSemaphores)
	{
		m_DeviceData->vkDestroySemaphore(m_DeviceData.device, l_Semaphore, nullptr);
	}
	for (const VkSemaphore l_Semaphore : m_ImageAvailableSemaphores)
	{
		m_DeviceData->vkDestroySemaphore(m_DeviceData.device, l_Semaphore, nullptr);
	}

	if (m_TrianglePipeline.pipeline)
	{
		m_DeviceData->vkDestroyPipeline(m_DeviceData.device, m_TrianglePipeline.pipeline, nullptr);
	}
	if (m_TrianglePipeline.layout)
	{
		m_DeviceData->vkDestroyPipelineLayout(m_DeviceData.device, m_TrianglePipeline.layout, nullptr);
	}
	for (const VkDescriptorSetLayout l_Layout : m_TrianglePipeline.descriptorSetLayouts)
	{
		if (l_Layout)
		{
			m_DeviceData->vkDestroyDescriptorSetLayout(m_DeviceData.device, l_Layout, nullptr);
		}
	}

	m_DeviceData->vkDestroyImageView(m_DeviceData.device, m_DepthBufferView, nullptr);
	vmaDestroyImage(m_DeviceData.allocator, m_DepthBuffer.image, m_DepthBuffer.alloc);

	for (vkp::cmd::CommandPool& l_Pool : m_CommandPools)
	{
		l_Pool.destroy(m_DeviceData.device);
	}

	m_Swapchain.destroy(m_DeviceData);

	m_DeviceData->vkDestroySemaphore(m_DeviceData.device, m_TimelineSemaphore, nullptr);

	vmaDestroyAllocator(m_DeviceData.allocator);
	vkDestroyDevice(m_DeviceData.device, nullptr);

	m_Window.destroy(m_Instance);
	vkp::destroyInstance(m_Instance, m_DebugUtils);

	volkFinalize();
}

void Engine::initImgui()
{
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGui::StyleColorsDark();

	const float l_MainScale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
	ImGuiStyle& l_Style = ImGui::GetStyle();
	l_Style.ScaleAllSizes(l_MainScale);
	l_Style.FontScaleDpi = l_MainScale;

	VkDescriptorPoolSize l_PoolSizes[] = {
		{ .type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, .descriptorCount = IMGUI_IMPL_VULKAN_MINIMUM_SAMPLED_IMAGE_POOL_SIZE },
		{ .type = VK_DESCRIPTOR_TYPE_SAMPLER, .descriptorCount = IMGUI_IMPL_VULKAN_MINIMUM_SAMPLER_POOL_SIZE },
	};

	VkDescriptorPoolCreateInfo l_PoolInfo{ .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
	l_PoolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
	l_PoolInfo.maxSets = 0;
	for (const VkDescriptorPoolSize& l_PoolSize : l_PoolSizes)
		l_PoolInfo.maxSets += l_PoolSize.descriptorCount;
	l_PoolInfo.poolSizeCount = static_cast<uint32_t>(IM_COUNTOF(l_PoolSizes));
	l_PoolInfo.pPoolSizes = l_PoolSizes;
	VULKAN_TRY(m_DeviceData->vkCreateDescriptorPool(m_DeviceData.device, &l_PoolInfo, nullptr, &m_ImguiDescriptorPool));

	VkPipelineRenderingCreateInfo l_PipelineRenderingInfo{ .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
	l_PipelineRenderingInfo.pNext = nullptr;
	l_PipelineRenderingInfo.colorAttachmentCount = 1;
	l_PipelineRenderingInfo.pColorAttachmentFormats = &m_Swapchain.properties.format.format;
	l_PipelineRenderingInfo.depthAttachmentFormat = VK_FORMAT_D32_SFLOAT;
	l_PipelineRenderingInfo.stencilAttachmentFormat = VK_FORMAT_UNDEFINED;

	ImGui_ImplSDL3_InitForVulkan(*m_Window);
	ImGui_ImplVulkan_InitInfo l_InitInfo{};
	l_InitInfo.Instance = m_Instance;
	l_InitInfo.PhysicalDevice = m_DeviceData.physicalDevice;
	l_InitInfo.Device = m_DeviceData.device;
	l_InitInfo.QueueFamily = m_QueueFamilyIndex;
	l_InitInfo.Queue = m_GraphicsQueue;
	l_InitInfo.PipelineCache = VK_NULL_HANDLE;
	l_InitInfo.DescriptorPool = m_ImguiDescriptorPool;
	l_InitInfo.MinImageCount = 2;
	l_InitInfo.ImageCount = m_Swapchain.images.size();
	l_InitInfo.Allocator = VK_NULL_HANDLE;
	l_InitInfo.UseDynamicRendering = true;
	l_InitInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
	l_InitInfo.PipelineInfoMain.PipelineRenderingCreateInfo = l_PipelineRenderingInfo;
	ImGui_ImplVulkan_Init(&l_InitInfo);
}

void Engine::imguiDraw()
{
	ImGui_ImplVulkan_NewFrame();
	ImGui_ImplSDL3_NewFrame();
	ImGui::NewFrame();

	ImGui::Begin("Config");
	ImGui::SliderFloat3("Tint", &m_ImguiTint.x, 0.0f, 1.0f);
	ImGui::End();

	ImGui::Render();
}

void Engine::recreateSwapchain(const Window::Size p_Extent)
{
	m_Swapchain.recreate(m_DeviceData, m_Window.getSurface(), p_Extent.toVkExtent2D());
	ensureFrameSlots(static_cast<uint32_t>(m_Swapchain.images.size()));
#ifndef NDEBUG
	spdlog::debug("Recreated swapchain with new extent: {}x{} ({} images)", p_Extent.width, p_Extent.height, m_Swapchain.images.size());
#endif
}
