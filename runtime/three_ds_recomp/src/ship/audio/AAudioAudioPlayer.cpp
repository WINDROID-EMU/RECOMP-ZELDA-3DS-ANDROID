#ifdef __ANDROID__

#include "ship/audio/AAudioAudioPlayer.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cstring>

namespace Ship {

AAudioAudioPlayer::AAudioAudioPlayer(AudioSettings settings)
    : AudioPlayer(settings), mInitialized(false), mNeedsRestart(false) {
}

AAudioAudioPlayer::~AAudioAudioPlayer() {
    SPDLOG_TRACE("destruct AAudio audio player");
    DoClose();
}

bool AAudioAudioPlayer::DoInit() {
    mNumChannels = this->GetNumOutputChannels();
    const size_t bytesPerFrame = sizeof(int16_t) * mNumChannels;

    {
        std::lock_guard<std::mutex> lock(mMutex);
        // Allocate an 8192-frame ring buffer (~32 KB stereo), decoupling emulator ticks from audio hardware
        mRingBufferSize = 8192 * bytesPerFrame;
        mRingBuffer.assign(mRingBufferSize, 0);
        mRingBufferReadPos = 0;
        mRingBufferWritePos = 0;
    }

    AAudioStreamBuilder* builder = nullptr;
    aaudio_result_t result = AAudio_createStreamBuilder(&builder);
    if (result != AAUDIO_OK || builder == nullptr) {
        SPDLOG_ERROR("AAudio: Failed to create stream builder: {}", AAudio_convertResultToText(result));
        return false;
    }

    AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_OUTPUT);
    AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
    AAudioStreamBuilder_setSharingMode(builder, AAUDIO_SHARING_MODE_SHARED);
    AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_I16);
    AAudioStreamBuilder_setChannelCount(builder, mNumChannels);
    AAudioStreamBuilder_setSampleRate(builder, this->GetSampleRate());
    AAudioStreamBuilder_setUsage(builder, AAUDIO_USAGE_GAME);
    AAudioStreamBuilder_setContentType(builder, AAUDIO_CONTENT_TYPE_MUSIC);
    AAudioStreamBuilder_setDataCallback(builder, AAudioDataCallback, this);
    AAudioStreamBuilder_setErrorCallback(builder, AAudioErrorCallback, this);

    result = AAudioStreamBuilder_openStream(builder, &mStream);
    AAudioStreamBuilder_delete(builder);

    if (result != AAUDIO_OK || mStream == nullptr) {
        SPDLOG_ERROR("AAudio: Failed to open stream: {}", AAudio_convertResultToText(result));
        mStream = nullptr;
        return false;
    }

    // Set buffer capacity to 2x burst size for low latency without underflows
    int32_t burst = AAudioStream_getFramesPerBurst(mStream);
    if (burst > 0) {
        AAudioStream_setBufferSizeInFrames(mStream, burst * 2);
    }

    result = AAudioStream_requestStart(mStream);
    if (result != AAUDIO_OK) {
        SPDLOG_ERROR("AAudio: Failed to start stream: {}", AAudio_convertResultToText(result));
        AAudioStream_close(mStream);
        mStream = nullptr;
        return false;
    }

    SPDLOG_INFO("AAudio initialized successfully: {} Hz, {} channels, burst frames {}, buffer size {}",
                AAudioStream_getSampleRate(mStream),
                AAudioStream_getChannelCount(mStream),
                burst,
                AAudioStream_getBufferSizeInFrames(mStream));

    mInitialized = true;
    mNeedsRestart = false;
    return true;
}

void AAudioAudioPlayer::DoClose() {
    mInitialized = false;

    if (mStream != nullptr) {
        AAudioStream_requestStop(mStream);
        AAudioStream_close(mStream);
        mStream = nullptr;
    }

    std::lock_guard<std::mutex> lock(mMutex);
    mRingBufferReadPos = 0;
    mRingBufferWritePos = 0;
    mRingBuffer.clear();
}

int32_t AAudioAudioPlayer::Buffered() {
    std::lock_guard<std::mutex> lock(mMutex);
    if (mRingBufferSize == 0) {
        return 0;
    }

    size_t bufferedBytes = 0;
    if (mRingBufferWritePos >= mRingBufferReadPos) {
        bufferedBytes = mRingBufferWritePos - mRingBufferReadPos;
    } else {
        bufferedBytes = mRingBufferSize - (mRingBufferReadPos - mRingBufferWritePos);
    }

    const size_t bytesPerFrame = sizeof(int16_t) * mNumChannels;
    return static_cast<int32_t>(bufferedBytes / bytesPerFrame);
}

