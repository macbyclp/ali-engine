#include "audio/audio.hpp"
#include "core/log.hpp"
#include <unordered_map>

#define MA_NO_ENCODING
#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

namespace eng {

struct AudioEngine::Impl {
    ma_engine engine{};
    bool ready = false;
    uint32_t next = 1;
    std::unordered_map<uint32_t, ma_sound*> sounds;
};

AudioEngine::AudioEngine() : p_(std::make_unique<Impl>()) {
    if (ma_engine_init(nullptr, &p_->engine) == MA_SUCCESS) {
        p_->ready = true;
        log::info("audio: engine ready");
    } else {
        log::warn("audio: no device, sound disabled");
    }
}

AudioEngine::~AudioEngine() {
    for (auto& [h, s] : p_->sounds) { ma_sound_uninit(s); delete s; }
    if (p_->ready) ma_engine_uninit(&p_->engine);
}

bool AudioEngine::ok() const { return p_->ready; }

uint32_t AudioEngine::play(const std::string& file, float volume, bool loop, bool spatial,
                           const glm::vec3& pos) {
    if (!p_->ready) return 0;
    auto* s = new ma_sound();
    ma_uint32 flags = MA_SOUND_FLAG_DECODE;
    if (ma_sound_init_from_file(&p_->engine, file.c_str(), flags, nullptr, nullptr, s) != MA_SUCCESS) {
        log::error("audio: cannot load %s", file.c_str());
        delete s;
        return 0;
    }
    ma_sound_set_volume(s, volume);
    ma_sound_set_looping(s, loop ? MA_TRUE : MA_FALSE);
    ma_sound_set_spatialization_enabled(s, spatial ? MA_TRUE : MA_FALSE);
    if (spatial) ma_sound_set_position(s, pos.x, pos.y, pos.z);
    ma_sound_start(s);
    uint32_t h = p_->next++;
    p_->sounds[h] = s;
    return h;
}

void AudioEngine::set_position(uint32_t h, const glm::vec3& pos) {
    auto it = p_->sounds.find(h);
    if (it != p_->sounds.end()) ma_sound_set_position(it->second, pos.x, pos.y, pos.z);
}
void AudioEngine::set_volume(uint32_t h, float v) {
    auto it = p_->sounds.find(h);
    if (it != p_->sounds.end()) ma_sound_set_volume(it->second, v);
}
void AudioEngine::stop(uint32_t h) {
    auto it = p_->sounds.find(h);
    if (it == p_->sounds.end()) return;
    ma_sound_stop(it->second);
    ma_sound_uninit(it->second);
    delete it->second;
    p_->sounds.erase(it);
}
void AudioEngine::set_listener(const glm::vec3& pos, const glm::vec3& fwd) {
    if (!p_->ready) return;
    ma_engine_listener_set_position(&p_->engine, 0, pos.x, pos.y, pos.z);
    ma_engine_listener_set_direction(&p_->engine, 0, fwd.x, fwd.y, fwd.z);
}

} // namespace eng
