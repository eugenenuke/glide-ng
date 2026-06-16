#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <vector>
#include <iostream>
#include <fstream>
#include <cstdint>

#include <glide.h>
#include "tlib.h"

void dumpBackBufferToPpm(const char* filename, int width, int height) {
    std::vector<uint16_t> buffer(width * height);
    // Read from back buffer
    grLfbReadRegion(GR_BUFFER_BACKBUFFER, 0, 0, width, height, width * 2, buffer.data());

    std::ofstream ofs(filename, std::ios::binary);
    if (!ofs) {
        std::cerr << "Failed to open dump file: " << filename << "\n";
        return;
    }
    ofs << "P6\n" << width << " " << height << "\n255\n";
    for (int i = 0; i < width * height; ++i) {
        uint16_t val = buffer[i];
        uint8_t r = ((val >> 11) & 0x1F) * 255 / 31;
        uint8_t g = ((val >> 5)  & 0x3F) * 255 / 63;
        uint8_t b = (val & 0x1F) * 255 / 31;
        uint8_t rgb[3] = {r, g, b};
        ofs.write(reinterpret_cast<const char*>(rgb), 3);
    }
    ofs.close();
    std::cout << "Dumped back buffer to " << filename << "\n";
}

int main(int argc, char **argv) {
    GrScreenResolution_t resolution = GR_RESOLUTION_640x480;
    float scrWidth = 640.0f;
    float scrHeight = 480.0f;

    tlSetScreen(scrWidth, scrHeight);

    grGlideInit();
    grSstSelect(0);
    assert(grSstWinOpen(0, resolution, GR_REFRESH_60Hz, GR_COLORFORMAT_ABGR, GR_ORIGIN_UPPER_LEFT, 2, 1));

    // Initialize console
    tlConSet(0.0f, 0.0f, 1.0f, 1.0f, 60, 30, 0xffffff);

    tlConOutput("GLIDE TEXT RENDERING VERIFICATION\n");
    tlConOutput("=================================\n");
    tlConOutput("ABCDEFGHIJKLMNOPQRSTUVWXYZ\n");
    tlConOutput("abcdefghijklmnopqrstuvwxyz\n");
    tlConOutput("0123456789 !@#$%%^&*()_+-=\n");
    tlConOutput("LINE 6: TESTING TEXT PARITY WITH OPENGLIDE\n");

    // Run for 15 frames to ensure window is fully mapped and active
    for (int frame = 0; frame < 15; ++frame) {
        grBufferClear(0xff0000, 0, GR_WDEPTHVALUE_FARTHEST);

        // Render the console text to the back buffer
        tlConRender();

        // On the 14th frame (just before swapping), dump the back buffer
        if (frame == 14) {
            dumpBackBufferToPpm("text_parity_output.ppm", 640, 480);
        }

        grBufferSwap(1);
    }

    grGlideShutdown();
    return 0;
}
