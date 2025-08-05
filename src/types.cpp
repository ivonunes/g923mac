#include "types.hpp"

Command::Command(std::initializer_list<std::uint8_t> init) {
    std::size_t i = 0;

    for (auto value : init) {
        if (i < COMMAND_MAX_LENGTH) {
            data[i++] = value;
        }
    }
    
    while (i < COMMAND_MAX_LENGTH) {
        data[i++] = 0;
    }
}
