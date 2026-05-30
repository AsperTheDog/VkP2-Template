#include "engine.hpp"

#include <vulkan/vk_enum_string_helper.h>

#include "spdlog/spdlog.h"
#include "vkp2/device.hpp"
#include "vkp2/instance.hpp"
#include "vkp2/extra/window.hpp"

constexpr bool g_AssertOnError = true;

static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(const VkDebugUtilsMessageSeverityFlagBitsEXT p_MessageSeverity, const VkDebugUtilsMessageTypeFlagsEXT p_MessageType, const VkDebugUtilsMessengerCallbackDataEXT* p_CallbackData, void* p_UserData)
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

		volkInitialize();

		vkp::InstanceBuilder l_Builder(VK_API_VERSION_1_3);
#ifndef NDEBUG
		l_Builder.enableValidationLayers(debugCallback);
#endif
		l_Builder.addExtensions(m_Window.getRequiredInstanceExtensions());
		auto [l_Instance, l_DebugMessenger] = l_Builder.build();
		m_Instance = l_Instance;
		m_DebugUtils = l_DebugMessenger;

        m_Window.createSurface(m_Instance);
	}

	{
		auto l_DeviceEval = vkp::device::LeanModern();
		const std::optional<VkPhysicalDevice> l_ChosenDevice = vkp::device::chooseBestPhysicalDevice(m_Instance, m_Window.getSurface(), &l_DeviceEval);
		if (!l_ChosenDevice)
		{
			spdlog::error("No suitable physical device found!");
			return;
		}

		m_PhysicalDevice = *l_ChosenDevice;
		
		m_Device = vkp::device::buildFromEval(m_PhysicalDevice, &l_DeviceEval);

		volkLoadDevice(m_Device);
		volkLoadDeviceTable(&m_DeviceTable, m_Device);
	}
}

void Engine::run()
{
	while (!m_Window.shouldClose())
	{
		m_Window.pollEvents();
	}
}

void Engine::destroy()
{
	vkDestroyDevice(m_Device, nullptr);

	m_Window.destroy(m_Instance);
	vkp::destroyInstance(m_Instance, m_DebugUtils);

	volkFinalize();
}
