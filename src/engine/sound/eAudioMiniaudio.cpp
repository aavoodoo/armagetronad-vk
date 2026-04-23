/*

*************************************************************************

ArmageTron -- Just another Tron Lightcycle Game in 3D.
Copyright (C) 2000  Manuel Moos (manuel@moosnet.de)

**************************************************************************

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

***************************************************************************

Audio implementation using miniaudio library

*/

#include "aa_config.h"

#ifndef DEDICATED

// miniaudio implementation
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

// For OGG/Vorbis decoding we use stb_vorbis
#include "stb_vorbis.c"

#include "eAudioInterface.h"
#include "tConsole.h"

#include <vector>
#include <mutex>
#include <atomic>
#include <cstring>
#include <cmath>

// Maximum number of channels
#define MAX_AUDIO_CHANNELS 64

/*******************************************************************************
 *
 * Helper: Decode audio file to PCM samples
 *
 *******************************************************************************/

struct AudioData
{
    std::vector<float> samples;
    int sampleRate;
    int channels;
    bool valid;

    AudioData() : sampleRate(0), channels(0), valid(false) {}
};

static AudioData LoadAudioFile(const char* filename)
{
    AudioData data;

    if (!filename || !filename[0])
    {
        return data;
    }

    // Try loading as OGG/Vorbis
    int channels, sampleRate;
    short* output;
    int numSamples = stb_vorbis_decode_filename(filename, &channels, &sampleRate, &output);

    if (numSamples > 0)
    {
        data.channels = channels;
        data.sampleRate = sampleRate;
        data.samples.resize(numSamples * channels);

        // Convert to float
        for (int i = 0; i < numSamples * channels; i++)
        {
            data.samples[i] = output[i] / 32768.0f;
        }

        free(output);
        data.valid = true;
        return data;
    }

    // Try loading as WAV using miniaudio's decoder
    ma_decoder decoder;
    ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 2, 44100);

    if (ma_decoder_init_file(filename, &config, &decoder) == MA_SUCCESS)
    {
        data.channels = decoder.outputChannels;
        data.sampleRate = decoder.outputSampleRate;

        // Read all frames
        std::vector<float> buffer(4096 * data.channels);
        ma_uint64 framesRead;

        while (ma_decoder_read_pcm_frames(&decoder, buffer.data(), 4096, &framesRead) == MA_SUCCESS && framesRead > 0)
        {
            size_t oldSize = data.samples.size();
            data.samples.resize(oldSize + framesRead * data.channels);
            std::memcpy(data.samples.data() + oldSize, buffer.data(), framesRead * data.channels * sizeof(float));
        }

        ma_decoder_uninit(&decoder);
        data.valid = !data.samples.empty();
    }

    return data;
}

/*******************************************************************************
 *
 * eAudioSoundMiniaudio - Sound effect implementation
 *
 *******************************************************************************/

class eAudioSoundMiniaudio : public eAudioSound
{
public:
    AudioData audioData;
    int volume;

    eAudioSoundMiniaudio() : volume(64) {}

    bool IsValid() const override { return audioData.valid; }
    void SetVolume(int vol) override { volume = vol; }
    int GetVolume() const override { return volume; }
};

/*******************************************************************************
 *
 * Channel mixing state
 *
 *******************************************************************************/

struct ChannelState
{
    eAudioSoundMiniaudio* sound;
    size_t position;
    bool playing;
    bool looping;
    int volume;
    int leftPan;
    int rightPan;
    int bearing;
    int distance;
    unsigned int startTime;
    bool finished;

    ChannelState() : sound(nullptr), position(0), playing(false), looping(false),
                     volume(128), leftPan(255), rightPan(255), bearing(0), distance(0),
                     startTime(0), finished(false) {}
};

/*******************************************************************************
 *
 * eAudioChannelMiniaudio - Channel implementation
 *
 *******************************************************************************/

class eAudioChannelMiniaudio : public eAudioChannel
{
public:
    int id;
    ChannelState* state;
    static unsigned int s_startTimeCounter;  // Monotonic counter for ordering

    eAudioChannelMiniaudio(int channelId, ChannelState* channelState)
        : id(channelId), state(channelState) {}

    int GetId() const override { return id; }

