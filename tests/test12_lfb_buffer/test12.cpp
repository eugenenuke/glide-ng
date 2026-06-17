#include "../framework/TestRunner.h"
#include <glide.h>
#include "tlib.h"
#include <memory>
#include <cassert>
#include <cstring>
#include <cstdlib>
#include <cstdio>

namespace GlideTest {

static const char *s_originString[] = {
  "GR_ORIGIN_UPPER_LEFT ",
  "GR_ORIGIN_LOWER_LEFT " // Corrected from legacy SDK typo "LOWER_RIGHT"
};

static const char *s_renderBufferString[] = {
  "GR_BUFFER_FRONTBUFFER",
  "GR_BUFFER_BACKBUFFER "
};

static const char *s_writeModeString[] = {
  "GR_LFBWRITEMODE_565       ",
  "GR_LFBWRITEMODE_555       ",
  "GR_LFBWRITEMODE_1555      ",
  "GR_LFBWRITEMODE_RESERVED1 ",
  "GR_LFBWRITEMODE_888       ",
  "GR_LFBWRITEMODE_8888      ",
  "GR_LFBWRITEMODE_RESERVED2 ",
  "GR_LFBWRITEMODE_RESERVED3 ",
  "GR_LFBWRITEMODE_RESERVED4 ",
  "GR_LFBWRITEMODE_RESERVED5 ",
  "GR_LFBWRITEMODE_RESERVED6 ",
  "GR_LFBWRITEMODE_RESERVED7 ",
  "GR_LFBWRITEMODE_565_DEPTH ",
  "GR_LFBWRITEMODE_555_DEPTH ",
  "GR_LFBWRITEMODE_1555_DEPTH",
  "GR_LFBWRITEMODE_ZA16      "
};

static const char *s_pixPipeString[] = {
  "PIXELPIPE DISABLED",
  "PIXELPIPE ENABLED "
};

class Test12 : public BaseTest {
private:
    float scrWidth = 640.0f;
    float scrHeight = 480.0f;
    GrHwConfiguration hwconfig;

    TlTexture texture;
    GrOriginLocation_t lfbOrigin{GR_ORIGIN_UPPER_LEFT};
    GrOriginLocation_t sstOrigin{GR_ORIGIN_UPPER_LEFT};
    GrBuffer_t lfbBuffer{GR_BUFFER_BACKBUFFER};
    GrBuffer_t sstBuffer{GR_BUFFER_BACKBUFFER};
    GrLfbWriteMode_t writeMode{GR_LFBWRITEMODE_565};
    FxBool pixPipe{FXFALSE};

    static const char* s_consoleTemplate;

public:
    std::string GetName() const override { return "test12"; }

    int GetTotalModes() const override { return 8; }

