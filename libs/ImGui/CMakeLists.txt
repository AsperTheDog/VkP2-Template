add_library(ImGui STATIC)
target_link_libraries(ImGui PUBLIC SDL3)
target_link_libraries(ImGui PUBLIC VkP2)

target_sources(ImGui PRIVATE
    "../../vendor/imgui/imgui.cpp"
    "../../vendor/imgui/imgui_demo.cpp"
    "../../vendor/imgui/imgui_draw.cpp"
    "../../vendor/imgui/imgui_tables.cpp"
    "../../vendor/imgui/imgui_widgets.cpp"
    "../../vendor/imgui/backends/imgui_impl_sdl3.cpp"
    "../../vendor/imgui/backends/imgui_impl_vulkan.cpp"
)
target_include_directories(ImGui PUBLIC
    "../../vendor/imgui/"
    "../../vendor/imgui/backends/"
)
target_compile_definitions(ImGui PUBLIC IMGUI_IMPL_VULKAN_USE_VOLK=1)
