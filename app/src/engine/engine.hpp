#pragma once
#include <volk.h>

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

	VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
	VkDevice m_Device = VK_NULL_HANDLE;
	VolkDeviceTable m_DeviceTable{};
};

