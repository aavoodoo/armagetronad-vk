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

Miniaudio-compatible channel and wav data implementation

*/

#include "aa_config.h"

#ifdef HAVE_MINIAUDIO
#ifndef DEDICATED

#include "eChannelMiniaudio.h"
#include "eAudioInterface.h"
#include "eGameObject.h"
#include "eCamera.h"
#include "eSoundMixer.h"
#include "tDirectories.h"
#include "tConsole.h"
#include "rSDL.h"

// External channel data storage for audio mixing
static std::vector<eExternalChannelData> s_channelData;
static bool s_channelsRegistered = false;

// stb_vorbis declarations - implementation is in eAudioMiniaudio.cpp
extern "C" {
typedef struct stb_vorbis stb_vorbis;
typedef struct {
    unsigned int sample_rate;
    int channels;
    unsigned int setup_memory_required;
    unsigned int setup_temp_memory_required;
    unsigned int temp_memory_required;
    int max_frame_size;
} stb_vorbis_info;
extern int stb_vorbis_decode_filename(const char *filename, int *channels, int *sample_rate, short **output);
extern stb_vorbis* stb_vorbis_open_filename(const char *filename, int *error, void *alloc_buffer);
extern stb_vorbis_info stb_vorbis_get_info(stb_vorbis *f);
extern void stb_vorbis_close(stb_vorbis *f);
extern int stb_vorbis_seek_start(stb_vorbis *f);
extern int stb_vorbis_get_samples_float_interleaved(stb_vorbis *f, int channels, float *buffer, int num_floats);
}

#include <cmath>
#include <cstring>

// Cycle sound speed settings (defined in gCycle.cpp)
// These control pitch variation for engine sounds
// CYCLE_SOUND_SPEED: reference speed where pitch = 1.0 (default 15)
// CYCLE_SOUND_MACH: doppler effect strength (default 0.1)
extern REAL sg_speedCycleSound;
extern REAL sg_speedCycleSoundMach;

/*******************************************************************************
 *
 * eWavData implementation
 *
 *******************************************************************************/

eWavData::eWavData()
    : m_channels(0), m_sampleRate(0), m_Volume(64)
{
}

eWavData::~eWavData()
{
}

eWavData::eWavData(const char* filename)
    : m_channels(0), m_sampleRate(0), m_Volume(64)
{
    LoadWavFile(filename);
}

void eWavData::LoadWavFile(const char* filename)
{
    if (!filename || !filename[0])
    {
        return;
    }

    // Try loading as OGG/Vorbis
    int channels, sampleRate;
    short* output;
    int numSamples = stb_vorbis_decode_filename(filename, &channels, &sampleRate, &output);

    if (numSamples > 0)
    {
        m_channels = channels;
        m_sampleRate = sampleRate;
        m_samples.resize(numSamples * channels);

        // Convert to float
        for (int i = 0; i < numSamples * channels; i++)
        {
            m_samples[i] = output[i] / 32768.0f;
        }

        free(output);
    }
    else
    {
        con << "Warning: Couldn't load sound file: " << filename << "\n";
    }
}

/*******************************************************************************
 *
 * eChannel implementation
 *
 *******************************************************************************/

int eChannel::numChannels = 0;

eChannel::eChannel()
    : m_Sound(nullptr), m_Position(0), m_LeftVol(1.0f), m_RightVol(1.0f), m_Pitch(1.0f),
      m_Delayed(false), m_StartNow(false), m_Volume(50),
      m_ChannelID(numChannels++), m_StartTime(0),
      m_isDirty(false), m_isPlaying(false),
      m_Owner(nullptr), m_Home(nullptr), m_continuous(false)
{
}