    void SetVolume(int volume) override
    {
        state->volume = volume;
    }

    void PlaySound(eAudioSound& sound) override
    {
        eAudioSoundMiniaudio* snd = static_cast<eAudioSoundMiniaudio*>(&sound);
        state->sound = snd;
        state->position = 0;
        state->looping = false;
        state->playing = true;
        state->volume = snd->GetVolume();
        state->startTime = ++s_startTimeCounter;  // Monotonic counter
        state->finished = false;
    }

    void LoopSound(eAudioSound& sound) override
    {
        eAudioSoundMiniaudio* snd = static_cast<eAudioSoundMiniaudio*>(&sound);
        state->sound = snd;
        state->position = 0;
        state->looping = true;
        state->playing = true;
        state->volume = snd->GetVolume();
        state->startTime = ++s_startTimeCounter;  // Monotonic counter
        state->finished = false;
    }

    void StopSound() override
    {
        state->playing = false;
        state->finished = true;
    }

    void SetPosition(int bearing, int dist) override
    {
        state->bearing = bearing;
        state->distance = dist;
    }

    void SetPanning(int left, int right) override
    {
        state->leftPan = left;
        state->rightPan = right;
    }

    void ClearEffects() override
    {
        state->leftPan = 255;
        state->rightPan = 255;
        state->bearing = 0;
        state->distance = 0;
    }

    bool IsPlaying() const override
    {
        return state->playing;
    }

    unsigned int GetStartTime() const override
    {
        return state->startTime;
    }
};

// Static member initialization
unsigned int eAudioChannelMiniaudio::s_startTimeCounter = 0;

/*******************************************************************************
 *
 * Music streaming state
 *
 *******************************************************************************/

struct MusicState
{
    stb_vorbis* vorbis;
    std::vector<float> buffer;
    int channels;
    int sampleRate;
    bool playing;
    bool paused;
    int volume;
    int fadeOutSamples;
    int fadeOutRemaining;
    bool finished;
    std::mutex mutex;

    MusicState() : vorbis(nullptr), channels(0), sampleRate(0), playing(false),
                   paused(false), volume(128), fadeOutSamples(0), fadeOutRemaining(0),
                   finished(false) {}
};

/*******************************************************************************
 *
 * eAudioMusicMiniaudio - Music implementation
 *
 *******************************************************************************/

class eAudioMusicMiniaudio : public eAudioMusic
{
public:
    MusicState state;
    tString filename;

    eAudioMusicMiniaudio() {}

    ~eAudioMusicMiniaudio()
    {
        if (state.vorbis)
        {
            stb_vorbis_close(state.vorbis);
        }
    }

    bool Load(const char* file)
    {
        filename = file;
        int error;
        state.vorbis = stb_vorbis_open_filename(file, &error, nullptr);
        if (state.vorbis)
        {
            stb_vorbis_info info = stb_vorbis_get_info(state.vorbis);
            state.channels = info.channels;
            state.sampleRate = info.sample_rate;
            return true;
        }
        return false;
    }

    bool IsValid() const override { return state.vorbis != nullptr; }

    bool Play(int loops) override
    {
        if (!state.vorbis) return false;
        stb_vorbis_seek_start(state.vorbis);
        state.playing = true;
        state.paused = false;
        state.finished = false;
        return true;
    }

    void Stop() override
    {
        state.playing = false;
        state.finished = true;
    }

    void Pause() override
    {
        state.paused = !state.paused;
    }

    void Resume() override
    {
        state.paused = false;
    }

    void FadeOut(int ms) override
    {
        // Calculate number of samples for fade
        state.fadeOutSamples = (state.sampleRate * ms) / 1000;
        state.fadeOutRemaining = state.fadeOutSamples;
    }

    void SetVolume(int vol) override
    {
        state.volume = vol;
    }

    bool IsPlaying() const override
    {
        return state.playing && !state.paused;
    }
};

/*******************************************************************************
 *
 * External channel registration (forward declarations for use in MixAudio)
 *
 *******************************************************************************/

static eExternalChannelData* s_externalChannels = nullptr;
static int s_numExternalChannels = 0;
static std::mutex s_externalChannelsMutex;

// External music registration for eMusicTrack (struct defined in eAudioInterface.h)
static eExternalMusicData* s_externalMusic = nullptr;
static std::mutex s_externalMusicMutex;

