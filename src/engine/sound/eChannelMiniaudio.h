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

Miniaudio-compatible channel and wav data types

*/

#ifndef ECHANNELMINIAUDIO_H
#define ECHANNELMINIAUDIO_H

#include "eCoord.h"
#include "tPlayList.h"
#include <memory>
#include <vector>

// Forward declarations
class eGameObject;
class eCamera;

/*******************************************************************************
 *
 * eWavData - Sound effect data (miniaudio version)
 *
 *******************************************************************************/

class eWavData
{
public:
    eWavData();
    ~eWavData();
    eWavData(const char* filename);
    void LoadWavFile(const char* filename);

    void* GetWavData() { return m_samples.empty() ? nullptr : m_samples.data(); }
    size_t GetSampleCount() const { return m_samples.size(); }
    int GetChannels() const { return m_channels; }
    int GetSampleRate() const { return m_sampleRate; }

    void SetVolume(int newVolume) { m_Volume = newVolume; }
    int GetVolume() const { return m_Volume; }

private:
    std::vector<float> m_samples;
    int m_channels;
    int m_sampleRate;
    int m_Volume;
};

/*******************************************************************************
 *
 * eChannel - Mixing channel (miniaudio version)
 *
 *******************************************************************************/

class eChannel
{
public:
    eChannel();

    void SetVolume(int volume);
    int GetId() const { return m_ChannelID; }
    void SetId(int newID) { m_ChannelID = newID; }

    void Set3d(eCoord home, eCoord soundPos, eCoord homeDirection);
    void Set3d(eCamera const& camera, eGameObject const& soundOrigin, REAL volume);
    void PlaySound(eWavData& sound);
    void LoopSound(eWavData& sound);
    void StopSound();

    void UnplaySound();

    unsigned int StartTime() const { return m_StartTime; }
    bool IsContinuous() const { return m_continuous; }

    void Update();

    bool isDirty() const { return m_isDirty; }
    bool isBusy() const { return m_isPlaying; }

    void SetOwner(eGameObject* owner);
    void DelayStarting() { m_Delayed = true; m_StartNow = false; }
    bool IsDelayed() const { return m_Delayed; }
    void Undelay() { m_StartNow = true; }
    void SetHome(eGameObject* home) { m_Home = home; }
    eGameObject* GetOwner() { return m_Owner; }

    static int numChannels;

    // Mixing state - accessible by audio device
    eWavData* m_Sound;
    size_t m_Position;
    float m_LeftVol;
    float m_RightVol;
    float m_Pitch;  // Pitch multiplier (1.0 = normal)

    void SetPitch(float pitch) { m_Pitch = pitch; }

    // Initialize channels after all are created (called from eSoundMixer)
    friend void eChannel_InitializeChannels(eChannel* channels, int numChannels);

private:
    bool m_Delayed;
    bool m_StartNow;
    int m_Volume;
    int m_ChannelID;
    unsigned int m_StartTime;
    bool m_isDirty;
    bool m_isPlaying;
    eGameObject* m_Owner;
    eGameObject* m_Home;
    bool m_continuous;
};

// Initialize channels after all are created (called from eSoundMixer::Init)
#include <deque>
void eChannel_InitializeChannels(std::deque<eChannel>& channels);

/*******************************************************************************
 *
 * eMusicTrack - Music track (miniaudio version)
 *
 *******************************************************************************/

class eSoundMixer;

class eMusicTrack
{
public:
    eMusicTrack();
    eMusicTrack(const char* filename, bool isinstalled = false);
    ~eMusicTrack();

    void SetVolume(int newVolume) { m_Volume = newVolume; }
    int GetVolume() const { return m_Volume; }

    void Loop() { m_Loop = true; }

    tString const& GetFileName() const { return m_Filename; }

    bool Play();
    void Stop();
    void Next();
    void Previous();
    void Pause();
    void VolumeUp();
    void VolumeDown();
    void Mute();

    void Update();
    void SetDirty();

    bool LoadSong(tSong thesong);
    void LoadPlaylist(const char* filename);

    void SetPlaylist(int i) { usePlaylist = i; if (m_Playlist) m_Playlist->SetPlaylist(i); }

    void FadeOut();
    bool IsPlaying() const { return m_IsPlaying; }
    bool HasSong() const { return m_HasSong; }
    void UnloadSong();

    static bool musicIsPlaying;
    void MusicFinished();
    static eMusicTrack* currentMusic;

private:
    void Init(bool isinstalled);
    void PlayCurrentSequence();

    bool m_IsInstalled;
    int m_Volume;
    bool m_Loop;
    bool m_IsPlaying;
    int usePlaylist;
    bool m_isDirty;
    bool m_HasSong;
    bool m_SequenceChange;
    double m_Pos;
    unsigned int m_StartTime;
    tSong m_CurrentSong;
    eSoundMixer& m_mixer;
    tString m_Filename;
    tPlayList* m_Playlist;

    // Miniaudio-specific
    void* m_vorbisDecoder;
    std::vector<float> m_buffer;
    int m_channels;
    int m_sampleRate;
};

#endif // ECHANNELMINIAUDIO_H
