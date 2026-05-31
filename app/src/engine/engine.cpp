#include "engine.hpp"

#include <vulkan/vk_enum_string_helper.h>

#include "spdlog/spdlog.h"
#include "vkp2/device.hpp"
#include "vkp2/instance.hpp"
#include "vkp2/sync.hpp"
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

		vkp::InstanceBuilder l_Builder{};
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

		m_DeviceData.physicalDevice = l_ChosenDevice.value();

		const vkp::device::DeviceReturn l_Return = vkp::device::buildFromEval(m_DeviceData.physicalDevice, &l_DeviceEval);
		m_DeviceData.device = l_Return.device;

		volkLoadDevice(m_DeviceData.device);
		volkLoadDeviceTable(&m_DeviceData.deviceTable, m_DeviceData.device);

		m_DeviceData.deviceTable.vkGetDeviceQueue(m_DeviceData.device, l_Return.queues[0].queueFamilyIndex, 0, &m_GraphicsQueue);
		m_DeviceData.deviceTable.vkGetDeviceQueue(m_DeviceData.device, l_Return.queues[0].queueFamilyIndex, 1, &m_TransferQueue);
	}

	{
		m_TimelineSemaphore = vkp::createTimelineSemaphore(m_DeviceData.device);
	}

	{
		m_Swapchain = vkp::Swapchain(m_DeviceData, m_Window.getSurface(), 3, m_Window.getSize().toVkExtent2D(), VK_PRESENT_MODE_FIFO_KHR);
	}

	{
		m_CommandPools.resize(m_Swapchain.properties.framesInFlight);
		vkp::cmd::CommandPool::createPools(m_CommandPools, m_DeviceData.device, 0);
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
	for (vkp::cmd::CommandPool& l_Pool : m_CommandPools)
	{
		l_Pool.destroy(m_DeviceData.device);
	}

	m_Swapchain.destroy(m_DeviceData);

	vkDestroySemaphore(m_DeviceData.device, m_TimelineSemaphore, nullptr);

	vkDestroyDevice(m_DeviceData.device, nullptr);

	m_Window.destroy(m_Instance);
	vkp::destroyInstance(m_Instance, m_DebugUtils);

	volkFinalize();
}