// Atomic flag for safe vorbis decoder shutdown
// When false, audio thread must not access the decoder
static std::atomic<bool> s_externalMusicDecoderValid{false};

/*******************************************************************************
 *
 * eAudioDeviceMiniaudio - Main device implementation
 *
 *******************************************************************************/

class eAudioDeviceMiniaudio : public eAudioDevice
{
private:
    ma_device device;
    bool active;
    int sampleRate;

    std::vector<ChannelState> channelStates;
    std::vector<std::unique_ptr<eAudioChannelMiniaudio>> channels;

    eAudioMusicMiniaudio* currentMusic;
    ChannelFinishedCallback channelFinishedCallback;
    MusicFinishedCallback musicFinishedCallback;
    LegacyMixCallback legacyMixCallback;
    void* legacyMixUserData;

    int musicVolume;
    int soundVolume;

    std::mutex mixMutex;

public:
    eAudioDeviceMiniaudio()
        : active(false), sampleRate(44100), currentMusic(nullptr),
          legacyMixCallback(nullptr), legacyMixUserData(nullptr),
          musicVolume(128), soundVolume(128) {}

    ~eAudioDeviceMiniaudio()
    {
        Shutdown();
    }

    static void AudioCallback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount)
    {
        eAudioDeviceMiniaudio* self = static_cast<eAudioDeviceMiniaudio*>(pDevice->pUserData);
        self->MixAudio(static_cast<float*>(pOutput), frameCount);
    }

    void MixAudio(float* output, ma_uint32 frameCount)
    {
        std::lock_guard<std::mutex> lock(mixMutex);

        // Clear output buffer
        std::memset(output, 0, frameCount * 2 * sizeof(float));

        // Mix all active internal channels
        for (size_t i = 0; i < channelStates.size(); i++)
        {
            ChannelState& ch = channelStates[i];
            if (!ch.playing || !ch.sound || !ch.sound->IsValid())
                continue;

            AudioData& audio = ch.sound->audioData;
            // Looping sounds need volume reduction to match SDL_Mixer balance
            float volumeScale = ch.looping ? 0.35f : 1.0f;
            float vol = (ch.volume / 128.0f) * (soundVolume / 128.0f) * volumeScale;
            float leftVol = vol * (ch.leftPan / 255.0f);
            float rightVol = vol * (ch.rightPan / 255.0f);

            // Apply distance attenuation
            if (ch.distance > 0)
            {
                float distFactor = 1.0f - (ch.distance / 255.0f);
                leftVol *= distFactor;
                rightVol *= distFactor;
            }

            for (ma_uint32 frame = 0; frame < frameCount; frame++)
            {
                if (ch.position >= audio.samples.size() / audio.channels)
                {
                    if (ch.looping)
                    {
                        ch.position = 0;
                    }
                    else
                    {
                        ch.playing = false;
                        ch.finished = true;
                        break;
                    }
                }

                float left, right;
                if (audio.channels == 1)
                {
                    left = right = audio.samples[ch.position];
                }
                else
                {
                    left = audio.samples[ch.position * 2];
                    right = audio.samples[ch.position * 2 + 1];
                }

                output[frame * 2] += left * leftVol;
                output[frame * 2 + 1] += right * rightVol;
                ch.position++;
            }
        }

        // Mix external channels (from eChannelMiniaudio) with sample rate conversion
        // Thread safety: Copy all VALUES under lock, mix without lock, write back positions under lock.
        // We never dereference pointers to external data outside the lock.
        struct ExtChannelMixData {
            const float* samples;       // Points to eWavData samples (stable during playback)
            size_t sampleCount;
            int audioChannels;
            int srcRate;
            size_t positionValue;       // COPY of position value (not a pointer!)
            bool looping;
            float leftVol;
            float rightVol;
            float pitch;
            int index;
            bool finished;
            size_t finalPosition;       // Position after mixing (to write back)
        };
        std::vector<ExtChannelMixData> channelsToMix;

        // Brief lock to copy channel data (VALUES only, no pointers to mutable external state)
        {
            std::lock_guard<std::mutex> extLock(s_externalChannelsMutex);
            for (int i = 0; i < s_numExternalChannels; i++)
            {
                eExternalChannelData& ch = s_externalChannels[i];
                if (!ch.playing || !ch.samples || ch.sampleCount == 0)
                    continue;

                ExtChannelMixData mixData;
                mixData.samples = ch.samples;
                mixData.sampleCount = ch.sampleCount;
                mixData.audioChannels = ch.audioChannels;
                mixData.srcRate = ch.sampleRate > 0 ? ch.sampleRate : 44100;
                mixData.positionValue = ch.positionValue;  // Copy VALUE, not pointer
                mixData.looping = ch.looping;
                mixData.pitch = ch.pitch > 0.0f ? ch.pitch : 1.0f;

                // Looping sounds (engine) need volume reduction to match SDL_Mixer balance
                float volumeScale = ch.looping ? 0.35f : 1.0f;
                float vol = (ch.volume / 128.0f) * (soundVolume / 128.0f) * volumeScale;
                mixData.leftVol = vol * ch.leftVol;
                mixData.rightVol = vol * ch.rightVol;
                mixData.index = i;
                mixData.finished = false;
                mixData.finalPosition = mixData.positionValue;

                channelsToMix.push_back(mixData);
            }
        }

        // Mix without holding lock - operate on local position copies
        for (auto& ch : channelsToMix)
        {
            size_t numFrames = ch.sampleCount / ch.audioChannels;
            // Apply pitch to the sample rate increment
            uint32_t increment = static_cast<uint32_t>((static_cast<uint64_t>(ch.srcRate) << 16) * ch.pitch / sampleRate);

            size_t localPos = ch.positionValue;  // Work with local copy

            for (ma_uint32 frame = 0; frame < frameCount; frame++)
            {
                size_t samplePos = localPos >> 16;

                if (samplePos >= numFrames)
                {
                    if (ch.looping)
                    {
                        localPos = 0;
                        samplePos = 0;
                    }
                    else
                    {
                        ch.finished = true;
                        break;
                    }
                }

                size_t nextPos = samplePos + 1;
                if (nextPos >= numFrames) nextPos = ch.looping ? 0 : samplePos;
                float frac = (localPos & 0xFFFF) / 65536.0f;

                float left, right;
                if (ch.audioChannels == 1)
                {
                    float s0 = ch.samples[samplePos];
                    float s1 = ch.samples[nextPos];
                    left = right = s0 + (s1 - s0) * frac;
                }
                else
                {
                    float l0 = ch.samples[samplePos * 2];
                    float r0 = ch.samples[samplePos * 2 + 1];
                    float l1 = ch.samples[nextPos * 2];
                    float r1 = ch.samples[nextPos * 2 + 1];
                    left = l0 + (l1 - l0) * frac;
                    right = r0 + (r1 - r0) * frac;
                }

                output[frame * 2] += left * ch.leftVol;
                output[frame * 2 + 1] += right * ch.rightVol;
                localPos += increment;
            }

            ch.finalPosition = localPos;  // Store result for write-back
        }

        // Brief lock to update positions and finished flags
        if (!channelsToMix.empty())
        {
            std::lock_guard<std::mutex> extLock(s_externalChannelsMutex);
            for (const auto& ch : channelsToMix)
            {
                if (s_externalChannels && ch.index < s_numExternalChannels)
                {
                    // Only update position if channel is still playing the same sound
                    // (game thread might have started a new sound, resetting position to 0)
                    if (s_externalChannels[ch.index].playing)
                    {
                        s_externalChannels[ch.index].positionValue = ch.finalPosition;
                    }
                    if (ch.finished)
                    {
                        s_externalChannels[ch.index].playing = false;
                    }
                }
            }
        }

        // Mix external music (from eMusicTrack in eChannelMiniaudio)
        // Thread safety: Check atomic flag BEFORE copying state, and check again before decoding.
        // This ensures we don't use a decoder that's being destroyed.
        eExternalMusicData extMusicCopy = {nullptr, false, false, 0};
        bool decoderValid = s_externalMusicDecoderValid.load(std::memory_order_acquire);

        if (decoderValid)
        {
            std::lock_guard<std::mutex> extMusicLock(s_externalMusicMutex);
            if (s_externalMusic)
            {
                extMusicCopy = *s_externalMusic;
            }
        }

        // Decode and mix without holding lock, but only if decoder is still valid
        // Check the atomic flag again before accessing the decoder
        if (extMusicCopy.playing && !extMusicCopy.paused && extMusicCopy.vorbisDecoder &&
            s_externalMusicDecoderValid.load(std::memory_order_acquire))
        {
            std::vector<float> musicBuffer(frameCount * 2);
            int samplesRead = stb_vorbis_get_samples_float_interleaved(
                static_cast<stb_vorbis*>(extMusicCopy.vorbisDecoder), 2, musicBuffer.data(), frameCount * 2);

            if (samplesRead > 0)
            {
                float vol = (extMusicCopy.volume / 128.0f) * (musicVolume / 128.0f);
                for (int i = 0; i < samplesRead * 2; i++)
                {
                    output[i] += musicBuffer[i] * vol;
                }
            }
        }

        // Mix internal music (from eAudioDevice::LoadMusic)
        if (currentMusic && currentMusic->IsPlaying())
        {
            MusicState& ms = currentMusic->state;
            std::lock_guard<std::mutex> musicLock(ms.mutex);

            if (ms.vorbis && !ms.paused)
            {
                std::vector<float> musicBuffer(frameCount * 2);
                int samplesRead = stb_vorbis_get_samples_float_interleaved(
                    ms.vorbis, 2, musicBuffer.data(), frameCount * 2);

                if (samplesRead == 0)
                {
                    ms.playing = false;
                    ms.finished = true;
                }
                else
                {
                    float vol = (ms.volume / 128.0f) * (musicVolume / 128.0f);

                    // Apply fade out
                    for (int i = 0; i < samplesRead * 2; i += 2)
                    {
                        float fadeVol = vol;
                        if (ms.fadeOutRemaining > 0)
                        {
                            fadeVol *= static_cast<float>(ms.fadeOutRemaining) / ms.fadeOutSamples;
                            ms.fadeOutRemaining--;
                            if (ms.fadeOutRemaining == 0)
                            {
                                ms.playing = false;
                                ms.finished = true;
                                break;
                            }
                        }

                        output[i] += musicBuffer[i] * fadeVol;
                        output[i + 1] += musicBuffer[i + 1] * fadeVol;
                    }
                }
            }
        }

        // Call legacy mix callback (for eSound.cpp compatibility)
        if (legacyMixCallback)
        {
            // Convert float to int16 for legacy callback
            std::vector<short> legacyBuffer(frameCount * 2);
            for (ma_uint32 i = 0; i < frameCount * 2; i++)
            {
                float sample = output[i];
                if (sample > 1.0f) sample = 1.0f;
                if (sample < -1.0f) sample = -1.0f;
                legacyBuffer[i] = static_cast<short>(sample * 32767.0f);
            }

            legacyMixCallback(legacyMixUserData, legacyBuffer.data(), frameCount * 2 * sizeof(short));

            // Convert back to float and add
            for (ma_uint32 i = 0; i < frameCount * 2; i++)
            {
                output[i] = legacyBuffer[i] / 32768.0f;
            }
        }

        // Clamp output
        for (ma_uint32 i = 0; i < frameCount * 2; i++)
        {
            if (output[i] > 1.0f) output[i] = 1.0f;
            if (output[i] < -1.0f) output[i] = -1.0f;
        }
    }

    bool Init(eAudioQuality quality, float bufferSize) override
    {
        if (quality == AUDIO_QUALITY_OFF)
        {
            return false;
        }

        switch (quality)
        {
        case AUDIO_QUALITY_LOW:
            sampleRate = 22050;
            break;
        case AUDIO_QUALITY_MEDIUM:
            sampleRate = 44100;
            break;
        case AUDIO_QUALITY_HIGH:
            sampleRate = 48000;
            break;
        default:
            sampleRate = 44100;
        }

        ma_device_config config = ma_device_config_init(ma_device_type_playback);
        config.playback.format = ma_format_f32;
        config.playback.channels = 2;
        config.sampleRate = sampleRate;
        config.dataCallback = AudioCallback;
        config.pUserData = this;

        // Set buffer size
        int samples = std::max(128, static_cast<int>((bufferSize * sampleRate) / 60));
        config.periodSizeInFrames = samples;

        if (ma_device_init(nullptr, &config, &device) != MA_SUCCESS)
        {
            // Try fallback sample rate
            sampleRate = 22050;
            config.sampleRate = sampleRate;
            if (ma_device_init(nullptr, &config, &device) != MA_SUCCESS)
            {
                tERR_WARN("Failed to initialize audio device");
                return false;
            }
        }

        if (ma_device_start(&device) != MA_SUCCESS)
        {
            ma_device_uninit(&device);
            tERR_WARN("Failed to start audio device");
            return false;
        }

        active = true;
        return true;
    }

    void Shutdown() override
    {
        if (active)
        {
            ma_device_uninit(&device);
            active = false;
        }
        channels.clear();
        channelStates.clear();
        currentMusic = nullptr;
    }

    bool IsActive() const override { return active; }
    int GetSampleRate() const override { return sampleRate; }

    std::unique_ptr<eAudioSound> LoadSound(const char* filename) override
    {
        auto sound = std::make_unique<eAudioSoundMiniaudio>();
        sound->audioData = LoadAudioFile(filename);
        if (!sound->audioData.valid)
        {
            tERR_WARN("Couldn't load sound file");
        }
        return sound;
    }

    int GetNumChannels() const override
    {
        return static_cast<int>(channels.size());
    }

    eAudioChannel* GetChannel(int index) override
    {
        if (index >= 0 && index < static_cast<int>(channels.size()))
        {
            return channels[index].get();
        }
        return nullptr;
    }

    int AllocateChannels(int numChannels) override
    {
        std::lock_guard<std::mutex> lock(mixMutex);

        channelStates.resize(numChannels);
        channels.clear();
        channels.reserve(numChannels);

        for (int i = 0; i < numChannels; i++)
        {
            channels.push_back(std::make_unique<eAudioChannelMiniaudio>(i, &channelStates[i]));
        }

        return numChannels;
    }

    void SetChannelFinishedCallback(ChannelFinishedCallback callback) override
    {
        channelFinishedCallback = callback;
    }

    std::unique_ptr<eAudioMusic> LoadMusic(const char* filename) override
    {
        auto music = std::make_unique<eAudioMusicMiniaudio>();
        if (!music->Load(filename))
        {
            tERR_WARN("Couldn't load music file");
        }
        currentMusic = music.get();
        return music;
    }

    void SetMusicFinishedCallback(MusicFinishedCallback callback) override
    {
        musicFinishedCallback = callback;
    }

    void SetMusicVolume(int volume) override
    {
        musicVolume = volume;
    }

    void SetSoundVolume(int volume) override
    {
        soundVolume = volume;
    }

    void SetLegacyMixCallback(LegacyMixCallback callback, void* udata) override
    {
        legacyMixCallback = callback;
        legacyMixUserData = udata;
    }

    // Check for finished channels/music (call from game loop)
    void Update()
    {
        for (size_t i = 0; i < channelStates.size(); i++)
        {
            if (channelStates[i].finished)
            {
                channelStates[i].finished = false;
                if (channelFinishedCallback)
                {
                    channelFinishedCallback(static_cast<int>(i));
                }
            }
        }

        if (currentMusic && currentMusic->state.finished)
        {
            currentMusic->state.finished = false;
            if (musicFinishedCallback)
            {
                musicFinishedCallback();
            }
        }
    }
};

