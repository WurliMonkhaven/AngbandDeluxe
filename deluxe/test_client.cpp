// Headless checks: no native window, GPU or computer-control automation.
#include "client.cpp"
#include <iostream>
#include <stdexcept>
#include <chrono>
static void check(bool value,const char *message) { if(!value) throw std::runtime_error(message); }
int main(int argc,char **argv) {
 try {
  check(argc==2,"Pass an unused settings-file path");
  const fs::path path=argv[1];
  check(!fs::exists(path) && !fs::exists(path.string()+".tmp"),"Test file already exists");
  Connection connection; UI ui{connection}; ui.settings_path=path.string();
  // Legacy preferences acquire sensible defaults.
  { std::ofstream out(path); out<<R"({"scale":1.25,"game_fraction":0.65})"; }
  ui.load_settings(); check(ui.scale==1.25f && !ui.fullscreen && ui.crt==0,"Legacy settings");
  check(ui.crt_strength==1,"Existing settings should default to Classic");
  ui.begin_settings(); ui.draft_scale=1.5f; ui.draft_crt=2; ui.draft_fullscreen=true;
  ui.draft_crt_strength=3; ui.draft_crt_settings.parts[Hum].enabled=true;
  check(!ui.crt_settings.parts[Hum].enabled,"Hum bar draft applied immediately");
  check(ui.scale==1.25f && ui.crt==0 && !ui.fullscreen,"Draft changed live settings");
  ui.begin_settings(); // Reopening after Cancel discards the draft.
  check(ui.draft_scale==1.25f && ui.draft_crt==0 && !ui.draft_fullscreen,"Draft was retained");
  check(ui.draft_crt_strength==1 && ui.crt_strength==1,"Cancelled strength was applied");
  check(!ui.draft_crt_settings.parts[Hum].enabled && !ui.crt_settings.parts[Hum].enabled,"Cancelled hum bar was applied");
  ui.draft_scale=1.5f; ui.draft_crt=1; ui.draft_crt_settings.parts[Hum].enabled=true;
  check(ui.apply_settings(nullptr),"Save settings");
  UI loaded{connection}; loaded.settings_path=path.string(); loaded.load_settings();
  check(loaded.scale==1.5f && loaded.crt==1 && !loaded.fullscreen,"Saved values");
  check(loaded.crt_settings.parts[Hum].enabled,"Hum bar did not persist");
  loaded.begin_settings(); loaded.draft_crt=2; loaded.draft_crt_settings.parts[Hum].enabled=false;
  check(loaded.apply_settings(nullptr),"Replace settings file");
  ui.load_settings(); check(!ui.crt_settings.parts[Hum].enabled,"Hum bar off did not persist"); check(ui.crt==2,"Full CRT persistence");
  for(int strength=0;strength<4;++strength) {
   loaded.begin_settings(); loaded.draft_crt_strength=strength; loaded.draft_crt_settings=CrtSettings(strength);
   check(loaded.apply_settings(nullptr),"Save CRT strength");
   ui.load_settings(); check(ui.crt_strength==strength,"Strength did not persist");
  }
  loaded.begin_settings(); loaded.draft_crt_strength=-1;
  for(int i=0;i<CrtPartCount;++i) loaded.draft_crt_settings.parts[i]={i%2==0,13.f+i*7.f};
  check(loaded.apply_settings(nullptr),"Save custom components"); ui.load_settings();
  check(ui.crt_strength==-1 && ui.crt_settings.serialize()==loaded.crt_settings.serialize(),"Custom components did not persist");
  ui.begin_settings(); ui.draft_crt_settings=CrtSettings(3); ui.begin_settings();
  check(ui.draft_crt_settings.serialize()==ui.crt_settings.serialize(),"Cancel retained component changes");
  loaded.settings_path=(path/"missing"/"settings.json").string();
  loaded.begin_settings(); loaded.draft_scale=.75f;
  check(!loaded.apply_settings(nullptr) && loaded.scale==1.5f,"Failed save changed active settings");
  check(crt_hum_trail(-.0001f)==0 && crt_hum_trail(0)==1,"Hum leading edge is not sharp");
  check(crt_hum_trail(.02f)>crt_hum_trail(.1f) && crt_hum_trail(.24f)==0,"Hum trailing edge does not fade");
  for(int strength=0;strength<4;++strength) {
   CrtSettings preset(strength);
   CrtCurve curve(ImVec2(20,30),ImVec2(400,300),preset);
   for(int y=0;y<=10;++y) for(int x=0;x<=10;++x) {
    const ImVec2 p(20+x*40.f,30+y*30.f); const auto inverse=curve.map(curve.map(p),true);
    check(std::abs(inverse.x-p.x)<.001f && std::abs(inverse.y-p.y)<.001f,"Curved mouse target mismatch");
   }
   check(preset.level(Glow)>0 && preset.level(Bloom)>0,"Preset missing effects");
  }
  for(float strength:{.1f,.5f,1.f}) {
   const float frame=crt_persistence_alpha(strength,1./60.);
   check(std::abs(frame*frame-crt_persistence_alpha(strength,1./30.))<.00001f,"Persistence depends on frame rate");
   check(crt_persistence_alpha(strength,.1)<.04f,"Afterimage does not fade quickly");
  }
  check(crt_persistence_alpha(0,.016)==0 && crt_persistence_alpha(1,.31)==0,"Disabled or stale history survives");
  fs::remove(path);
  std::cout<<"Graphics settings and CRT input/decay checks passed\n";
  return 0;
 } catch(const std::exception &e) { std::cerr<<e.what()<<'\n'; return 1; }
}
