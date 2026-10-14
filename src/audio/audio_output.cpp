#include "audio/audio_output.h"

#include "media/audio_decoder.h"

#include <miniaudio.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>
#include <stdexcept>

namespace sharkcut {

namespace {
// Medio segundo de audio preparado por adelantado.
constexpr std::size_t kBufferFloats = kAudioSampleRate / 2 * kAudioChannels;
} // namespace

struct AudioOutput::Impl {
    ma_context context{};
    ma_device device{};
    bool contextReady = false;
    bool deviceReady = false;

    AudioRingBuffer buffer{kBufferFloats};
    int latency = 0;

    // El callback toma este mutex con try_lock: reset() lo usa para vaciar el búfer
    // sin que el callback esté leyendo. Nunca se espera dentro del callback.
    std::mutex consumerMutex;
    std::atomic<bool> playing{false};
    std::atomic<float> volume{1.0f};

    // Reloj. `sequence` es impar mientras reset() lo modifica (lectura sin bloqueos).
    std::atomic<std::uint64_t> sequence{0};
    std::atomic<std::int64_t> baseSample{0};
    std::atomic<std::int64_t> consumed{0};
    std::atomic<std::uint64_t> requestId{0};

    // Picos acumulados hasta que alguien los lee (takePeakLevels).
    std::atomic<float> peakLeft{0.0f};
    std::atomic<float> peakRight{0.0f};

    static void raisePeak(std::atomic<float>& peak, float value) {
        float current = peak.load(std::memory_order_relaxed);
        while (value > current &&
               !peak.compare_exchange_weak(current, value, std::memory_order_relaxed)) {
        }
    }

    static void onData(ma_device* device, void* output, const void*, ma_uint32 frames) {
        auto* self = static_cast<Impl*>(device->pUserData);
        auto* out = static_cast<float*>(output);
        const std::size_t wanted = static_cast<std::size_t>(frames) * kAudioChannels;

        std::unique_lock lock(self->consumerMutex, std::try_to_lock);
        if (!lock.owns_lock() || !self->playing.load(std::memory_order_acquire)) {
            std::memset(out, 0, wanted * sizeof(float));
            return;
        }
        // Lee solo tramas completas (los dos canales).
        std::size_t got = self->buffer.read(out, wanted);
        got -= got % kAudioChannels;
        std::memset(out + got, 0, (wanted - got) * sizeof(float)); // falta de datos: silencio

        float left = 0.0f;
        float right = 0.0f;
        for (std::size_t i = 0; i + 1 < got; i += kAudioChannels) {
            left = std::max(left, std::fabs(out[i]));
            right = std::max(right, std::fabs(out[i + 1]));
        }
        raisePeak(self->peakLeft, left);
        raisePeak(self->peakRight, right);

        const float gain = self->volume.load(std::memory_order_relaxed);
        if (gain != 1.0f) {
            for (std::size_t i = 0; i < got; ++i) {
                out[i] *= gain;
            }
        }
        // Si no había datos el reloj no avanza: el video espera al audio.
        self->consumed.fetch_add(static_cast<std::int64_t>(got / kAudioChannels),
                                 std::memory_order_release);
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

AudioOutput::AudioOutput(Device device) : d(std::make_unique<Impl>()) {
    const ma_backend nullBackend[] = {ma_backend_null};
    const bool useNull = device == Device::Null;
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
    d->latency = static_cast<int>(d->device.playback.internalPeriodSizeInFrames *
                                  d->device.playback.internalPeriods);

    if (ma_device_start(&d->device) != MA_SUCCESS) {
        throw std::runtime_error("No se pudo iniciar el dispositivo de audio");
    }
}

AudioOutput::~AudioOutput() = default;

std::string AudioOutput::backendName() const {
    return ma_get_backend_name(d->context.backend);
}

std::string AudioOutput::deviceName() const {
    return d->device.playback.name;
}

int AudioOutput::latencyFrames() const {
    return d->latency;
}

AudioRingBuffer& AudioOutput::buffer() {
    return d->buffer;
}

void AudioOutput::reset(std::int64_t baseSample, std::uint64_t requestId) {
    d->playing.store(false, std::memory_order_release);
    std::lock_guard lock(d->consumerMutex); // espera a que el callback termine
    d->buffer.reset();
    d->sequence.fetch_add(1, std::memory_order_acq_rel); // impar: modificando
    d->baseSample.store(baseSample, std::memory_order_relaxed);
    d->consumed.store(0, std::memory_order_relaxed);
    d->requestId.store(requestId, std::memory_order_relaxed);
    d->sequence.fetch_add(1, std::memory_order_acq_rel); // par: listo
}

void AudioOutput::setPlaying(bool playing) {
    d->playing.store(playing, std::memory_order_release);
}

bool AudioOutput::playing() const {
    return d->playing.load(std::memory_order_acquire);
}

void AudioOutput::setVolume(float volume) {
    d->volume.store(volume < 0.0f ? 0.0f : volume, std::memory_order_relaxed);
}

AudioOutput::Clock AudioOutput::clock() const {
    for (;;) {
        const std::uint64_t before = d->sequence.load(std::memory_order_acquire);
        if (before % 2 != 0) {
            continue; // reset() en curso (dura microsegundos)
        }
        const std::int64_t base = d->baseSample.load(std::memory_order_relaxed);
        const std::int64_t consumed = d->consumed.load(std::memory_order_acquire);
        const std::uint64_t id = d->requestId.load(std::memory_order_relaxed);
        if (d->sequence.load(std::memory_order_acquire) == before) {
            // Lo último que se entregó aún tarda `latency` muestras en sonar.
            const std::int64_t played = consumed > d->latency ? consumed - d->latency : 0;
            return {base + played, id};
        }
    }
}

AudioOutput::Levels AudioOutput::takePeakLevels() {
    return {d->peakLeft.exchange(0.0f, std::memory_order_relaxed),
            d->peakRight.exchange(0.0f, std::memory_order_relaxed)};
}

} // namespace sharkcut