/*******************************************************************************
 *
 * Factory function
 *
 *******************************************************************************/

std::unique_ptr<eAudioDevice> CreateAudioDevice()
{
    return std::make_unique<eAudioDeviceMiniaudio>();
}

// Global audio device accessor
static eAudioDevice* s_globalAudioDevice = nullptr;

eAudioDevice* GetGlobalAudioDevice()
{
    return s_globalAudioDevice;
}

void SetGlobalAudioDevice(eAudioDevice* device)
{
    s_globalAudioDevice = device;
}

// External channel registration implementation
void RegisterExternalChannels(eExternalChannelData* channels, int numChannels)
{
    std::lock_guard<std::mutex> lock(s_externalChannelsMutex);
    s_externalChannels = channels;
    s_numExternalChannels = numChannels;
}

void UpdateExternalChannel(int index, const eExternalChannelData& data)
{
    std::lock_guard<std::mutex> lock(s_externalChannelsMutex);
    if (s_externalChannels && index >= 0 && index < s_numExternalChannels)
    {
        s_externalChannels[index] = data;
    }
}

// Update just the position value (for game thread to sync position if needed)
void UpdateExternalChannelPosition(int index, size_t newPosition)
{
    std::lock_guard<std::mutex> lock(s_externalChannelsMutex);
    if (s_externalChannels && index >= 0 && index < s_numExternalChannels)
    {
        s_externalChannels[index].positionValue = newPosition;
    }
}

