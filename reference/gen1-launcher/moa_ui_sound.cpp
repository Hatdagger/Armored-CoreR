/* Menu sounds from the player's disc. See moa_ui_sound.h.
 *
 * Written fresh (the old fork's moa_ui_sound.c was not looked at for code):
 * the bank is found through the framework's own disc reader (PS1::ISOReader,
 * which already handles .cue/.bin, .iso and .chd), decoded by moa_vab.h, and
 * played through SDL3 audio streams, one per sound, so different sounds can
 * overlap and the same sound restarts instead of piling up.
 */
#include "moa_ui_sound.h"
#include "moa_vab.h"
#include "iso_reader.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace moa_sound {
namespace {

/* The game plays every menu sound as program 0, note 60. */
constexpr int kProgram = 0;
constexpr int kNote = 60;
/* Menu clicks sit a little under the game's own mix. */
constexpr float kLevel = 0.8f;

std::mutex g_lock;
std::string g_path;                 /* the disc last asked for */
std::atomic<bool> g_ready{false};
std::vector<moa_vab::Clip> g_clips;
std::vector<SDL_AudioStream*> g_streams;
SDL_AudioDeviceID g_device = 0;
int g_enabled = -1;                 /* -1 not read yet */

std::string upper(std::string s) {
    for (char& c : s) c = (char)std::toupper((unsigned char)c);
    return s;
}

/* One directory's entries, read straight from its sectors (the reader's own
 * listing only goes one folder deep). */
bool find_in_dir(PS1::ISOReader& iso, uint32_t lba, uint32_t size, const std::string& want,
                 PS1::ISOFileEntry& out) {
    uint8_t sec[2048];
    const uint32_t sectors = (size + 2047) / 2048;
    for (uint32_t s = 0; s < sectors && s < 64; ++s) {
        if (!iso.ReadSector(lba + s, sec)) return false;
        for (uint32_t at = 0; at < 2048;) {
            const uint8_t len = sec[at];
            if (len == 0) break;                    /* rest of this sector is padding */
            if (at + len > 2048 || len < 34) break;
            const uint8_t name_len = sec[at + 32];
            if (34u + name_len <= len) {
                std::string name((const char*)sec + at + 33, name_len);
                const size_t semi = name.find(';');
                if (semi != std::string::npos) name.resize(semi);
                if (upper(name) == want) {
                    out.name = name;
                    out.lba = (uint32_t)(sec[at + 2] | (sec[at + 3] << 8) |
                                         (sec[at + 4] << 16) | ((uint32_t)sec[at + 5] << 24));
                    out.size = (uint32_t)(sec[at + 10] | (sec[at + 11] << 8) |
                                          (sec[at + 12] << 16) | ((uint32_t)sec[at + 13] << 24));
                    out.is_directory = (sec[at + 25] & 2) != 0;
                    return true;
                }
            }
            at += len;
        }
    }
    return false;
}

/* Entries 0 (header) and 1 (samples) of GG/MS/MENU_VAB.T. A .T archive's
 * first sector holds halfwords [count, start0, ..., end] in sectors from the
 * archive's start; entry n runs from idx[n+1] to idx[n+2]. */
bool read_menu_bank(const std::string& path, std::vector<uint8_t>& vh, std::vector<uint8_t>& vb) {
    PS1::ISOReader iso;
    if (!iso.Open(path)) return false;
    PS1::ISOFileEntry gg{}, ms{}, file{};
    bool found = false;
    for (const auto& e : iso.ListFiles(""))
        if (e.is_directory && upper(e.name) == "GG") { gg = e; found = true; break; }
    if (!found) return false;
    if (!find_in_dir(iso, gg.lba, gg.size, "MS", ms) || !ms.is_directory) return false;
    if (!find_in_dir(iso, ms.lba, ms.size, "MENU_VAB.T", file) || file.is_directory) return false;

    uint8_t idx[2048];
    if (!iso.ReadSector(file.lba, idx)) return false;
    auto half = [&](int i) { return (uint32_t)(idx[i * 2] | (idx[i * 2 + 1] << 8)); };
    const uint32_t count = half(0);
    const uint32_t file_sectors = (file.size + 2047) / 2048;
    if (count < 2 || count > 64) return false;
    auto read_entry = [&](int n, std::vector<uint8_t>& out) {
        const uint32_t a = half(n + 1), b = half(n + 2);
        if (b <= a || b > file_sectors || b - a > 256) return false;
        out.resize((size_t)(b - a) * 2048);
        for (uint32_t s = a; s < b; ++s)
            if (!iso.ReadSector(file.lba + s, out.data() + (size_t)(s - a) * 2048)) return false;
        return true;
    };
    return read_entry(0, vh) && read_entry(1, vb);
}

void load(std::string path) {
    std::vector<uint8_t> vh, vb;
    std::vector<moa_vab::Clip> clips;
    if (!read_menu_bank(path, vh, vb) ||
        !moa_vab::read_bank(vh.data(), vh.size(), vb.data(), vb.size(), kProgram, kNote, clips)) {
        std::fprintf(stderr, "[menu sounds] no menu sound bank on %s\n", path.c_str());
        return;
    }
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) return;
    const SDL_AudioDeviceID dev = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr);
    if (!dev) return;
    SDL_AudioSpec out{};
    SDL_GetAudioDeviceFormat(dev, &out, nullptr);
    std::vector<SDL_AudioStream*> streams(clips.size(), nullptr);
    for (size_t i = 0; i < clips.size(); ++i) {
        if (clips[i].pcm.empty()) continue;
        const SDL_AudioSpec in{SDL_AUDIO_S16LE, 1, clips[i].rate};
        streams[i] = SDL_CreateAudioStream(&in, &out);
        if (streams[i]) SDL_BindAudioStream(dev, streams[i]);
    }
    std::lock_guard<std::mutex> hold(g_lock);
    if (path != g_path) {            /* another disc was picked meanwhile */
        for (SDL_AudioStream* s : streams) if (s) SDL_DestroyAudioStream(s);
        SDL_CloseAudioDevice(dev);
        return;
    }
    for (SDL_AudioStream* s : g_streams) if (s) SDL_DestroyAudioStream(s);
    if (g_device) SDL_CloseAudioDevice(g_device);
    g_clips = std::move(clips);
    g_streams = std::move(streams);
    g_device = dev;
    g_ready = true;
}

