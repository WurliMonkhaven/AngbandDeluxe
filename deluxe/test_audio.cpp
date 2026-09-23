#include "audio_player.h"
#include <iostream>
#include <stdexcept>
static void check(bool ok,const char *message) { if(!ok) throw std::runtime_error(message); }
int main(int argc,char **argv) {
 try {
  check(argc==2,"Pass the staged audio directory");
  SDL_SetHint(SDL_HINT_AUDIO_DRIVER,"dummy");
  AudioPlayer player;
  check(player.open(argv[1]),player.error.c_str());
  AudioSettings options;
  player.configure(options,true);
  check(player.play("potion",1000000000),"Preloaded potion did not queue");
  check(!player.play("potion",1000000001),"Burst duplicates must be suppressed");
  check(!player.play("unknown"),"Unknown cues should be silent");
  player.clear();
  for(const char *cue:{"ui","target","melee","spell","crt"}) {
   check(player.play(cue,2000000000),"A mapped cue failed to queue"); player.clear();
  }
  options.enabled=false; player.configure(options,true);
  check(!player.play("potion",3000000000),"Disabled audio must not queue");
  options.enabled=true; player.configure(options,false);
  check(!player.play("potion",3000000000),"Unfocused audio must not queue");
  options.gameplay=0; player.configure(options,true);
  check(!player.play("potion",3000000000),"Gameplay mute must be independent");
  check(player.play("ui",3000000000),"Gameplay mute must preserve interface cues");
  player.clear(); options.gameplay=1; options.interface_volume=0; player.configure(options,true);
  check(!player.play("ui",4000000000),"Interface mute must be independent");
  check(player.play("potion",4000000000),"Interface mute must preserve gameplay");
  check(AudioPlayer::engine_cue("hitpoint_warn").empty(),"Health warning is not a melee hit");
  check(AudioPlayer::engine_cue("miss").empty(),"Misses must not play impact sounds");
  check(AudioPlayer::engine_cue("spell")=="spell","Successful spell mapping");
  check(AudioPlayer::engine_cue("target_confirmed")=="target","Confirmed target mapping");
  player.close();
  check(!player.open(std::filesystem::path(argv[1])/"missing"),"Missing pack should fail gracefully");
  check(!player.play("ui"),"Failed device/pack must remain safe to use");
  player.close(); SDL_Quit();
  std::cout<<"Audio loading, playback, cooldown, focus and independent volume checks passed\n";
  return 0;
 } catch(const std::exception &e) { std::cerr<<e.what()<<'\n'; return 1; }
}
