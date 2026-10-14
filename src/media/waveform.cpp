#include "media/waveform.h"

#include "media/audio_decoder.h"
#include "media/media_error.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <system_error>

namespace sharkcut {

namespace {

constexpr char kMagic[4] = {'S', 'C', 'W', 'F'};
constexpr std::uint32_t kFormatVersion = 1;

std::filesystem::path pathFromUtf8(const std::string& utf8) {
    return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
}

void writeU32(std::ostream& out, std::uint32_t value) {
    const std::array<char, 4> bytes{
        static_cast<char>(value & 0xFF), static_cast<char>((value >> 8) & 0xFF),
        static_cast<char>((value >> 16) & 0xFF), static_cast<char>((value >> 24) & 0xFF)};
    out.write(bytes.data(), 4);
}

std::optional<std::uint32_t> readU32(std::istream& in) {
    std::array<unsigned char, 4> bytes{};
    if (!in.read(reinterpret_cast<char*>(bytes.data()), 4)) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(bytes[0]) | (static_cast<std::uint32_t>(bytes[1]) << 8) |
           (static_cast<std::uint32_t>(bytes[2]) << 16) |
           (static_cast<std::uint32_t>(bytes[3]) << 24);
}

} // namespace

std::uint8_t Waveform::encode(float peak) {
    if (!(peak > 0.0f)) {
        return 0;
    }
    const double db = 20.0 * std::log10(static_cast<double>(peak));
    const double level = std::clamp((db - kFloorDb) / -kFloorDb, 0.0, 1.0);
    return static_cast<std::uint8_t>(std::lround(level * 255.0));
}

double Waveform::level(std::uint8_t value, double gainDb) {
    if (value == 0) {
        return 0.0;
    }
    const double db = kFloorDb + (static_cast<double>(value) / 255.0) * -kFloorDb + gainDb;
    if (db <= kFloorDb) {
        return 0.0;
    }
    return std::clamp(std::sqrt(std::pow(10.0, db / 20.0)), 0.0, 1.0);
}

Waveform computeWaveform(const std::string& path, const std::atomic<bool>* cancel) {
    AudioDecoder decoder(path);
    Waveform waveform;
    constexpr std::size_t kChunkBlocks = 200; // un segundo por lectura
    std::vector<float> buffer(kChunkBlocks * Waveform::kBlockSamples * kAudioChannels);
    float peak = 0.0f;
    int inBlock = 0;
    for (;;) {
        if (cancel && cancel->load(std::memory_order_relaxed)) {
            throw MediaError("Cancelado");
        }
        const std::size_t got = decoder.read(buffer.data(), kChunkBlocks * Waveform::kBlockSamples);
        if (got == 0) {
            break;
        }
        for (std::size_t i = 0; i < got; ++i) {
            const float l = std::abs(buffer[i * kAudioChannels]);
            const float r = std::abs(buffer[i * kAudioChannels + 1]);
            peak = std::max(peak, std::max(l, r));
            if (++inBlock == Waveform::kBlockSamples) {
                waveform.peaks.push_back(Waveform::encode(peak));
                peak = 0.0f;
                inBlock = 0;
            }
        }
    }
    if (inBlock > 0) {
        waveform.peaks.push_back(Waveform::encode(peak));
    }
    return waveform;
}

bool saveWaveform(const Waveform& waveform, const std::filesystem::path& file) {
    std::error_code error;
    std::filesystem::create_directories(file.parent_path(), error);
    std::filesystem::path temp = file;
    temp += ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        out.write(kMagic, 4);
        writeU32(out, kFormatVersion);
        writeU32(out, static_cast<std::uint32_t>(Waveform::kBlockSamples));
        writeU32(out, static_cast<std::uint32_t>(waveform.peaks.size()));
        out.write(reinterpret_cast<const char*>(waveform.peaks.data()),
                  static_cast<std::streamsize>(waveform.peaks.size()));
        out.close();
        if (!out) {
            std::filesystem::remove(temp, error);
            return false;
        }
    }
    std::filesystem::rename(temp, file, error);
    if (error) {
        std::filesystem::remove(temp, error);
        return false;
    }
    return true;
}

std::optional<Waveform> loadWaveform(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    char magic[4] = {};
    if (!in.read(magic, 4) || std::memcmp(magic, kMagic, 4) != 0) {
        return std::nullopt;
    }
    const auto version = readU32(in);
    const auto block = readU32(in);
    const auto count = readU32(in);
    if (!version || *version != kFormatVersion || !block ||
        *block != static_cast<std::uint32_t>(Waveform::kBlockSamples) || !count) {
        return std::nullopt;
    }
    Waveform waveform;
    waveform.peaks.resize(*count);
    if (!in.read(reinterpret_cast<char*>(waveform.peaks.data()),
                 static_cast<std::streamsize>(waveform.peaks.size()))) {
        return std::nullopt;
    }
    return waveform;
}

std::string waveformCacheName(const std::string& mediaPath) {
    const std::filesystem::path path = pathFromUtf8(mediaPath);
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error) {
        return {};
    }
    const auto modified = std::filesystem::last_write_time(path, error);
    if (error) {
        return {};
    }
    // FNV-1a de 64 bits sobre la ruta, el tamaño y la fecha.
    std::uint64_t hash = 14695981039346656037ull;
    auto mix = [&hash](const void* data, std::size_t length) {
        const auto* bytes = static_cast<const unsigned char*>(data);
        for (std::size_t i = 0; i < length; ++i) {
            hash ^= bytes[i];
            hash *= 1099511628211ull;
        }
    };
    mix(mediaPath.data(), mediaPath.size());
    const auto sizeValue = static_cast<std::uint64_t>(size);
    mix(&sizeValue, sizeof(sizeValue));
    const auto ticks = static_cast<std::int64_t>(modified.time_since_epoch().count());
    mix(&ticks, sizeof(ticks));
    char name[32];
    std::snprintf(name, sizeof(name), "%016llx.onda", static_cast<unsigned long long>(hash));
    return name;
}

} // namespace sharkcut
