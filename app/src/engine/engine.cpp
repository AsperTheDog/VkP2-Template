#include "engine.hpp"

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
#include "vkp2/dyn/barrier.hpp"
#include "vkp2/extra/window.hpp"

constexpr bool g_AssertOnError = false;

constexpr VkPipelineStageFlags2 g_DepthStages = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;

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
	m_FrameArena.setGrowHandler([](const size_t p_RequestedBytes, const size_t p_ChunkBytes, void*)
	{
#ifndef NDEBUG
		spdlog::warn("Frame arena budget exceeded: took a chunk of {} bytes for a {} byte request", p_ChunkBytes, p_RequestedBytes);
#else
		(void)p_RequestedBytes;
		(void)p_ChunkBytes;
#endif
	});

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
		m_Swapchain = vkp::Swapchain(m_DeviceData, m_Window.getSurface(), c_FramesInFlight, m_Window.getSize().toVkExtent2D(), VK_PRESENT_MODE_FIFO_KHR, g_PreferredSurfaceFormats);
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
		ensureFrameSlots(c_FramesInFlight);

#ifndef NDEBUG
		spdlog::debug("Created {} frame resources", m_FrameResources.size());
#endif
	}

	{
		vkp::shader::CompileOptions l_Opts;
		l_Opts.cacheFolder = "cache/spv";

		m_TriangleShader = vkp::shader::compileFromFile<true>("shaders/triangle.slang", "triangle", l_Opts);

		VkShaderModule l_Vert = m_TriangleShader.createModule(m_DeviceData, VK_SHADER_STAGE_VERTEX_BIT);
		VkShaderModule l_Frag = m_TriangleShader.createModule(m_DeviceData, VK_SHADER_STAGE_FRAGMENT_BIT);

		const VkFormat l_ColorFormats[]{ m_Swapchain.properties.format.format };

		vkp::dyn::PipelineBuilder<> l_Builder;
		l_Builder.useReflection(m_TriangleShader)
			.setPipelineCacheFolder("cache/pipeline")
			.setColorFormats(l_ColorFormats)
			.setDepthFormat(VK_FORMAT_D32_SFLOAT)
			.setDepthTest(true, true)
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
		vkp::uploadBuffer(m_DeviceData, m_FrameResources[0].commandPool.handle, m_TransferQueue, m_TriangleVertexBuffer, l_Vertices, sizeof(l_Vertices));

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

#ifndef NDEBUG
	spdlog::debug("Initialization complete, entering main loop");
#endif

	m_FrameArena.reset();
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
	while (m_FrameResources.size() < p_Count)
	{
		FrameResources& l_Frame = m_FrameResources.emplace_back();
		l_Frame.commandPool.init(m_DeviceData.device, m_QueueFamilyIndex);
		l_Frame.commandBuffer = l_Frame.commandPool.allocate(m_DeviceData.device, VK_COMMAND_BUFFER_LEVEL_PRIMARY);
		l_Frame.imageAvailableSemaphore = vkp::createSemaphore(m_DeviceData.device);
		l_Frame.timelineValue = 0;
	}
}

