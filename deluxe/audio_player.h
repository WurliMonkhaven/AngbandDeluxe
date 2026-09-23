// Preloaded, bounded SDL audio voices. No disk access or synthesis during play.
#pragma once
#include <SDL3/SDL.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <stdexcept>
#include <vector>

struct AudioSettings {
 bool enabled=true;
 float master=.8f, gameplay=.8f, interface_volume=.55f;
 nlohmann::json serialize() const { return {{"enabled",enabled},{"master",master},{"gameplay",gameplay},{"interface",interface_volume}}; }
 void load(const nlohmann::json &j) {
  enabled=j.value("enabled",true);
  master=std::clamp(j.value("master",.8f),0.f,1.f);
  gameplay=std::clamp(j.value("gameplay",.8f),0.f,1.f);
  interface_volume=std::clamp(j.value("interface",.55f),0.f,1.f);
 }
};

class AudioPlayer {
 struct Sample { std::vector<Uint8> bytes; };
 struct Cue { std::vector<Sample> samples; bool interface_cue=false; Uint64 last=0; size_t next=0; };
 struct Voice { SDL_AudioStream *stream=nullptr; bool interface_cue=false; };
 SDL_AudioDeviceID device=0;
 std::array<Voice,4> voices{};
 std::map<std::string,Cue> cues;
 AudioSettings settings;
 bool focused=true;
public:
 std::string error;
 AudioPlayer()=default;
 AudioPlayer(const AudioPlayer&)=delete;
 AudioPlayer &operator=(const AudioPlayer&)=delete;
 ~AudioPlayer() { close(); }
 void clear() { for(auto &v:voices) if(v.stream) SDL_ClearAudioStream(v.stream); }
 void close() {
  for(auto &v:voices) { if(v.stream) SDL_DestroyAudioStream(v.stream); v.stream=nullptr; }
  if(device) SDL_CloseAudioDevice(device);
  device=0; cues.clear();
 }
 bool open(const std::filesystem::path &directory) {
  close(); error.clear();
  if(!SDL_InitSubSystem(SDL_INIT_AUDIO)) { error=SDL_GetError(); return false; }
  SDL_AudioSpec spec{SDL_AUDIO_S16LE,1,48000};
  device=SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,&spec);
  if(!device) { error=SDL_GetError(); return false; }
  for(auto &v:voices) {
   v.stream=SDL_CreateAudioStream(&spec,&spec);
   if(!v.stream || !SDL_BindAudioStream(device,v.stream)) { error=SDL_GetError(); close(); return false; }
  }
  try {
   std::ifstream in(directory/"pack.json"); nlohmann::json pack; in>>pack;
   for(auto it=pack.begin();it!=pack.end();++it) {
    Cue cue; cue.interface_cue=it.value().value("interface",false);
    for(const auto &entry:it.value().at("files")) {
     const auto filename=entry.get<std::string>();
     if(filename.empty() || std::filesystem::path(filename).filename()!=filename) throw std::runtime_error("Invalid audio filename");
     SDL_AudioSpec source{}; Uint8 *bytes=nullptr; Uint32 size=0;
     if(!SDL_LoadWAV((directory/filename).string().c_str(),&source,&bytes,&size)) throw std::runtime_error(SDL_GetError());
     const bool valid=source.format==spec.format && source.channels==1 && source.freq==48000 && size>0 && size<=48000*2*5;
     if(valid) cue.samples.push_back({std::vector<Uint8>(bytes,bytes+size)});
     SDL_free(bytes);
     if(!valid) throw std::runtime_error("Sounds must be 48 kHz mono 16-bit WAV, at most five seconds");
    }
    if(!cue.samples.empty()) cues.emplace(it.key(),std::move(cue));
   }
  } catch(const std::exception &e) { error=e.what(); close(); return false; }
  return true;
 }
 void configure(const AudioSettings &value,bool has_focus) {
  if(focused==has_focus && settings.enabled==value.enabled && settings.master==value.master &&
     settings.gameplay==value.gameplay && settings.interface_volume==value.interface_volume) return;
  settings=value; focused=has_focus;
  if(!focused || !settings.enabled || settings.master==0) clear();
  for(auto &v:voices) if(v.stream) SDL_SetAudioStreamGain(v.stream,gain(v.interface_cue));
 }
 float gain(bool interface_cue) const {
  // Headroom for four overlapping effects from the starter pack (peak <= .7).
  return settings.master*(interface_cue?settings.interface_volume:settings.gameplay)*.34f;
 }
 bool play(const std::string &name,Uint64 now=SDL_GetTicksNS()) {
  if(!device || !focused || !settings.enabled || settings.master<=0) return false;
  auto it=cues.find(name); if(it==cues.end()) return false;
  auto &cue=it->second;
  if(gain(cue.interface_cue)<=0 || (cue.last && now-cue.last<100000000ull)) return false;
  if(name=="crt") clear(); // Power-off takes priority and cuts off lingering effects.
  for(auto &voice:voices) if(SDL_GetAudioStreamQueued(voice.stream)==0 && SDL_GetAudioStreamAvailable(voice.stream)==0) {
   voice.interface_cue=cue.interface_cue;
   SDL_SetAudioStreamGain(voice.stream,gain(cue.interface_cue));
   auto &sample=cue.samples[cue.next%cue.samples.size()];
   if(!SDL_PutAudioStreamData(voice.stream,sample.bytes.data(),int(sample.bytes.size()))) return false;
   SDL_FlushAudioStream(voice.stream);
   cue.last=now; ++cue.next; return true;
  }
  return false; // Drop excess effects rather than creating a delayed backlog.
 }
 static std::string engine_cue(const std::string &event) {
  if(event=="target_confirmed") return "target";
  if(event=="quaff") return "potion";
  if(event=="spell" || event=="pray") return "spell";
  if(event=="hit" || event=="hit_good" || event=="hit_great" || event=="hit_superb" || event=="hit_hi_great" || event=="hit_hi_superb" || event=="mon_hit" || event=="mon_punch" || event=="mon_kick" || event=="mon_claw" || event=="mon_bite" || event=="mon_butt" || event=="mon_crush") return "melee";
  return "";
 }
};