    void SetMode(int modeIndex) override {
        switch (modeIndex) {
        case 0:
            lfbBuffer = GR_BUFFER_BACKBUFFER;
            sstBuffer = GR_BUFFER_BACKBUFFER;
            lfbOrigin = GR_ORIGIN_UPPER_LEFT;
            sstOrigin = GR_ORIGIN_UPPER_LEFT;
            break;
        case 1:
            lfbBuffer = GR_BUFFER_BACKBUFFER;
            sstBuffer = GR_BUFFER_BACKBUFFER;
            lfbOrigin = GR_ORIGIN_LOWER_LEFT;
            sstOrigin = GR_ORIGIN_UPPER_LEFT;
            break;
        case 2:
            lfbBuffer = GR_BUFFER_BACKBUFFER;
            sstBuffer = GR_BUFFER_BACKBUFFER;
            lfbOrigin = GR_ORIGIN_UPPER_LEFT;
            sstOrigin = GR_ORIGIN_LOWER_LEFT;
            break;
        case 3:
            lfbBuffer = GR_BUFFER_BACKBUFFER;
            sstBuffer = GR_BUFFER_BACKBUFFER;
            lfbOrigin = GR_ORIGIN_LOWER_LEFT;
            sstOrigin = GR_ORIGIN_LOWER_LEFT;
            break;
        case 4:
            lfbBuffer = GR_BUFFER_FRONTBUFFER;
            sstBuffer = GR_BUFFER_BACKBUFFER;
            lfbOrigin = GR_ORIGIN_UPPER_LEFT;
            sstOrigin = GR_ORIGIN_UPPER_LEFT;
            break;
        case 5:
            lfbBuffer = GR_BUFFER_FRONTBUFFER;
            sstBuffer = GR_BUFFER_BACKBUFFER;
            lfbOrigin = GR_ORIGIN_LOWER_LEFT;
            sstOrigin = GR_ORIGIN_UPPER_LEFT;
            break;
        case 6:
            lfbBuffer = GR_BUFFER_BACKBUFFER;
            sstBuffer = GR_BUFFER_FRONTBUFFER;
            lfbOrigin = GR_ORIGIN_UPPER_LEFT;
            sstOrigin = GR_ORIGIN_UPPER_LEFT;
            break;
        case 7:
            lfbBuffer = GR_BUFFER_BACKBUFFER;
            sstBuffer = GR_BUFFER_FRONTBUFFER;
            lfbOrigin = GR_ORIGIN_LOWER_LEFT;
            sstOrigin = GR_ORIGIN_UPPER_LEFT;
            break;
        default:
            break;
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

        // Load texture from disk
        assert(tlLoadTexture("ext_tests/build_tests/decal1.3df",
                             &texture.info,
                             &texture.tableType,
                             &texture.tableData));
    }

    std::vector<int> GetCaptureFrames() const override {
        return { 13, 14 }; // Verify both front buffer and back buffer presentations
    }

    bool RenderFrame(int frameIndex) override {
        if (frameIndex % 2 == 0) { // Even frame: Clear, Draw LFB, Draw Back Buffer Console
            grRenderBuffer(GR_BUFFER_BACKBUFFER);
            grBufferClear(0, 0, 0);
            grRenderBuffer(GR_BUFFER_FRONTBUFFER);
            grBufferClear(0, 0, 0);

            // Set Glide State
            grRenderBuffer(sstBuffer);
            grSstOrigin(sstOrigin);

            // Attempt Lock and Write
            GrLfbInfo_t info;
            info.size = sizeof(info);
            if (grLfbLock(GR_LFB_WRITE_ONLY, 
                           lfbBuffer,
                           writeMode,
                           lfbOrigin,
                           pixPipe,
                           &info)) {
                FxU16* shortData = (FxU16*)info.lfbPtr;
                FxU16* srcData = (FxU16*)texture.info.data;
                FxU32 shortStride = info.strideInBytes >> 1;

                for (int y = 0; y < 256; y++) {
                    for (int x = 0; x < 256; x++) {
                        shortData[y * shortStride + x] = srcData[y * 256 + x];
                    }
                }
                grLfbUnlock(GR_LFB_WRITE_ONLY, lfbBuffer);
            }

            // Draw Console for BACKBUFFER
            grRenderBuffer(GR_BUFFER_BACKBUFFER);
            tlConClear();
            tlConOutput(s_consoleTemplate,
                        s_renderBufferString[1], // BACKBUFFER
                        s_originString[lfbOrigin],
                        s_originString[sstOrigin],
                        s_renderBufferString[lfbBuffer],
                        s_renderBufferString[sstBuffer],
                        s_pixPipeString[pixPipe],
                        s_writeModeString[writeMode]);
            tlConRender();
        } 
        else { // Odd frame: Only Draw Front Buffer Console (preserves swapped LFB buffer)
            grRenderBuffer(GR_BUFFER_BACKBUFFER); // Render overlay on the new back buffer
            tlConClear();
            tlConOutput(s_consoleTemplate,
                        s_renderBufferString[0], // FRONTBUFFER
                        s_originString[lfbOrigin],
                        s_originString[sstOrigin],
                        s_renderBufferString[lfbBuffer],
                        s_renderBufferString[sstBuffer],
                        s_pixPipeString[pixPipe],
                        s_writeModeString[writeMode]);
            tlConRender();
        }

        return true;
    }

    void Shutdown() override {
        grGlideShutdown();
    }
};

const char* Test12::s_consoleTemplate = {
  "Current Buffer: %s\n"
    "1 - lock yOrigin        (%s)\n"
      "2 - glide yOrigin       (%s)\n"
        "3 - lfb render buffer   (%s)\n"
          "4 - glide render buffer (%s)\n"
            "5 - pixpipe enable      (%s)\n"
              "6 - lfb write mode      (%s)\n"
                "Press any other key to quit\n"
};

std::unique_ptr<BaseTest> CreateTest12() {
    return std::make_unique<Test12>();
}

} // namespace GlideTest
