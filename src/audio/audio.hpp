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

    // Returns a handle (0 on failure). spatial=false plays as UI/2D sound.
    uint32_t play(const std::string& file, float volume, bool loop, bool spatial,
                  const glm::vec3& pos);
    void set_position(uint32_t handle, const glm::vec3& pos);
    void set_volume(uint32_t handle, float volume);
    void stop(uint32_t handle);
    void set_listener(const glm::vec3& pos, const glm::vec3& forward);

private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};

} // namespace eng
