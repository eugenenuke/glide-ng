#include "../framework/TestRunner.h"
#include <glide.h>
#include "tlib.h"
#include <memory>
#include <cassert>
#include <cstring>
#include <cstdlib>
#include <cstdio>

namespace GlideTest {

static const GrLfbSrcFmt_t s_validFormats[] = {
    GR_LFB_SRC_FMT_565,
    GR_LFB_SRC_FMT_555,
    GR_LFB_SRC_FMT_1555,
    GR_LFB_SRC_FMT_888,
    GR_LFB_SRC_FMT_8888,
    GR_LFB_SRC_FMT_565_DEPTH,
    GR_LFB_SRC_FMT_555_DEPTH,
    GR_LFB_SRC_FMT_1555_DEPTH,
    GR_LFB_SRC_FMT_ZA16
};

static const char *s_sourceFormatString[] = {
  "GR_LFB_SRC_FMT_565       ", // 0
  "GR_LFB_SRC_FMT_555       ", // 1
  "GR_LFB_SRC_FMT_1555      ", // 2
  "INVALID FORMAT           ", // 3
  "GR_LFB_SRC_FMT_888       ", // 4
  "GR_LFB_SRC_FMT_8888      ", // 5
  "INVALID FORMAT           ", // 6
  "INVALID FORMAT           ", // 7
  "INVALID FORMAT           ", // 8
  "INVALID FORMAT           ", // 9
  "INVALID FORMAT           ", // 10
  "INVALID FORMAT           ", // 11
  "GR_LFB_SRC_FMT_565_DEPTH ", // 12
  "GR_LFB_SRC_FMT_555_DEPTH ", // 13
  "GR_LFB_SRC_FMT_1555_DEPTH", // 14
  "GR_LFB_SRC_FMT_ZA16      "  // 15
};

class Test26 : public BaseTest {
private:
    float scrWidth = 640.0f;
    float scrHeight = 480.0f;
    GrHwConfiguration hwconfig;

    TlTexture texture;
    void* image{nullptr};
    void* readImage{nullptr};
    FxU32 bpp{0};
    GrLfbSrcFmt_t sourceFormat{GR_LFB_SRC_FMT_565};
    uint32_t imageWidth{256};
    uint32_t imageHeight{256};

    int x{0};
    int y{0};

    static void imageConvert(void* dst, void* src, GrLfbSrcFmt_t format, FxU32* bpp) {
        FxU32 x, y;
        FxU32* longData = (FxU32*)dst;
        FxU16* shortData = (FxU16*)dst;
        FxU16* srcData = (FxU16*)src;
        FxU32 longStride = 256;
        FxU32 shortStride = 256;
        FxU32 longColor;
        FxU16 shortColor;

        switch (format) {
        case GR_LFB_SRC_FMT_565:
        case GR_LFB_SRC_FMT_555:
        case GR_LFB_SRC_FMT_1555:
            *bpp = 2;
            break;
        case GR_LFB_SRC_FMT_888:
        case GR_LFB_SRC_FMT_8888:
        case GR_LFB_SRC_FMT_565_DEPTH:
        case GR_LFB_SRC_FMT_1555_DEPTH:
        case GR_LFB_SRC_FMT_555_DEPTH:
            *bpp = 4;
            break;
        default:
            *bpp = 2;
            break;
        }

        for (y = 0; y < 256; y++) {
            for (x = 0; x < 256; x++) {
                switch (format) {
                case GR_LFB_SRC_FMT_565:
                    shortData[y * shortStride + x] = srcData[y * 256 + x];
                    break;

                case GR_LFB_SRC_FMT_555:
                case GR_LFB_SRC_FMT_1555:
                    shortColor = srcData[y * 256 + x];
                    shortColor = ((0x8000) | // Alpha == 1
                                  ((shortColor >> 1) & 0x7C00) |
                                  ((shortColor >> 1) & 0x03E0) |
                                  ((shortColor)&0x1f));
                    shortData[y * shortStride + x] = shortColor;
                    break;

                case GR_LFB_SRC_FMT_888:
                case GR_LFB_SRC_FMT_8888:
                    longColor = srcData[y * 256 + x];
                    longColor = ((0xFF000000) |
                                 ((longColor << 8) & 0x00F80000) |
                                 ((longColor << 5) & 0x0000FC00) |
                                 ((longColor << 3) & 0x000000F8));
                    longData[y * longStride + x] = longColor;
                    break;

                case GR_LFB_SRC_FMT_565_DEPTH:
                    longColor = srcData[y * 256 + x];
                    longData[y * longStride + x] = longColor;
                    break;

                case GR_LFB_SRC_FMT_1555_DEPTH:
                case GR_LFB_SRC_FMT_555_DEPTH:
                    longColor = srcData[y * 256 + x];
                    longColor = ((0x00008000) |
                                 ((longColor >> 1) & 0x00007C00) |
                                 ((longColor >> 1) & 0x000003E0) |
                                 ((longColor) & 0x0000001f));
                    longData[y * longStride + x] = longColor;
                    break;

                default:
                    break;
                }
            }
        }
    }

public:
    std::string GetName() const override { return "test26"; }

