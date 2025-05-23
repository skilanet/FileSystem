#include <algorithm>
#include <iostream>

#include "../include/file_system_defs.h"

int main(int argc, char *argv[]) {
    constexpr FileSystem::Header header{
        "v1.0.0",
        std::numeric_limits<uint64_t>::max(), std::numeric_limits<uint16_t>::max(),
        std::numeric_limits<uint32_t>::max(),
        std::numeric_limits<uint32_t>::max(), std::numeric_limits<uint16_t>::max(),
        std::numeric_limits<uint32_t>::max(), std::numeric_limits<uint16_t>::max(),
        std::numeric_limits<uint32_t>::max(), std::numeric_limits<uint16_t>::max(),
        std::numeric_limits<uint32_t>::max()
    };
    std::cout << sizeof(char[16]) + sizeof(uint64_t) + sizeof(uint16_t) + sizeof(uint32_t) +
            sizeof(uint32_t) + sizeof(uint16_t) +
            sizeof(uint32_t) + sizeof(uint16_t) +
            sizeof(uint32_t) + sizeof(uint16_t) +
            sizeof(uint32_t) << std::endl;
    std::cout << sizeof(header) << std::endl;
}
