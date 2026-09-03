#include "engine.hpp"

#include <vulkan/vk_enum_string_helper.h>

#include "spdlog/spdlog.h"
#include "vkp2/device.hpp"
#include "vkp2/instance.hpp"
#include "vkp2/sync.hpp"
#include "vkp2/extra/window.hpp"

constexpr bool g_AssertOnError = true;

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
#ifndef NDEBUG
		spdlog::debug("Initialized window with size: {}x{}", m_Window.getSize().width, m_Window.getSize().height);
#endif
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

		m_DeviceData.deviceTable.vkGetDeviceQueue(m_DeviceData.device, l_Return.queues[0].queueFamilyIndex, 0, &m_GraphicsQueue);
		m_DeviceData.deviceTable.vkGetDeviceQueue(m_DeviceData.device, l_Return.queues[0].queueFamilyIndex, 1, &m_TransferQueue);

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
		m_CommandPools.resize(m_Swapchain.properties.framesInFlight);
		vkp::cmd::CommandPool::createPools(m_CommandPools, m_DeviceData.device, 0);
		m_CommandBuffers.reserve(m_Swapchain.properties.framesInFlight);
		for (vkp::cmd::CommandPool& l_Pool : m_CommandPools)
		{
			m_CommandBuffers.push_back(l_Pool.allocate(m_DeviceData.device, VK_COMMAND_BUFFER_LEVEL_PRIMARY));
		}

#ifndef NDEBUG
		spdlog::debug("Created {} command pools and allocated {} command buffers", m_CommandPools.size(), m_CommandBuffers.size());
#endif
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

#ifndef NDEBUG
	spdlog::debug("Destroying engine...");
#endif
	vkDeviceWaitIdle(m_DeviceData.device);

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

void Engine::recreateSwapchain(const Window::Size p_Extent)
{
	m_Swapchain.recreate(m_DeviceData, m_Window.getSurface(), p_Extent.toVkExtent2D());
#ifndef NDEBUG
	spdlog::debug("Recreated swapchain with new extent: {}x{}", p_Extent.width, p_Extent.height);
#endif
}
