// headphone-girl build: device-side agent tools (SD card, profiles, face
// cache, dances, face GIFs). All file access is confined to /sd/agent.
#pragma once

#include <functional>
#include <string>
#include <vector>

class Display;
class Camera;
class CoreS3Servo;

namespace hg_agent {
// Call once after the display, camera and servo are created.
void RegisterTools(Display* display, Camera* camera, CoreS3Servo* servo);
// Park the SD card chip-select high so the card never answers on the
// shared LCD bus (GPIO35 is both LCD D/C and SD MISO on CoreS3).
void ParkSdChipSelect();

// ── v3: used by the Settings screen and the board ──
std::string SdStatusText();
std::vector<std::string> ListMusic(int max = 20);   // /sd/music/*.mp3
bool PlaySdMusic(const std::string& name, std::string* why);   // "" = random
void StopSdMusic();
bool SdMusicPlaying();
std::string SdMusicNow();
std::vector<std::string> PresetNames();   // wiggle nod disco robot sway
bool PlayPreset(const std::string& name, int loops, std::string* why);
bool StartRecording(std::string* name_out, std::string* why);   // joystick dance
bool StopRecording(std::string* path_out, std::string* why);
bool IsRecording();
bool IsDancing();     // preset, saved dance or groove running (idle motion pauses)
std::vector<std::string> ListDances();
bool PlaySavedDance(const std::string& name, std::string* why);
void StopDance();
bool GrooveEnabled();
void SetGrooveEnabled(bool on);
bool ArmGroove(int max_seconds);
// v3.5: party face. Called with true when music or a dance starts and false
// when everything has stopped (from the hg tasks, never from a timer).
void SetPartyHook(std::function<void(bool)> hook);
bool IsParty();
// v3.5 head swipes. Forward: next SD song, next server song, next dance, or
// a wiggle when nothing plays. Back: stop the music or dance.
void SwipeNext();
void SwipeStop();
// v3.6.7-dj: previous song (Mac songs: asks the server for "previous song";
// nothing on SD songs). MusicPlaying: an SD song or a server song is playing now.
void SwipePrev();
bool MusicPlaying();
// v3.6.7-dj DJ mode. Enabled = saved setting (Settings > Music, voice "DJ mode",
// tool self.dj.mode). Shown = enabled AND a song is playing. The hook gets
// shown on/off changes (from the hg tasks).
bool DjEnabled();
void SetDjEnabled(bool on);
bool DjShown();
void SetDjHook(std::function<void(bool)> hook);
void SetDjTrack(const std::string& title);
std::string DjTitle();
// VU bars on the DJ screen (tool self.dj.vu): the display's SetDjVu.
void SetDjVuHook(std::function<bool(const std::string&, int)> hook);
// v3.6.7-dj4 rave mode (session only). The hook is the display's SetRave.
bool DjRave();
void SetDjRave(bool on);
void SetDjRaveHook(std::function<void(bool)> hook);
bool CameraEnabled();
void SetCameraEnabled(bool on);
}  // namespace hg_agent