// Initialize channel data and register with audio device
// Called from eSoundMixer after all channels are created
void eChannel_InitializeChannels(std::deque<eChannel>& channels)
{
    int numChannels = static_cast<int>(channels.size());

    // Resize the channel data array
    s_channelData.resize(numChannels);

    // Initialize each channel's data
    // Note: We use positionValue (value copy) instead of position pointer.
    // This avoids the thread safety issue where the deque could be modified
    // while the audio thread holds a pointer to channel data.
    for (int i = 0; i < numChannels; i++)
    {
        eExternalChannelData& data = s_channelData[i];
        data.samples = nullptr;
        data.sampleCount = 0;
        data.audioChannels = 0;
        data.sampleRate = 44100;
        data.positionValue = 0;  // Value copy, not pointer
        data.leftVol = 1.0f;
        data.rightVol = 1.0f;
        data.playing = false;
        data.looping = false;
        data.volume = 64;
        data.pitch = 1.0f;
    }

    // Register with audio device
    if (!s_channelData.empty())
    {
        RegisterExternalChannels(s_channelData.data(), static_cast<int>(s_channelData.size()));
        s_channelsRegistered = true;
    }
}

void eChannel::SetVolume(int volume)
{
    m_Volume = volume;
}

void eChannel::Set3d(eCoord home, eCoord soundPos, eCoord homeDirection)
{
    // Calculate relative sound position
    eCoord soundPosRelative = (soundPos - home).Turn(homeDirection.Conj());

    REAL distance = soundPosRelative.Norm();

    // Calculate bearing in radians
    double bearing = atan2(-soundPosRelative.x, -soundPosRelative.y);

    // Convert to stereo panning
    float pan = static_cast<float>(sin(bearing));

    // Distance attenuation
    const REAL microphoneSize = 3;
    float distFactor = static_cast<float>(microphoneSize / (sqrt(distance) + microphoneSize));

    m_LeftVol = distFactor * (1.0f - pan * 0.5f);
    m_RightVol = distFactor * (1.0f + pan * 0.5f);

    // Update external channel data with mutex protection
    if (m_ChannelID >= 0 && m_ChannelID < static_cast<int>(s_channelData.size()))
    {
        // Thread-safe read then update
        eExternalChannelData data = GetExternalChannel(m_ChannelID);
        data.leftVol = m_LeftVol;
        data.rightVol = m_RightVol;
        UpdateExternalChannel(m_ChannelID, data);
    }
}

void eChannel::Set3d(eCamera const& camera, eGameObject const& soundOrigin, REAL volume)
{
    REAL r, l, doppler;
    camera.GetSoundVolume(soundOrigin, r, l, doppler);
    m_LeftVol = static_cast<float>(l * volume);
    m_RightVol = static_cast<float>(r * volume);

    // Update external channel data with mutex protection
    if (m_ChannelID >= 0 && m_ChannelID < static_cast<int>(s_channelData.size()))
    {
        // Thread-safe read then update
        eExternalChannelData data = GetExternalChannel(m_ChannelID);
        data.leftVol = m_LeftVol;
        data.rightVol = m_RightVol;
        UpdateExternalChannel(m_ChannelID, data);
    }
}

void eChannel::PlaySound(eWavData& sound)
{
    if (m_isPlaying)
    {
        StopSound();
    }

    m_Sound = &sound;
    m_Position = 0;  // Fixed-point 16.16 position
    m_continuous = false;
    m_isPlaying = true;
    m_StartTime = static_cast<unsigned int>(SDL_GetTicks());

    // Update external channel data for mixing (using mutex-protected function)
    if (m_ChannelID >= 0 && m_ChannelID < static_cast<int>(s_channelData.size()))
    {
        eExternalChannelData data;
        data.samples = sound.GetSampleCount() > 0 ? static_cast<const float*>(sound.GetWavData()) : nullptr;
        data.sampleCount = sound.GetSampleCount();
        data.audioChannels = sound.GetChannels();
        data.sampleRate = sound.GetSampleRate();
        data.positionValue = 0;  // Value copy, not pointer
        data.leftVol = m_LeftVol;
        data.rightVol = m_RightVol;
        data.playing = true;
        data.looping = false;
        data.volume = sound.GetVolume();
        data.pitch = m_Pitch;
        UpdateExternalChannel(m_ChannelID, data);
    }
}

