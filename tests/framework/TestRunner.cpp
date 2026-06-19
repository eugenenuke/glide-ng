#include "TestRunner.h"
#include <glide.h>
#include <iostream>
#include <memory>
#include <vector>
#include <string>
#include <cstring>
#include <fstream>
#include <algorithm>
#include <filesystem>

// Forward declarations of test factory functions
namespace GlideTest {
    std::unique_ptr<BaseTest> CreateTest00();
    std::unique_ptr<BaseTest> CreateTest01();
    std::unique_ptr<BaseTest> CreateTest02();
    std::unique_ptr<BaseTest> CreateTest03();
    std::unique_ptr<BaseTest> CreateTest04();
    std::unique_ptr<BaseTest> CreateTest05();
    std::unique_ptr<BaseTest> CreateTest06();
    std::unique_ptr<BaseTest> CreateTest07();
    std::unique_ptr<BaseTest> CreateTest08();
    std::unique_ptr<BaseTest> CreateTest09();
    std::unique_ptr<BaseTest> CreateTest10();
    std::unique_ptr<BaseTest> CreateTest11();
    std::unique_ptr<BaseTest> CreateTest12();
    std::unique_ptr<BaseTest> CreateTest13();
    std::unique_ptr<BaseTest> CreateTest14();
    std::unique_ptr<BaseTest> CreateTest15();
    std::unique_ptr<BaseTest> CreateTest16();
    std::unique_ptr<BaseTest> CreateTest17();
    std::unique_ptr<BaseTest> CreateTest18();
    std::unique_ptr<BaseTest> CreateTest19();
    std::unique_ptr<BaseTest> CreateTest29();
    std::unique_ptr<BaseTest> CreateTest21();
    std::unique_ptr<BaseTest> CreateTest26();
}

void dumpBufferToPpm(const std::string& filepath, int width, int height) {
    std::vector<uint16_t> buffer(width * height);
    // Read from front buffer (after buffer swap)
    grLfbReadRegion(GR_BUFFER_FRONTBUFFER, 0, 0, width, height, width * 2, buffer.data());

    std::ofstream ofs(filepath, std::ios::binary);
    if (!ofs) {
        std::cerr << "Failed to open dump file: " << filepath << "\n";
        return;
    }
    ofs << "P6\n" << width << " " << height << "\n255\n";

    // Allocate a single contiguous buffer for the PPM pixel data (3 bytes per pixel)
    std::vector<uint8_t> ppmData(width * height * 3);
    for (int i = 0; i < width * height; ++i) {
        uint16_t val = buffer[i];
        ppmData[i * 3 + 0] = ((val >> 11) & 0x1F) * 255 / 31;
        ppmData[i * 3 + 1] = ((val >> 5)  & 0x3F) * 255 / 63;
        ppmData[i * 3 + 2] = (val & 0x1F) * 255 / 31;
    }

    // Write the entire pixel buffer to disk in a single contiguous write!
    ofs.write(reinterpret_cast<const char*>(ppmData.data()), ppmData.size());
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
    // tests.push_back(GlideTest::CreateTest00()); // Disabled in favor of hardware-accurate Catch2 unit test validation!
    tests.push_back(GlideTest::CreateTest01());
    tests.push_back(GlideTest::CreateTest02());
    tests.push_back(GlideTest::CreateTest03());
    tests.push_back(GlideTest::CreateTest04());
    tests.push_back(GlideTest::CreateTest05());
    tests.push_back(GlideTest::CreateTest06());
    tests.push_back(GlideTest::CreateTest07());
    tests.push_back(GlideTest::CreateTest08());
    tests.push_back(GlideTest::CreateTest09());
    tests.push_back(GlideTest::CreateTest10());
    tests.push_back(GlideTest::CreateTest11());
    tests.push_back(GlideTest::CreateTest12());
    tests.push_back(GlideTest::CreateTest13());
    tests.push_back(GlideTest::CreateTest14());
    tests.push_back(GlideTest::CreateTest15());
    tests.push_back(GlideTest::CreateTest16());
    tests.push_back(GlideTest::CreateTest17());
    tests.push_back(GlideTest::CreateTest18());
    tests.push_back(GlideTest::CreateTest19());
    tests.push_back(GlideTest::CreateTest29());
    tests.push_back(GlideTest::CreateTest21());
    tests.push_back(GlideTest::CreateTest26());

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

    for (int m = 0; m < activeTest->GetTotalModes(); ++m) {
        activeTest->Initialize();
        activeTest->SetMode(m);

        std::string destDir = "tests/test_dumps";
        if (mode == "capture") {
            destDir = "tests/reference_dumps";
        } else {
            const char* rendererStr = grGetString(GR_RENDERER);
            std::string renderer = rendererStr ? rendererStr : "";
            if (renderer.find("OpenGL ES") != std::string::npos) {
                destDir = "tests/opengl_es_dumps";
            } else if (renderer.find("Software") != std::string::npos) {
                destDir = "tests/software_dumps";
            } else {
                destDir = "tests/vulkan_dumps";
            }
        }
        std::filesystem::create_directories(destDir);

        int maxFrames = activeTest->GetMaxFrames();
        std::vector<int> captureFrames = activeTest->GetCaptureFrames();

        for (int frame = 0; frame < maxFrames; ++frame) {
            if (!activeTest->RenderFrame(frame)) {
                break;
            }
            grBufferSwap(1);
            if (std::find(captureFrames.begin(), captureFrames.end(), frame) != captureFrames.end()) {
                char filename[256];
                std::snprintf(filename, sizeof(filename), "%s/%s_mode%d_frame%d.ppm",
                             destDir.c_str(), activeTest->GetName().c_str(), m, frame);
                dumpBufferToPpm(filename, 640, 480);
            }
        }
        activeTest->Shutdown();
    }

    return 0;
}
