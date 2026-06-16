#include "TestRunner.h"
#include <glide.h>
#include <iostream>
#include <memory>
#include <vector>
#include <string>
#include <cstring>
#include <fstream>
#include <algorithm>

// Forward declarations of test factory functions
namespace GlideTest {
    std::unique_ptr<BaseTest> CreateTest00();
    std::unique_ptr<BaseTest> CreateTest01();
    std::unique_ptr<BaseTest> CreateTest02();
    std::unique_ptr<BaseTest> CreateTest03();
}

void dumpBufferToPpm(const std::string& filepath, int width, int height) {
    std::vector<uint16_t> buffer(width * height);
    // Read from back buffer
    grLfbReadRegion(GR_BUFFER_BACKBUFFER, 0, 0, width, height, width * 2, buffer.data());

    std::ofstream ofs(filepath, std::ios::binary);
    if (!ofs) {
        std::cerr << "Failed to open dump file: " << filepath << "\n";
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
    std::cout << "Dumped frame buffer to " << filepath << "\n";
}

int main(int argc, char** argv) {
    std::string testName = "";
    std::string mode = "run"; // "run" or "capture"

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--test") == 0 && i + 1 < argc) {
            testName = argv[i + 1];
            ++i;
        } else if (std::strcmp(argv[i], "--mode") == 0 && i + 1 < argc) {
            mode = argv[i + 1];
            ++i;
        }
    }

    if (testName.empty()) {
        std::cerr << "Usage: " << argv[0] << " --test <test_name> [--mode <run|capture>]\n";
        return 1;
    }

    std::vector<std::unique_ptr<GlideTest::BaseTest>> tests;
    tests.push_back(GlideTest::CreateTest00());
    tests.push_back(GlideTest::CreateTest01());
    tests.push_back(GlideTest::CreateTest02());
    tests.push_back(GlideTest::CreateTest03());
    // Future tests will be added here!

    GlideTest::BaseTest* activeTest = nullptr;
    for (const auto& t : tests) {
        if (t->GetName() == testName) {
            activeTest = t.get();
            break;
        }
    }

    if (!activeTest) {
        std::cerr << "Test '" << testName << "' not found in registry.\n";
        return 1;
    }

    std::string destDir = (mode == "capture") ? "tests/reference_dumps" : "tests/test_dumps";

    std::cout << "Running test: " << activeTest->GetName() << " in mode: " << mode << "\n";

    for (int m = 0; m < activeTest->GetTotalModes(); ++m) {
        activeTest->Initialize();
        activeTest->SetMode(m);

        int maxFrames = activeTest->GetMaxFrames();
        int captureFrame = activeTest->GetCaptureFrameIndex();

        for (int frame = 0; frame < maxFrames; ++frame) {
            if (!activeTest->RenderFrame(frame)) {
                break;
            }
            if (frame == captureFrame) {
                char filename[256];
                std::snprintf(filename, sizeof(filename), "%s/%s_mode%d_frame%d.ppm",
                             destDir.c_str(), activeTest->GetName().c_str(), m, frame);
                dumpBufferToPpm(filename, 640, 480);
            }
            grBufferSwap(1);
        }
        activeTest->Shutdown();
    }

    return 0;
}