void Engine::ensureDepthResources()
{
	constexpr VmaAllocationCreateInfo l_AllocInfo{
		.flags = 0,
		.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
	};

	const VkExtent2D l_Extent = m_Swapchain.properties.extent;
	uint32_t l_Created = 0;

	auto l_Transitions = vkp::dyn::makeBarrierBuilder(m_FrameArena.allocator<void>());

	for (FrameResources& l_Frame : m_FrameResources)
	{
		if (l_Frame.depthView != VK_NULL_HANDLE && l_Frame.depthExtent.width == l_Extent.width && l_Frame.depthExtent.height == l_Extent.height)
		{
			continue;
		}

		if (l_Frame.depthView != VK_NULL_HANDLE)
		{
			m_DeviceData->vkDestroyImageView(m_DeviceData.device, l_Frame.depthView, nullptr);
			l_Frame.depthView = VK_NULL_HANDLE;
		}
		vkp::destroyImage(m_DeviceData, l_Frame.depthBuffer);

		l_Frame.depthBuffer = vkp::createDepthBuffer(m_DeviceData, l_Extent, l_AllocInfo);
		l_Frame.depthView = vkp::createImageView(m_DeviceData, l_Frame.depthBuffer);
		l_Frame.depthExtent = l_Extent;
		++l_Created;

		l_Transitions.image(l_Frame.depthBuffer, {
			.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED, .newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
			.srcStage = VK_PIPELINE_STAGE_2_NONE,	.srcAccess = VK_ACCESS_2_NONE,
			.dstStage = g_DepthStages,				.dstAccess = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT
		});
	}

	if (l_Created > 0)
	{
		vkp::cmd::immediateSubmitScope(m_DeviceData, m_DeviceData.device, m_FrameResources.front().commandPool.handle, m_GraphicsQueue, [&](const VkCommandBuffer p_Cb)
			{
#ifndef NDEBUG
				spdlog::debug("Transitioning {} depth buffers in one barrier call", l_Created);
#endif
				l_Transitions.record(m_DeviceData, p_Cb);
			});
	}

#ifndef NDEBUG
	if (l_Created == 0)
	{
		spdlog::debug("Kept {} depth buffers at {}x{}", m_FrameResources.size(), l_Extent.width, l_Extent.height);
	}
	else
	{
		spdlog::debug("Created {} depth buffers of {}x{} ({} kept)", l_Created, l_Extent.width, l_Extent.height, m_FrameResources.size() - l_Created);
		spdlog::debug("Transitions for those came out of the frame arena: {} of {} bytes used, high water {}", m_FrameArena.used(), m_FrameArena.budget(), m_FrameArena.highWater());
		for (const FrameResources& l_Frame : m_FrameResources)
		{
			spdlog::debug("Depth buffer image: {}, view: {}, size: {}, memoryType: {}", fmt::ptr(l_Frame.depthBuffer.data.image), fmt::ptr(l_Frame.depthView), l_Frame.depthBuffer.data.info.size, l_Frame.depthBuffer.data.info.memoryType);
		}
	}
#endif
}

