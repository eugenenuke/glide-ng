#include <catch2/catch_test_macros.hpp>
#include <SDL.h>
#include <chrono>
#include <thread>
#include <vector>
#include <mutex>
#include <cstring>
#include <iostream>

#include "core/BackendManager.h"
#include "core/WrapperConfig.h"
#include "core/JsonConfigLoader.h"
#include "backends/SoftwareBackendBase.h"

namespace {
// RAII Helper to initialize and clean up SDL event subsystem for headless testing
struct SdlInitHelper {
  SdlInitHelper() {
    if (SDL_Init(SDL_INIT_EVENTS) < 0) {
      std::cerr << "Failed to initialize SDL events: " << SDL_GetError() << std::endl;
    }
  }
  ~SdlInitHelper() {
    SDL_Quit();
  }
};

// RAII Helper to ensure clean backend shutdown even on assertion failures
struct BackendLifetimeHelper {
  ~BackendLifetimeHelper() {
    auto& manager = GlideWrapper::BackendManager::GetInstance();
    if (manager.GetBackend()) {
      manager.GetBackend()->DetachWindow();
    }
    manager.ShutdownBackend();
    std::cout << "[DEBUG-TEST] BackendLifetimeHelper: Backend shut down cleanly." << std::endl;
  }
};

void PrintPolledEvent(const SDL_Event& ev, const char* context) {
  std::cout << "[DEBUG-TEST] [" << context << "] Polled event: type=0x" << std::hex << ev.type;
  if (ev.type == SDL_KEYDOWN || ev.type == SDL_KEYUP) {
    std::cout << " (KEY), sym=" << std::dec << ev.key.keysym.sym 
              << ", state=" << (int)ev.key.state;
  }
  std::cout << std::dec << std::endl;
}
} // namespace

