// Headless checks: no native window, GPU or computer-control automation.
#include "client.cpp"
#include <iostream>
#include <stdexcept>
#include <chrono>
static void check(bool value,const char *message) { if(!value) throw std::runtime_error(message); }
static BackendReader::Batch read_test_frames(const std::string &wire) {
 auto stream=SDL_IOFromConstMem(wire.data(),wire.size());
 check(stream!=nullptr,"Test stream creation failed");
 BackendReader::Batch result;
 {
  BackendReader reader(stream,nullptr);
  const auto deadline=SDL_GetTicksNS()+2000000000ull;
  do {
   auto batch=reader.take();
   for(auto &frame:batch.frames) result.frames.push_back(std::move(frame));
   result.error+=batch.error; result.finished=batch.finished;
   if(!result.finished) SDL_DelayNS(1000000);
  } while(!result.finished && SDL_GetTicksNS()<deadline);
 }
 SDL_CloseIO(stream);
 check(result.finished,"Background reader did not finish");
 return result;
}
int main(int argc,char **argv) {
 try {
  check(argc==2,"Pass an unused settings-file path");
  auto messages=read_test_frames("{\"seq\":1}\n{\"seq\":2}\n");
  check(messages.error.empty() && messages.frames.size()==2 && messages.frames[0]["seq"]==1 && messages.frames[1]["seq"]==2,"Reader must preserve final message order at EOF");
  check(!read_test_frames("{bad}\n").error.empty(),"Malformed frame must be reported");
  check(!read_test_frames("{\"seq\":1}").error.empty(),"Incomplete final frame must be reported");
  check(!read_test_frames(std::string(1048577,'x')).error.empty(),"Oversized frame must be rejected");
  json view_state={{"phase","playing"},{"readiness","ready"},{"dungeon",{{"width",1},{"height",1},{"cells",json::array({json::array({json::array({46,1,0,0,0,0,64,1,1,0,1,0,1})})})}}}};
  check(dungeon_view(view_state),"Semantic dungeon selection");
  RenderGrid decoded;
  decoded.update(view_state);
  check(decoded.semantic && decoded.width==1 && decoded.height==1 && decoded.cells[0].glyph==64,"Actor must cover terrain");
  view_state["dungeon"]["cells"][0][0][6]=32;
  decoded.update(view_state);
  check(decoded.cells[0].glyph==32,"Opaque blank overlay must hide terrain");
  view_state["dungeon"]["cells"][0][0][6]=0;
  decoded.update(view_state);
  check(decoded.cells[0].glyph==46,"Absent actor must reveal terrain");
  view_state["terminal"]=json::array({json::array({json::array({65,2}),json::array({66,3})})});
  view_state["phase"]="store";
  decoded.update(view_state);
  check(!decoded.semantic && decoded.width==2 && decoded.cells[1].glyph==66,"Fallback must replace cached map");
  decoded.update(json::object());
  check(decoded.cells.empty() && decoded.width==0,"Empty state must clear cached map");
  view_state["phase"]="playing";
  view_state["phase"]="store"; check(!dungeon_view(view_state),"Store must use terminal fallback");
  view_state["phase"]="playing"; view_state["readiness"]="awaiting_prompt";
  check(!dungeon_view(view_state),"Target/prompt must use terminal fallback");
  view_state["targeting"]={{"mode","look"}};
  check(dungeon_view(view_state),"Native targeting keeps the dungeon view");
  view_state.erase("targeting"); view_state["aiming"]=true;
  check(dungeon_view(view_state),"Native aim-direction prompt keeps the dungeon view");
  view_state["aiming"]=false;
  view_state["direction_prompt"]=true;
  check(dungeon_view(view_state),"Movement direction prompts must retain the clickable dungeon");
  view_state["direction_prompt"]=false;
  view_state["item_selection"]=true;
  check(dungeon_view(view_state),"Item selection must retain the dungeon behind the native selector");
  view_state["item_selection"]=false;
  int tile_x=-1,tile_y=-1;
  check(grid_cell_at(15,25,10,20,4,3,tile_x,tile_y) && tile_x==1 && tile_y==1,"Tile hit test must use fitted cell dimensions");
  check(!grid_cell_at(-1,5,10,20,4,3,tile_x,tile_y),"Letterbox must not select a tile");
  check(!grid_cell_at(40,5,10,20,4,3,tile_x,tile_y),"Right edge is outside the map");
  view_state["message_pending"]=true;
  check(dungeon_view(view_state),"Message acknowledgement retains dungeon view");
  view_state["message_pending"]=false;
  view_state["readiness"]="ready"; view_state["dungeon"]["width"]=2;
  check(!dungeon_view(view_state),"Incomplete viewport must use terminal fallback");
  for(int tube=0;tube<3;++tube) {
   CrtSettings preset; preset.tube(tube);
   check(!preset.parts[Hum].enabled && !preset.parts[Interference].enabled,"Realistic tubes should not include signal faults");
   CrtSettings restored; restored.load(preset.serialize());
   check(restored==preset,"Tube layout/raster controls failed to round-trip");
   check(preset.level(Beam)>0 && preset.level(Glass)>0 && preset.level(Focus)>0,"Tube preset is missing optics");
  }
  Connection input; input.connected=true; input.state={{"readiness","ready"},{"context","input-test"}};
  check(input.key("up"),"Ready input should be dispatched");
  const auto queued_input=input.outgoing;
  check(!input.key("down") && input.outgoing==queued_input,"Busy input must stay with the UI until accepted");
  input.busy=false; input.prompt={{"prompt_id","confirm"}};
  check(!input.key("down"),"Game input must not leak into a confirmation");
  input.prompt=json::object();
  check(input.key("down"),"Input should resume after acknowledgement");
  Connection pickup; pickup.connected=true; pickup.state={{"context","pickup-test"}};
  pickup.target("dungeon.pickup",{{"x",4},{"y",5}});
  check(pickup.busy && pickup.pickup_travel,"Pickup travel must be cancellable");
  check(!pickup.key("up"),"Travel must not leak queued movement");
  check(pickup.key("escape") && !pickup.pickup_travel,"Escape must interrupt busy pickup travel");
  check(!pickup.key("escape"),"Only one interruption should be sent before acknowledgement");
  Connection discard; discard.connected=true; discard.capabilities={{"debug.quit",1}};
  discard.quit_without_saving();
  check(discard.busy && discard.close_requested && !discard.closed,"Quit must wait for backend acknowledgement");
  check(discard.requests.at("r1")=="debug.quit","Quit without saving must use the non-saving request");
  discard.receive({{"kind","response"},{"id","r1"},{"result",json::object()}});
  check(discard.closed && discard.close_confirmed && !discard.return_to_menu,"Acknowledged quit must close the app");
  discard.process_stopped(0);
  check(discard.messages.empty() && !discard.restart_ready,"Deliberate quit must not show a backend error or restart");
  HealthGlitch health;
  json health_state={{"phase","playing"},{"player",{{"hp",30},{"hp_warning",30},{"death_pending",false}}}};
  check(health.update(health_state,1)==0,"HP at warning threshold should not glitch");
  health_state["player"]["hp"]=29;
  const float mild=health.update(health_state,2);
  check(health.update(health_state,2,false,true)==0,"Low health animation switch");
  health_state["player"]["hp"]=0;
  check(mild>0 && mild<health.update(health_state,3),"Low HP glitch severity");
  health_state["player"]["hp"]=-10;
  check(health.update(health_state,4)<=.3f,"Bloodlust negative HP must not imply death");
  health_state["player"]["death_pending"]=true;
  check(health.update(health_state,5,true,false)==0,"Death animation switch");
  check(health.update(health_state,5)==1 && health.update(health_state,6)<1,"Fatal burst should settle quickly");
  health_state["phase"]="dead";
  check(health.update(health_state,7)==0,"Tombstone must stop health glitches");
  health_state["phase"]="playing"; health_state["player"]["death_pending"]=false;
  health_state["player"]["hp_warning"]=0;
  check(health.update(health_state,8)==0,"Disabled HP warning must suppress low-health glitches");
  health_state["player"]["hp_warning"]=30; health_state["player"]["hp"]=40;
  check(health.update(health_state,9)==0,"Healing must stop health glitches");
  check(resource_fraction(-10,20)==0 && resource_fraction(0,20)==0,"Dead HP must be empty, not an indeterminate progress bar");
  check(resource_fraction(10,20)==.5f && resource_fraction(30,20)==1,"Resource bar bounds");
  check(resource_fraction(10,0)==0 && resource_fraction(10,-1)==0,"Invalid resource maximum");
  Connection death; death.connected=true;
  auto state_event=[](const char *phase) { return json{{"kind","event"},{"event","state.changed"},
   {"data",{{"phase",phase},{"messages",json::array()},{"terminal",json::object()}}}}; };
  death.receive(state_event("dead"));
  check(death.connected && !death.restart_ready,"Tombstone must remain interactive");
  death.receive(state_event("finished"));
  check(!death.restart_ready,"Wait for engine cleanup before returning to menu");
  death.process_stopped(0);
  check(death.restart_ready && !death.closed && !death.connected && death.messages.empty(),"Normal death should return to launcher without backend-stopped notice");
  Connection crash; crash.receive(state_event("dead")); crash.process_stopped(1);
  check(!crash.restart_ready && !crash.messages.empty(),"Death-screen crash must remain visible");
  Connection incomplete; incomplete.receive(state_event("playing")); incomplete.process_stopped(0);
  check(!incomplete.restart_ready && !incomplete.messages.empty(),"Unannounced exit must remain visible");
  Connection menu; menu.close_confirmed=true; menu.return_to_menu=true; menu.process_stopped(0);
  check(menu.restart_ready && !menu.closed,"Save and return to menu regression");
  Connection quit; quit.close_confirmed=true; quit.closed=true; quit.process_stopped(0);
  check(quit.closed && !quit.restart_ready,"Save and quit must not relaunch");
  const fs::path path=argv[1];
  check(!fs::exists(path) && !fs::exists(path.string()+".tmp"),"Test file already exists");
  Connection connection; UI ui{connection}; ui.settings_path=path.string();
  connection.connected=true; connection.state={{"context","more-test"},{"message_pending",true}};
  check(!ui.proceed_click(),"Click acknowledgement must default off");
  ui.proceed_with_click=true;
  check(ui.proceed_click() && connection.busy,"Enabled click must acknowledge more");
  const auto more_wire=connection.outgoing;
  check(!ui.proceed_click() && connection.outgoing==more_wire,"Busy click must not queue another action");
  connection.busy=false; connection.state["message_pending"]=false;
  check(!ui.proceed_click() && connection.outgoing==more_wire,"Click must not acknowledge other prompts");
  connection.state["message_pending"]=true; connection.prompt={{"prompt_id","test"}};
  check(!ui.proceed_click(),"Structured prompts must not be dismissed by click");
  connection.prompt=json::object();
  // Legacy preferences acquire sensible defaults.
  { std::ofstream out(path); out<<R"({"scale":1.25,"game_fraction":0.65})"; }
  ui.load_settings(); check(ui.scale==1.25f && !ui.fullscreen && ui.crt==0,"Legacy settings");
  check(ui.crt_strength==1,"Existing settings should default to Classic");
  ui.begin_settings(); ui.draft_scale=1.5f; ui.draft_crt=2; ui.draft_fullscreen=true;
  check(!ui.proceed_with_click,"Legacy settings must default click acknowledgement off");
  check(!ui.click_exits_look,"Legacy settings must default click exits look off");
  ui.draft_click_exits_look=true;
  check(!ui.click_exits_look,"Look click draft applied immediately");
  ui.draft_proceed_with_click=true;
  check(!ui.proceed_with_click,"Gameplay draft must not apply immediately");
  ui.draft_low_animation=false; ui.draft_death_animation=false;
  check(ui.low_animation && ui.death_animation,"Animation drafts applied immediately");
  ui.draft_crt_strength=3; ui.draft_crt_settings.parts[Hum].enabled=true;
  check(!ui.crt_settings.parts[Hum].enabled,"Hum bar draft applied immediately");
  check(ui.scale==1.25f && ui.crt==0 && !ui.fullscreen,"Draft changed live settings");
  ui.begin_settings(); // Reopening after Cancel discards the draft.
  check(!ui.draft_proceed_with_click,"Cancelled gameplay draft retained");
  check(!ui.draft_click_exits_look,"Cancelled look click draft retained");
  check(ui.draft_low_animation && ui.draft_death_animation,"Cancelled animation draft retained");
  check(ui.draft_scale==1.25f && ui.draft_crt==0 && !ui.draft_fullscreen,"Draft was retained");
  check(ui.draft_crt_strength==1 && ui.crt_strength==1,"Cancelled strength was applied");
  check(!ui.draft_crt_settings.parts[Hum].enabled && !ui.crt_settings.parts[Hum].enabled,"Cancelled hum bar was applied");
  ui.draft_scale=1.5f; ui.draft_crt=1; ui.draft_crt_settings.parts[Hum].enabled=true;
  ui.draft_crt_settings.raster_lines=720; ui.draft_crt_settings.mask=2; ui.draft_crt_settings.tube_preset=-1;
  ui.draft_low_animation=false; ui.draft_death_animation=true;
  ui.draft_click_exits_look=true;
  ui.draft_proceed_with_click=true;
  check(ui.apply_settings(nullptr),"Save settings");
  UI loaded{connection}; loaded.settings_path=path.string(); loaded.load_settings();
  check(loaded.proceed_with_click,"Gameplay option did not persist");
  check(loaded.click_exits_look,"Click exits look did not persist");
  check(loaded.crt_settings.raster_lines==720 && loaded.crt_settings.mask==2,"Staged raster/mask settings did not persist");
  check(!loaded.low_animation && loaded.death_animation,"Independent animation switches did not persist");
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
  std::cout<<"Session lifecycle, resource bars, graphics settings and CRT input/decay checks passed\n";
  return 0;
 } catch(const std::exception &e) { std::cerr<<e.what()<<'\n'; return 1; }
}