void Engine::recreateSwapchain(const Window::Size p_Extent)
{
	m_DeviceData->vkQueueWaitIdle(m_GraphicsQueue);
	m_Swapchain.recreate(m_DeviceData, m_Window.getSurface(), p_Extent.toVkExtent2D());
	ensureFrameSlots(c_FramesInFlight);
	ensureDepthResources();
#ifndef NDEBUG
	spdlog::debug("Recreated swapchain with new extent: {}x{} ({} images)", p_Extent.width, p_Extent.height, m_Swapchain.images.size());
#endif
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
	l_InitInfo.ImageCount = m_Swapchain.properties.framesInFlight + 1;
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

void Engine::drawFrame()
{
	FrameResources& l_Frame = m_FrameResources[m_CurrentFrame];

	if (l_Frame.timelineValue != 0)
	{
		vkp::cmd::waitTimeline(m_DeviceData, m_TimelineSemaphore, l_Frame.timelineValue);
	}

	uint32_t l_ImageIndex = 0;
	const VkResult l_AcquireResult = m_DeviceData->vkAcquireNextImageKHR(m_DeviceData.device, m_Swapchain.swapchain, UINT64_MAX, l_Frame.imageAvailableSemaphore, VK_NULL_HANDLE, &l_ImageIndex);
	if (l_AcquireResult == VK_ERROR_OUT_OF_DATE_KHR)
	{
		return;
	}

	const VkSemaphore l_RenderFinished = m_Swapchain.renderFinishedSemaphores[l_ImageIndex];

	m_DeviceData->vkResetCommandPool(m_DeviceData.device, l_Frame.commandPool.handle, 0);

	vkp::cmd::recordingScope(m_DeviceData, l_Frame.commandBuffer, true, [&](const VkCommandBuffer p_Cb)
	{
		constexpr VkClearValue l_ClearColor{ .color = { .float32 = { 0.15f, 0.15f, 0.2f, 1.0f } } };
		constexpr VkClearValue l_ClearDepth{ .depthStencil = { .depth = 1.0f, .stencil = 0 } };

		vkp::cmd::RenderingInfoBuilder l_RenderTargets{};
		l_RenderTargets
			.setRenderArea(m_Swapchain.properties.extent)
			.color({
				.imageView = m_Swapchain.imageViews[l_ImageIndex],
				.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
				.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
				.clearValue = l_ClearColor,
			})
			.depth({
				.imageView = l_Frame.depthView,
				.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
				.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
				.clearValue = l_ClearDepth,
			});

		const VkRenderingInfo l_RenderingInfo = l_RenderTargets.build();

		vkp::cmd::BasicBarrierBuilder<0, 0, 1> l_Barriers{};
		const vkp::ImageProperties l_SwapchainImage = m_Swapchain.imageProperties();

#ifndef NDEBUG
		constexpr float l_LabelColor[4]{ 1.0f, 1.0f, 1.0f, 1.0f };
		vkp::cmd::debugScope(p_Cb, "triangle", l_LabelColor, [&](const VkCommandBuffer p_Cb)
		{
#endif
			l_Barriers
				.image(m_Swapchain.images[l_ImageIndex], l_SwapchainImage, {
					.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,						 .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
					.srcStage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, .srcAccess = VK_ACCESS_2_NONE,
					.dstStage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, .dstAccess = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT
				}).record(m_DeviceData, p_Cb);

			vkp::cmd::renderScope(m_DeviceData, p_Cb, l_RenderingInfo, [&](const VkCommandBuffer p_Cb)
			{
				m_DeviceData->vkCmdBindPipeline(p_Cb, VK_PIPELINE_BIND_POINT_GRAPHICS, m_TrianglePipeline.pipeline);

				vkp::cmd::pushConstants(m_DeviceData, p_Cb, m_TrianglePipeline.layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(m_ImguiTint), &m_ImguiTint);

				constexpr VkDeviceSize l_Offset = 0;
				m_DeviceData->vkCmdBindVertexBuffers(p_Cb, 0, 1, &m_TriangleVertexBuffer.buffer, &l_Offset);
				m_DeviceData->vkCmdDraw(p_Cb, 3, 1, 0, 0);

				ImDrawData* l_DrawData = ImGui::GetDrawData();
				ImGui_ImplVulkan_RenderDrawData(l_DrawData, p_Cb);
			});

			l_Barriers.clear()
				.image(m_Swapchain.images[l_ImageIndex], l_SwapchainImage, {
					.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,		 .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
					.srcStage = VK_PIPELINE_STAGE_2_NONE,						 .srcAccess = VK_ACCESS_2_NONE,
					.dstStage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, .dstAccess = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT
				}).record(m_DeviceData, p_Cb);
#ifndef NDEBUG
		});
#endif
	});

	++m_TimelineValue;

	constexpr VkPipelineStageFlags2 l_WaitStage2 = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
	const VkCommandBuffer l_CommandBuffers[]{ l_Frame.commandBuffer };
	const vkp::cmd::SemaphoreSubmit l_Waits[]{
		{ .semaphore = l_Frame.imageAvailableSemaphore, .stageMask = l_WaitStage2, .value = 0 },
	};

	const vkp::cmd::SemaphoreSubmit l_Signals[]{
		{ .semaphore = l_RenderFinished, .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, .value = 0 },
		{ .semaphore = m_TimelineSemaphore, .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, .value = m_TimelineValue },
	};

	vkp::cmd::submit2<1, 1, 2>(m_DeviceData, m_GraphicsQueue, l_CommandBuffers, l_Waits, l_Signals, VK_NULL_HANDLE);
	l_Frame.timelineValue = m_TimelineValue;

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
		m_DeviceData->vkQueueWaitIdle(m_GraphicsQueue);
	}

	m_CurrentFrame = (m_CurrentFrame + 1) % c_FramesInFlight;

	m_FrameArena.reset();
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

	for (FrameResources& l_Frame : m_FrameResources)
	{
		vkp::destroySemaphore(m_DeviceData.device, l_Frame.imageAvailableSemaphore);
		vkp::destroyImageView(m_DeviceData, l_Frame.depthView);
		vkp::destroyImage(m_DeviceData, l_Frame.depthBuffer);
		l_Frame.commandPool.destroy(m_DeviceData.device);
	}

	vkp::pipeline::destroyPipeline(m_DeviceData, m_TrianglePipeline);

	m_Swapchain.destroy(m_DeviceData);

	m_DeviceData->vkDestroySemaphore(m_DeviceData.device, m_TimelineSemaphore, nullptr);

	vmaDestroyAllocator(m_DeviceData.allocator);
	vkDestroyDevice(m_DeviceData.device, nullptr);

	m_Window.destroy(m_Instance);
	vkp::destroyInstance(m_Instance, m_DebugUtils);

	volkFinalize();
}
