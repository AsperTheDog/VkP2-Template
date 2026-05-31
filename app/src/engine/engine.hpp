#pragma once
#include <volk.h>

#include "vkp2/command_buffer.hpp"
#include "vkp2/device.hpp"
#include "vkp2/swapchain.hpp"
#include "vkp2/extra/window.hpp"

class Engine
{
public:
	void init();
	void run();
	void destroy();

private:
	Window m_Window;

	VkInstance m_Instance = VK_NULL_HANDLE;
	VkDebugUtilsMessengerEXT m_DebugUtils = VK_NULL_HANDLE;

	vkp::device::DeviceData m_DeviceData{};

	VkSemaphore m_TimelineSemaphore = VK_NULL_HANDLE;
	VkQueue m_GraphicsQueue = VK_NULL_HANDLE;
	VkQueue m_TransferQueue = VK_NULL_HANDLE;

	vkp::Swapchain m_Swapchain{};

	std::vector<vkp::cmd::CommandPool> m_CommandPools;
};

