#include "fast/renderer/content_hash.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <iomanip>
#include <string>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: azahar_hasher <file_path> [offset] [size]\n";
        return 1;
    }
    std::string path = argv[1];
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        std::cerr << "Cannot open " << path << "\n";
        return 1;
    }
    std::vector<uint8_t> buffer((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    size_t offset = 0;
    size_t size = buffer.size();
    if (argc >= 3) {
        offset = std::stoull(argv[2]);
    }
    if (argc >= 4) {
        size = std::stoull(argv[3]);
    }
    if (offset + size > buffer.size()) {
        std::cerr << "Out of bounds\n";
        return 1;
    }
    std::span<const uint8_t> data(buffer.data() + offset, size);
    uint64_t hash = Fast::Renderer::ContentHash64(data);
    std::cout << std::uppercase << std::hex << std::setw(16) << std::setfill('0') << hash << "\n";
    return 0;
}
