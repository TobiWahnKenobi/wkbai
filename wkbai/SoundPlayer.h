// wkbre2 - WK Engine Reimplementation
// (C) 2021 AdrienTD
// Licensed under the GNU General Public License 3

#pragma once

#include <memory>
#include <string>

struct Vector3;

struct SoundPlayer {
	virtual ~SoundPlayer() = default;
	virtual void init() = 0;
	virtual void deinit() = 0;
	virtual void playSound(const std::string& filePath) = 0;
	virtual void playSound3D(const std::string& filePath, const Vector3& pos, float refDist, float maxDist) = 0;
	virtual void setListenerPosition(const Vector3& pos, const Vector3& at) = 0;
	virtual void playMusic(const std::string& filePath) = 0;
	virtual bool isMusicPlaying() = 0;
	// STOP_SOUND: the sounds an object played stopped. soundOwner: the object id the next
	// playSound/playSound3D calls belong to (0: none, set back after them)
	uint32_t soundOwner = 0;
	virtual void stopSoundsOf(uint32_t owner) {}
	// Applies the music and effects volumes of g_renderSettings to what is playing
	virtual void applyVolumes() {}
	// A looping sound in one of MAX_LOOPS slots (the waterfalls'), set every frame: that file at
	// that place, its gain (times the effects volume; 0 stops it) and pitch
	static constexpr int MAX_LOOPS = 6;
	virtual void setLoop(int slot, const std::string& filePath, const Vector3& pos, float refDist, float gain, float pitch) {}

	static std::unique_ptr<SoundPlayer> s_soundPlayer;
	static SoundPlayer* getSoundPlayer();
};