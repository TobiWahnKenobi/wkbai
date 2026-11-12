// wkbai - the original's intro video (docs/engine/front-end.md)
// Licensed under the GNU General Public License 3
//
// Decoded with Windows' Media Foundation (a part of the operating system): the source reader
// gives the picture as 32-bit BGR frames and the sound as 16-bit PCM; the frames go to a
// texture drawn letterboxed, the sound to an OpenAL stream (SoundPlayer::streamQueue). Both
// follow the clock from the first frame, so they stay together.

#include "IntroVideo.h"
#include "../gfx/renderer.h"
#include "../gfx/bitmap.h"
#include "../window.h"
#include "../imguiimpl.h"
#include "../imgui/imgui.h"
#include "../settings.h"
#include "../SoundPlayer.h"
#include "../util/util.h"
#include "../file.h"
#include <algorithm>
#include <nlohmann/json.hpp>
#include <SDL3/SDL_timer.h>
#include <SDL3/SDL_scancode.h>
#include <cstdio>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>

extern std::filesystem::path g_gamePath;

namespace {
	template <class T> void Release(T*& p) { if (p) { p->Release(); p = nullptr; } }

	struct IntroDecoder {
		IMFSourceReader* reader = nullptr;
		int width = 0, height = 0;
		int stride = 0;
		bool hasAudio = false, videoDone = false, audioDone = false;
		int audioRate = 0, audioChannels = 0;

		bool open(const std::filesystem::path& file) {
			IMFAttributes* attrs = nullptr;
			if (FAILED(MFCreateAttributes(&attrs, 1)))
				return false;
			// the source reader converts the picture to RGB32 itself
			attrs->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
			const HRESULT hr = MFCreateSourceReaderFromURL(file.wstring().c_str(), attrs, &reader);
			Release(attrs);
			if (FAILED(hr) || !reader)
				return false;
			reader->SetStreamSelection((DWORD)MF_SOURCE_READER_ALL_STREAMS, FALSE);
			// the picture
			IMFMediaType* type = nullptr;
			MFCreateMediaType(&type);
			type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
			type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
			HRESULT v = reader->SetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, type);
			Release(type);
			if (FAILED(v))
				return false;
			reader->SetStreamSelection((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, TRUE);
			IMFMediaType* current = nullptr;
			if (FAILED(reader->GetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, &current)))
				return false;
			UINT32 w = 0, h = 0;
			MFGetAttributeSize(current, MF_MT_FRAME_SIZE, &w, &h);
			INT32 s = 0;
			if (FAILED(current->GetUINT32(MF_MT_DEFAULT_STRIDE, (UINT32*)&s)))
				s = (INT32)w * 4;
			Release(current);
			width = (int)w;
			height = (int)h;
			stride = s;
			if (width <= 0 || height <= 0)
				return false;
			// the sound, as 16-bit PCM (the video plays without it if there is none)
			MFCreateMediaType(&type);
			type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
			type->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
			type->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
			if (SUCCEEDED(reader->SetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, type))) {
				reader->SetStreamSelection((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, TRUE);
				if (SUCCEEDED(reader->GetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, &current))) {
					audioRate = (int)MFGetAttributeUINT32(current, MF_MT_AUDIO_SAMPLES_PER_SECOND, 0);
					audioChannels = (int)MFGetAttributeUINT32(current, MF_MT_AUDIO_NUM_CHANNELS, 0);
					hasAudio = audioRate > 0 && (audioChannels == 1 || audioChannels == 2);
					Release(current);
				}
			}
			Release(type);
			if (!hasAudio) {
				reader->SetStreamSelection((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, FALSE);
				audioDone = true;
			}
			return true;
		}

		// the next sample of a stream: its data and time (seconds); false at its end
		bool read(DWORD stream, std::vector<uint8_t>& data, double& time) {
			DWORD flags = 0;
			LONGLONG ts = 0;
			IMFSample* sample = nullptr;
			if (FAILED(reader->ReadSample(stream, 0, nullptr, &flags, &ts, &sample)))
				return false;
			if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
				Release(sample);
				return false;
			}
			data.clear();
			time = ts / 1e7;
			if (!sample)
				return true; // a gap: nothing this time
			IMFMediaBuffer* buffer = nullptr;
			if (SUCCEEDED(sample->ConvertToContiguousBuffer(&buffer))) {
				BYTE* bytes = nullptr;
				DWORD length = 0;
				if (SUCCEEDED(buffer->Lock(&bytes, nullptr, &length))) {
					data.assign(bytes, bytes + length);
					buffer->Unlock();
				}
				Release(buffer);
			}
			Release(sample);
			return true;
		}

		~IntroDecoder() { Release(reader); }
	};
}

