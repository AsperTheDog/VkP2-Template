#pragma once
#include <array>
#include <cstddef>

#include <volk.h>
#include <glm/glm.hpp>

#define VKP2_INCLUDE_EXTRA
#include "vkp2/vkp2.hpp"

inline constexpr VkSurfaceFormatKHR PREFERRED_SURFACE_FORMATS[]{
	{ .format = VK_FORMAT_B8G8R8A8_SRGB, .colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR },
};

class Engine
{
public:
	Engine() : m_FrameArena(m_FrameArenaBuffer.data(), FRAME_ARENA_BYTES) {}

	void init();
	void run();
	void destroy();

private:
	static constexpr uint32_t FRAMES_IN_FLIGHT = 3;

	static constexpr size_t FRAME_ARENA_BYTES = 64 * 1024ULL;
	alignas(std::max_align_t) std::array<std::byte, FRAME_ARENA_BYTES> m_FrameArenaBuffer{};
	vkp::dyn::FrameArena m_FrameArena;

	void initImgui();
	void imguiDraw();

	void recreateSwapchain(Window::Size p_Extent);
	void ensureDepthResources();
	void drawFrame();
	void ensureFrameSlots(uint32_t p_Count);

	struct FrameResources
	{
		VkSemaphore imageAvailableSemaphore = VK_NULL_HANDLE;
		vkp::cmd::CommandPool commandPool{};
		VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
		uint64_t timelineValue = 0;

		vkp::Image depthBuffer{};
		VkImageView depthView = VK_NULL_HANDLE;
		VkExtent2D depthExtent{};
	};

	Window m_Window;

	VkInstance m_Instance = VK_NULL_HANDLE;
	VkDebugUtilsMessengerEXT m_DebugUtils = VK_NULL_HANDLE;

	vkp::device::DeviceData m_DeviceData{};

	VkSemaphore m_TimelineSemaphore = VK_NULL_HANDLE;
	VkQueue m_GraphicsQueue = VK_NULL_HANDLE;
	VkQueue m_TransferQueue = VK_NULL_HANDLE;
	uint32_t m_QueueFamilyIndex = 0;

	vkp::Swapchain m_Swapchain{};

	std::vector<FrameResources> m_FrameResources{};
	uint64_t m_TimelineValue = 0;
	uint32_t m_CurrentFrame = 0;

	vkp::BufferData m_TriangleVertexBuffer{};

	vkp::shader::Shader<true> m_TriangleShader;
	vkp::dyn::PipelineData<> m_TrianglePipeline{};

	VkDescriptorPool m_ImguiDescriptorPool = VK_NULL_HANDLE;

	glm::vec4 m_ImguiTint{1.f, 1.f, 1.f, 1.f};
};

