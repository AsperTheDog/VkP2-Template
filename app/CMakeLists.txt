add_executable(VkP2Template)
target_link_libraries(VkP2Template PRIVATE SDL3)
target_link_libraries(VkP2Template PRIVATE ImGui)
target_link_libraries(VkP2Template PRIVATE VkP2)
target_link_libraries(VkP2Template PRIVATE spdlog)
target_link_libraries(VkP2Template PRIVATE Threads::Threads)

file(GLOB VkP2Template_SOURCES_0 CONFIGURE_DEPENDS
    "src/*.cpp"
    "src/*.hpp"
)
target_sources(VkP2Template PRIVATE ${VkP2Template_SOURCES_0})
target_include_directories(VkP2Template PRIVATE
    "src/"
)
if(WIN32)
    add_custom_command(TARGET VkP2Template POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$ENV{VULKAN_SDK}/Bin/slang.dll"
            $<TARGET_FILE_DIR:VkP2Template>
    )
endif()
if(MSVC)
    target_compile_options(VkP2Template PRIVATE
        "/W4"
    )
else()
    target_compile_options(VkP2Template PRIVATE
        "-Wall"
        "-Wextra"
    )
endif()
target_compile_definitions(VkP2Template PRIVATE $<$<CONFIG:Debug>:DEBUG=1>)

get_target_property(VkP2Template_ALL_SOURCES VkP2Template SOURCES)
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${VkP2Template_ALL_SOURCES})

add_custom_command(TARGET VkP2Template POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_RUNTIME_DLLS:VkP2Template>
        $<TARGET_FILE_DIR:VkP2Template>
    COMMAND_EXPAND_LISTS
)
