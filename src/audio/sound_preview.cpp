#include "audio/sound_preview.h"

#include "media/audio_decoder.h"

#include <miniaudio.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <vector>

namespace sharkcut {

// Los efectos son cortos: se decodifican enteros al pedirlos y el callback de audio solo
// copia de memoria (sin esperas: si play() está cambiando el sonido, ese trozo es silencio).
struct SoundPreview::Impl {
    ma_context context{};
    ma_device device{};
    bool contextReady = false;
    bool deviceReady = false;

    std::mutex mutex;           // protege `samples` (el callback solo usa try_lock)
    std::vector<float> samples; // estéreo intercalado, 48 kHz
    std::size_t position = 0;   // en muestras por canal (bajo `mutex`)
    std::atomic<bool> active{false};

    static void onData(ma_device* device, void* output, const void*, ma_uint32 frames) {
        auto* self = static_cast<Impl*>(device->pUserData);
        auto* out = static_cast<float*>(output);
        std::memset(out, 0, sizeof(float) * frames * kAudioChannels);
        if (!self->active.load()) {
            return;
        }
        std::unique_lock lock(self->mutex, std::try_to_lock);
        if (!lock.owns_lock()) {
            return;
        }
        const std::size_t total = self->samples.size() / kAudioChannels;
        const std::size_t count = std::min<std::size_t>(frames, total - self->position);
        std::memcpy(out, self->samples.data() + self->position * kAudioChannels,
                    sizeof(float) * count * kAudioChannels);
        self->position += count;
        if (self->position >= total) {
            self->active.store(false);
        }
    }

    ~Impl() {
        if (deviceReady) {
            ma_device_uninit(&device);
        }
        if (contextReady) {
            ma_context_uninit(&context);
        }
    }
};

SoundPreview::SoundPreview(AudioOutput::Device device) : d(std::make_unique<Impl>()) {
    const ma_backend nullBackend[] = {ma_backend_null};
    const bool useNull = device == AudioOutput::Device::Null;
    if (ma_context_init(useNull ? nullBackend : nullptr, useNull ? 1u : 0u, nullptr, &d->context) !=
        MA_SUCCESS) {
        throw std::runtime_error("No se pudo iniciar el sistema de audio");
    }
    d->contextReady = true;
    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_f32;
    config.playback.channels = kAudioChannels;
    config.sampleRate = kAudioSampleRate;
    config.dataCallback = &Impl::onData;
    config.pUserData = d.get();
    if (ma_device_init(&d->context, &config, &d->device) != MA_SUCCESS) {
        throw std::runtime_error("No se pudo abrir el dispositivo de audio");
    }
    d->deviceReady = true;
    if (ma_device_start(&d->device) != MA_SUCCESS) {
        throw std::runtime_error("No se pudo iniciar el dispositivo de audio");
    }
}

SoundPreview::~SoundPreview() = default;

void SoundPreview::play(const std::string& path) {
    // Se decodifica fuera del mutex: el callback sigue con lo anterior mientras tanto.
    std::vector<float> samples;
    try {
        AudioDecoder decoder(path);
        std::vector<float> chunk(4096 * kAudioChannels);
        while (const std::size_t got = decoder.read(chunk.data(), 4096)) {
            samples.insert(samples.end(), chunk.begin(),
                           chunk.begin() + static_cast<std::ptrdiff_t>(got * kAudioChannels));
        }
    } catch (const std::exception&) {
        stop();
        throw std::runtime_error("No se pudo abrir el sonido");
    }
    std::lock_guard lock(d->mutex);
    d->samples = std::move(samples);
    d->position = 0;
    d->active.store(!d->samples.empty());
}

void SoundPreview::stop() {
    d->active.store(false);
}

bool SoundPreview::playing() const {
    return d->active.load();
}

} // namespace sharkcut