TEST_CASE("Keyboard Event Handling Modes & Thread Safety", "[Keyboard]") {
  SdlInitHelper sdlInit;

  auto& reg = GlideWrapper::EmulationRegistry::GetInstance();
  auto& manager = GlideWrapper::BackendManager::GetInstance();

  SECTION("Configuration Mode Parsing") {
    // 1. Test default mode is Bypass
    GlideWrapper::WrapperConfig cfgDefault;
    REQUIRE(cfgDefault.keyHandlingMode == GlideWrapper::KeyHandlingMode::Bypass);

    // 2. Test environment variable override
    ::setenv("GLIDE_WRAPPER_KEY_HANDLING", "debounced", 1);
    GlideWrapper::JsonConfigLoader loader;
    GlideWrapper::WrapperConfig cfgEnv;
    loader.Load("non_existent_config.json", cfgEnv);
    REQUIRE(cfgEnv.keyHandlingMode == GlideWrapper::KeyHandlingMode::Debounced);

    ::setenv("GLIDE_WRAPPER_KEY_HANDLING", "grab_only", 1);
    loader.Load("non_existent_config.json", cfgEnv);
    REQUIRE(cfgEnv.keyHandlingMode == GlideWrapper::KeyHandlingMode::GrabOnly);

    ::setenv("GLIDE_WRAPPER_KEY_HANDLING", "bypass", 1);
    loader.Load("non_existent_config.json", cfgEnv);
    REQUIRE(cfgEnv.keyHandlingMode == GlideWrapper::KeyHandlingMode::Bypass);

    ::unsetenv("GLIDE_WRAPPER_KEY_HANDLING");
  }

  SECTION("Bypass Mode Event Flow") {
    BackendLifetimeHelper lifetime;

    reg.GetConfig().backend = "software";
    reg.GetConfig().keyHandlingMode = GlideWrapper::KeyHandlingMode::Bypass;
    reg.GetConfig().forceNoWindow = true;
    manager.EstablishBackend(reg.GetConfig());
    auto* backend = manager.GetBackend();
    REQUIRE(backend != nullptr);

    backend->AttachWindow(nullptr, 800, 600, false);

    // Drain event queue first
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
      PrintPolledEvent(ev, "Bypass-Drain");
    }

    // 1. Push physical KEYDOWN
    SDL_Event keydown;
    std::memset(&keydown, 0, sizeof(keydown));
    keydown.type = SDL_KEYDOWN;
    keydown.key.keysym.sym = SDLK_RIGHT;
    keydown.key.state = SDL_PRESSED;
    keydown.key.repeat = 0;
    std::cout << "[DEBUG-TEST] Pushing KEYDOWN for SDLK_RIGHT" << std::endl;
    REQUIRE(SDL_PushEvent(&keydown) == 1);

    // 2. Push physical KEYUP immediately (simulating a rapid hardware queue input)
    SDL_Event keyup;
    std::memset(&keyup, 0, sizeof(keyup));
    keyup.type = SDL_KEYUP;
    keyup.key.keysym.sym = SDLK_RIGHT;
    keyup.key.state = SDL_RELEASED;
    keyup.key.repeat = 0;
    std::cout << "[DEBUG-TEST] Pushing KEYUP for SDLK_RIGHT" << std::endl;
    REQUIRE(SDL_PushEvent(&keyup) == 1);

    // 3. Poll first event: Must be KEYDOWN
    REQUIRE(SDL_PollEvent(&ev) == 1);
    PrintPolledEvent(ev, "Bypass-KeyDown-Poll");
    REQUIRE(ev.type == SDL_KEYDOWN);
    REQUIRE(ev.key.keysym.sym == SDLK_RIGHT);

    // 4. Poll second event: Must be KEYUP immediately (no debounce delay in Bypass mode)
    int pollResult = SDL_PollEvent(&ev);
    std::cout << "[DEBUG-TEST] Poll result for KEYUP: " << pollResult << std::endl;
    if (pollResult == 1) {
      PrintPolledEvent(ev, "Bypass-KeyUp-Poll");
    }
    REQUIRE(pollResult == 1);
    REQUIRE(ev.type == SDL_KEYUP);
    REQUIRE(ev.key.keysym.sym == SDLK_RIGHT);
  }

  SECTION("Safe Debouncer Mode: Basic Press and Debounced Release") {
    BackendLifetimeHelper lifetime;

    reg.GetConfig().backend = "software";
    reg.GetConfig().keyHandlingMode = GlideWrapper::KeyHandlingMode::Debounced;
    reg.GetConfig().forceNoWindow = true;
    manager.EstablishBackend(reg.GetConfig());
    auto* backend = manager.GetBackend();
    REQUIRE(backend != nullptr);

    backend->AttachWindow(nullptr, 800, 600, false);

    // Drain event queue
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
      PrintPolledEvent(ev, "Debounce-Drain");
    }

    // 1. Push physical KEYDOWN (returns 1 because it's first press)
    SDL_Event keydown;
    std::memset(&keydown, 0, sizeof(keydown));
    keydown.type = SDL_KEYDOWN;
    keydown.key.keysym.sym = SDLK_RIGHT;
    keydown.key.state = SDL_PRESSED;
    keydown.key.repeat = 0;
    std::cout << "[DEBUG-TEST] Pushing KEYDOWN for SDLK_RIGHT" << std::endl;
    REQUIRE(SDL_PushEvent(&keydown) == 1);

    // Poll: Must receive KEYDOWN immediately
    REQUIRE(SDL_PollEvent(&ev) == 1);
    PrintPolledEvent(ev, "Debounce-KeyDown-Poll");
    REQUIRE(ev.type == SDL_KEYDOWN);
    REQUIRE(ev.key.keysym.sym == SDLK_RIGHT);
    REQUIRE(SDL_PollEvent(&ev) == 0); // No other events

    // 2. Push physical KEYUP (returns 0 because it's debounced and dropped from the queue by the filter!)
    SDL_Event keyup;
    std::memset(&keyup, 0, sizeof(keyup));
    keyup.type = SDL_KEYUP;
    keyup.key.keysym.sym = SDLK_RIGHT;
    keyup.key.state = SDL_RELEASED;
    keyup.key.repeat = 0;
    std::cout << "[DEBUG-TEST] Pushing KEYUP for SDLK_RIGHT (Expected to be debounced/dropped)" << std::endl;
    REQUIRE(SDL_PushEvent(&keyup) == 0); // Corrected: Expect 0 since the filter returns 0!

    // Poll: Must NOT receive KEYUP immediately because of debouncing!
    int pollResult = SDL_PollEvent(&ev);
    std::cout << "[DEBUG-TEST] Poll result for immediate KEYUP: " << pollResult << std::endl;
    if (pollResult == 1) {
      PrintPolledEvent(ev, "Debounce-ImmediateKeyUp-Poll");
    }
    REQUIRE(pollResult == 0);

    // 3. Wait 25ms (greater than 20ms debounce window) and process releases
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
    backend->SwapBuffers(); // Calls ProcessPendingKeyReleases

    // Poll: Now we must receive the debounced KEYUP!
    REQUIRE(SDL_PollEvent(&ev) == 1);
    PrintPolledEvent(ev, "Debounce-DelayedKeyUp-Poll");
    REQUIRE(ev.type == SDL_KEYUP);
    REQUIRE(ev.key.keysym.sym == SDLK_RIGHT);
  }

  SECTION("Safe Debouncer Mode: Rapid Tapping / Re-press Suppression Fix") {
    BackendLifetimeHelper lifetime;

    reg.GetConfig().backend = "software";
    reg.GetConfig().keyHandlingMode = GlideWrapper::KeyHandlingMode::Debounced;
    reg.GetConfig().forceNoWindow = true;
    manager.EstablishBackend(reg.GetConfig());
    auto* backend = manager.GetBackend();
    REQUIRE(backend != nullptr);

    backend->AttachWindow(nullptr, 800, 600, false);

    // Drain event queue
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {}

    // 1. Press Key A
    SDL_Event keydown1;
    std::memset(&keydown1, 0, sizeof(keydown1));
    keydown1.type = SDL_KEYDOWN;
    keydown1.key.keysym.sym = SDLK_LEFT;
    keydown1.key.state = SDL_PRESSED;
    REQUIRE(SDL_PushEvent(&keydown1) == 1);

    // 2. Release Key A (schedules debounce release, returns 0)
    SDL_Event keyup1;
    std::memset(&keyup1, 0, sizeof(keyup1));
    keyup1.type = SDL_KEYUP;
    keyup1.key.keysym.sym = SDLK_LEFT;
    keyup1.key.state = SDL_RELEASED;
    REQUIRE(SDL_PushEvent(&keyup1) == 0); // Corrected: Expect 0 since the filter returns 0!

    // 3. IMMEDIATELY (no delay) press Key A again (rapid tap!)
    // Returns 1, and nested-injects the pending KEYUP!
    SDL_Event keydown2;
    std::memset(&keydown2, 0, sizeof(keydown2));
    keydown2.type = SDL_KEYDOWN;
    keydown2.key.keysym.sym = SDLK_LEFT;
    keydown2.key.state = SDL_PRESSED;
    REQUIRE(SDL_PushEvent(&keydown2) == 1);

    // Poll events: We MUST receive:
    // - KEYDOWN (first press)
    // - KEYUP (force-dispatched because of the rapid re-press!)
    // - KEYDOWN (second press)
    // In that exact order!
    std::vector<SDL_Event> events;
    while (SDL_PollEvent(&ev)) {
      PrintPolledEvent(ev, "RapidTap-Poll");
      events.push_back(ev);
    }

    REQUIRE(events.size() == 3);
    REQUIRE(events[0].type == SDL_KEYDOWN);
    REQUIRE(events[0].key.keysym.sym == SDLK_LEFT);
    REQUIRE(events[1].type == SDL_KEYUP);
    REQUIRE(events[1].key.keysym.sym == SDLK_LEFT);
    REQUIRE(events[2].type == SDL_KEYDOWN);
    REQUIRE(events[2].key.keysym.sym == SDLK_LEFT);
  }

  SECTION("Safe Debouncer Mode: Concurrent Keypresses") {
    BackendLifetimeHelper lifetime;

    reg.GetConfig().backend = "software";
    reg.GetConfig().keyHandlingMode = GlideWrapper::KeyHandlingMode::Debounced;
    reg.GetConfig().forceNoWindow = true;
    manager.EstablishBackend(reg.GetConfig());
    auto* backend = manager.GetBackend();
    REQUIRE(backend != nullptr);

    backend->AttachWindow(nullptr, 800, 600, false);

    // Drain event queue
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {}

    // Simulating simultaneous hold of UP (accelerate) and RIGHT (steer)
    SDL_Event keyUpPress;
    std::memset(&keyUpPress, 0, sizeof(keyUpPress));
    keyUpPress.type = SDL_KEYDOWN;
    keyUpPress.key.keysym.sym = SDLK_UP;
    keyUpPress.key.state = SDL_PRESSED;

    SDL_Event keyRightPress;
    std::memset(&keyRightPress, 0, sizeof(keyRightPress));
    keyRightPress.type = SDL_KEYDOWN;
    keyRightPress.key.keysym.sym = SDLK_RIGHT;
    keyRightPress.key.state = SDL_PRESSED;

    REQUIRE(SDL_PushEvent(&keyUpPress) == 1);
    REQUIRE(SDL_PushEvent(&keyRightPress) == 1);

    // Both must be received immediately
    std::vector<SDL_Event> events;
    while (SDL_PollEvent(&ev)) {
      events.push_back(ev);
    }
    REQUIRE(events.size() == 2);
    REQUIRE(events[0].type == SDL_KEYDOWN);
    REQUIRE(events[1].type == SDL_KEYDOWN);

    // Now release both simultaneously (schedules debounces)
    SDL_Event keyUpRelease;
    std::memset(&keyUpRelease, 0, sizeof(keyUpRelease));
    keyUpRelease.type = SDL_KEYUP;
    keyUpRelease.key.keysym.sym = SDLK_UP;
    keyUpRelease.key.state = SDL_RELEASED;

    SDL_Event keyRightRelease;
    std::memset(&keyRightRelease, 0, sizeof(keyRightRelease));
    keyRightRelease.type = SDL_KEYUP;
    keyRightRelease.key.keysym.sym = SDLK_RIGHT;
    keyRightRelease.key.state = SDL_RELEASED;

    REQUIRE(SDL_PushEvent(&keyUpRelease) == 0); // Corrected: Expect 0 since the filter returns 0!
    REQUIRE(SDL_PushEvent(&keyRightRelease) == 0); // Corrected: Expect 0 since the filter returns 0!

    // None should appear immediately
    REQUIRE(SDL_PollEvent(&ev) == 0);

    // Wait 25ms and SwapBuffers to dispatch
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
    backend->SwapBuffers();

    // Both should be in the queue now!
    events.clear();
    while (SDL_PollEvent(&ev)) {
      events.push_back(ev);
    }
    REQUIRE(events.size() == 2);
    REQUIRE(events[0].type == SDL_KEYUP);
    REQUIRE(events[1].type == SDL_KEYUP);
  }
}