void eChannel::LoopSound(eWavData& sound)
{
    if (m_isPlaying)
    {
        StopSound();
    }

    m_Sound = &sound;
    m_Position = 0;  // Fixed-point 16.16 position
    m_continuous = true;
    m_isPlaying = true;
    m_StartTime = static_cast<unsigned int>(SDL_GetTicks());

    // Update external channel data for mixing (using mutex-protected function)
    if (m_ChannelID >= 0 && m_ChannelID < static_cast<int>(s_channelData.size()))
    {
        eExternalChannelData data;
        data.samples = sound.GetSampleCount() > 0 ? static_cast<const float*>(sound.GetWavData()) : nullptr;
        data.sampleCount = sound.GetSampleCount();
        data.audioChannels = sound.GetChannels();
        data.sampleRate = sound.GetSampleRate();
        data.positionValue = 0;  // Value copy, not pointer
        data.leftVol = m_LeftVol;
        data.rightVol = m_RightVol;
        data.playing = true;
        data.looping = true;
        data.volume = sound.GetVolume();
        data.pitch = m_Pitch;
        UpdateExternalChannel(m_ChannelID, data);
    }
}

void eChannel::StopSound()
{
    m_isDirty = true;
    m_isPlaying = false;
    m_continuous = false;

    // Update external channel data with mutex protection
    if (m_ChannelID >= 0 && m_ChannelID < static_cast<int>(s_channelData.size()))
    {
        // Thread-safe read then update
        eExternalChannelData data = GetExternalChannel(m_ChannelID);
        data.playing = false;
        data.looping = false;
        UpdateExternalChannel(m_ChannelID, data);
    }
}

void eChannel::UnplaySound()
{
    m_isDirty = true;
    m_isPlaying = false;
}

void eChannel::SetOwner(eGameObject* owner)
{
    m_Owner = owner;
}

void eChannel::Update()
{
    if (m_isDirty && !m_isPlaying)
    {
        m_LeftVol = 1.0f;
        m_RightVol = 1.0f;
        m_Pitch = 1.0f;
        m_isDirty = false;
    }

    if (m_continuous)
    {
        if (m_Delayed)
        {
            if (m_StartNow)
            {
                m_StartNow = false;
                m_Delayed = false;
                if (m_Sound)
                {
                    m_Position = 0;
                    m_isPlaying = true;
                }
                if (m_Home && m_Owner)
                {
                    Set3d(m_Home->Position(), m_Owner->Position(), m_Home->Direction());
                }
            }
        }
        else
        {
            // If owner is gone, stop the sound
            if (!m_Owner)
            {
                StopSound();
                return;
            }
            // Only apply 3D positioning if we have a home reference
            // Otherwise keep playing with default center panning
            if (m_Home)
            {
                Set3d(m_Home->Position(), m_Owner->Position(), m_Home->Direction());
            }

            // Update pitch based on owner's speed (for engine sound effect)
            // Uses CYCLE_SOUND_SPEED setting (default 15) as reference speed
            // At reference speed, pitch = 1.0; faster = higher pitch, slower = lower pitch
            float speed = static_cast<float>(m_Owner->Speed());
            float referenceSpeed = static_cast<float>(sg_speedCycleSound);
            if (referenceSpeed < 1.0f) referenceSpeed = 1.0f;  // Avoid division by zero
            m_Pitch = speed / referenceSpeed;
            // Clamp pitch to reasonable range (0.5 = half speed, 3.0 = triple speed)
            if (m_Pitch < 0.5f) m_Pitch = 0.5f;
            if (m_Pitch > 3.0f) m_Pitch = 3.0f;

            // Update external channel data with new pitch
            if (m_ChannelID >= 0 && m_ChannelID < static_cast<int>(s_channelData.size()))
            {
                eExternalChannelData data = GetExternalChannel(m_ChannelID);
                data.pitch = m_Pitch;
                UpdateExternalChannel(m_ChannelID, data);
            }
        }
    }
}

/*******************************************************************************
 *
 * eMusicTrack implementation
 *
 *******************************************************************************/

bool eMusicTrack::musicIsPlaying = false;
eMusicTrack* eMusicTrack::currentMusic = nullptr;

// External music data for audio device mixing
static eExternalMusicData s_musicData = {nullptr, false, false, 100};
static bool s_musicRegistered = false;