    int GetTotalModes() const override { return 9; }

    void SetMode(int modeIndex) override {
        if (modeIndex >= 0 && modeIndex < 9) {
            sourceFormat = s_validFormats[modeIndex];
            if (image) {
                std::memset(image, 0, imageWidth * imageHeight * sizeof(FxU32));
                imageConvert(image, texture.info.data, sourceFormat, &bpp);
            }
        }
    }

    void Initialize() override {
        tlSetScreen(scrWidth, scrHeight);
        grGlideInit();
        assert(grSstQueryHardware(&hwconfig));
        grSstSelect(0);
        assert(grSstWinOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz,
                           GR_COLORFORMAT_ABGR, GR_ORIGIN_UPPER_LEFT, 2, 1));

        tlConSet(0.0f, 0.0f, 1.0f, 0.5f, 60, 15, 0xffffff);
        grDitherMode(GR_DITHER_DISABLE);

        // Load texture from disk
        assert(tlLoadTexture("ext_tests/build_tests/decal1.3df",
                             &texture.info,
                             &texture.tableType,
                             &texture.tableData));

        imageWidth = 256;
        imageHeight = 256;
        image = std::calloc(imageWidth * imageHeight, sizeof(FxU32));
        readImage = std::calloc(imageWidth * imageHeight, sizeof(FxU32));

        sourceFormat = GR_LFB_SRC_FMT_565;
        imageConvert(image, texture.info.data, sourceFormat, &bpp);

        x = 0;
        y = 0;
        tlConOutput("Width - %d\n", imageWidth);
    }

    bool RenderFrame(int frameIndex) override {
        (void)frameIndex;
        grBufferClear(0x00303030, 0, 0);

        if ((imageWidth < (FxU32)tlScaleX(1.0f)) &&
            (imageHeight < (FxU32)tlScaleY(1.0f)) &&
            grLfbWriteRegion(GR_BUFFER_BACKBUFFER,
                             x, y, sourceFormat,
                             imageWidth, imageHeight, 256 * bpp, image)) {
            tlConClear();
            tlConOutput("Width - %d\n", imageWidth);
            tlConOutput("LFB Source Format: %s\n", s_sourceFormatString[sourceFormat]);

            if (sourceFormat == GR_LFB_SRC_FMT_565) {
                std::memcpy(readImage, image, 256 * 256 * sizeof(FxU16));
                grLfbReadRegion(GR_BUFFER_BACKBUFFER,
                                 x, y, imageWidth, imageHeight,
                                 256 * 2, readImage);
                if (std::memcmp(image, readImage, 256 * 256 * 2) != 0) {
                    tlConOutput("Failed Readback Test\n");
                } else {
                    tlConOutput("Passed Readback Test\n");
                }
            }
        } else {
            tlConClear();
            tlConOutput("grLfbWriteRegion Failed\n");
        }

        // Animate coordinate x, y deterministically
        x++;
        if (x + 256 >= (int)tlScaleX(1.0f)) {
            x = 0;
            y++;
        }
        if (y + 256 >= (int)tlScaleY(1.0f)) {
            y = 0;
        }

        tlConRender();
        return true;
    }

    void Shutdown() override {
        if (image) {
            std::free(image);
            image = nullptr;
        }
        if (readImage) {
            std::free(readImage);
            readImage = nullptr;
        }
        grGlideShutdown();
    }
};

std::unique_ptr<BaseTest> CreateTest26() {
    return std::make_unique<Test26>();
}

} // namespace GlideTest