// External music registration for eMusicTrack
void RegisterExternalMusic(eExternalMusicData* music)
{
    std::lock_guard<std::mutex> lock(s_externalMusicMutex);
    s_externalMusic = music;
    // Mark decoder as valid if music has a decoder
    s_externalMusicDecoderValid.store(music && music->vorbisDecoder != nullptr, std::memory_order_release);
}

void UpdateExternalMusic(const eExternalMusicData& data)
{
    // If setting decoder to null, first mark as invalid to stop audio thread from using it
    if (data.vorbisDecoder == nullptr)
    {
        s_externalMusicDecoderValid.store(false, std::memory_order_release);
        // Memory barrier ensures audio thread sees this before we update s_externalMusic
        std::atomic_thread_fence(std::memory_order_seq_cst);
    }

    {
        std::lock_guard<std::mutex> lock(s_externalMusicMutex);
        if (s_externalMusic)
        {
            s_externalMusic->vorbisDecoder = data.vorbisDecoder;
            s_externalMusic->playing = data.playing;
            s_externalMusic->paused = data.paused;
            s_externalMusic->volume = data.volume;
        }
    }

    // If setting a valid decoder, mark as valid after updating the struct
    if (data.vorbisDecoder != nullptr)
    {
        s_externalMusicDecoderValid.store(true, std::memory_order_release);
    }
}

