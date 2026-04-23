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

*/

#ifndef EAUDIOINTERFACE_H
#define EAUDIOINTERFACE_H

#include "tString.h"
#include <memory>
#include <functional>
#include <mutex>

// Forward declarations
class eGameObject;
class eCamera;
class eCoord;

// Audio quality levels
enum eAudioQuality
{
    AUDIO_QUALITY_OFF = 0,
    AUDIO_QUALITY_LOW = 1,
    AUDIO_QUALITY_MEDIUM = 2,
    AUDIO_QUALITY_HIGH = 3
};

// Opaque handle types for audio resources
typedef void* eAudioSoundHandle;
typedef void* eAudioMusicHandle;

/*******************************************************************************
 *
 * eAudioSound - Represents a loaded sound effect
 *
 *******************************************************************************/

class eAudioSound
{
public:
    virtual ~eAudioSound() = default;

    virtual bool IsValid() const = 0;
    virtual void SetVolume(int volume) = 0;
    virtual int GetVolume() const = 0;
};

/*******************************************************************************
 *
 * eAudioChannel - Represents a mixing channel for sound effects
 *
 *******************************************************************************/

class eAudioChannel
{
public:
    virtual ~eAudioChannel() = default;

    virtual int GetId() const = 0;
    virtual void SetVolume(int volume) = 0;

    // Play a sound once
    virtual void PlaySound(eAudioSound& sound) = 0;

    // Loop a sound continuously
    virtual void LoopSound(eAudioSound& sound) = 0;

    // Stop playing
    virtual void StopSound() = 0;

    // Set 3D position (bearing 0-360 degrees, distance 0-255)
    virtual void SetPosition(int bearing, int distance) = 0;

    // Set stereo panning (left 0-255, right 0-255)
    virtual void SetPanning(int left, int right) = 0;

    // Clear any effects
    virtual void ClearEffects() = 0;

    // Check if channel is playing
    virtual bool IsPlaying() const = 0;

    // Get start time (for finding oldest channel)
    virtual unsigned int GetStartTime() const = 0;
};

/*******************************************************************************
 *
 * eAudioMusic - Represents a music track
 *
 *******************************************************************************/

class eAudioMusic
{
public:
    virtual ~eAudioMusic() = default;

    virtual bool IsValid() const = 0;
    virtual bool Play(int loops = 0) = 0;  // loops: -1 = infinite, 0 = play once
    virtual void Stop() = 0;
    virtual void Pause() = 0;
    virtual void Resume() = 0;
    virtual void FadeOut(int ms) = 0;
    virtual void SetVolume(int volume) = 0;
    virtual bool IsPlaying() const = 0;
};

/*******************************************************************************
 *
 * eAudioDevice - Main audio device interface
 *
 *******************************************************************************/

class eAudioDevice
{
public:
    virtual ~eAudioDevice() = default;

    // Initialize the audio system
    virtual bool Init(eAudioQuality quality, float bufferSize) = 0;

    // Shutdown the audio system
    virtual void Shutdown() = 0;

    // Check if initialized successfully
    virtual bool IsActive() const = 0;

    // Get the sample rate
    virtual int GetSampleRate() const = 0;

    // Sound effect management
    virtual std::unique_ptr<eAudioSound> LoadSound(const char* filename) = 0;

    // Channel management
    virtual int GetNumChannels() const = 0;
    virtual eAudioChannel* GetChannel(int index) = 0;
    virtual int AllocateChannels(int numChannels) = 0;

    // Set channel finished callback
    using ChannelFinishedCallback = std::function<void(int channel)>;
    virtual void SetChannelFinishedCallback(ChannelFinishedCallback callback) = 0;

    // Music management
    virtual std::unique_ptr<eAudioMusic> LoadMusic(const char* filename) = 0;

    // Set music finished callback
    using MusicFinishedCallback = std::function<void()>;
    virtual void SetMusicFinishedCallback(MusicFinishedCallback callback) = 0;

    // Set global music volume (0-128)
    virtual void SetMusicVolume(int volume) = 0;

    // Set global sound effect volume (0-128)
    virtual void SetSoundVolume(int volume) = 0;

    // Legacy audio mixing callback (for eSound.cpp compatibility)
    using LegacyMixCallback = void(*)(void* udata, signed short* stream, int len);
    virtual void SetLegacyMixCallback(LegacyMixCallback callback, void* udata) = 0;
};

/*******************************************************************************
 *
 * External channel registration for mixing
 *
 *******************************************************************************/

// Structure for external channel mixing data
// Thread safety: All fields are value copies, not pointers to external data.
// The audio thread reads these under lock and operates on local copies.
struct eExternalChannelData
{
    const float* samples;       // Pointer to sample data (owned by eWavData, lifetime managed by game thread)
    size_t sampleCount;         // Total samples (frames * channels)
    int audioChannels;          // Number of audio channels (1 or 2)
    int sampleRate;             // Sample rate of the sound
    size_t positionValue;       // Current playback position VALUE (in fixed-point 16.16) - not a pointer!
    float leftVol;              // Left volume (0.0 - 1.0)
    float rightVol;             // Right volume (0.0 - 1.0)
    bool playing;               // Is channel playing?
    bool looping;               // Should loop?
    int volume;                 // Sound volume (0-128)
    float pitch;                // Pitch multiplier (1.0 = normal, 2.0 = octave up)
};

// Register external channels for mixing
void RegisterExternalChannels(eExternalChannelData* channels, int numChannels);
void UpdateExternalChannel(int index, const eExternalChannelData& data);

// Update just the position value (for game thread to sync position if needed)
void UpdateExternalChannelPosition(int index, size_t newPosition);

// External music registration for eMusicTrack
struct eExternalMusicData
{
    void* vorbisDecoder;        // stb_vorbis pointer
    bool playing;
    bool paused;
    int volume;
};
void RegisterExternalMusic(eExternalMusicData* music);
void UpdateExternalMusic(const eExternalMusicData& data);

// Thread-safe read of external channel data
eExternalChannelData GetExternalChannel(int index);

/*******************************************************************************
 *
 * Factory function to create the audio device
 *
 *******************************************************************************/

// Create the platform-specific audio device
std::unique_ptr<eAudioDevice> CreateAudioDevice();

// Get the global audio device instance (set by eSoundMixer)
eAudioDevice* GetGlobalAudioDevice();
void SetGlobalAudioDevice(eAudioDevice* device);

#endif // EAUDIOINTERFACE_H