eMusicTrack::eMusicTrack()
    : m_IsInstalled(false), m_Volume(100), m_Loop(false), m_IsPlaying(false),
      usePlaylist(0), m_isDirty(false), m_HasSong(false), m_SequenceChange(false),
      m_Pos(0), m_StartTime(0), m_mixer(eSoundMixer::GetMixer()),
      m_Playlist(nullptr), m_vorbisDecoder(nullptr), m_channels(0), m_sampleRate(0)
{
    currentMusic = this;
}

eMusicTrack::eMusicTrack(const char* filename, bool isinstalled)
    : m_IsInstalled(isinstalled), m_Volume(100), m_Loop(false), m_IsPlaying(false),
      usePlaylist(0), m_isDirty(false), m_HasSong(false), m_SequenceChange(false),
      m_Pos(0), m_StartTime(0), m_mixer(eSoundMixer::GetMixer()),
      m_Filename(filename), m_Playlist(nullptr), m_vorbisDecoder(nullptr),
      m_channels(0), m_sampleRate(0)
{
    Init(isinstalled);
    currentMusic = this;
}

eMusicTrack::~eMusicTrack()
{
    // Stop playback first to ensure audio thread isn't reading the decoder
    // Thread safety: UpdateExternalMusic will set the atomic decoder-valid flag to false
    // BEFORE clearing the decoder pointer, ensuring the audio thread sees the flag change
    // and stops using the decoder.
    if (m_vorbisDecoder)
    {
        eExternalMusicData data;
        data.vorbisDecoder = nullptr;  // This triggers the atomic flag update in UpdateExternalMusic
        data.playing = false;
        data.paused = false;
        data.volume = 0;
        UpdateExternalMusic(data);

        // Wait briefly for audio thread to complete its current callback
        // At 44100Hz with 512 sample buffer, callbacks are ~11ms apart.
        // Waiting 20ms ensures the audio thread has completed at least one callback
        // and seen the atomic flag change.
        SDL_Delay(20);

        // Now safe to close the decoder
        stb_vorbis_close(static_cast<stb_vorbis*>(m_vorbisDecoder));
    }
    delete m_Playlist;
}

void eMusicTrack::Init(bool isinstalled)
{
    if (m_Filename.Size() > 0)
    {
        int error;
        m_vorbisDecoder = stb_vorbis_open_filename(m_Filename.c_str(), &error, nullptr);
        if (m_vorbisDecoder)
        {
            stb_vorbis_info info = stb_vorbis_get_info(static_cast<stb_vorbis*>(m_vorbisDecoder));
            m_channels = info.channels;
            m_sampleRate = info.sample_rate;
            m_HasSong = true;
        }
    }
}

bool eMusicTrack::Play()
{
    if (!m_vorbisDecoder && m_Filename.Size() > 0)
    {
        int error;
        m_vorbisDecoder = stb_vorbis_open_filename(m_Filename.c_str(), &error, nullptr);
        if (m_vorbisDecoder)
        {
            stb_vorbis_info info = stb_vorbis_get_info(static_cast<stb_vorbis*>(m_vorbisDecoder));
            m_channels = info.channels;
            m_sampleRate = info.sample_rate;
            m_HasSong = true;
        }
    }

    if (m_vorbisDecoder)
    {
        stb_vorbis_seek_start(static_cast<stb_vorbis*>(m_vorbisDecoder));
        m_IsPlaying = true;
        musicIsPlaying = true;
        m_StartTime = static_cast<unsigned int>(SDL_GetTicks());

        // Register with audio device for mixing
        if (!s_musicRegistered)
        {
            RegisterExternalMusic(&s_musicData);
            s_musicRegistered = true;
        }

        // Update with mutex protection
        eExternalMusicData data;
        data.vorbisDecoder = m_vorbisDecoder;
        data.playing = true;
        data.paused = false;
        data.volume = m_Volume;
        UpdateExternalMusic(data);

        return true;
    }
    return false;
}

void eMusicTrack::Stop()
{
    m_IsPlaying = false;
    musicIsPlaying = false;

    // Update with mutex protection
    eExternalMusicData data;
    data.vorbisDecoder = m_vorbisDecoder;
    data.playing = false;
    data.paused = false;
    data.volume = m_Volume;
    UpdateExternalMusic(data);
}

