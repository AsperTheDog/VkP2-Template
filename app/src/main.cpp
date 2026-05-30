#include "engine/engine.hpp"

int main(int argc, char** argv)
{
    Engine l_Engine{};
    l_Engine.init();
    l_Engine.run();
    l_Engine.destroy();
    return 0;
}
