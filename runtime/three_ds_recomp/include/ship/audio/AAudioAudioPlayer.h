#pragma once

#ifdef __ANDROID__

#include "AudioPlayer.h"
#include <aaudio/AAudio.h>
#include <mutex>
#include <vector>
#include <atomic>

namespace Ship {

/**
 * @brief Native Android AudioPlayer implementation backed by the AAudio NDK API.
 *
 * AAudioAudioPlayer provides low-latency audio output on Android devices (API 26+)
 * using a high-priority hardware callback stream and a thread-safe circular ring buffer.
 * It bypasses the SDL audio subsystem completely for native Android playback.
 */
class AAudioAudioPlayer final : public AudioPlayer {
  public:
    explicit AAudioAudioPlayer(AudioSettings settings);
    ~AAudioAudioPlayer();

    /**
     * @brief Returns the number of PCM frames currently queued in the ring buffer.
     */
    int32_t Buffered() override;

  protected:
    /**
     * @brief Opens and starts the AAudio stream in low-latency callback mode.
     */
    bool DoInit() override;

    /**
     * @brief Stops and closes the active AAudio stream and clears the ring buffer.
     */
    void DoClose() override;

    /**
     * @brief Writes interleaved PCM samples into the ring buffer for hardware consumption.
     */
    void DoPlay(const uint8_t* buf, size_t len) override;

    /**
     * @brief Flushes any queued audio data from the ring buffer.
     */
    void DoFlush() override;

  private:
    static aaudio_data_callback_result_t AAudioDataCallback(
        AAudioStream* stream,
        void* userData,
        void* audioData,
        int32_t numFrames);

    static void AAudioErrorCallback(
        AAudioStream* stream,
        void* userData,
        aaudio_result_t error);

    aaudio_data_callback_result_t OnAudioData(void* audioData, int32_t numFrames);
    void OnAudioError(aaudio_result_t error);

    AAudioStream* mStream = nullptr;
    int32_t mNumChannels = 2;
    std::atomic<bool> mInitialized{false};
    std::atomic<bool> mNeedsRestart{false};

    std::mutex mMutex;
    std::vector<uint8_t> mRingBuffer;
    size_t mRingBufferSize = 0;
    size_t mRingBufferReadPos = 0;
    size_t mRingBufferWritePos = 0;
};

} // namespace Ship

#endif // __ANDROID__
