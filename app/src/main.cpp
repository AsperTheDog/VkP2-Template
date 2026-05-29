#include <volk.h>
#include <vkp2/instance.hpp>
#include <vkp2/extra/window.hpp>

#include <vulkan/vk_enum_string_helper.h>

#include "spdlog/spdlog.h"

constexpr bool g_AssertOnError = false;

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
        assert(!g_AssertOnError);
    }

    return VK_FALSE;
}

int main(int argc, char** argv)
{
    Window l_Window{};
	l_Window.initMaximized("Vulkan App");

    volkInitialize();

	vkp::InstanceBuilder l_Builder(VK_API_VERSION_1_3);
#ifndef NDEBUG
	l_Builder.enableValidationLayers(debugCallback);
#endif
    l_Builder.addExtensions(l_Window.getRequiredInstanceExtensions());
    auto [l_Instance, l_DebugMessenger] = l_Builder.build();

    vkp::destroyInstance(l_Instance, l_DebugMessenger);

    return 0;
}
