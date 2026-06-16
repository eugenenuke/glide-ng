#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace GlideTest {

class BaseTest {
public:
    virtual ~BaseTest() = default;

    // Unique name of the test (e.g. "test00")
    virtual std::string GetName() const = 0;
    
    // Setup resolutions, initialize data
    virtual void Initialize() = 0;
    
    // Configure the specific sub-mode/input configuration (for tests that cycle through features)
    virtual void SetMode(int modeIndex) { (void)modeIndex; }
    
    // Returns the total number of interactive/input modes to test
    virtual int GetTotalModes() const { return 1; }
    
    // Render a single frame. Returns false if the test should stop.
    virtual bool RenderFrame(int frameIndex) = 0;
    
    // Shutdown Glide and release resources
    virtual void Shutdown() = 0;
    
    // Frame pacing controls
    virtual int GetMaxFrames() const { return 15; } // Let Xvfb/window map completely
    virtual int GetCaptureFrameIndex() const { return 14; } // Frame to dump
};

} // namespace GlideTest