// Thread-safe read of external channel data
eExternalChannelData GetExternalChannel(int index)
{
    std::lock_guard<std::mutex> lock(s_externalChannelsMutex);
    if (s_externalChannels && index >= 0 && index < s_numExternalChannels)
    {
        return s_externalChannels[index];
    }
    return eExternalChannelData{};
}

#else // DEDICATED

// Null implementation for dedicated server
#include "eAudioInterface.h"

class eAudioDeviceNull : public eAudioDevice
{
public:
    bool Init(eAudioQuality, float) override { return false; }
    void Shutdown() override {}
    bool IsActive() const override { return false; }
    int GetSampleRate() const override { return 0; }
    std::unique_ptr<eAudioSound> LoadSound(const char*) override { return nullptr; }
    int GetNumChannels() const override { return 0; }
    eAudioChannel* GetChannel(int) override { return nullptr; }
    int AllocateChannels(int) override { return 0; }
    void SetChannelFinishedCallback(ChannelFinishedCallback) override {}
    std::unique_ptr<eAudioMusic> LoadMusic(const char*) override { return nullptr; }
    void SetMusicFinishedCallback(MusicFinishedCallback) override {}
    void SetMusicVolume(int) override {}
    void SetSoundVolume(int) override {}
    void SetLegacyMixCallback(LegacyMixCallback, void*) override {}
};

std::unique_ptr<eAudioDevice> CreateAudioDevice()
{
    return std::make_unique<eAudioDeviceNull>();
}

// Global audio device accessor (null for dedicated server)
static eAudioDevice* s_globalAudioDevice = nullptr;

eAudioDevice* GetGlobalAudioDevice()
{
    return s_globalAudioDevice;
}

void SetGlobalAudioDevice(eAudioDevice* device)
{
    s_globalAudioDevice = device;
}

// Stub implementations for external channel registration
void RegisterExternalChannels(eExternalChannelData*, int) {}
void UpdateExternalChannel(int, const eExternalChannelData&) {}
void UpdateExternalChannelPosition(int, size_t) {}
void RegisterExternalMusic(eExternalMusicData*) {}
void UpdateExternalMusic(const eExternalMusicData&) {}
eExternalChannelData GetExternalChannel(int) { return eExternalChannelData{}; }

#endif // DEDICATED