std::string choice_file() {
    char* dir = SDL_GetPrefPath("", "Armored-Core-Gen-1");
    if (!dir) return "";
    std::string p = std::string(dir) + "menu-sounds.txt";
    SDL_free(dir);
    return p;
}

} /* namespace */

void set_disc(const char* path) {
    if (!path || !path[0]) return;
    {
        std::lock_guard<std::mutex> hold(g_lock);
        if (g_path == path) return;
        g_path = path;
    }
    std::thread(load, std::string(path)).detach();
}

bool ready() { return g_ready; }

bool enabled() {
    if (g_enabled < 0) {
        g_enabled = 1;
        const std::string p = choice_file();
        if (FILE* f = p.empty() ? nullptr : std::fopen(p.c_str(), "rb")) {
            char buf[8] = {0};
            if (std::fread(buf, 1, 3, f) == 3 && std::string(buf, 3) == "off") g_enabled = 0;
            std::fclose(f);
        }
    }
    return g_enabled == 1;
}

void set_enabled(bool on) {
    g_enabled = on ? 1 : 0;
    const std::string p = choice_file();
    if (FILE* f = p.empty() ? nullptr : std::fopen(p.c_str(), "wb")) {
        std::fputs(on ? "on\n" : "off\n", f);
        std::fclose(f);
    }
}

void play(int sfx, int volume) {
    if (!g_ready || volume <= 0 || !enabled()) return;
    std::lock_guard<std::mutex> hold(g_lock);
    if (sfx < 0 || sfx >= (int)g_streams.size() || !g_streams[sfx]) return;
    SDL_AudioStream* s = g_streams[sfx];
    const moa_vab::Clip& c = g_clips[sfx];
    SDL_ClearAudioStream(s);
    SDL_SetAudioStreamGain(s, c.volume * kLevel * std::min(volume, 100) / 100.0f);
    SDL_PutAudioStreamData(s, c.pcm.data(), (int)(c.pcm.size() * sizeof(int16_t)));
}

} /* namespace moa_sound */