void eMusicTrack::Pause()
{
    m_IsPlaying = !m_IsPlaying;
    musicIsPlaying = m_IsPlaying;

    // Update with mutex protection
    eExternalMusicData data;
    data.vorbisDecoder = m_vorbisDecoder;
    data.playing = m_IsPlaying;
    data.paused = !m_IsPlaying;
    data.volume = m_Volume;
    UpdateExternalMusic(data);
}

void eMusicTrack::VolumeUp()
{
    m_Volume = std::min(128, m_Volume + 8);

    // Update with mutex protection
    eExternalMusicData data;
    data.vorbisDecoder = m_vorbisDecoder;
    data.playing = m_IsPlaying;
    data.paused = !m_IsPlaying;
    data.volume = m_Volume;
    UpdateExternalMusic(data);
}

void eMusicTrack::VolumeDown()
{
    m_Volume = std::max(0, m_Volume - 8);

    // Update with mutex protection
    eExternalMusicData data;
    data.vorbisDecoder = m_vorbisDecoder;
    data.playing = m_IsPlaying;
    data.paused = !m_IsPlaying;
    data.volume = m_Volume;
    UpdateExternalMusic(data);
}

void eMusicTrack::Mute()
{
    m_Volume = 0;

    // Update with mutex protection
    eExternalMusicData data;
    data.vorbisDecoder = m_vorbisDecoder;
    data.playing = m_IsPlaying;
    data.paused = !m_IsPlaying;
    data.volume = 0;
    UpdateExternalMusic(data);
}

void eMusicTrack::FadeOut()
{
    // Simple immediate stop for now
    Stop();
}

void eMusicTrack::Next()
{
    if (m_Playlist && !m_Playlist->empty())
    {
        m_CurrentSong = m_Playlist->GetNextSong();
        LoadSong(m_CurrentSong);
    }
}

void eMusicTrack::Previous()
{
    if (m_Playlist && !m_Playlist->empty())
    {
        m_CurrentSong = m_Playlist->GetPreviousSong();
        LoadSong(m_CurrentSong);
    }
}

bool eMusicTrack::LoadSong(tSong thesong)
{
    UnloadSong();
    m_CurrentSong = thesong;
    m_Filename = thesong.location;

    int error;
    m_vorbisDecoder = stb_vorbis_open_filename(m_Filename.c_str(), &error, nullptr);
    if (m_vorbisDecoder)
    {
        stb_vorbis_info info = stb_vorbis_get_info(static_cast<stb_vorbis*>(m_vorbisDecoder));
        m_channels = info.channels;
        m_sampleRate = info.sample_rate;
        m_HasSong = true;
        return true;
    }
    return false;
}

void eMusicTrack::UnloadSong()
{
    if (m_vorbisDecoder)
    {
        // Stop playback first and signal audio thread
        eExternalMusicData data;
        data.vorbisDecoder = nullptr;  // This triggers the atomic flag update
        data.playing = false;
        data.paused = false;
        data.volume = 0;
        UpdateExternalMusic(data);

        // Wait for audio thread to see the change
        SDL_Delay(20);

        stb_vorbis_close(static_cast<stb_vorbis*>(m_vorbisDecoder));
        m_vorbisDecoder = nullptr;
    }
    m_HasSong = false;
}

void eMusicTrack::LoadPlaylist(const char* filename)
{
    if (!m_Playlist)
    {
        m_Playlist = new tPlayList();
    }
    m_Playlist->LoadPlaylist(filename);
}

void eMusicTrack::Update()
{
    // Nothing needed for now
}

void eMusicTrack::SetDirty()
{
    m_isDirty = true;
}

void eMusicTrack::MusicFinished()
{
    m_IsPlaying = false;
    musicIsPlaying = false;
    eSoundMixer::SongFinished();
}

void eMusicTrack::PlayCurrentSequence()
{
    // Not needed for OGG files
}

#endif // DEDICATED
#endif // HAVE_MINIAUDIO
