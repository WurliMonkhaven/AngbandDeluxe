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
  {
   DungeonCameraSettings defaults; check(!defaults.enabled && defaults.follow,"Camera is opt-in with following enabled");
   defaults.enabled=true; defaults.follow=false; DungeonCameraSettings restored; restored.load(defaults.serialize());
   check(restored.enabled && !restored.follow,"Camera preferences round trip");
   DungeonCamera camera; json view={{"level_id","1"},{"width",198},{"height",66}},player={{"x",40},{"y",20}};
   camera.update(view,player,true);
   check(camera.x==40.5f && camera.y==20.5f,"Initial camera centers the player");
   camera.pan(100,-40,10,20); player["x"]=45; camera.update(view,player,true);
   check(camera.x==30.5f && camera.y==22.5f && camera.paused,"Pan pauses player following");
   camera.return_to_player(player); player["x"]=46; camera.update(view,player,true);
   check(camera.x==46.5f && !camera.paused,"Return to player resumes following");
   const float wx=camera.x+(600-400)/10.f,wy=camera.y+(220-300)/20.f;
   camera.zoom_at(2,600,220,800,600,10,20,false);
   check(std::abs(camera.x+(600-400)/(10*camera.zoom)-wx)<.001f && std::abs(camera.y+(220-300)/(20*camera.zoom)-wy)<.001f,"Zoom anchors the hovered world position");
   camera.pan(1000,1000,10,20); view["level_id"]="2"; camera.update(view,player,true);
   check(camera.x==46.5f && camera.y==20.5f && !camera.paused,"Floor change resets panning");
   player["x"]=55; camera.update(view,player,false); check(camera.x==46.5f,"Fixed camera does not follow walking");
   json state={{"targeting",{{"x",170},{"y",55}}}};
   camera.reveal_target(state,80,30); check(camera.x>=132.5f && camera.y>=42.5f,"Keyboard target is brought into view");
   camera.pan(100,100,10,20); float x=camera.x; camera.reveal_target(state,80,30);
   check(camera.x==x,"Stationary target does not fight manual panning");
   camera.zoom_at(100,0,0,800,600,10,20,true); check(camera.zoom==4,"Maximum zoom is bounded");
   camera.zoom_at(-100,0,0,800,600,10,20,true); check(camera.zoom==.35f,"Minimum zoom is bounded");
  }
  {
   GameTuning editor;
   json spec={{"id","player:start-gold"},{"group","player"},{"label","Starting wealth"},{"description","Starting gold"},{"category","Player & inventory"},{"default",600},{"minimum",0},{"maximum",60000},{"tier",false}};
   json catalog={{"entries",json::array({spec})},{"values",{{"player:start-gold",600}}},{"revision","1"}};
   editor.load(catalog); check(!editor.changed() && editor.modified_count()==0,"Loading tuning leaves a clean draft");
   editor.values["player:start-gold"]=1200; check(editor.changed() && editor.modified_count()==1,"Tuning edits remain draft changes");
   editor.modified_only=true; check(editor.visible(spec),"Modified filter includes custom values");
   editor.restore(); check(editor.values["player:start-gold"]==600 && !editor.visible(spec),"Reset restores defaults and updates filtering");
   editor.load(catalog); check(!editor.changed(),"Reloading discards unsaved tuning");
   editor.warning="Bad file"; editor.restore(); check(editor.changed(),"Explicit reset can repair a corrupt all-default file");
   Connection tuning_connection; tuning_connection.connected=true; UI tuning_ui{tuning_connection};
   tuning_ui.game_tuning.load(catalog); tuning_ui.game_tuning.values["player:start-gold"]=900;
   check(!tuning_ui.save_next_settings(nullptr) && tuning_ui.saving_tuning && tuning_ui.settings_saving,"Settings must wait for tuning validation before closing");
   check(tuning_connection.outgoing.find("tuning.set")!=std::string::npos && !tuning_connection.busy,"Writing next-launch preferences must not hold the gameplay command lock");
  }
  for(int i=0;i<5;++i) {
   ThemeSettings theme; theme.preset(i); theme.invert_dungeon=true; ThemeSettings loaded; loaded.load(theme.serialize());
   check(loaded.serialize()==theme.serialize(),"Theme presets must round-trip exactly");
  }
  DeluxeTheme::current.invert_dungeon=true;
  check(DeluxeTheme::dungeon_colour(IM_COL32(12,15,20,180))==IM_COL32(243,240,235,180),"Dungeon inversion preserves alpha");
  DeluxeTheme::current=ThemeSettings{};
  ThemeSettings manual; manual.preset(2); manual.light_styling=false;
  ThemeSettings restored; restored.load(manual.serialize());
  check(!restored.light_styling,"Explicit dark styling must survive a light background");
  manual.preset(0); manual.light_styling=true; restored.load(manual.serialize());
  check(restored.light_styling,"Explicit light styling must survive a dark background");
  auto legacy=manual.serialize(); legacy.erase("light_styling"); restored.load(legacy);
  check(!restored.light_styling,"Legacy original theme keeps dark styling");
  manual.preset(2); legacy=manual.serialize(); legacy.erase("light_styling"); restored.load(legacy);
  check(restored.light_styling,"Legacy light themes retain their appearance");
  ThemeSettings bounded; bounded.load({{"rounding",100},{"accent",json::array({-1,2,.5})},{"text","invalid"}});
  check(bounded.rounding==16 && bounded.accent.x==0 && bounded.accent.y==1,"Theme input must be bounded");
  {
   MotionFeedback motion; std::deque<json> events;
   json state={{"phase","playing"},{"revision","1"},{"dungeon",{{"level_id","one"}}},{"monsters",json::array({{{"index",7},{"visible",true},{"x",11},{"y",10}}})}};
   json walk={{"index",7},{"x",10},{"y",10},{"tx",11},{"ty",10},{"blink",false},{"tiles",json::array()}};
   auto batch=[&](json e,double born) { return json{{"level_id","one"},{"received",born},{"effects",json::array({e})}}; };
   events.push_back(batch(walk,1)); motion.update(events,state,true,true,1);
   check(motion.offset(11,10,1).x==-1,"Walking begins at the previous tile");
   check(motion.offset(11,10,1.05).x<0 && motion.offset(11,10,1.05).x>-.5,"Walking eases into the actual position");
   auto blink=walk; blink["blink"]=true; blink["tiles"]=json::array({json::array({10,10})});
   events.push_back(batch(blink,1.02)); motion.update(events,state,true,true,1.02);
   check(motion.walks.empty() && motion.ripples.size()==1,"Even an adjacent blink cancels walking and creates a ripple");
   motion.update(events,state,true,true,2); check(motion.ripples.empty(),"Ripples expire without blocking input");
   events.push_back(batch(walk,2)); state["monsters"][0]["visible"]=false; state["revision"]="2";
   motion.update(events,state,true,true,2); check(motion.walks.empty(),"Hidden actors never animate");
   state["monsters"][0]["visible"]=true; events.push_back(batch(walk,3)); motion.update(events,state,false,false,3);
   check(motion.walks.empty() && motion.ripples.empty(),"Animation switches suppress presentation");
   events.push_back(batch(walk,3)); state["dungeon"]["level_id"]="two"; motion.update(events,state,true,true,3);
   check(motion.walks.empty(),"Old-level events are discarded");
  }
  {
   Connection c; c.connected=true;
   c.state={{"phase","playing"},{"readiness","ready"},{"message_pending",true},
    {"player",{{"character_sheet",json::object()}}}};
   check(!c.ready(),"Pending messages block gameplay actions even with stale ready metadata");
   c.save(true); check(c.outgoing.empty() && !c.close_requested,"Blocked save must not send a request or close the app");
   check(c.can_view_character(),"Read-only character details remain available during continuation messages");
   UI ui{c}; ui.execute("core.character");
   check(ui.open_character_sheet && c.outgoing.empty() && c.state["message_pending"].get<bool>(),"Details opens locally without consuming a waiting message");
   c.prompt={{"type","confirmation"}};
   check(!c.can_view_character(),"Character modal does not stack over an unanswered native prompt");
   c.prompt=json::object(); c.pending_prompt={{"type","confirmation"}};
   check(!c.can_view_character(),"Deferred native prompts also own the modal");
   c.state["message_pending"]=false;
   check(!c.ready() && !c.key(54),"Deferred prompts block save and movement before their modal opens");
   c.target("dungeon.click",{{"x",1},{"y",1}});
   check(c.outgoing.empty(),"Deferred prompts cannot be bypassed through mouse actions");
   c.pending_prompt=json::object(); c.state["message_pending"]=false;
   check(c.ready() && c.can_view_character(),"Controls become available again after continuation");
   c.save(true); check(c.close_requested && c.busy && !c.outgoing.empty(),"Ready save and quit still submits normally");
  }
  {
   SceneTransitions scene;
   json state={{"phase","playing"},{"player",{{"depth",0},{"floor","down staircase"},{"x",1},{"y",0}}},
    {"dungeon",{{"level_id","town"},{"x",0},{"y",0}}}};
   RenderGrid old; old.semantic=true; old.width=2; old.height=1; old.cells={{'.',1},{'@',1}};
   scene.observe(state,old,1); check(scene.kind==SceneTransitions::Kind::Load,"First semantic scene scans in");
   scene.observe(state,old,1.1); check(scene.started==1,"Repeated snapshots do not restart loading");
   state["dungeon"]["level_id"]="depth1"; state["player"]["depth"]=1; state["player"]["floor"]="up staircase";
   scene.observe(state,old,2); check(scene.kind==SceneTransitions::Kind::Down && scene.previous.cells.size()==2,"Descending preserves one outgoing glyph grid");
   state["dungeon"]["level_id"]="town2"; state["player"]["depth"]=0; state["player"]["floor"]="open floor";
   scene.observe(state,old,3); check(scene.kind==SceneTransitions::Kind::Up,"Ascending sweeps upwards");
   state["dungeon"]["level_id"]="recall"; state["player"]["depth"]=8;
   scene.observe(state,old,4); check(scene.kind==SceneTransitions::Kind::Dissolve,"Non-stair level changes dissolve");
   state["store"]={{"name","Temple"}}; scene.observe(state,old,5); check(scene.kind==SceneTransitions::Kind::Shop && scene.label=="Temple","Shop arrival fades through dark");
   check(scene.hold_shop(5.05) && !scene.hold_shop(5.13),"Shop keeps the outgoing frame only before black");
   check(scene.shop_darkness(5)==0 && scene.shop_darkness(5.12)>.999f && scene.shop_darkness(5.31)==0,"Shop fade crosses black before revealing the incoming view");
   state.erase("dungeon"); scene.observe(state,old,5.1); check(scene.started==5,"Missing semantic view during a shop prompt does not restart it");
   state.erase("store"); scene.observe(state,old,6); check(scene.kind==SceneTransitions::Kind::Shop,"Leaving a shop transitions back");
   state["dungeon"]={{"level_id","recall"},{"x",0},{"y",0}}; scene.observe(state,old,6.1); check(scene.started==6,"Returning shop view does not count as a new floor");
   state["player"]["death_pending"]=true; scene.observe(state,old,7);
   check(scene.kind==SceneTransitions::Kind::Death && scene.previous.cells.empty(),"Death drains the live scene without hiding the final message");
   check(scene.desaturation(7)==0 && std::abs(scene.desaturation(7.45)-.5f)<.001f,"Death colour drains smoothly");
   scene.death(state,old,8); check(scene.started==7,"Acknowledging death does not restart the fade");
   check(!scene.active(8) && scene.desaturation(20)==1,"The completed fade holds greyscale");
   scene.restore_colour(20);
   check(scene.desaturation(20)==1 && std::abs(scene.desaturation(20.3)-.5f)<.001f && scene.desaturation(20.6)==0,"Native death screen gradually regains full colour");
   scene.restore_colour(20.3); check(scene.desaturation(20.6)==0,"Repeated summary frames do not restart colour return");
   scene.start(SceneTransitions::Kind::Death,20);
   const float partial=scene.desaturation(20.2); scene.restore_colour(20.2);
   check(std::abs(scene.desaturation(20.2)-partial)<.001f && scene.desaturation(20.5)<partial,"Early acknowledgement reverses the fade without a colour jump");
   state["player"]["death_pending"]=false; state["player"]["hp"]=10; scene.observe(state,old,21);
   check(scene.desaturation(21)==0,"Surviving death restores colour");
   scene.death(state,old,22); scene.reset(); check(scene.desaturation(22)==0,"A new session restores colour");
   scene.dismiss(); check(scene.previous.cells.empty(),"Expired snapshots release their storage");
   Connection live; live.connected=true; live.state={{"readiness","ready"},{"context","c"}}; live.transitions.start(SceneTransitions::Kind::Down,1,old);
   check(live.key(50) && live.transitions.kind==SceneTransitions::Kind::None && !live.outgoing.empty(),"Movement immediately skips a transition and reaches the backend");
  }
  auto full_map_frame=read_test_frames(json{{"map",std::string(1500000,'x')}}.dump()+"\n");
  check(full_map_frame.error.empty() && full_map_frame.frames.size()==1,"Full-level snapshots above the old 1 MiB limit are accepted");
  auto messages=read_test_frames("{\"seq\":1}\n{\"seq\":2}\n");
  check(messages.error.empty() && messages.frames.size()==2 && messages.frames[0]["seq"]==1 && messages.frames[1]["seq"]==2,"Reader must preserve final message order at EOF");
  check(!read_test_frames("{bad}\n").error.empty(),"Malformed frame must be reported");
  check(!read_test_frames("{\"seq\":1}").error.empty(),"Incomplete final frame must be reported");
  check(!read_test_frames(std::string(4194305,'x')).error.empty(),"Oversized frame must be rejected");
  {
   InventoryChanges gains;
   json potion={{"id","old"},{"binding_key","potion"},{"kind_key","potion"},{"location","Pack"},{"quantity",3},{"label","unknown potion"}};
   json sword={{"id","sword"},{"binding_key","sword-0"},{"kind_key","sword"},{"location","Weapon"},{"quantity",1}};
   json snapshot={{"phase","playing"},{"items",json::array({potion,sword})}};
   gains.update(snapshot); check(gains.pending.empty(),"Loaded possessions must be the baseline, not new loot");
   snapshot["items"][0]["id"]="new-revision"; snapshot["items"][0]["label"]="identified potion";
   snapshot["items"][1]["location"]="Pack"; gains.update(snapshot);
   check(gains.pending.empty(),"Identification, new handles and equipment moves must stay quiet");
   snapshot["items"][1]["binding_key"]="sword-enchanted"; gains.update(snapshot);
   check(gains.pending.empty(),"Enchanting existing gear must not count as acquiring gear");
   snapshot["items"][0]["quantity"]=5; gains.update(snapshot);
   check(gains.find(potion) && gains.find(potion)->amount==2 && !gains.find(potion)->fresh,"Stack growth should report the acquired quantity");
   gains.update(snapshot); check(gains.find(potion)->amount==2,"Repeated snapshots must not repeat gains");
   gains.acknowledge(potion); check(!gains.find(potion),"Reading an item clears its marker");
   snapshot["phase"]="store"; auto stock=potion; stock["location"]="Store"; stock["quantity"]=50;
   snapshot["items"].push_back(stock); gains.update(snapshot);
   check(gains.pending.empty(),"Shop stock must not count as owned inventory");
   auto arrows=json{{"binding_key","arrow"},{"kind_key","arrow"},{"location","Quiver"},{"quantity",10}};
   snapshot["items"].push_back(arrows); gains.update(snapshot);
   check(gains.find(arrows) && gains.find(arrows)->fresh,"New quiver items should be marked new");
   gains.acknowledge(arrows); snapshot["items"].back()["quantity"]=6; arrows["location"]="Pack"; arrows["quantity"]=4;
   snapshot["items"].push_back(arrows); gains.update(snapshot);
   check(gains.pending.empty(),"Splitting a stack across pack and quiver must not create loot");
   snapshot["items"][0]["quantity"]=6; gains.update(snapshot);
   check(gains.find(potion)->amount==1,"Purchases that grow an existing stack must be tracked");
   snapshot["items"].erase(0); gains.update(snapshot);
   check(!gains.find(potion),"Dropping or using the last carried stack clears its marker");
   gains.reset(); gains.update(snapshot); check(gains.pending.empty(),"Another session starts with a fresh baseline");
  }
  Connection audio_events;
  check(RestDialog::reply(0,100)=="&" && RestDialog::reply(1,100)=="*" && RestDialog::reply(2,100)=="!","Rest choices preserve native semantics");
  check(RestDialog::reply(3,1)=="1" && RestDialog::reply(3,9999)=="9999" && RestDialog::reply(3,0).empty() && RestDialog::reply(3,10000).empty(),"Rest turn bounds");
  audio_events.receive({{"kind","event"},{"event","activity.changed"},{"data",{{"resting",true}}}});
  check(audio_events.resting,"Rest activity starts indicator");
  audio_events.receive({{"kind","event"},{"event","activity.changed"},{"data",{{"resting",false}}}});
  check(!audio_events.resting,"Rest activity clears indicator");
  for(int i=0;i<100;++i) audio_events.receive({{"kind","event"},{"event","sound.play"},{"data",{{"name","quaff"}}}});
  check(audio_events.sound_cues.size()==16,"Sound burst queue must be bounded");
  check(!audio_events.busy && audio_events.outgoing.empty(),"Sound cues must not change input readiness or issue requests");
  AudioSettings audio_roundtrip; audio_roundtrip.enabled=false; audio_roundtrip.master=.31f;
  AudioSettings audio_loaded; audio_loaded.load(audio_roundtrip.serialize());
  check(!audio_loaded.enabled && audio_loaded.master==.31f,"Audio settings must roundtrip");
  json run={{"player",{{"name","Test"},{"race","Elf"},{"class","Mage"},{"level",3}}},{"items",json::array()},{"messages",json::array({{{"text","You die."},{"count",1}}})},{"cause","a test"},{"ended","2026-09-23"}};
  RunHistory history;
  history.directory=std::string(argv[1])+"-runs";
  check(!fs::exists(history.directory),"Use a fresh run-history test directory");
  history.begin(run);
  check(history.error.empty(),"Run archive must save");
  const auto archived=history.current;
  history.load(); check(history.records.size()==1 && history.records[0]==archived,"Run archive must retain complete final state");
  { std::ofstream bad(history.directory/"broken.json"); bad<<"broken"; }
  history.load(); check(history.records.size()==1 && !history.error.empty(),"A corrupt archive must not hide readable runs");
  fs::remove_all(history.directory); // This directory was verified absent before this test created it.
  Connection postgame; postgame.connected=true;
  postgame.state={{"phase","playing"},{"readiness","ready"}};
  postgame.receive({{"kind","event"},{"event","state.changed"},{"data",{{"phase","dead"},{"run",run}}}});
  check(!postgame.ready() && postgame.state["phase"]=="playing" && postgame.run_report==run,"Post-mortem must freeze gameplay and block commands");
  const auto finish_request=postgame.outgoing;
  postgame.receive({{"kind","event"},{"event","state.changed"},{"data",{{"phase","finished"},{"run",run}}}});
  check(postgame.outgoing==finish_request && postgame.postgame_finished,"Death completion must only be requested once");
  postgame.process_stopped(0);
  check(postgame.restart_ready && postgame.run_report==run,"Backend completion must preserve the native death screen");
  EngineOptions option_draft;
  json option_data={{"context","context-1"},{"entries",json::array({{{"id","show_damage"},{"label","Show damage"},{"value",false}}})},{"hitpoint_warn",3},{"delay_factor",40},{"lazymove_delay",0}};
  option_draft.load(option_data);
  check(option_draft.changes().empty(),"Opening engine settings must not change options");
  option_draft.values["show_damage"]=true;
  check(option_draft.changes()==json{{"show_damage",true}},"Only changed engine options should be submitted");
  option_draft.reset(); option_draft.load(option_data);
  check(option_draft.changes().empty() && !option_draft.values["show_damage"].get<bool>(),"Cancel/reopen must discard engine option edits");
  Connection option_connection;
  option_connection.connected=true; option_connection.busy=true;
  option_connection.options_request=option_connection.send("options.get");
  option_connection.receive({{"id",option_connection.options_request},{"result",option_data}});
  check(option_connection.busy && option_connection.options_result==option_data,"Options query must not unlock gameplay");
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
  {
   Connection saving; saving.connected=true; saving.receive(state_event("playing")); saving.state["readiness"]="ready";
   saving.save(true); check(saving.saving && !saving.ready(),"Save progress starts with the request");
   const auto id=saving.requests.rbegin()->first;
   saving.receive(state_event("playing")); check(saving.saving && !saving.ready(),"State events must not dismiss saving feedback");
   saving.receive({{"id",id},{"result",json::object()}});
   check(saving.close_confirmed && !saving.closed && saving.saving,"Keep rendering shutdown progress until the backend exits");
   saving.process_stopped(0); check(saving.closed && !saving.saving,"Completed shutdown closes the application");
   Connection failed; failed.connected=true; failed.receive(state_event("playing")); failed.state["readiness"]="ready";
   failed.save(true); const auto failure_id=failed.requests.rbegin()->first;
   failed.receive({{"id",failure_id},{"error",{{"message","Save failed"}}}});
   check(!failed.saving && !failed.close_requested && !failed.closed,"Save failure restores the UI instead of trapping it behind progress");
  }
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
  // Layout persistence and structural edits must preserve a single, visible dungeon.
  {
   WorkspaceLayout layout;
   const auto original=layout.arrangement();
   auto panel_node=[&](int panel) {
    std::function<int(const WorkspaceLayout::Node&)> find=[&](const WorkspaceLayout::Node &n) -> int {
     if(!n.axis && WorkspaceLayout::contains(n,panel)) return n.id;
     for(const auto &child:n.children) if(int id=find(child)) return id;
     return 0;
    };
    return find(layout.root);
   };
   check(layout.move({WorkspaceLayout::Inventory,0,5}),"Inventory can float without losing its state ID");
   check(layout.floating.size()==1 && layout.contains(WorkspaceLayout::Inventory),"Floating panel retained");
   check(layout.move({WorkspaceLayout::Inventory,panel_node(WorkspaceLayout::Dungeon),2}),"Dock inventory beside dungeon");
   check(layout.floating.empty(),"Empty floating group removed after docking");
   check(layout.move({WorkspaceLayout::Spells,panel_node(WorkspaceLayout::Inventory),0}),"Stack spells with inventory");
   check(!layout.move({WorkspaceLayout::Dungeon,0,6}),"Dungeon cannot be hidden");
   check(!layout.move({WorkspaceLayout::Messages,panel_node(WorkspaceLayout::Dungeon),0}),"Dungeon cannot be covered by tabs");
   check(layout.move({WorkspaceLayout::Messages,0,6}) && !layout.contains(WorkspaceLayout::Messages),"Hide optional panel");
   layout.reveal(WorkspaceLayout::Messages); check(layout.contains(WorkspaceLayout::Messages),"Recover hidden panel");
   WorkspaceLayout restored; check(restored.restore(layout.arrangement()),"Round trip custom layout");
   check(restored.contains(WorkspaceLayout::Dungeon) && restored.contains(WorkspaceLayout::Spells),"Round trip preserves panels");
   auto malformed=original; malformed["root"]["axis"]=20;
   const auto valid=restored.arrangement();
   check(!restored.restore(malformed) && restored.arrangement()==valid,"Malformed layout is transactional");
   auto duplicate=original; duplicate["floating"]={{{"node",WorkspaceLayout::encode(layout.leaf({WorkspaceLayout::Dungeon}))}}};
   check(!restored.restore(duplicate),"Duplicate panels rejected");
   {
    WorkspaceLayout edits; edits.begin_edit(); const auto initial=edits.edit_key();
    edits.move({WorkspaceLayout::Inventory,0,5}); edits.record_edit(); const auto floated=edits.edit_key();
    edits.floating[0].x=.3f; edits.floating[0].y=.25f; edits.record_edit();
    edits.undo_edit(); check(edits.edit_key()==floated,"Undo restores a floating panel's position as one action");
    edits.undo_edit(); check(edits.edit_key()==initial,"Undo restores original docking and IDs");
    edits.redo_edit(); check(edits.edit_key()==floated,"Redo restores the dock change");
    edits.panel_headings[WorkspaceLayout::TrackedCreature]=false; edits.record_edit();
    check(edits.redo_steps.empty(),"New edits clear the redo branch");
    check(edits.serialize()["panel_headings"][WorkspaceLayout::TrackedCreature]==true,"Draft headings do not leak into saved preferences");
    edits.finish_edit(true); check(edits.edit_key()==initial,"Cancel restores the entire customization session");
    edits.begin_edit(); edits.preset(1); edits.record_edit(); edits.undo_edit();
    check(edits.edit_key()==initial,"Starting layout changes can be undone");
    edits.redo_edit(); edits.finish_edit();
    check(!edits.contains(WorkspaceLayout::Inventory) && edits.undo_steps.empty(),"Done retains the result and ends history");
   }
   layout.before=original; layout.editing=true;
   check(layout.serialize()["current"]==original,"Unsaved edits must not leak into settings writes");
   check(layout.restore(layout.before),"Cancel recovers original arrangement");
   for(int preset=0;preset<3;++preset) { layout.preset(preset); check(restored.restore(layout.arrangement()),"Every preset is valid"); }
   layout.preset(1);
   check(!layout.contains(WorkspaceLayout::Inventory) && !layout.contains(WorkspaceLayout::Map) && layout.contains(WorkspaceLayout::Character),"Dungeon first hides optional panels");
   layout.toggle(WorkspaceLayout::Inventory);
   check(layout.contains(WorkspaceLayout::Inventory) && layout.floating.size()==1,"Visibility toggle restores a hidden panel");
   layout.toggle(WorkspaceLayout::Inventory);
   check(!layout.contains(WorkspaceLayout::Inventory) && layout.floating.empty(),"Visibility toggle hides a present panel");
   layout.toggle(WorkspaceLayout::Dungeon); check(layout.contains(WorkspaceLayout::Dungeon),"Visibility toggle protects dungeon");
   layout.preset(2);
   std::function<bool(const WorkspaceLayout::Node&,int)> standalone=[&](const WorkspaceLayout::Node &n,int p) {
    if(!n.axis) return n.tabs.size()==1 && n.tabs[0]==p;
    return standalone(n.children[0],p)||standalone(n.children[1],p);
   };
   check(standalone(layout.root,WorkspaceLayout::Map) && standalone(layout.root,WorkspaceLayout::Messages) && standalone(layout.root,WorkspaceLayout::Spells),"Command centre has separate map, message and spell panes");
   auto legacy=layout.arrangement(); legacy.erase("version");
   WorkspaceLayout old; old.restore(legacy); old.move({WorkspaceLayout::DungeonDetails,0,6});
   legacy=old.arrangement(); legacy.erase("version");
   check(restored.restore(legacy) && restored.contains(WorkspaceLayout::DungeonDetails),"Legacy layout gains independent dungeon details");
   restored.toggle(WorkspaceLayout::DungeonDetails);
   check(old.restore(restored.arrangement()) && !old.contains(WorkspaceLayout::DungeonDetails),"New layout preserves hidden dungeon details");
   {
    WorkspaceLayout previous; previous.move({WorkspaceLayout::TrackedCreature,0,6});
    auto v2=previous.arrangement(); v2["version"]=2;
    WorkspaceLayout migrated;
    check(migrated.restore(v2) && migrated.contains(WorkspaceLayout::TrackedCreature),"Old layouts gain a separate creature tracker");
    migrated.move({WorkspaceLayout::TrackedCreature,0,6});
    check(previous.restore(migrated.arrangement()) && !previous.contains(WorkspaceLayout::TrackedCreature),"Hidden tracker stays hidden after reload");
    migrated.detach(WorkspaceLayout::TrackedCreature,true);
    check(previous.restore(migrated.arrangement()) && previous.detached[WorkspaceLayout::TrackedCreature].open,"Detached tracker persists");
   }
   {
    WorkspaceLayout previous; previous.move({WorkspaceLayout::Status,0,6});
    auto v3=previous.arrangement(); v3["version"]=3;
    WorkspaceLayout migrated; check(migrated.restore(v3) && migrated.contains(WorkspaceLayout::Status),"Legacy layouts gain a status panel");
    migrated.move({WorkspaceLayout::Status,0,6});
    check(previous.restore(migrated.arrangement()) && !previous.contains(WorkspaceLayout::Status),"Hidden status panel stays hidden after reload");
    migrated.detach(WorkspaceLayout::Status,true);
    check(previous.restore(migrated.arrangement()) && previous.detached[WorkspaceLayout::Status].open,"Detached status panel persists");
   }
   old.panel_headings[WorkspaceLayout::TrackedCreature]=false; restored.load(old.serialize());
   check(!restored.heading(WorkspaceLayout::TrackedCreature) && restored.heading(WorkspaceLayout::Character),"Independent panel headings persist");
   restored.panel_headings[WorkspaceLayout::Character]=false;
   check(restored.heading(WorkspaceLayout::Character) && !WorkspaceLayout::has_heading(WorkspaceLayout::Character),"Character identity cannot be hidden");
   WorkspaceLayout legacy_headings; legacy_headings.load({{"version",3},{"show_headings",false}});
   check(!legacy_headings.heading(WorkspaceLayout::TrackedCreature) && !legacy_headings.heading(WorkspaceLayout::Messages),"Legacy global heading preference migrates");
   old.dividers_locked=false; restored.load(old.serialize());
   check(!restored.dividers_locked,"Divider unlock preference persists");
   check(!old.floating_locked,"Floating panels are movable by default");
   old.floating_locked=true; restored.load(old.serialize());
   check(restored.floating_locked,"Floating lock preference persists");
   layout.preset(0);
   layout.toggle(WorkspaceLayout::Target);
   const auto hidden_target_layout=layout.arrangement();
   layout.reveal(WorkspaceLayout::Target,false);
   check(layout.arrangement()==hidden_target_layout && layout.floating.empty(),"Look must respect a hidden target panel without creating an overlay");
   layout.preset(0); layout.reveal(WorkspaceLayout::Target,false);
   check(layout.find(panel_node(WorkspaceLayout::Target))->active==WorkspaceLayout::Target,"Look still selects an existing target tab");
   ui.layout.preset(1); ui.layout.saved["Test layout"]=ui.layout.arrangement();
  }
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
  ui.draft_scene_animation=false; check(ui.scene_animation,"Scene draft must not apply before saving");
  ui.draft_low_animation=false; ui.draft_death_animation=false; ui.draft_combat_animation=false; ui.draft_sleep_animation=false; ui.draft_item_glow=false; ui.draft_fear_animation=false; ui.draft_level_animation=false;
  check(ui.low_animation && ui.death_animation && ui.combat_animation && ui.sleep_animation && ui.fear_animation,"Animation drafts applied immediately");
  ui.draft_crt_strength=3; ui.draft_crt_settings.parts[Hum].enabled=true;
  check(!ui.crt_settings.parts[Hum].enabled,"Hum bar draft applied immediately");
  check(ui.scale==1.25f && ui.crt==0 && !ui.fullscreen,"Draft changed live settings");
  ui.draft_audio_settings.master=.1f; ui.draft_audio_settings.enabled=false;
  ui.draft_fonts.interface_font="Hack-Regular.ttf"; ui.draft_theme.preset(2);
  check(!ui.theme_settings.custom,"Theme drafts must not change live settings");
  check(ui.font_settings.interface_font=="Nouveau_IBM.ttf","Font preview must not change the live interface");
  ui.begin_settings(); // Reopening after Cancel discards the draft.
  check(ui.draft_fonts.interface_font=="Nouveau_IBM.ttf","Cancel must discard font choices");
  check(!ui.draft_theme.custom,"Cancel must discard theme edits");
  check(ui.draft_audio_settings.enabled && ui.draft_audio_settings.master==.8f,"Cancel must discard audio edits");
  check(!ui.draft_proceed_with_click,"Cancelled gameplay draft retained");
  check(!ui.draft_click_exits_look,"Cancelled look click draft retained");
  check(!ui.draft_quick_targeting,"Cancel retained quick targeting draft");
  check(!ui.draft_quickbar_enabled,"Cancel retained quickbar draft");
  check(ui.draft_scene_animation,"Cancel restores the transition preference");
  check(ui.draft_low_animation && ui.draft_death_animation && ui.draft_combat_animation && ui.draft_sleep_animation && ui.draft_fear_animation,"Cancelled animation draft retained");
  check(ui.draft_scale==1.25f && ui.draft_crt==0 && !ui.draft_fullscreen,"Draft was retained");
  check(ui.draft_crt_strength==1 && ui.crt_strength==1,"Cancelled strength was applied");
  check(!ui.draft_crt_settings.parts[Hum].enabled && !ui.crt_settings.parts[Hum].enabled,"Cancelled hum bar was applied");
  ui.draft_scale=1.5f; ui.draft_crt=1; ui.draft_crt_settings.parts[Hum].enabled=true;
  ui.draft_crt_settings.raster_lines=720; ui.draft_crt_settings.mask=2; ui.draft_crt_settings.tube_preset=-1;
  ui.draft_scene_animation=false;
  ui.draft_low_animation=false; ui.draft_death_animation=true; ui.draft_combat_animation=false; ui.draft_sleep_animation=false; ui.draft_item_glow=false; ui.draft_fear_animation=false; ui.draft_level_animation=false;
  ui.draft_click_exits_look=true;
  ui.draft_proceed_with_click=true;
  ui.draft_quick_targeting=true;
  ui.draft_quickbar_enabled=true;
  ui.quickbar.profile="test-character"; ui.quickbar.slots()[0]=Quickbar::command_binding({{"id","core.hold"},{"label","Hold"}});
  ui.draft_audio_settings.master=.43f; ui.draft_audio_settings.gameplay=.25f;
  ui.draft_fonts.interface_font="Hack-Regular.ttf"; ui.draft_fonts.dungeon_font="Flexi_IBM_VGA_True.ttf";
  ui.draft_theme.preset(3); ui.draft_theme.rounding=11; ui.draft_theme.invert_dungeon=true; ui.draft_theme.decorations=false;
  check(ui.apply_settings(nullptr),"Save settings");
  UI loaded{connection}; loaded.settings_path=path.string(); loaded.load_settings();
  check(loaded.font_settings.interface_font=="Hack-Regular.ttf" && loaded.font_settings.dungeon()=="Flexi_IBM_VGA_True.ttf","Both font choices persist");
  check(loaded.theme_settings.serialize()==ui.draft_theme.serialize(),"Theme colours and styling persist");
  check(!loaded.scene_animation,"Scene transitions setting persists");
  check(loaded.audio_settings.master==.43f && loaded.audio_settings.gameplay==.25f,"Audio volumes must persist after Save and Close");
  check(loaded.proceed_with_click,"Gameplay option did not persist");
  check(loaded.click_exits_look,"Click exits look did not persist");
  check(loaded.quick_targeting,"Quick targeting did not persist");
  loaded.quickbar.profile="test-character";
  check(loaded.quickbar_enabled && loaded.quickbar.slots()[0]["command"]=="core.hold","Quickbar settings and assignments did not persist");
  loaded.quickbar.profile="other-character"; check(loaded.quickbar.slots()[0].is_null(),"Characters must not share slots");
  check(loaded.crt_settings.raster_lines==720 && loaded.crt_settings.mask==2,"Staged raster/mask settings did not persist");
  check(!loaded.item_glow,"Ground item glow setting did not persist");
  check(!loaded.low_animation && loaded.death_animation && !loaded.combat_animation && !loaded.sleep_animation && !loaded.fear_animation && !loaded.level_animation,"Independent animation switches did not persist");
  check(loaded.scale==1.5f && loaded.crt==1 && !loaded.fullscreen,"Saved values");
  check(loaded.crt_settings.parts[Hum].enabled,"Hum bar did not persist");
  loaded.begin_settings(); loaded.draft_crt=2; loaded.draft_crt_settings.parts[Hum].enabled=false;
  check(loaded.apply_settings(nullptr),"Replace settings file");
  ui.load_settings(); check(!ui.crt_settings.parts[Hum].enabled,"Hum bar off did not persist"); check(ui.crt==2,"Full CRT persistence");
  loaded.begin_settings(); loaded.draft_crt=3;
  check(loaded.apply_settings(nullptr),"Save main-window-only CRT");
  ui.load_settings(); check(ui.crt==3,"Main-window-only CRT must persist independently of Full");
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
  ImGui::CreateContext(); DeluxeTheme::apply();
  auto &io=ImGui::GetIO(); io.IniFilename=nullptr; io.DisplaySize=ImVec2(1280,800);
  FontLibrary font_library; font_library.load(DELUXE_FONTS_DIR);
  unsigned char *pixels; int atlas_w,atlas_h;
  io.Fonts->GetTexDataAsRGBA32(&pixels,&atlas_w,&atlas_h); io.Fonts->SetTexID(1);
  for(size_t i=0;i<font_choices.size();++i) {
   check(font_library.fonts[i]!=nullptr,"Every bundled font must load");
   const float ratio=font_library.cell_ratio(font_choices[i].file);
   check(ratio>.1f && ratio<3.f,"Dungeon cell metrics must be usable");
   check(font_library.fonts[i]->GetFontBaked(18)->FindGlyphNoFallback('@')!=nullptr,"Every dungeon face needs the player glyph");
  }
  check(ui.layout.saved.contains("Test layout"),"Named layout retained by preferences");
  FontSettings invalid_fonts; invalid_fonts.load({{"interface","../outside.ttf"},{"dungeon",42}});
  check(invalid_fonts.interface_font=="Cousine-Regular.ttf" && invalid_fonts.dungeon()==invalid_fonts.interface_font,"Unknown fonts safely default and dungeon inherits");
  {
   UI final_ui{postgame}; final_ui.run_history.current=archived;
   ImGui::NewFrame(); ImGui::Begin("Old gameplay popup"); ImGui::OpenPopup("Stale menu"); ImGui::End();
   const json badge_player={{"study",2},{"statuses",json::array({{{"name","Poisoned"}},{{"name","Fast"}},{{"name","Blessed"}}})}};
   check(StatusEffects::height(badge_player,150)>StatusEffects::height(badge_player,900),"Status panel grows when badges wrap");
   check(StatusEffects::height(json::object(),400,false)==ImGui::GetTextLineHeight(),"Empty status panel fits its placeholder");
   const json character_metrics={{"gold",100000},{"armour",100},{"speed",10},{"stats",json::array({18,18,18,18,18})}};
   check(CharacterOverview::height(character_metrics,250)>CharacterOverview::height(character_metrics,900),"Character panel reserves height for stacked metrics");
   const json dungeon_metrics={{"depth",12},{"light",2},{"feeling","1 / 4"},{"floor","Open floor"}};
   check(CharacterOverview::dungeon_columns(dungeon_metrics,2000)==4 && CharacterOverview::dungeon_columns(dungeon_metrics,400)==2,"Dungeon height uses the same width breakpoint as its tiles");
   check(CharacterOverview::dungeon_height(dungeon_metrics,400)>CharacterOverview::dungeon_height(dungeon_metrics,2000),"Two-row Dungeon details reserve enough height");
   auto recall_metrics=dungeon_metrics; recall_metrics["recall"]=5;
   check(CharacterOverview::dungeon_height(recall_metrics,400)>CharacterOverview::dungeon_height(dungeon_metrics,400),"Dungeon notices expand its fitted height");
   check(CharacterOverview::tracked_height(false)<CharacterOverview::tracked_height(true),"Hiding tracker heading reclaims height");
   final_ui.draw(nullptr);
   check(!ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId|ImGuiPopupFlags_AnyPopupLevel),"Death screen must dismiss stale gameplay popups");
   ImGui::Render();
   auto draw_final=[&] { ImGui::NewFrame(); final_ui.draw(nullptr); ImGui::Render(); };
   final_ui.run_history.current["items"]=json::array({{{"label","a Dagger"},{"location","weapon"},{"quantity",1},{"description","A test weapon."}}});
   check(final_ui.showing_postgame,"Acknowledged death must show the summary immediately");
   io.AddMousePosEvent(500,300); io.AddMouseButtonEvent(0,true); draw_final();
   io.AddMouseButtonEvent(0,false); draw_final();
   check(!io.MouseDown[0],"Death screen must not retain the acknowledgement mouse press");
   draw_final(); draw_final();
   ImGuiID bar_id=0,tab_id=0; ImVec2 tab_pos;
   for(int i=0;i<GImGui->TabBars.GetMapSize();++i) if(auto *bar=GImGui->TabBars.TryGetMapData(i)) {
    if(bar->CurrFrameVisible!=GImGui->FrameCount) continue;
    for(auto &tab:bar->Tabs) if(std::string(ImGui::TabBarGetTabName(bar,&tab))=="Final belongings") {
     bar_id=bar->ID; tab_id=tab.ID;
     tab_pos=ImVec2(bar->BarRect.Min.x+tab.Offset+tab.Width*.5f,bar->BarRect.GetCenter().y);
    }
   }
   check(tab_id!=0,"Final belongings tab must be visible");
   io.AddMousePosEvent(tab_pos.x,tab_pos.y); draw_final();
   io.AddMouseButtonEvent(0,true); draw_final();
   io.AddMouseButtonEvent(0,false); draw_final();
   check(GImGui->TabBars.GetByKey(bar_id)->SelectedTabId==tab_id,"The first death-screen click must switch tabs");
  }
  for(float width:{600.f,1200.f}) {
   ImGui::NewFrame(); ImGui::SetNextWindowSize(ImVec2(width,750));
   ImGui::Begin("Post-mortem layout"); history.error.clear(); history.draw(); ImGui::End(); ImGui::Render();
  }
  for(float width:{240.f,500.f}) {
   ImGui::NewFrame(); ImGui::SetNextWindowSize(ImVec2(width,650));
   ImGui::Begin("Engine options layout");
   option_draft.draw();
   ImGui::End(); ImGui::Render();
   check(option_draft.changes().empty(),"Rendering native options must not edit them");
  }
  SDL_strlcpy(option_draft.search,"no matching setting",sizeof(option_draft.search));
   {
    ImGui::NewFrame(); ImGui::Begin("Layout measurements");
    WorkspaceLayout measured;
    auto enabled=[](int) { return true; };
    measured.compact_height=[](int p,float width) {
     if(p==WorkspaceLayout::Character) return 290.f;
     if(p==WorkspaceLayout::DungeonDetails) return width<450?110.f:60.f;
     return 0.f;
    };
    auto tabs=measured.leaf({WorkspaceLayout::Character,WorkspaceLayout::DungeonDetails});
    float character=measured.fitted_height(tabs,enabled,500);
    check(character>=290 && character<350,"Compact tab groups fit their selected content and tab strip");
    tabs.active=WorkspaceLayout::DungeonDetails;
    check(measured.fitted_height(tabs,enabled,500)<character-150,"Switching compact tabs releases unused height");
    tabs.tabs.push_back(WorkspaceLayout::Inventory);
    check(measured.fitted_height(tabs,enabled,500)==0,"Mixed tab groups stay resizable");
    check(measured.minimum(tabs,enabled,500).y>=character,"Mixed groups preserve the tallest compact panel's minimum");
    auto row=measured.split(1,.9f,measured.leaf({WorkspaceLayout::Inventory}),measured.leaf({WorkspaceLayout::DungeonDetails}));
    check(measured.minimum(row,enabled,1000).y>=110,"Height measurement uses constrained split widths");
    measured.editing=true;
    check(measured.minimum(tabs,enabled,500).y>=character,"Editing preserves content and tab height");
    ImGui::End(); ImGui::Render();
   }
  ImGui::NewFrame(); ImGui::Begin("Options empty search"); option_draft.draw(); ImGui::End(); ImGui::Render();
  {
  {
   Connection browser_connection; browser_connection.connected=true;
   browser_connection.state={{"readiness","ready"},{"phase","playing"},{"revision","inventory-test"},
    {"items",json::array({{{"id","sword"},{"label","a sword"},{"location","Weapon"}},
      {{"id","potion"},{"label","a potion"},{"location","Pack"},{"quantity",1},{"actions",json::array({"core.quaff","core.drop"})}},
      {{"id","floor"},{"label","a scroll"},{"location","Floor"},{"on_player_tile",true}}})}};
   browser_connection.receive({{"kind","event"},{"event","inventory.open"},{"data",json::object()}});
   UI browser{browser_connection}; browser.selected="sword"; SDL_strlcpy(browser.item_filter,"sword",sizeof(browser.item_filter));
   ImGui::NewFrame(); ImGui::Begin("Inventory browser test"); browser.inventory_window(); ImGui::End(); ImGui::Render();
   check(browser.inventory_window_open && browser.inventory_selected=="potion","Native inventory selects only pack items");
   check(browser.selected=="sword" && std::string(browser.item_filter)=="sword","Inventory window preserves sidebar selection and search");
   browser.inventory_window_drawing=true; browser.execute("core.quaff","potion"); browser.inventory_window_drawing=false;
   check(browser.inventory_action=="core.quaff" && browser.inventory_action_item=="potion" && browser_connection.outgoing.empty(),"Inventory action must defer until the modal closes");
   ImGui::ClosePopupsOverWindow(nullptr,false);
  }
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
  check(Quickbar::default_item_binding(potion)["command"]=="core.quaff","Dragging a potion defaults to Quaff");
  auto ground_potion=potion; ground_potion["location"]="Floor";
  check(Quickbar::default_item_binding(ground_potion).is_null(),"Ground items must not become carried-item drag bindings");
  Quickbar dragged; dragged.profile="drag-test";
  dragged.slots()[0]=binding; dragged.slots()[0]["custom_text"]="Healing";
  const auto original_drag=dragged.slots()[0];
  json slot_payload={{"profile","drag-test"},{"source",0},{"binding",original_drag}};
  check(dragged.accept_drop(slot_payload,2) && dragged.slots()[0].is_null() && dragged.slots()[2]==original_drag && dragged.dirty,"Dragging to an empty slot moves the complete customized binding");
  check(!dragged.accept_drop(slot_payload,3),"A stale drag must not duplicate a moved slot");
  dragged.slots()[4]=Quickbar::command_binding({{"id","core.rest"},{"label","Rest"}});
  const auto displaced=dragged.slots()[4]; slot_payload["source"]=2;
  check(dragged.accept_drop(slot_payload,4) && dragged.slots()[2]==displaced && dragged.slots()[4]==original_drag,"Occupied slots swap without losing either action");
  slot_payload["source"]=4;
  check(!dragged.accept_drop(slot_payload,4),"Dropping onto the source slot is a no-op");
  slot_payload["profile"]="other-character";
  check(!dragged.accept_drop(slot_payload,1),"Cross-character drags are rejected");
  check(dragged.accept_drop(json{{"profile","drag-test"},{"source",-1},{"binding",binding}},4) && dragged.slots()[4]==binding,"Inventory drops replace the destination binding");
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
  hot.state["items"][1]["spells"][0]["failure"]=19;
  hot.state["items"][1]["spells"][0]["description"]="Fires a bolt of magic.";
  auto spell_tip=Quickbar::tooltip_details(spell_binding,hot,cast);
  check(spell_tip["description"]=="Fires a bolt of magic." && spell_tip["facts"][0]=="Mana 1   Failure 19%" && !spell_tip.value("warning","").empty(),"Spell tooltip must expose live effect, cost, failure and low-mana warning");
  hot.state["items"][1]["spells"][0]["can_cast"]=false;
  hot.state["items"][1]["spells"][0]["cast_reason"]="You are too confused to cast.";
  check(Quickbar::tooltip_details(spell_binding,hot,Quickbar::resolve(spell_binding,hot))["reason"]=="You are too confused to cast.","Spell tooltip uses the engine's current failure reason");
  hot.state["items"][1]["spells"][0]["can_cast"]=true;
  auto item_tip=Quickbar::tooltip_details(binding,hot,Quickbar::resolve(binding,hot));
  check(item_tip.value("title","").find(potion.value("label",""))!=std::string::npos,"Item tooltip follows the current item");
  for(float tooltip_width:{350.f,850.f}) {
   ImGui::NewFrame(); ImGui::SetNextWindowSize(ImVec2(tooltip_width,180)); ImGui::Begin("Quickbar tooltip host");
   Quickbar::tooltip(spell_binding,hot,cast,0);
   ImGui::End(); ImGui::Render();
  }
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
   json player={{"statuses",json::array({
    {{"id","FAST"},{"name","Haste"},{"kind","benefit"},{"priority",2},{"visible",true},{"duration",20}},
    {{"id","FOOD"},{"name","Fed"},{"visible",false}},
    {{"id","CUT"},{"name","Deep Gash"},{"kind","harm"},{"priority",0},{"visible",true},{"duration",500},{"counter_kind","severity"}},
    {{"id","SHERO"},{"name","Berserk"},{"kind","mixed"},{"priority",1},{"visible",true},{"duration",12}},
    {{"id","OPP_FIRE"},{"name","Resist fire"},{"kind","benefit"},{"priority",2},{"visible",true},{"duration",15}}
   })}};
   const auto effects=StatusEffects::active(player);
   check(effects.size()==4 && effects.front()["id"]=="CUT" && effects[1]["id"]=="SHERO","Status badges must suppress normal food and prioritise harmful effects");
   for(float width:{160.f,350.f,650.f}) {
    ImGui::NewFrame(); ImGui::SetNextWindowSize(ImVec2(width,500)); ImGui::Begin("Status layout test");
    const float available=ImGui::GetContentRegionAvail().x;
    ImGui::BeginGroup(); StatusEffects::draw(player); ImGui::EndGroup();
    check(ImGui::GetItemRectSize().x<=available+1,"Status badges must wrap within the information panel");
    ImGui::End(); ImGui::Render();
   }
   player["statuses"]=json::array(); check(StatusEffects::active(player).empty(),"Expired effects must disappear");
  }
  {
   MapOverview map; map.centre=ImVec2(10,10);
   const ImVec2 mouse(130,70),origin(0,0),size(200,200);
   const auto anchor=MapOverview::world_at(mouse,origin,size,map.centre,5);
   map.zoom_at(3,mouse,origin,size,5);
   const auto after=MapOverview::world_at(mouse,origin,size,map.centre,15);
   check(std::abs(anchor.x-after.x)<.001f && std::abs(anchor.y-after.y)<.001f,"Map zoom must preserve the tile under the pointer");
   map.zoom_at(100,mouse,origin,size,5); check(map.zoom==24,"Map zoom must be bounded");
   Connection view; UI map_ui{view};
   view.catalog={{"features",json::array({{{"id",0},{"name","Unknown"}},{{"id",1},{"name","Open floor"},{"map_kind","floor"}},{{"id",2},{"name","Staircase down"},{"map_kind","down"}},{{"id",3},{"name","General store"},{"map_kind","shop"}}})}};
   view.state={{"map",{{"level_id","one"},{"known",json::array({json::array({0,1,2,0}),json::array({1,3,1,0})})}}},{"player",{{"x",1},{"y",1}}},{"dungeon",{{"x",0},{"y",0},{"width",3},{"height",2}}}};
   for(int frame=0;frame<3;++frame) {
    ImGui::NewFrame(); ImGui::SetNextWindowSize(ImVec2(frame==0?260:600,500)); ImGui::Begin("Map overview test");
    map_ui.minimap(); ImGui::End(); ImGui::Render();
   }
   check(view.outgoing.empty(),"Viewing the map must never issue game commands");
   map_ui.map_overview.zoom=4; map_ui.map_overview.fitted=false;
   view.state["map"]["level_id"]="two";
   ImGui::NewFrame(); ImGui::Begin("Map overview test"); map_ui.minimap(); ImGui::End(); ImGui::Render();
   check(map_ui.map_overview.zoom==1 && map_ui.map_overview.fitted,"A new floor must reset the map to fit");
  }
  {
   Connection recall; recall.connected=true; recall.capabilities["knowledge"]=1;
   UI inspector{recall}; recall.busy=true;
   inspector.inspect_creature(10); const auto old=recall.creature_request;
   inspector.inspect_creature(11); const auto latest=recall.creature_request;
   recall.receive({{"id",old},{"result",{{"id",10},{"name","Old creature"}}}});
   check(recall.creature_detail.is_null(),"Old creature recall must not overwrite the current selection");
   const json detail={{"id",11},{"name","Aimless-looking merchant"},{"group","Townsfolk"},{"stats",json::array({{{"label","Sightings"},{"value",3}}})},{"description_sections",json::array({{{"title","Combat"},{"text","Known attacks appear here."}}})}};
   recall.receive({{"id",latest},{"result",detail}});
   check(recall.busy && recall.creature_detail["id"]==11 && recall.knowledge_detail.is_null(),"Creature recall must preserve command locks and independent knowledge selection");
   check(inspector.select_creatures_tab,"Inspect must select the Creatures side tab");
   auto updated=detail; updated["name"]="Updated recall"; updated["category"]="creatures";
   recall.receive({{"kind","event"},{"event","knowledge.changed"},{"data",updated}});
   check(recall.creature_detail["name"]=="Updated recall" && recall.busy,"Live lore must update without releasing the command lock");
   updated["id"]=10; updated["name"]="Another race";
   recall.receive({{"kind","event"},{"event","knowledge.changed"},{"data",updated}});
   check(recall.creature_detail["id"]==11,"Updates for another inspected race must be ignored");

   for(float width:{320.f,600.f}) {
    ImGui::NewFrame(); ImGui::SetNextWindowSize(ImVec2(width,600)); ImGui::Begin("Creature inspection test");
    inspector.creatures(); ImGui::End(); ImGui::Render();
   }
  }
  {
   Connection knowledge; knowledge.connected=true; knowledge.busy=true;
   KnowledgeBrowser browser; browser.open(knowledge);
   const auto old=knowledge.knowledge_list_request;
   browser.load(knowledge,"terrain");
   const auto current=knowledge.knowledge_list_request;
   knowledge.receive({{"id",old},{"result",{{"entries",json::array()}}}});
   check(knowledge.knowledge_list.is_null(),"Stale knowledge listings must not replace the selected category");
   knowledge.receive({{"id",current},{"result",{{"entries",json::array({{{"id",1},{"name","Floor"},{"group","Terrain"}}})}}}});
   check(knowledge.busy && !knowledge.knowledge_list.is_null(),"Knowledge replies must not release an active game command");
   browser.select(knowledge,1); const auto detail=knowledge.knowledge_detail_request;
   browser.select(knowledge,2);
   knowledge.receive({{"id",detail},{"result",{{"name","Old selection"}}}});
   check(knowledge.knowledge_detail.is_null(),"Stale knowledge details must not replace a newer selection");
   check(KnowledgeBrowser::matches("Potion of Speed","SPEED"),"Knowledge search must ignore case");
   browser.category="creatures";
   knowledge.knowledge_list={{"entries",json::array({{{"id",1},{"name","Merchant"},{"group","People"}}})}};
   browser.selected=1;
   knowledge.knowledge_detail={{"name","Merchant"},{"group","People"},{"stats",json::array({{{"label","Sightings"},{"value",2}}})},{"description_sections",json::array({{{"title","Description"},{"text","A merchant wanders the town."}}})}};
   for(int frame=0;frame<3;++frame) {
    ImGui::NewFrame(); ImGui::SetNextWindowSize(ImVec2(frame==0?540:1000,650)); ImGui::Begin("Knowledge test");
    browser.contents(knowledge); ImGui::End(); ImGui::Render();
   }
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
    const float font=ImGui::GetFontSize();
    const float ribbon_width=font*33;
    const std::string long_message="This seems a quiet, peaceful place, and there may not be much interesting here.";
    const auto short_layout=DungeonFeedback::ribbon_layout("A short message.","Click to continue",ribbon_width,600);
    const auto long_layout=DungeonFeedback::ribbon_layout(long_message,"Click to continue",ribbon_width,600);
    check(long_layout.body_height>short_layout.body_height && long_layout.height>short_layout.height,"Wrapped messages must grow the ribbon");
    const float body_bottom=font*.65f+font*1.35f+long_layout.body_height;
    const float hint_top=long_layout.height-font*.65f-long_layout.hint_height;
    check(hint_top>=body_bottom+font*.34f,"Continuation hint must have a gap below the wrapped message");
    const auto narrow=DungeonFeedback::ribbon_layout(long_message,"Continue with your usual key",font*15,600);
    check(narrow.hint_height>font && !narrow.truncated,"Narrow ribbon must account for wrapping in its hint too");
    const auto bounded=DungeonFeedback::ribbon_layout(std::string(2000,'W'),"Click to continue",ribbon_width,font*9);
    check(bounded.truncated && bounded.height<=font*9,"Long excerpts must stay inside the viewport with an overflow cue");
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
  CharacterSelect roster;
  json saved_characters=json::array({{{"id","Hero"},{"name","Hero"},{"identity","Human Warrior"},{"level",12},{"depth",8}},
    {{"id","OldSave"},{"description","Older save description"}},{{"id","Fallen"},{"dead",true},{"name","Fallen"}}});
  for(int frame=0;frame<6;++frame) {
   roster.selected=frame==2?"OldSave":frame==3?"Fallen":"Missing";
   ImGui::NewFrame(); ImGui::SetNextWindowSize(ImVec2(frame%2?600.f:1200.f,800));
   ImGui::Begin("Character selection test");
   check(roster.draw(frame==4?json::array():saved_characters,true)==0,"Browsing characters must not launch or mutate saves");
   ImGui::End(); ImGui::Render();
   if(frame==0) check(roster.selected=="Hero","Missing selection should choose the first character");
   check(ImGui::GetDrawData()->TotalVtxCount>0,"Character roster must render");
  }
  Connection replay; replay.connected=true; replay.replay_save="Hero";
  auto hello_id=replay.send("hello");
  replay.receive({{"id",hello_id},{"result",{{"capabilities",{{"session.replay",1},{"interaction.birth",1}}}}}});
  bool replay_sent=false;
  for(const auto &request:replay.requests) {
   check(request.second!="saves.list","Direct replay should bypass the character selector");
   if(request.second=="session.replay") replay_sent=true;
  }
  check(replay_sent && replay.character_save=="Hero" && replay.busy,"Handshake must resume the completed save through replay");
  json sleep_view={{"x",3},{"y",4},{"width",1},{"height",1},{"cells",json::array({json::array({json::array({46,1,0,0,0,0,111,1,1,1,1,0,0})})})}};
  json sleeper={{"x",3},{"y",4},{"visible",true},{"asleep",true}};
  {
   json glow_view={{"x",0},{"y",0},{"width",1},{"height",1},{"cells",json::array({json::array({json::array({46,1,0,0,33,1,0,0,1,1,1,0,0})})})}};
   json glow_item={{"x",0},{"y",0},{"aura","artifact"}};
   check(ItemGlow::kind(glow_item,glow_view)==2,"Observed artifact gets holy glow");
   glow_item["aura"]="cursed"; check(ItemGlow::kind(glow_item,glow_view)==3,"Known curse gets crimson glow");
   glow_item["aura"]="rune"; check(ItemGlow::kind(glow_item,glow_view)==1,"Known rune gets gentle glow");
   glow_item.erase("aura"); check(!ItemGlow::kind(glow_item,glow_view),"Old backends and ordinary items have no glow");
   glow_item["aura"]="artifact";
   for(int field:{10,11,6,12,4}) {
    auto hidden=glow_view; hidden["cells"][0][0][field]=(field==10 || field==4)?0:1;
    check(!ItemGlow::kind(glow_item,hidden),"Glow must not reveal unseen, hallucinated or covered objects");
   }
   glow_item["x"]=1; check(!ItemGlow::kind(glow_item,glow_view),"Glow stays within dungeon viewport");
  }
  check(MonsterFeedback::eligible(sleeper,sleep_view),"Visible sleeping monster should have sleep markers");
  sleeper["afraid"]=true;
  check(MonsterFeedback::eligible(sleeper,sleep_view,"afraid"),"Feared monsters must use the engine fear flag");
  sleeper["afraid"]=false;
  check(!MonsterFeedback::eligible(sleeper,sleep_view,"afraid"),"Recovered monsters must lose fear markers");
  sleeper["asleep"]=false;
  check(!MonsterFeedback::eligible(sleeper,sleep_view),"Waking must immediately remove sleep markers");
  sleeper["asleep"]=true; sleeper["visible"]=false;
  check(!MonsterFeedback::eligible(sleeper,sleep_view),"Unseen sleepers must not be revealed");
  sleeper["visible"]=true; sleeper["x"]=5;
  check(!MonsterFeedback::eligible(sleeper,sleep_view),"Off-panel sleepers must be excluded");
  sleeper["x"]=3; sleep_view["cells"][0][0][11]=1;
  check(!MonsterFeedback::eligible(sleeper,sleep_view),"Hallucinated actors must not reveal sleep state");
  sleep_view["cells"][0][0][11]=0;
  ImGui::NewFrame(); ImGui::Begin("Sleep feedback host");
  MonsterFeedback::draw(ImGui::GetWindowDrawList(),{{"dungeon",sleep_view},{"monsters",json::array({sleeper})}},ImGui::GetCursorScreenPos(),ImVec2(300,200),20,24,10);
  ImGui::End(); ImGui::Render();
  DungeonTooltip hover;
  CombatFeedback combat;
  std::deque<json> combat_queue;
  json combat_state={{"phase","playing"},{"dungeon",{{"level_id","one"}}}};
  const json hit={{"level_id","one"},{"x",4},{"y",5},{"amount",3},{"kind","damage"},{"player",true},{"received",10.0}};
  combat_queue={hit,hit}; combat.update(combat_queue,combat_state,true,10.01);
  check(combat.marks.size()==1 && combat.marks[0].amount==6 && combat_queue.empty(),"Rapid hits combine without replaying events");
  ImGui::NewFrame(); ImGui::Begin("Combat feedback host");
  combat.draw(ImGui::GetWindowDrawList(),ImGui::GetCursorScreenPos(),ImVec2(400,300),12,20,0,0,10.1);
  ImGui::End(); ImGui::Render();
  combat.update(combat_queue,combat_state,true,10.8);
  check(combat.marks.empty(),"Combat feedback expires independently of gameplay turns");
  combat_queue={hit}; combat.update(combat_queue,combat_state,false,10.01);
  check(combat.marks.empty() && combat_queue.empty(),"Disabled feedback discards pending effects");
  combat_queue={hit}; combat_state["dungeon"]["level_id"]="two";
  combat.update(combat_queue,combat_state,true,10.01);
  check(combat.marks.empty(),"Old-level feedback must not appear on a new floor");
  const json history_combat={{"text","The orc hits you."},{"group","combat"},{"count",3}};
  const json history_loot={{"text","You collect gold."},{"group","loot"}};
  const json history_system={{"text","[SYSTEM] Game saved."},{"system",true}};
  check(MessageHistory::accepts(history_combat,1,"ORC") && !MessageHistory::accepts(history_combat,2,""),"Message history combines category and case-insensitive search");
  check(MessageHistory::accepts(history_loot,2,"") && MessageHistory::accepts(history_system,3,"saved"),"Loot and system message filters");
  check(MessageHistory::accepts(json{{"text","An unclassified event"}},4,"") && !MessageHistory::accepts(json{{"text","  "}},0,""),"History retains unclassified messages and omits blank separators");
  Connection history_connection;
  history_connection.messages={history_combat,history_loot,history_system};
  MessageHistory message_history_test;
  io.AddKeyEvent(ImGuiKey_Escape,false);
  message_history_test.open=true;
  for(int filter=0;filter<5;++filter) {
   message_history_test.filter=filter;
   ImGui::NewFrame(); ImGui::Begin("History host"); message_history_test.draw(history_connection); ImGui::End(); ImGui::Render();
  }
  io.AddKeyEvent(ImGuiKey_Escape,true);
  ImGui::NewFrame(); ImGui::Begin("History host"); check(message_history_test.draw(history_connection),"Escape closes message history"); ImGui::End(); ImGui::Render();
  io.AddKeyEvent(ImGuiKey_Escape,false);
  check(history_connection.outgoing.empty(),"Browsing message history must not send gameplay input");
  RestDialog rest;
  for(int mode=0;mode<4;++mode) {
   rest.mode=mode;
   ImGui::NewFrame(); ImGui::Begin("Rest dialog test");
   rest.draw(audio_events,false);
   ImGui::End(); ImGui::Render();
  }
  check(!hover.dwell(true,"first",5,6,1),"Tooltip must not appear immediately");
  check(!hover.dwell(true,"first",5,6,1.5),"Tooltip dwell delay");
  check(hover.dwell(true,"first",5,6,1.6),"Stationary hover should show tooltip");
  check(!hover.dwell(true,"next",5,6,2),"State changes must restart tooltip dwell");
  check(!hover.dwell(false,"next",5,6,3),"Clicks, prompts and targeting must dismiss tooltip");
  check(!hover.dwell(true,"next",6,6,4),"Moving tiles must restart tooltip dwell");
  json hover_state={{"dungeon",{{"x",5},{"y",6},{"width",1},{"height",1},
    {"cells",json::array({json::array({json::array({46,1,0,0,63,1,0,0,1,0,0,0,0})})})},
    {"items",json::array({{{"x",5},{"y",6},{"label","a remembered Scroll"},{"color",1}}})}}},
    {"items",json::array({{{"x",5},{"y",6},{"label","Live unseen replacement"}}})},
    {"monsters",json::array({{{"x",5},{"y",6},{"name","hidden creature"},{"visible",false}}})}};
  json hover_catalog={{"features",json::array({{{"id",1},{"name","open floor"}}})}};
  auto remembered= DungeonTooltip::describe_tile(hover_state,hover_catalog,5,6);
  check(!remembered.contains("name") && remembered["items"][0]["label"]=="a remembered Scroll" && !remembered["seen"].get<bool>(),"Tooltip must describe memory rather than live hidden entities");
  hover_state["terrain_actions"]=json::array({{{"x",5},{"y",6},{"action","disarm"},{"hint","Click to attempt disarming; right-click for Disarm."}}});
  check(DungeonTooltip::describe_tile(hover_state,hover_catalog,5,6).value("hint","")=="Click to attempt disarming; right-click for Disarm.","Tooltip uses the engine's contextual click hint");
  hover_state["monsters"][0]={{"x",5},{"y",6},{"name","sleepy orc"},{"visible",true},{"hp",3},{"max_hp",8},{"condition","asleep"}};
  auto creature= DungeonTooltip::describe_tile(hover_state,hover_catalog,5,6);
  check(creature["name"]=="Sleepy orc" && creature["subtitle"]=="Asleep" && creature["hp"]==3,"Creature tooltip details");
  for(int i=0;i<2;++i) {
   ImGui::NewFrame(); ImGui::SetNextWindowSize(ImVec2(i?320.f:900.f,650)); ImGui::Begin("Dungeon hover host");
   hover.x=5; hover.y=6; hover.content=nullptr; hover.draw(hover_state,hover_catalog);
   ImGui::End(); ImGui::Render();
  }
  hover_state["dungeon"]["cells"][0][0][11]=1;
  auto hallucination=DungeonTooltip::describe_tile(hover_state,hover_catalog,5,6);
  check(hallucination.value("hallucinating",false) && !hallucination.contains("name") && hallucination["items"].empty(),"Hallucination should not turn an appearance into a definite identification");
  check(!hallucination.contains("hint"),"Hallucinations must not receive definite terrain hints");
  LevelFeedback levels;
  json level_state={{"phase","playing"},{"player",{{"level",5},{"max_level",5}}}};
  levels.update(level_state,10);
  check(levels.intensity(10.2)==0,"Loading a character must not celebrate existing levels");
  level_state["player"]["level"]=level_state["player"]["max_level"]=7;
  levels.update(level_state,11);
  check(levels.level==7 && levels.intensity(11.3)>0,"A multi-level gain celebrates the new level once");
  levels.update(level_state,11.5);
  check(levels.started==11 && levels.intensity(14)==0,"Repeated snapshots cannot restart the flourish");
  level_state["player"]["level"]=4; levels.update(level_state,15);
  level_state["player"]["level"]=7; levels.update(level_state,16);
  check(levels.started==11,"Restoring drained levels is quiet");
  levels.reset(); levels.update(level_state,20);
  check(levels.intensity(20.3)==0,"New sessions establish a fresh silent baseline");
  json floor_state={{"phase","playing"},{"items",json::array({
   {{"location","Floor"},{"on_player_tile",true},{"can_pickup",true}},
   {{"location","Floor"},{"on_player_tile",true},{"can_pickup",false}},
   {{"location","Floor"},{"on_player_tile",false}},{{"location","Pack"}}})}};
  check(FloorItems::collect(floor_state).size()==2,"Current pile includes uncarryable items but excludes distant and owned items");
  floor_state["phase"]="store";
  check(FloorItems::collect(floor_state).empty(),"Floor section is limited to dungeon play");
  check(QuantityPicker::half(5)==2 && QuantityPicker::half(1)==1,"Quantity halves round down and remain usable");
  check(!QuantityPicker::valid(0,5) && !QuantityPicker::valid(6,5) && QuantityPicker::valid(5,5),"Quantity bounds protect native replies");
  Connection quantity_connection;
  quantity_connection.prompt={{"type","quantity"},{"maximum",5},{"item",{{"label","5 Rations of Food"},{"name_color",3}}},{"gold",30},{"purchase_totals",json::array({5,10,15,20,25})}};
  QuantityPicker picker;
  for(int n: {1,3,5,0,6}) {
   picker.amount=n;
   ImGui::NewFrame(); ImGui::Begin("Quantity test"); picker.draw(quantity_connection,false); ImGui::End(); ImGui::Render();
  }
  check(quantity_connection.outgoing.empty(),"Changing quantity must not execute an action before confirmation");
  {
   ProjectileFeedback fx;
   json state={{"phase","playing"},{"dungeon",{{"level_id","one"}}}};
   json batch={{"received",10.0},{"level_id","one"},{"effects",json::array({
    {{"element","FIRE"},{"blast",false},{"tiles",json::array({{2,3,0},{3,3,0}})}},
    {{"element","COLD"},{"blast",true},{"tiles",json::array({{4,3,0},{5,3,1}})}}})}};
   std::deque<json> events{batch}; fx.update(events,state,true,10.05);
   check(events.empty() && fx.tiles.size()==4,"Projectile events are consumed once");
   check(fx.tiles[1].delay>fx.tiles[0].delay && fx.tiles[3].delay>fx.tiles[2].delay,"Trails advance and blasts expand by native distance");
   ImGui::NewFrame(); ImGui::Begin("Projectile test");
   fx.draw(ImGui::GetWindowDrawList(),{0,0},{400,300},18,24,0,0,10.16);
   ImGui::End(); ImGui::Render();
   fx.update(events,state,true,10.5); check(fx.tiles.empty(),"Effects expire without new input");
   events.push_back(batch); fx.update(events,state,true,11); check(fx.tiles.empty(),"Stale queued effects are discarded");
   batch["received"]=11.; events.push_back(batch); fx.update(events,state,false,11);
   check(events.empty() && fx.tiles.empty(),"Disabled effects do not accumulate");
   events.push_back(batch); state["dungeon"]["level_id"]="two"; fx.update(events,state,true,11);
   check(fx.tiles.empty(),"Effects cannot leak to another level");
  }
  {
   ProjectileFeedback fx;
   json state={{"phase","playing"},{"dungeon",{{"level_id","breath"}}}};
   json batch={{"received",20.0},{"level_id","breath"},{"effects",json::array({
    {{"element","FIRE"},{"blast",true},{"arc",true},{"tiles",json::array({{2,3,1},{4,3,3},{7,3,6}})}}})}};
   std::deque<json> events{batch}; fx.update(events,state,true,20.05);
   check(fx.tiles.size()==3 && fx.tiles[0].arc,"Breaths retain native affected tiles");
   check(fx.tiles[0].delay<fx.tiles[1].delay && fx.tiles[1].delay<fx.tiles[2].delay,"Breath wave rolls away from its source");
   check(fx.tiles[0].glyph=='^',"Fire breath has its own wisp treatment");
   ImGui::NewFrame(); ImGui::Begin("Breath test");
   fx.draw(ImGui::GetWindowDrawList(),{0,0},{400,300},18,24,0,0,20.2);
   ImGui::End(); ImGui::Render();
   fx.update(events,state,true,20.5); check(!fx.tiles.empty(),"Breath tail lasts longer than a bolt");
   fx.update(events,state,true,20.65); check(fx.tiles.empty(),"Breath clears without input or engine waits");
   events.push_back(batch); fx.update(events,state,true,21); check(fx.tiles.empty(),"Stale breath does not replay");
   batch["received"]=21.; events.push_back(batch); fx.update(events,state,false,21); check(fx.tiles.empty(),"Animation setting disables breath too");
   events.push_back(batch); state["dungeon"]["level_id"]="other"; fx.update(events,state,true,21); check(fx.tiles.empty(),"Breath cannot follow the player to another level");
  }
  {
   json state={{"context","aim-2"},{"blast_radius",2}};
   json preview={{"context","aim-2"},{"x",4},{"y",5},{"tiles",json::array({{4,5},{5,5},{4,6}})}};
   check(BlastPreview::matches(preview,state,4,5),"Current blast footprint matches its target");
   check(!BlastPreview::matches(preview,state,5,5),"Old mouse positions never reuse a footprint");
   state["context"]="aim-3";
   check(!BlastPreview::matches(preview,state,4,5),"Old targeting contexts never reuse a footprint");
   state["context"]="aim-2"; state["blast_radius"]=0;
   check(!BlastPreview::matches(preview,state,4,5),"Cancelling or switching to a non-ball removes the preview");
   ImGui::NewFrame(); ImGui::Begin("Blast preview test");
   BlastPreview::draw(ImGui::GetWindowDrawList(),preview,{0,0},{200,200},18,24,0,0,1);
   ImGui::End(); ImGui::Render();
   Connection connection; connection.state={{"context","aim-2"}};
   auto id=connection.send("targeting.blast"); connection.blast_request=id; connection.busy=true;
   connection.receive({{"kind","response"},{"id",id},{"result",preview}});
   check(connection.busy && connection.blast_request.empty(),"Preview replies do not unlock a pending game command");
  }
  {
   const json gear=json::array({{{"location","right hand"},{"label","A ring of strength"}},{{"location","left hand"},{"label","A ring of speed"}},{{"location","Pack"},{"label","Spare ring"}}});
   check(EquipmentPortrait::item_at(gear,"right hand")->value("label","")=="A ring of strength","Portrait keeps the two ring slots distinct");
   check(EquipmentPortrait::item_at(gear,"head")==nullptr,"Empty slots do not invent equipped items");
   const json slots=json::array({{{"label","right hand"},{"occupied",true}},{{"label","left hand"},{"occupied",true}},{{"label","head"},{"occupied",false}},{{"label","unusual slot"},{"occupied",false}}});
   for(float width:{980.f,440.f}) {
    ImGui::NewFrame(); ImGui::SetNextWindowSize({width,650}); ImGui::Begin("Equipment test");
    EquipmentPortrait::draw(gear,slots,"Test save"); ImGui::End(); ImGui::Render();
    check(ImGui::GetDrawData()->TotalVtxCount>0,"Portrait renders both diagram and compact layouts");
   }
  }
  {
   RunJournal journal;
   json entry={{"kind","artifact"},{"text","Found the Phial of Galadriel"},{"turn",250},{"level",8},{"depth",12}};
   check(RunJournal::accepts(entry,4,"phial") && !RunJournal::accepts(entry,1,""),"Journal filters meaningful milestone categories and text");
   json history={{"entries",json::array({entry})}};
   for(float width:{850.f,430.f}) {
    ImGui::NewFrame(); ImGui::SetNextWindowSize({width,600}); ImGui::Begin("Journal test");
    journal.contents(history); ImGui::End(); ImGui::Render();
   }
  }
  KeybindingEditor binding_editor;
  const json binding_data={{"mode",0},{"revision",2},{"bindings",json::array()},
   {"commands",json::array({{{"id",82},{"label","Rest for a while"},{"group","Action commands"},{"original","R"},{"rogue","R"}}})},
   {"keys",json::array({{{"mode",0},{"key",114},{"default_action","Read a scroll"}}})}};
  binding_editor.load(binding_data);
  check(binding_editor.conflict(114)=="Read a scroll","Binding conflicts include the underlying native command");
  binding_editor.command=82; binding_editor.listening=true;
  SDL_Event capture{}; capture.type=SDL_EVENT_TEXT_INPUT; capture.text.text="r";
  check(binding_editor.event(capture) && binding_editor.candidate==114 && !binding_editor.changed(),"Capture waits for explicit assignment");
  binding_editor.assign(); check(binding_editor.changed() && binding_editor.conflict(114)=="Rest for a while","Assignment replaces one trigger in the draft");
  binding_editor.mode=1; check(binding_editor.conflict(114)=="Unassigned","Draft bindings are keyset-specific");
  binding_editor.mode=0; binding_editor.remove(114); check(!binding_editor.changed(),"Restoring removes only the override");
  binding_editor.command=82; binding_editor.listening=true;
  capture={}; capture.type=SDL_EVENT_KEY_DOWN; capture.key.key=SDLK_KP_PERIOD; capture.key.scancode=SDL_SCANCODE_KP_PERIOD;
  binding_editor.event(capture); capture={}; capture.type=SDL_EVENT_TEXT_INPUT; capture.text.text="."; binding_editor.event(capture);
  check(binding_editor.candidate==0 && binding_editor.listening,"Keypad text cannot accidentally become a captured binding");
  capture={}; capture.type=SDL_EVENT_KEY_DOWN; capture.key.key=SDLK_F6;
  binding_editor.event(capture); check(binding_editor.candidate==137,"Function-key capture matches game input encoding");
  binding_editor.listening=true; capture.key.key=SDLK_ESCAPE; binding_editor.event(capture);
  check(binding_editor.command==-1 && !binding_editor.listening,"Escape cancels capture without submitting a binding");
  binding_editor.load(binding_data);
  ImGui::NewFrame(); ImGui::Begin("Keybinding editor test"); binding_editor.draw(); ImGui::End(); ImGui::Render();
  ImGui::DestroyContext();
  fs::remove(path);
  std::cout<<"Session lifecycle, resource bars, graphics settings and CRT input/decay checks passed\n";
  return 0;
 } catch(const std::exception &e) { std::cerr<<e.what()<<'\n'; return 1; }
}