void PlayIntroVideo(IRenderer* gfx, const std::filesystem::path& given)
{
	if (!gfx || !g_settings.value("introVideo", true))
		return;
	const std::filesystem::path file = given.empty() ? g_gamePath / "Warrior Kings Battles FMV.wmv" : given;
	std::error_code ec;
	if (!std::filesystem::is_regular_file(file, ec))
		return;
	// test runs (benchmarks, menu tests, the built-in tests) skip it unless asked for
	if ((g_settings.contains("benchmark") || g_settings.contains("menuTest") || g_settings.value("test", false))
		&& !g_settings.contains("introVideoSeconds"))
		return;
	// (COM may be set up already on this thread, in either model: either way it can be used)
	CoInitializeEx(nullptr, COINIT_MULTITHREADED | COINIT_DISABLE_OLE1DDE);
	if (FAILED(MFStartup(MF_VERSION))) {
		printf("Intro video: Media Foundation unavailable\n");
		return;
	}
	{
		IntroDecoder dec;
		if (!dec.open(file)) {
			printf("Intro video: can't decode %s\n", PathToUtf8(file).c_str());
		}
		else {
			printf("Intro video: %s, %d x %d, sound %d Hz x %d\n", PathToUtf8(file).c_str(), dec.width, dec.height, dec.audioRate, dec.audioChannels);
			SoundPlayer* sound = SoundPlayer::getSoundPlayer();
			const bool withSound = dec.hasAudio && g_settings.value("soundEnabled", true) && sound->streamBegin(dec.audioRate, dec.audioChannels);
			Bitmap bmp;
			bmp.width = dec.width;
			bmp.height = dec.height;
			bmp.format = BMFORMAT_B8G8R8A8;
			bmp.pixels.resize((size_t)dec.width * dec.height * 4);
			texture tex = nullptr;
			std::vector<uint8_t> data;
			double frameTime = 0.0;
			bool haveFrame = false;
			const uint64_t start = SDL_GetTicks();
			bool skipped = false;
			// test runs: "introVideoSeconds" stops it after that many seconds (with a screenshot
			// at the end if "introVideoScreenshot" is a path)
			const double limit = g_settings.value("introVideoSeconds", 1e9);
			while (!g_windowQuit && !(dec.videoDone && (dec.audioDone || !withSound || sound->streamQueuedSeconds() <= 0.0))) {
				HandleWindow();
				if (g_keyPressed[SDL_SCANCODE_ESCAPE] || g_keyPressed[SDL_SCANCODE_SPACE] || g_keyPressed[SDL_SCANCODE_RETURN] || g_mousePressed[1]) {
					skipped = true;
					break;
				}
				const double now = (SDL_GetTicks() - start) / 1000.0;
				if (now > limit)
					break;
				// the sound: half a second ahead
				while (withSound && !dec.audioDone && sound->streamQueuedSeconds() < 0.5) {
					double t;
					if (!dec.read((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, data, t)) {
						dec.audioDone = true;
						break;
					}
					sound->streamQueue(data.data(), data.size());
				}
				// the picture: the latest frame due
				bool newFrame = false;
				while (!dec.videoDone && (!haveFrame || frameTime <= now)) {
					double t;
					if (!dec.read((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, data, t)) {
						dec.videoDone = true;
						break;
					}
					if (data.size() < (size_t)dec.height * std::abs(dec.stride))
						continue;
					// rows top-down (a negative stride: bottom-up), alpha opaque
					for (int y = 0; y < dec.height; y++) {
						const uint8_t* src = dec.stride >= 0 ? &data[(size_t)y * dec.stride] : &data[(size_t)(dec.height - 1 - y) * -dec.stride];
						uint8_t* dst = &bmp.pixels[(size_t)y * dec.width * 4];
						memcpy(dst, src, (size_t)dec.width * 4);
						for (int x = 0; x < dec.width; x++)
							dst[x * 4 + 3] = 255;
					}
					frameTime = t;
					haveFrame = newFrame = true;
					if (t > now)
						break;
				}
				if (newFrame) {
					if (tex)
						gfx->UpdateTexture(tex, bmp);
					else
						tex = gfx->CreateTexture(bmp, 1);
				}
				// black, the picture letterboxed at its aspect
				ImGuiImpl_NewFrame();
				ImDrawList* dl = ImGui::GetBackgroundDrawList();
				const float W = (float)g_windowWidth, H = (float)g_windowHeight;
				dl->AddRectFilled(ImVec2(0, 0), ImVec2(W, H), IM_COL32(0, 0, 0, 255));
				if (tex) {
					const float k = std::min(W / dec.width, H / dec.height);
					const float w = dec.width * k, h = dec.height * k;
					dl->AddImage((ImTextureID)tex, ImVec2((W - w) / 2, (H - h) / 2), ImVec2((W + w) / 2, (H + h) / 2));
				}
				gfx->ClearFrame();
				gfx->BeginDrawing();
				gfx->InitImGuiDrawing();
				ImGuiImpl_Render(gfx);
				const std::string shot = g_settings.value("introVideoScreenshot", std::string());
				if (!shot.empty() && now + 0.05 > limit)
					gfx->SaveScreenshot(shot.c_str());
				gfx->EndDrawing();
			}
			printf("Intro video: %s after %.1f s\n", skipped ? "skipped" : "ended", (SDL_GetTicks() - start) / 1000.0);
			if (withSound)
				sound->streamEnd();
			if (tex)
				gfx->FreeTexture(tex);
		}
	}
	MFShutdown();
}

#else
void PlayIntroVideo(IRenderer*, const std::filesystem::path&) {}
#endif
