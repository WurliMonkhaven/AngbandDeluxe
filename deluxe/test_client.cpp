// Headless checks: no native window, GPU or computer-control automation.
#include "client.cpp"
#include "imgui_internal.h"
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
  view_state["native_prompt"]=true;
  check(dungeon_view(view_state),"Native confirmations must retain the dungeon view");
  view_state["phase"]="birth";
  check(!dungeon_view(view_state),"Birth must retain terminal presentation behind native prompts");
  view_state["phase"]="playing"; view_state.erase("native_prompt");
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
  check(!ui.quick_targeting,"Quick targeting defaults off");
  check(!ui.quickbar_enabled,"Quickbar defaults off");
  ui.draft_quickbar_enabled=true;
  check(!ui.quickbar_enabled,"Quickbar draft must not apply");
  ui.draft_quick_targeting=true;
  check(!ui.quick_targeting,"Quick targeting draft must not apply immediately");
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
  check(!ui.draft_quick_targeting,"Cancel retained quick targeting draft");
  check(!ui.draft_quickbar_enabled,"Cancel retained quickbar draft");
  check(ui.draft_low_animation && ui.draft_death_animation,"Cancelled animation draft retained");
  check(ui.draft_scale==1.25f && ui.draft_crt==0 && !ui.draft_fullscreen,"Draft was retained");
  check(ui.draft_crt_strength==1 && ui.crt_strength==1,"Cancelled strength was applied");
  check(!ui.draft_crt_settings.parts[Hum].enabled && !ui.crt_settings.parts[Hum].enabled,"Cancelled hum bar was applied");
  ui.draft_scale=1.5f; ui.draft_crt=1; ui.draft_crt_settings.parts[Hum].enabled=true;
  ui.draft_crt_settings.raster_lines=720; ui.draft_crt_settings.mask=2; ui.draft_crt_settings.tube_preset=-1;
  ui.draft_low_animation=false; ui.draft_death_animation=true;
  ui.draft_click_exits_look=true;
  ui.draft_proceed_with_click=true;
  ui.draft_quick_targeting=true;
  ui.draft_quickbar_enabled=true;
  ui.quickbar.profile="test-character"; ui.quickbar.slots()[0]=Quickbar::command_binding({{"id","core.hold"},{"label","Hold"}});
  check(ui.apply_settings(nullptr),"Save settings");
  UI loaded{connection}; loaded.settings_path=path.string(); loaded.load_settings();
  check(loaded.proceed_with_click,"Gameplay option did not persist");
  check(loaded.click_exits_look,"Click exits look did not persist");
  check(loaded.quick_targeting,"Quick targeting did not persist");
  loaded.quickbar.profile="test-character";
  check(loaded.quickbar_enabled && loaded.quickbar.slots()[0]["command"]=="core.hold","Quickbar settings and assignments did not persist");
  loaded.quickbar.profile="other-character"; check(loaded.quickbar.slots()[0].is_null(),"Characters must not share slots");
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
  ImVec2 connector_start,connector_end;
  check(target_connector(ImVec2(0,0),ImVec2(100,0),10,20,connector_start,connector_end) && connector_start.x==5 && connector_end.x==95 && connector_end.y==0,"Horizontal connector must end at box edge");
  check(target_connector(ImVec2(0,0),ImVec2(0,-100),10,20,connector_start,connector_end) && connector_start.y==-10 && connector_end.y==-90,"Vertical connector must respect cell height");
  check(target_connector(ImVec2(0,0),ImVec2(100,100),10,20,connector_start,connector_end) && connector_end.x==95 && connector_end.y==95,"Diagonal connector must intersect the near box edge");
  check(!target_connector(ImVec2(0,0),ImVec2(0,0),10,20,connector_start,connector_end),"Same-cell connector must not divide by zero");
  // Exercise the real store layout headlessly, including empty home, shrinking
  // stock and modal-busy states. No desktop input or native window is used.
  ImGui::CreateContext();
  auto &io=ImGui::GetIO(); io.IniFilename=nullptr; io.DisplaySize=ImVec2(1280,800);
  unsigned char *pixels; int atlas_w,atlas_h;
  io.Fonts->GetTexDataAsRGBA32(&pixels,&atlas_w,&atlas_h); io.Fonts->SetTexID(1);
  {
  ImGui::NewFrame(); ImGui::Begin("Quickbar text layout");
  const float text_size=ImGui::GetFontSize();
  auto roomy=Quickbar::text_layout("Bazinga",ImGui::CalcTextSize("Bazinga").x+8,text_size+1);
  check(roomy.lines.size()==1 && roomy.font_size==text_size,"Custom text must use the full available slot width");
  auto wrapped=Quickbar::text_layout("Magic Missile",ImGui::CalcTextSize("Missile").x+1,text_size*2+1);
  check(wrapped.lines.size()==2 && wrapped.font_size==text_size,"Custom labels must wrap before shrinking");
  ImGui::End(); ImGui::Render();
  Connection hot; hot.connected=true; hot.character_save="quickbar-test";
  json potion={{"id","old-handle"},{"binding_key","potion-kind"},{"label","a Potion"},{"location","Pack"},{"quantity",2},{"actions",json::array({"core.quaff","core.drop"})}};
  hot.state={{"phase","playing"},{"readiness","ready"},{"revision","7"},{"terminal",json::array()},{"items",json::array({potion})}};
  UI hot_ui{hot}; hot_ui.grid_focus=true; hot_ui.quickbar_enabled=true; hot_ui.quickbar.profile=hot.character_save;
  hot_ui.quickbar.slots()[0]=Quickbar::item_binding(potion,"core.quaff");
  auto binding=hot_ui.quickbar.slots()[0];
  check(Quickbar::item_choices(potion) && !Quickbar::spell_choices(potion),"Warrior items must not create a spell menu");
  auto no_actions=potion; no_actions["actions"]=json::array();
  check(!Quickbar::item_choices(no_actions),"Items with empty action lists must not create submenus");
  hot_ui.quickbar.begin_customize(0);
  hot_ui.quickbar.draft_icon=1; SDL_strlcpy(hot_ui.quickbar.draft_text,"Heal",65);
  hot_ui.quickbar.draft_color[0]=.25f;
  check(hot_ui.quickbar.slots()[0]==binding,"Customization draft must not modify binding before Save");
  hot_ui.quickbar.begin_customize(0);
  check(hot_ui.quickbar.draft_icon==0 && hot_ui.quickbar.draft_text[0]==0,"Reopening after Cancel must discard customization");
  hot_ui.quickbar.draft_icon=1; SDL_strlcpy(hot_ui.quickbar.draft_text,"Heal",65); hot_ui.quickbar.draft_color[0]=.25f;
  hot_ui.quickbar.save_customize();
  hot_ui.quickbar.draft_icon=3;
  check(hot_ui.quickbar.appearance_draft()["icon_style"]=="scroll","Icon picker must retain existing saved icon identities");
  hot_ui.quickbar.begin_customize(0);
  Quickbar restored; restored.load(hot_ui.quickbar.profiles); restored.profile=hot.character_save;
  check(restored.slots()[0]["custom_text"]=="Heal" && restored.slots()[0]["custom_color"][0]==.25f,"Custom appearance must survive serialization");
  check(Quickbar::resolve(restored.slots()[0],hot).item=="old-handle","Custom appearance must not change action resolution");

  check(Quickbar::resolve(binding,hot).amount==2,"Potion quantity");
  hot.state["items"]=json::array(); check(!Quickbar::resolve(binding,hot).reason.empty(),"Depleted stack must disable slot");
  potion["id"]="new-handle"; potion["quantity"]=5; hot.state["items"]=json::array({potion});
  check(Quickbar::resolve(binding,hot).item=="new-handle" && Quickbar::resolve(binding,hot).amount==5,"Reacquired stack must resolve current handle");
  potion["location"]="Floor"; hot.state["items"]=json::array({potion});
  check(!Quickbar::resolve(binding,hot).reason.empty(),"Ground item must not substitute carried consumable");
  potion["location"]="Pack"; hot.state["items"]=json::array({potion});
  json book={{"id","book-now"},{"binding_key","book-kind"},{"label","First Spells"},{"location","Pack"},{"book_available",true}};
  json spell={{"id","0"},{"label","Magic Missile"},{"mana",1},{"can_cast",false}};
  check(!Quickbar::spell_choices(book),"Book without spells must not create empty menu");
  auto spell_binding=Quickbar::spell_binding(book,spell);
  book["spells"]=json::array({spell});
  check(Quickbar::spell_choices(book),"Book with spells must allow assignments");
  hot.state["items"].push_back(book);
  check(!Quickbar::resolve(spell_binding,hot).reason.empty(),"Unknown spell must be unavailable");
  hot.state["items"][1]["spells"][0]["can_cast"]=true;
  hot.state["items"][1]["spells"][0]["low_mana"]=true;
  auto cast=Quickbar::resolve(spell_binding,hot);
  check(cast.reason.empty() && cast.item=="book-now" && cast.spell=="0" && cast.mana && cast.amount==1,"Learned low-mana spell must retain engine confirmation path");
  SDL_Event digit{}; digit.type=SDL_EVENT_KEY_DOWN; digit.key.scancode=SDL_SCANCODE_1;
  check(hot_ui.quickbar_event(digit),"Top-row shortcut must be captured");
  check(hot.busy && hot.next==1 && hot.outgoing.find("new-handle")!=std::string::npos,"Shortcut must execute current item once");
  SDL_Event text{}; text.type=SDL_EVENT_TEXT_INPUT; text.text.text="1";
  check(hot_ui.quickbar_event(text),"Shortcut text must not leak into engine prompt");
  hot.prompt={{"type","confirmation"}}; digit.key.repeat=true;
  check(hot_ui.quickbar_event(digit) && hot.next==1,"Held shortcut must not repeat into confirmation");
  hot_ui.quickbar_event(text); digit.type=SDL_EVENT_KEY_UP; hot_ui.quickbar_event(digit);
  digit.type=SDL_EVENT_KEY_DOWN; digit.key.repeat=false;
  check(!hot_ui.quickbar_event(digit),"Prompt numeric input must remain available");
  hot.prompt=json::object(); hot.busy=false;
  digit.key.scancode=SDL_SCANCODE_KP_1; check(!hot_ui.quickbar_event(digit),"Numpad movement must pass through");
  digit.key.scancode=SDL_SCANCODE_1; digit.key.mod=SDL_KMOD_SHIFT;
  check(!hot_ui.quickbar_event(digit),"Modified digits must pass through");
  digit.key.mod=SDL_KMOD_NONE; hot_ui.quickbar_enabled=false;
  check(!hot_ui.quickbar_event(digit),"Disabled quickbar must preserve normal digits");
  hot_ui.quickbar_enabled=true; io.WantTextInput=true;
  check(!hot_ui.quickbar_event(digit),"Text fields must retain digits"); io.WantTextInput=false;
  hot.state["message_pending"]=true; check(!hot_ui.quickbar_event(digit),"More acknowledgement must retain digits"); hot.state["message_pending"]=false;
  const auto rename=hot.send("saves.rename",{{"save",hot.character_save},{"name","Renamed"}});
  hot.receive({{"id",rename},{"result",json::object()}}); hot_ui.quickbar.sync_saves(hot.save_changes);
  check(hot_ui.quickbar.profiles.contains("Renamed") && !hot_ui.quickbar.profiles.contains(hot.character_save),"Renaming a save must preserve its bar");
  const auto failed=hot.send("saves.delete",{{"save","Renamed"}});
  hot.receive({{"id",failed},{"error",{{"message","Test failure"}}}}); hot_ui.quickbar.sync_saves(hot.save_changes);
  check(hot_ui.quickbar.profiles.contains("Renamed"),"Failed deletion must preserve bindings");
  hot_ui.quickbar.profile="Renamed";
  for(float width:{350.f,850.f}) {
   ImGui::NewFrame(); ImGui::SetNextWindowSize(ImVec2(width,150)); ImGui::Begin("Quickbar layout");
   hot_ui.quickbar.draw(hot);
   if(width==350.f) { hot_ui.quickbar.begin_customize(0); hot_ui.quickbar.open_customize=true; }
   hot_ui.quickbar.customize_window(); ImGui::End(); ImGui::Render();
  }
  ImGui::NewFrame(); ImGui::Begin("Quickbar layout");
  if(ImGui::BeginPopupModal("Customize quickbar slot")) { ImGui::CloseCurrentPopup(); ImGui::EndPopup(); }
  ImGui::End(); ImGui::Render();
  }
  {
   Connection creator; creator.connected=true; creator.character_save="BirthTest";
   json choice={{"id",0},{"name","Human"},{"modifiers",json::array({0,0,0,0,0})},{"abilities",json::array()}};
   choice["spellcasting"]=true;
   choice["abilities"]=json::array({{{"name","Spell Choice"},{"description","You may choose your own spells to study."}},{{"name","Combat Regeneration"},{"description","You draw power from combat. Spell points grow as you fight and fade as your blood cools. Casting spells restores some health; the more hurt you are, the greater the benefit."}}});
   json stats=json::array();
   for(int i=0;i<5;++i) stats.push_back({{"id",i},{"name","STR"},{"base",10},{"total","10"},{"can_buy",true}});
   creator.state={{"phase","birth"},{"revision","1"},{"birth",{{"races",json::array({choice})},{"classes",json::array({choice})},{"race",0},{"class",0},{"stats",stats},{"name",""},{"history","Generated history"},{"options",json::array()}}}};
   BirthPanel birth;
   for(int i=0;i<10;++i) {
    birth.step=i%5; if(i) birth.initialized=true;
    creator.busy=i==6; creator.state["birth"]["rolled"]=i>=5;
    ImGui::NewFrame(); ImGui::SetNextWindowSize(ImVec2(i<5?1100.f:620.f,650)); ImGui::Begin("Birth layout test");
    birth.draw(creator); ImGui::End(); ImGui::Render();
   }
   ImGui::NewFrame(); ImGui::SetNextWindowSize(ImVec2(620,600)); ImGui::Begin("Allocation regression");
   BirthPanel::stats(creator,creator.state["birth"],true);
   auto *allocation=ImGui::TableFindByID(ImGui::GetID("Point allocation"));
   check(allocation && allocation->ColumnsCount==7,"Editable allocation must have its own seven-column table");
   check(allocation->Columns[6].WidthGiven>=2*(ImGui::CalcTextSize("+").x+2*ImGui::GetStyle().FramePadding.x)+ImGui::GetStyle().ItemSpacing.x-1,"Adjust column must fit both buttons");
   BirthPanel::stats(creator,creator.state["birth"],false);
   auto *preview_table=ImGui::TableFindByID(ImGui::GetID("Attribute preview"));
   allocation=ImGui::TableFindByID(ImGui::GetID("Point allocation"));
   check(preview_table && preview_table!=allocation && allocation->ColumnsCount==7,"Preview must not overwrite allocation table state");
   ImGui::End(); ImGui::Render();
   check(creator.outgoing.empty(),"Birth layout must not issue commands while browsing");
   creator.busy=false; BirthPanel::act(creator,"buy",{{"choice",1}});
   check(creator.busy && creator.outgoing.find("birth.action")!=std::string::npos && creator.outgoing.find("revision")!=std::string::npos,"Birth action must carry current revision and wait for engine");
   creator.return_to_menu=true; const auto cancel=creator.send("birth.cancel",{{"revision","1"}});
   creator.receive({{"id",cancel},{"result",json::object()}}); creator.process_stopped(0);
   check(creator.restart_ready,"Cancelled birth must return to main menu without backend stopped error");
  }
  {
   Connection feedback; feedback.connected=true; feedback.state={{"context","view-1"},{"readiness","ready"},{"phase","playing"},{"dungeon",{{"level_id","1"}}}};
   feedback.capabilities["interaction.route"]=1;
   feedback.route_sent=SDL_GetTicksNS()-120000001;
   feedback.preview_route(4,5);
   check(!feedback.busy && !feedback.route_request.empty(),"Preview must not block input");
   const auto request=feedback.route_request; const auto count=feedback.next;
   feedback.preview_route(6,7); check(feedback.next==count,"Only one preview may be in flight");
   feedback.busy=true;
   feedback.receive({{"id",request},{"result",{{"context","view-1"},{"x",4},{"y",5},{"reachable",true},{"path",json::array({json::array({4,5})})}}}});
   check(feedback.busy && feedback.route["x"]==4,"Preview reply must not unblock a movement command");
   auto stale=feedback.send("dungeon.route"); feedback.receive({{"id",stale},{"result",{{"context","older"},{"x",9}}}});
   check(feedback.route["x"]==4,"Stale route must be discarded");
   feedback.receive({{"kind","event"},{"event","travel.changed"},{"data",{{"label","Travel cancelled by input"},{"active",false},{"interrupted",true}}}});
   check(feedback.messages.front()["text"]=="[SYSTEM] Travel cancelled by input","Interrupted travel must be recorded in message history");
   feedback.state["message_pending"]=true;
   feedback.previous_messages=json::array({{{"text","The scruffy little dog bites you. LOW HITPOINT WARNING! More messages follow."}}});
   for(int i=0;i<3;++i) {
    ImGui::NewFrame(); ImGui::SetNextWindowSize(ImVec2(i==0?350:900,450)); ImGui::Begin("Dungeon feedback test");
    auto origin=ImGui::GetCursorScreenPos(),area=ImGui::GetContentRegionAvail(); auto *draw=ImGui::GetWindowDrawList();
    DungeonFeedback::route(feedback,draw,origin,area,12,22,0,0,true,4,5);
    DungeonFeedback::ribbon(feedback,draw,origin,area,i==0);
    ImGui::End(); ImGui::Render(); feedback.state["message_pending"]=i==0;
   }
  }
  Connection shop; shop.connected=true;
  shop.state={{"phase","store"},{"context","shop-1"},{"player",{{"gold",100}}},
   {"items",json::array({{{"id","stock-1"},{"label","a Dagger"},{"location","Store"},{"quantity",2},{"description","Weapon description"}},
                         {{"id","gear-1"},{"label","a Sword"},{"location","weapon"},{"quantity",1},{"description","Equipped description"}}})},
   {"store",{{"name","Weapon Smiths"},{"ready",true},{"home",false},
    {"stock",json::array({{{"item_id","stock-1"},{"unit_price",20},{"compare_with",json::array({"gear-1"})}}})},
    {"inventory",json::array({{{"item_id","gear-1"},{"unit_price",30},{"eligible",true}}})}}}};
  shop.state["items"][0]["description_sections"]=json::array({{{"id","combat"},{"title","Combat"},{"text","Combat info: legacy fallback"}}});
  shop.state["items"][0]["combat_details"]=json::array({
   {{"kind","blows"},{"value",270}},{{"kind","damage"},{"value",155}},
   {{"kind","throw_damage"},{"value",76}},{{"kind","range"},{"value",120}},{{"kind","break"},{"value",35}},
   {{"kind","upgrade"},{"str",1},{"dex",0},{"value",330}},{{"kind","upgrade"},{"str",0},{"dex",1},{"value",275}},
   {{"kind","damage_variant"},{"label","Dragons"},{"value",420}},{{"kind","throw_variant"},{"label","Not resistant to fire"},{"value",215}},
   {{"kind","warning"},{"label","Heavy weapon"}}});
  StorePanel panel; panel.last_name="Weapon Smiths"; panel.stock_selection=panel.inventory_selection=0;
  for(int frame=0;frame<4;++frame) {
   if(frame==1) { shop.state["store"]["ready"]=false; shop.prompt={{"type","confirmation"}}; }
   if(frame==2) { shop.state["store"]["home"]=true; shop.state["store"]["stock"]=json::array(); }
   if(frame==3) io.DisplaySize=ImVec2(800,600);
   ImGui::NewFrame(); ImGui::SetNextWindowPos(ImVec2(0,0)); ImGui::SetNextWindowSize(io.DisplaySize);
   ImGui::Begin("Store test"); panel.draw(shop); ImGui::End(); ImGui::Render();
   check(ImGui::GetDrawData()->TotalVtxCount>0,"Store did not produce UI geometry");
  }
  check(shop.outgoing.empty(),"Inspecting store items must never issue a transaction");
  check(panel.stock_selection==-1,"Removed stock must clear an invalid selection");
  Connection magic; magic.connected=true;
  json spell={{"id","spell-0"},{"label","Magic Missile"},{"description","Fires a bolt of magical energy."},
   {"level",1},{"mana",1},{"failure",22},{"status","Untried"},{"can_cast",true},{"can_study",false}};
  magic.state={{"readiness","ready"},{"revision","spell-test"},{"player",{{"new_spells",1}}},
   {"items",json::array({{{"id","book-1"},{"label","First Spells"},{"book_available",true},{"choose_spells",true},{"spells",json::array({spell})}}})}};
  SpellPanel spell_panel;
  for(int frame=0;frame<3;++frame) {
   ImGui::NewFrame(); ImGui::SetNextWindowSize(ImVec2(400,550)); ImGui::Begin("Spell test");
   check(!spell_panel.draw(magic),"Inspecting a spell must not execute it");
   ImGui::End(); ImGui::Render();
  }
  check(magic.outgoing.empty(),"Browsing must send no gameplay commands");
  magic.prompt={{"type","choice"},{"selection_kind","spell"},{"prompt_id","spell-prompt"},{"choices",json::array({spell})}};
  io.AddKeyEvent(ImGuiKey_Escape,true);
  ImGui::NewFrame(); ImGui::Begin("Spell prompt test");
  check(spell_panel.prompt(magic,true),"Escape must cancel a native spell selection");
  ImGui::End(); ImGui::Render();
  const auto cancelled=json::parse(magic.outgoing);
  check(cancelled["method"]=="prompt.reply" && cancelled["params"]["value"].is_null(),"Spell cancellation must use the engine prompt");
  io.AddKeyEvent(ImGuiKey_Escape,false);
  json overview={{"name",""},{"race","Elf"},{"class","Mage"},{"title","Novice"},{"level",1},
   {"hp",9},{"max_hp",9},{"sp",0},{"max_sp",2},{"food",8989},{"food_max",10000},
   {"experience",4},{"level_start_experience",0},{"next_level_experience",12},
   {"stats",{13,48,9,11,13}},{"gold",322},{"floor","open floor"},
   {"tracked_creature",{{"name","aimless-looking merchant"},{"visible",true},{"hp",2},{"max_hp",5}}}};
  for(int frame=0;frame<4;++frame) {
   if(frame==1) { overview["hp"]=-3; overview["feeling"]="9 / ?"; }
   if(frame==2) { overview["name"]="A longer character name"; overview["level"]=50; overview["next_level_experience"]=0; }
   if(frame==3) overview["tracked_creature"]["visible"]=false;
   ImGui::NewFrame(); ImGui::SetNextWindowPos(ImVec2(0,0)); ImGui::SetNextWindowSize(ImVec2(frame%2?320.f:600.f,700));
   ImGui::Begin("Overview test");
   check(!CharacterOverview::draw(overview,frame!=1),"Rendering overview must not open character details");
   ImGui::End(); ImGui::Render();
   check(ImGui::GetDrawData()->TotalVtxCount>0,"Overview did not produce UI geometry");
  }
  overview["character_sheet"]={{"rows",json::array({{{"group","Identity"},{"label","Name"},{"value","Test hero"},{"color",1}}})},
   {"attributes",json::array({{{"label","STR"},{"base","18/20"},{"race",0},{"class",3},{"equipment",0},{"best","18/50"},{"current","18/50"}}})}};
  ui.c.state["player"]=overview; ui.c.outgoing.clear();
  ui.execute("core.character");
  check(ui.open_character_sheet && ui.c.outgoing.empty(),"Character details must open locally without spending a turn");
  for(int frame=0;frame<3;++frame) {
   if(frame==2) io.AddKeyEvent(ImGuiKey_Escape,true);
   ImGui::NewFrame(); ImGui::Begin("Sheet host");
   bool closed=CharacterSheet::draw(overview,frame==0);
   if(frame==2) check(closed,"Escape must close the native character sheet");
   ImGui::End(); ImGui::Render();
  }
  io.AddKeyEvent(ImGuiKey_Escape,false);
  Connection preview; preview.connected=true; preview.capabilities["item.compare"]=1;
  preview.state={{"readiness","ready"},{"revision","1"}};
  const json candidate={{"id","candidate"},{"comparison_available",true}};
  ImGui::NewFrame(); ImGui::Begin("Comparison test");
  ItemComparison::draw(preview,candidate);
  ImGui::End(); ImGui::Render();
  check(preview.requests.size()==1 && !preview.busy,"Comparison must be requested once without blocking input");
  const auto request=preview.requests.begin()->first;
  json option={{"slot_label","Left hand"},{"replaces","Empty slot"},
   {"metrics",json::array({{{"id","armour"},{"label","Armour"},{"before",10},{"after",13},{"delta",3},{"scale",1}},
                          {{"id","STR"},{"label","STR"},{"before",18},{"after",28},{"delta",10},{"scale",0}}})},
   {"changes",json::array({{{"label","Fire"},{"before","Unprotected"},{"after","Resistant"}}})}};
  preview.receive({{"id",request},{"result",{{"item","candidate"},{"revision","1"},{"fully_known",false},{"options",json::array({option,option})}}}});
  for(int frame=0;frame<2;++frame) {
   ImGui::NewFrame(); ImGui::Begin("Comparison test"); ItemComparison::draw(preview,candidate); ImGui::End(); ImGui::Render();
  }
  check(preview.next==1 && !preview.busy,"Cached comparison must not resend or block gameplay");
  ItemRules rules_panel; rules_panel.open=true;
  for(int frame=0;frame<3;++frame) {
   if(frame==1) preview.item_rules={{"revision","1"},{"rules",json::array({{{"id","kind-1-1"},{"type","kind"},{"label","Potions"},{"value","Ignore when known"}}})}};
   if(frame==2) io.AddKeyEvent(ImGuiKey_Escape,true);
   ImGui::NewFrame(); ImGui::Begin("Rules host"); bool closed=rules_panel.draw(preview); ImGui::End(); ImGui::Render();
   if(frame==2) check(closed,"Escape should close rules without mutation");
  }
  for(const auto &request:preview.requests) check(request.second=="item.rules.list","Browsing rules must not mutate item preferences");
  ImGui::DestroyContext();
  fs::remove(path);
  std::cout<<"Session lifecycle, resource bars, graphics settings and CRT input/decay checks passed\n";
  return 0;
 } catch(const std::exception &e) { std::cerr<<e.what()<<'\n'; return 1; }
}
