#include "media/waveform.h"

#include "media/audio_decoder.h"
#include "media/media_error.h"
#include "media/media_info.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <exception>
#include <fstream>
#include <limits>
#include <system_error>
#include <thread>

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

namespace {

// Cada tramo empieza a decodificar un poco antes (0,1 s) y lo descarta, para que el
// decodificador se asiente (AAC necesita la trama anterior) y el pico no cambie.
constexpr std::int64_t kPreroll = kAudioSampleRate / 10;
// Tramos de un minuto o más: abrir el archivo y saltar cuesta poco frente a eso.
constexpr std::int64_t kMinPartSamples = 60LL * kAudioSampleRate;
constexpr int kMaxParts = 4;

// Pico de `samples` muestras estéreo intercaladas (los dos canales juntos). Ocho
// acumuladores para que la CPU haga varias comparaciones a la vez.
float peakOf(const float* data, std::size_t samples) {
    const std::size_t count = samples * kAudioChannels;
    std::array<float, 8> acc{};
    std::size_t i = 0;
    for (; i + 8 <= count; i += 8) {
        for (std::size_t k = 0; k < 8; ++k) {
            acc[k] = std::max(acc[k], std::abs(data[i + k]));
        }
    }
    float peak = *std::max_element(acc.begin(), acc.end());
    for (; i < count; ++i) {
        peak = std::max(peak, std::abs(data[i]));
    }
    return peak;
}

struct PartResult {
    std::vector<std::uint8_t> peaks;
    bool reachedEnd = false; // el audio terminó dentro de este tramo
};

// Picos de los bloques [firstBlock, lastBlock) (lastBlock < 0: hasta el final del audio).
PartResult computePart(const std::string& path, std::int64_t firstBlock, std::int64_t lastBlock,
                       const std::atomic<bool>* cancel) {
    AudioDecoder decoder(path);
    constexpr std::size_t kBlock = Waveform::kBlockSamples;
    constexpr std::size_t kChunkBlocks = 200; // un segundo por lectura
    std::vector<float> buffer(kChunkBlocks * Waveform::kBlockSamples * kAudioChannels);

    const std::int64_t start = firstBlock * Waveform::kBlockSamples;
    if (start > 0) {
        const std::int64_t from = std::max<std::int64_t>(0, start - kPreroll);
        decoder.seekToSample(from);
        auto skip = static_cast<std::size_t>(start - from);
        while (skip > 0) {
            const std::size_t got =
                decoder.read(buffer.data(), std::min(skip, kChunkBlocks * Waveform::kBlockSamples));
            if (got == 0) {
                return {{}, true};
            }
            skip -= got;
        }
    }

    PartResult result;
    std::int64_t remaining = lastBlock < 0 ? std::numeric_limits<std::int64_t>::max()
                                           : (lastBlock - firstBlock) * Waveform::kBlockSamples;
    if (lastBlock >= 0) {
        result.peaks.reserve(static_cast<std::size_t>(lastBlock - firstBlock));
    }
    float peak = 0.0f;
    std::size_t inBlock = 0;
    while (remaining > 0) {
        if (cancel && cancel->load(std::memory_order_relaxed)) {
            throw MediaError("Cancelado");
        }
        const auto wanted = static_cast<std::size_t>(std::min<std::int64_t>(
            remaining, static_cast<std::int64_t>(kChunkBlocks * Waveform::kBlockSamples)));
        const std::size_t got = decoder.read(buffer.data(), wanted);
        if (got == 0) {
            result.reachedEnd = true;
            break;
        }
        remaining -= static_cast<std::int64_t>(got);
        std::size_t i = 0;
        while (i < got) {
            const std::size_t n = std::min(got - i, kBlock - inBlock);
            peak = std::max(peak, peakOf(buffer.data() + i * kAudioChannels, n));
            i += n;
            inBlock += n;
            if (inBlock == kBlock) {
                result.peaks.push_back(Waveform::encode(peak));
                peak = 0.0f;
                inBlock = 0;
            }
        }
    }
    if (inBlock > 0) {
        result.peaks.push_back(Waveform::encode(peak));
    }
    return result;
}

} // namespace

Waveform computeWaveform(const std::string& path, const std::atomic<bool>* cancel, int parts) {
    // Decodificar el audio es lo que más tarda (y va en un solo hilo): un VOD largo se
    // reparte en tramos que se decodifican a la vez y se juntan en orden.
    std::int64_t totalBlocks = 0;
    if (parts != 1) {
        const std::int64_t durationUs = probeMedia(path).durationUs;
        const std::int64_t samples = durationUs * kAudioSampleRate / 1'000'000;
        totalBlocks = samples / Waveform::kBlockSamples;
        if (parts <= 0) {
            const auto cores = static_cast<std::int64_t>(std::thread::hardware_concurrency());
            parts = static_cast<int>(std::clamp<std::int64_t>(
                std::min<std::int64_t>(samples / kMinPartSamples, std::max<std::int64_t>(cores, 1)),
                1, kMaxParts));
        }
        if (totalBlocks < parts) {
            parts = 1;
        }
    }
    if (parts <= 1) {
        Waveform waveform;
        waveform.peaks = computePart(path, 0, -1, cancel).peaks;
        return waveform;
    }

    std::vector<PartResult> results(static_cast<std::size_t>(parts));
    std::vector<std::exception_ptr> errors(static_cast<std::size_t>(parts));
    auto run = [&](int part) {
        const std::int64_t first = totalBlocks * part / parts;
        const std::int64_t last = part + 1 == parts ? -1 : totalBlocks * (part + 1) / parts;
        try {
            results[static_cast<std::size_t>(part)] = computePart(path, first, last, cancel);
        } catch (...) {
            errors[static_cast<std::size_t>(part)] = std::current_exception();
        }
    };
    std::vector<std::thread> threads;
    threads.reserve(static_cast<std::size_t>(parts - 1));
    for (int part = 1; part < parts; ++part) {
        threads.emplace_back(run, part);
    }
    run(0);
    for (std::thread& thread : threads) {
        thread.join();
    }
    for (const std::exception_ptr& error : errors) {
        if (error) {
            std::rethrow_exception(error);
        }
    }

    Waveform waveform;
    waveform.peaks.reserve(static_cast<std::size_t>(totalBlocks) + kMaxParts);
    for (const PartResult& result : results) {
        waveform.peaks.insert(waveform.peaks.end(), result.peaks.begin(), result.peaks.end());
        if (result.reachedEnd) {
            break; // el archivo dura menos de lo que decía: lo que sigue está vacío
        }
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