void AAudioAudioPlayer::DoPlay(const uint8_t* buf, size_t len) {
    if (mNeedsRestart) {
        SPDLOG_WARN("AAudio: Restarting audio stream after device change");
        DoClose();
        if (!DoInit()) {
            return;
        }
    }

    if (!mInitialized || mRingBufferSize == 0) {
        return;
    }

    std::lock_guard<std::mutex> lock(mMutex);

    const size_t bytesPerFrame = sizeof(int16_t) * mNumChannels;
    const size_t maxBufferedBytes = 6000 * bytesPerFrame;

    size_t bufferedBytes = (mRingBufferWritePos >= mRingBufferReadPos)
        ? (mRingBufferWritePos - mRingBufferReadPos)
        : (mRingBufferSize - (mRingBufferReadPos - mRingBufferWritePos));

    if (bufferedBytes + len > maxBufferedBytes) {
        // Prevent queue from growing unbounded and desyncing game audio
        return;
    }

    size_t available = (mRingBufferWritePos >= mRingBufferReadPos)
        ? (mRingBufferSize - (mRingBufferWritePos - mRingBufferReadPos) - 1)
        : (mRingBufferReadPos - mRingBufferWritePos - 1);

    if (available >= len) {
        size_t writeEnd = mRingBufferWritePos + len;
        if (writeEnd <= mRingBufferSize) {
            std::memcpy(mRingBuffer.data() + mRingBufferWritePos, buf, len);
        } else {
            size_t firstChunk = mRingBufferSize - mRingBufferWritePos;
            std::memcpy(mRingBuffer.data() + mRingBufferWritePos, buf, firstChunk);
            std::memcpy(mRingBuffer.data(), buf + firstChunk, len - firstChunk);
        }
        mRingBufferWritePos = (mRingBufferWritePos + len) % mRingBufferSize;
    }
}

void AAudioAudioPlayer::DoFlush() {
    std::lock_guard<std::mutex> lock(mMutex);
    mRingBufferReadPos = 0;
    mRingBufferWritePos = 0;
    if (!mRingBuffer.empty()) {
        std::fill(mRingBuffer.begin(), mRingBuffer.end(), 0);
    }
}

aaudio_data_callback_result_t AAudioAudioPlayer::AAudioDataCallback(
    AAudioStream* /*stream*/,
    void* userData,
    void* audioData,
    int32_t numFrames) {
    auto* player = static_cast<AAudioAudioPlayer*>(userData);
    return player->OnAudioData(audioData, numFrames);
}

void AAudioAudioPlayer::AAudioErrorCallback(
    AAudioStream* /*stream*/,
    void* userData,
    aaudio_result_t error) {
    auto* player = static_cast<AAudioAudioPlayer*>(userData);
    player->OnAudioError(error);
}

aaudio_data_callback_result_t AAudioAudioPlayer::OnAudioData(void* audioData, int32_t numFrames) {
    const size_t bytesPerFrame = sizeof(int16_t) * mNumChannels;
    const size_t bytesNeeded = static_cast<size_t>(numFrames) * bytesPerFrame;
    auto* output = static_cast<uint8_t*>(audioData);

    std::lock_guard<std::mutex> lock(mMutex);

    if (mRingBufferSize == 0) {
        std::memset(output, 0, bytesNeeded);
        return AAUDIO_CALLBACK_RESULT_CONTINUE;
    }

    size_t availableBytes = (mRingBufferWritePos >= mRingBufferReadPos)
        ? (mRingBufferWritePos - mRingBufferReadPos)
        : (mRingBufferSize - (mRingBufferReadPos - mRingBufferWritePos));

    size_t bytesToCopy = std::min(bytesNeeded, availableBytes);

    if (bytesToCopy > 0) {
        size_t readEnd = mRingBufferReadPos + bytesToCopy;
        if (readEnd <= mRingBufferSize) {
            std::memcpy(output, mRingBuffer.data() + mRingBufferReadPos, bytesToCopy);
        } else {
            size_t firstChunk = mRingBufferSize - mRingBufferReadPos;
            std::memcpy(output, mRingBuffer.data() + mRingBufferReadPos, firstChunk);
            std::memcpy(output + firstChunk, mRingBuffer.data(), bytesToCopy - firstChunk);
        }
        mRingBufferReadPos = (mRingBufferReadPos + bytesToCopy) % mRingBufferSize;
    }

    // Zero-fill underrun frames to prevent stuttering/noise artifacts
    if (bytesToCopy < bytesNeeded) {
        std::memset(output + bytesToCopy, 0, bytesNeeded - bytesToCopy);
    }

    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

void AAudioAudioPlayer::OnAudioError(aaudio_result_t error) {
    SPDLOG_WARN("AAudio stream error: {}", AAudio_convertResultToText(error));
    if (error == AAUDIO_ERROR_DISCONNECTED) {
        mNeedsRestart = true;
    }
}

} // namespace Ship

#endif // __ANDROID__
