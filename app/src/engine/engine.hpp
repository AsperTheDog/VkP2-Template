#pragma once
#include <volk.h>

#include "vkp2/buffer.hpp"
#include "vkp2/command_buffer.hpp"
#include "vkp2/device.hpp"
#include "vkp2/image.hpp"
#include "vkp2/pipeline.hpp"
#include "vkp2/shader.hpp"
#include "vkp2/swapchain.hpp"
#include "vkp2/extra/window.hpp"

class Engine
{
public:
	void init();
	void run();
	void destroy();

private:
	void recreateSwapchain(Window::Size p_Extent);
	void drawFrame();
	void ensureFrameSlots(uint32_t p_Count);

	Window m_Window;

	VkInstance m_Instance = VK_NULL_HANDLE;
	VkDebugUtilsMessengerEXT m_DebugUtils = VK_NULL_HANDLE;

	vkp::device::DeviceData m_DeviceData{};

	VkSemaphore m_TimelineSemaphore = VK_NULL_HANDLE;
	VkQueue m_GraphicsQueue = VK_NULL_HANDLE;
	VkQueue m_TransferQueue = VK_NULL_HANDLE;

	vkp::Swapchain m_Swapchain{};

	std::vector<vkp::cmd::CommandPool> m_CommandPools;
	std::vector<VkCommandBuffer> m_CommandBuffers;

	std::vector<VkSemaphore> m_ImageAvailableSemaphores;
	std::vector<VkSemaphore> m_RenderFinishedSemaphores;
	std::vector<uint64_t> m_SlotTimelineValues;
	uint64_t m_TimelineValue = 0;
	uint32_t m_CurrentFrame = 0;

	vkp::BufferData m_TriangleVertexBuffer{};
	vkp::ImageData m_DepthBuffer{};
	VkImageView m_DepthBufferView = VK_NULL_HANDLE;

	vkp::shader::Shader<true> m_TriangleShader;
	vkp::pipeline::PipelineData m_TrianglePipeline{};
};

