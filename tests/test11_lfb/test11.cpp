#include "../framework/TestRunner.h"
#include <glide.h>
#include "tlib.h"
#include <memory>
#include <cassert>
#include <cstring>

namespace GlideTest {

class Test11 : public BaseTest {
private:
    float scrWidth = 640.0f;
    float scrHeight = 480.0f;
    GrHwConfiguration hwconfig;

    unsigned short colorBuf[64][64];
    unsigned short grabBuf[64][64];

    unsigned long randx = 1;

    unsigned int iRandom(unsigned int maxr) {
        unsigned int n, retval;

        if (maxr > 0xFFFFFFF) {
            do {
                retval = iRandom(0xFFFFFFF);
                retval |= iRandom(maxr >> 28) << 28;
            } while (retval > maxr);
            return retval;
        }
        for (n = 1; n < 32; n++)
            if (((unsigned)1 << n) > maxr) break;
        do {
            randx = randx * 1103515245 + 12345;
            retval = (randx & 0x7fffffff) >> (31 - n);
        } while (retval > maxr);
        return retval;
    }

    int rRandom(int s, int e) {
        return s + iRandom(e - s);
    }

public:
    std::string GetName() const override { return "test11"; }

    void Initialize() override {
        tlSetScreen(scrWidth, scrHeight);
        grGlideInit();
        assert(grSstQueryHardware(&hwconfig));
        grSstSelect(0);
        assert(grSstWinOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz,
                           GR_COLORFORMAT_ABGR, GR_ORIGIN_UPPER_LEFT, 2, 1));
        
        tlConSet(0.0f, 0.0f, 1.0f, 0.5f, 60, 15, 0xffffff);
        grDitherMode(GR_DITHER_DISABLE);

        /* Create Source Bitmap to be copied to framebuffer */
        for (int y = 0; y < 64; y++) {
            for (int x = 0; x < 64; x++) {
                FxU8 red = x << 2;
                FxU8 grn = y << 2;
                FxU8 blu = (x + y) << 1;
                colorBuf[y][x] = (red & 0xF8) << 8;
                colorBuf[y][x] |= (grn & 0xFC) << 3;
                colorBuf[y][x] |= (blu & 0xF8) >> 3;
            }
        }
        randx = 1; // Reset random seed for determinism
        tlConOutput("Press any key to quit\n");
    }

    bool RenderFrame(int frameIndex) override {
        (void)frameIndex;
        grBufferClear(0x00, 0, 0);

        GrLfbInfo_t info;
        info.size = sizeof(GrLfbInfo_t);

        // Lock back buffer for writing
        if (grLfbLock(GR_LFB_WRITE_ONLY, GR_BUFFER_BACKBUFFER, GR_LFBWRITEMODE_565, GR_ORIGIN_UPPER_LEFT, FXFALSE, &info) == FXFALSE) {
            tlConOutput("Error, failed to take write lock\n");
            return false;
        }

        // Generate deterministic random positions based on frame index
        int startX = rRandom(64, (int)tlScaleX(1.0f) - 65);
        int startY = rRandom(64, (int)tlScaleY(1.0f) - 65);

        // Render image to back buffer
        for (int y = 0; y < 64; y++) {
            for (int x = 0; x < 64; x++) {
                FxU16* pixel = (FxU16*)(((char*)info.lfbPtr) + (y + startY) * info.strideInBytes + (x + startX) * 2);
                *pixel = colorBuf[y][x];
            }
        }
        grLfbUnlock(GR_LFB_WRITE_ONLY, GR_BUFFER_BACKBUFFER);

        // Lock back buffer for reading (to verify the write and avoid frame swap complexity in visual capture)
        if (grLfbLock(GR_LFB_READ_ONLY, GR_BUFFER_BACKBUFFER, GR_LFBWRITEMODE_ANY, GR_ORIGIN_UPPER_LEFT, FXFALSE, &info) == FXFALSE) {
            tlConOutput("Error, failed to take read lock\n");
            return false;
        }

        // Grab the source image out of the back buffer
        std::memset(grabBuf, 0, sizeof(grabBuf));
        for (int y = 0; y < 64; y++) {
            for (int x = 0; x < 64; x++) {
                FxU16* pixel = (FxU16*)(((char*)info.lfbPtr) + (y + startY) * info.strideInBytes + (x + startX) * 2);
                grabBuf[y][x] = *pixel;
            }
        }
        grLfbUnlock(GR_LFB_READ_ONLY, GR_BUFFER_BACKBUFFER);

        tlConClear();
        // Compare the source image to the readback image
        if (std::memcmp(colorBuf, grabBuf, sizeof(colorBuf)) != 0) {
            tlConOutput("Failed readback test\n");
        } else {
            tlConOutput("Passed readback test\n");
        }

        tlConRender();
        return true;
    }

    void Shutdown() override {
        grGlideShutdown();
    }
};

std::unique_ptr<BaseTest> CreateTest11() {
    return std::make_unique<Test11>();
}

} // namespace GlideTest
