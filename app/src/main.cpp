#include "engine/engine.hpp"
#include "spdlog/spdlog.h"

int main([[maybe_unused]] int argc, [[maybe_unused]] char** argv)
{
#ifndef NDEBUG
    spdlog::set_level(spdlog::level::debug);
#endif

    Engine l_Engine{};
    l_Engine.init();
    l_Engine.run();
    l_Engine.destroy();
    return 0;
}
