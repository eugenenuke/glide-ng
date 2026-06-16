#include <iostream>
#include <fstream>
#include <vector>
#include <cstdint>
#include <cstring>

bool SimpleRleDecode(uint16_t width, uint16_t height, uint8_t pixelsize, const uint8_t* mem, uint8_t* buff, size_t compressedSize) {
    uint32_t count = width * height;
    uint8_t run, lit;
    const uint8_t* memEnd = mem + compressedSize;
    
    while (count && mem < memEnd) {
        if (*mem & 0x80) {
            run = *mem & 0x7f;
            run++;
            mem++;
            if (count < run)
                return false;
            count -= run;
            while (run) {
                std::memcpy(buff, mem, pixelsize);
                run--;
                buff += pixelsize;
            }
            mem += pixelsize;
        }
        else {
            lit = *mem;
            lit++;
            mem++;
            if (count < lit)
                return false;
            count -= lit;
            while (lit) {
                std::memcpy(buff, mem, pixelsize);
                lit--;
                buff += pixelsize;
                mem += pixelsize;
            }
        }
    }
    return count == 0;
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <input.rle> <output.ppm>\n";
        return 1;
    }

    std::ifstream ifs(argv[1], std::ios::binary);
    if (!ifs) {
        std::cerr << "Failed to open input file: " << argv[1] << "\n";
        return 1;
    }

    uint32_t signature = 0;
    uint16_t width = 0;
    uint16_t height = 0;
    uint8_t depth = 0;
    uint8_t type = 0;

    ifs.read(reinterpret_cast<char*>(&signature), sizeof(signature));
    ifs.read(reinterpret_cast<char*>(&width), sizeof(width));
    ifs.read(reinterpret_cast<char*>(&height), sizeof(height));
    ifs.read(reinterpret_cast<char*>(&depth), sizeof(depth));
    ifs.read(reinterpret_cast<char*>(&type), sizeof(type));

    if (!ifs) {
        std::cerr << "Failed to read header\n";
        return 1;
    }

    // Read remaining compressed data
    std::vector<uint8_t> compressedData((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    ifs.close();

    uint8_t pixelsize = depth / 8;
    if (pixelsize != 2) {
        std::cerr << "Unsupported pixelsize: " << (int)pixelsize << " (only 16-bit depth is supported)\n";
        return 1;
    }

    std::vector<uint8_t> decompressedData(width * height * pixelsize);
    if (!SimpleRleDecode(width, height, pixelsize, compressedData.data(), decompressedData.data(), compressedData.size())) {
        std::cerr << "RLE decoding failed\n";
        return 1;
    }

    // Write PPM file (P6 format)
    std::ofstream ofs(argv[2], std::ios::binary);
    if (!ofs) {
        std::cerr << "Failed to open output file: " << argv[2] << "\n";
        return 1;
    }

    ofs << "P6\n" << width << " " << height << "\n255\n";

    const uint16_t* pixels16 = reinterpret_cast<const uint16_t*>(decompressedData.data());
    for (uint32_t i = 0; i < width * height; ++i) {
        uint16_t val = pixels16[i];
        uint8_t r = ((val >> 11) & 0x1F) * 255 / 31;
        uint8_t g = ((val >> 5)  & 0x3F) * 255 / 63;
        uint8_t b = (val & 0x1F) * 255 / 31;
        uint8_t rgb[3] = {r, g, b};
        ofs.write(reinterpret_cast<const char*>(rgb), 3);
    }
    ofs.close();

    std::cout << "Successfully converted " << argv[1] << " to " << argv[2] << "\n";
    return 0;
}
