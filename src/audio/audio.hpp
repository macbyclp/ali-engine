#pragma once
#include <glm/glm.hpp>
#include <cstdint>
#include <memory>
#include <string>

namespace eng {

// Thin wrapper over miniaudio's high-level engine: fire-and-forget or handled
// sounds, 3D positioning, one listener. No-ops gracefully if no audio device.
class AudioEngine {
public:
    AudioEngine();
    ~AudioEngine();
    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    bool ok() const;

    struct PlayOpts {
        float volume = 1.0f;
        bool loop = false;
        bool spatial = false;
        bool stream = false;      // decode-on-the-fly -- use for music, not SFX
        float pitch = 1.0f;
        float fade_in_ms = 0.0f;
        glm::vec3 pos{0.0f};
        std::string bus;
    };
    // Returns a handle (0 on failure).
    uint32_t play(const std::string& file, const PlayOpts& opts);
    // Legacy shorthand.
    uint32_t play(const std::string& file, float volume, bool loop, bool spatial,
                  const glm::vec3& pos, const std::string& bus = {});
    void set_position(uint32_t handle, const glm::vec3& pos);
    void set_volume(uint32_t handle, float volume);
    void set_pitch(uint32_t handle, float pitch);
    void stop(uint32_t handle, float fade_out_ms = 0.0f);
    void set_listener(const glm::vec3& pos, const glm::vec3& forward);

    // Mixer buses. "master" is the final output; other names are sub-groups.
    void set_bus_volume(const std::string& bus, float volume);
    float bus_volume(const std::string& bus) const;
    void stop_bus(const std::string& bus);   // stop every sound on that bus

private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};

} // namespace eng
