#include "audio/audio.hpp"
#include "core/log.hpp"
#include <string>
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
    std::unordered_map<uint32_t, std::string> sound_bus;
    std::unordered_map<std::string, ma_sound_group*> buses;
    std::unordered_map<std::string, float> bus_gain;

    ma_sound_group* bus(const std::string& name) {
        if (name.empty() || name == "master") return nullptr;
        auto it = buses.find(name);
        if (it != buses.end()) return it->second;
        auto* g = new ma_sound_group();
        if (ma_sound_group_init(&engine, 0, nullptr, g) != MA_SUCCESS) {
            delete g;
            buses[name] = nullptr;
            return nullptr;
        }
        buses[name] = g;
        bus_gain[name] = 1.0f;
        return g;
    }
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
    for (auto& [n, g] : p_->buses) { if (g) { ma_sound_group_uninit(g); delete g; } }
    if (p_->ready) ma_engine_uninit(&p_->engine);
}

bool AudioEngine::ok() const { return p_->ready; }

uint32_t AudioEngine::play(const std::string& file, float volume, bool loop, bool spatial,
                           const glm::vec3& pos, const std::string& bus) {
    PlayOpts o;
    o.volume = volume; o.loop = loop; o.spatial = spatial; o.pos = pos; o.bus = bus;
    return play(file, o);
}

uint32_t AudioEngine::play(const std::string& file, const PlayOpts& o) {
    if (!p_->ready) return 0;
    ma_sound_group* group = p_->bus(o.bus);
    auto* s = new ma_sound();
    ma_uint32 flags = o.stream ? MA_SOUND_FLAG_STREAM : MA_SOUND_FLAG_DECODE;
    if (ma_sound_init_from_file(&p_->engine, file.c_str(), flags, group, nullptr, s) != MA_SUCCESS) {
        log::error("audio: cannot load %s", file.c_str());
        delete s;
        return 0;
    }
    ma_sound_set_volume(s, o.volume);
    ma_sound_set_pitch(s, o.pitch <= 0.0f ? 1.0f : o.pitch);
    ma_sound_set_looping(s, o.loop ? MA_TRUE : MA_FALSE);
    ma_sound_set_spatialization_enabled(s, o.spatial ? MA_TRUE : MA_FALSE);
    if (o.spatial) ma_sound_set_position(s, o.pos.x, o.pos.y, o.pos.z);
    if (o.fade_in_ms > 0.0f)
        ma_sound_set_fade_in_milliseconds(s, 0.0f, o.volume, (ma_uint64)o.fade_in_ms);
    ma_sound_start(s);
    uint32_t h = p_->next++;
    p_->sounds[h] = s;
    if (!o.bus.empty()) p_->sound_bus[h] = o.bus;
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
void AudioEngine::set_pitch(uint32_t h, float pitch) {
    auto it = p_->sounds.find(h);
    if (it != p_->sounds.end() && pitch > 0.0f) ma_sound_set_pitch(it->second, pitch);
}
void AudioEngine::stop(uint32_t h, float fade_out_ms) {
    auto it = p_->sounds.find(h);
    if (it == p_->sounds.end()) return;
    if (fade_out_ms > 0.0f) {
        // fade then let it finish on its own; miniaudio stops a faded-to-0 sound
        ma_sound_set_fade_in_milliseconds(it->second, -1.0f, 0.0f, (ma_uint64)fade_out_ms);
        ma_sound_set_stop_time_in_milliseconds(it->second, (ma_uint64)fade_out_ms);
        return;
    }
    ma_sound_stop(it->second);
    ma_sound_uninit(it->second);
    delete it->second;
    p_->sounds.erase(it);
    p_->sound_bus.erase(h);
}
void AudioEngine::set_listener(const glm::vec3& pos, const glm::vec3& fwd) {
    if (!p_->ready) return;
    ma_engine_listener_set_position(&p_->engine, 0, pos.x, pos.y, pos.z);
    ma_engine_listener_set_direction(&p_->engine, 0, fwd.x, fwd.y, fwd.z);
}

void AudioEngine::set_bus_volume(const std::string& bus, float v) {
    if (!p_->ready) return;
    if (bus.empty() || bus == "master") {
        ma_engine_set_volume(&p_->engine, v);
        p_->bus_gain["master"] = v;
        return;
    }
    if (ma_sound_group* g = p_->bus(bus)) {
        ma_sound_group_set_volume(g, v);
        p_->bus_gain[bus] = v;
    }
}
float AudioEngine::bus_volume(const std::string& bus) const {
    std::string k = (bus.empty() ? "master" : bus);
    auto it = p_->bus_gain.find(k);
    return it == p_->bus_gain.end() ? 1.0f : it->second;
}
void AudioEngine::stop_bus(const std::string& bus) {
    for (auto it = p_->sounds.begin(); it != p_->sounds.end();) {
        auto b = p_->sound_bus.find(it->first);
        bool match = bus.empty() || bus == "master" ||
                     (b != p_->sound_bus.end() && b->second == bus);
        if (match) {
            ma_sound_stop(it->second);
            ma_sound_uninit(it->second);
            delete it->second;
            p_->sound_bus.erase(it->first);
            it = p_->sounds.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace eng
