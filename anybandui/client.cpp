// AnybandUI desktop client. GPLv2. No engine headers or linked engine state.
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include "imgui.h"
#include "imgui_internal.h" // Close stale popups when replacing gameplay with the post-mortem.
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlgpu3.h"
#include "crt_renderer.h"
#include "runtime_paths.h"
#include "font_library.h"
#include "tileset.h"
#include "workspace_layout.h"
#include <iostream>
#include "dungeon_view.h"
#include "dungeon_camera.h"
#include "backend_reader.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <chrono>
#include <stdexcept>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <vector>
using json = nlohmann::json;
namespace fs = std::filesystem;

// Trim a connector against both cell rectangles so it never crosses a glyph
// or continues through the targeting box. Handles non-square terminal cells.
static bool target_connector(ImVec2 from,ImVec2 to,float cw,float ch,ImVec2 &start,ImVec2 &end) {
 const float dx=to.x-from.x,dy=to.y-from.y;
 const float span=std::max(std::abs(dx)/cw,std::abs(dy)/ch);
 if(span<=1.f) return false;
 const float trim=.5f/span;
 start=ImVec2(from.x+dx*trim,from.y+dy*trim);
 end=ImVec2(to.x-dx*trim,to.y-dy*trim);
 return true;
}
static float resource_fraction(int value,int maximum) {
 return maximum>0?std::clamp(float(value)/float(maximum),0.f,1.f):0.f;
}
struct HealthGlitch {
 double death_started=-1;
 float update(const json &state,double seconds,bool low=true,bool death=true) {
  const auto phase=state.value("phase","");
  if((phase!="playing" && phase!="store") || !state.contains("player")) { death_started=-1; return 0; }
  const auto &p=state["player"];
  if(p.value("death_pending",false)) {
   if(!death) { death_started=-1; return 0; }
   if(death_started<0) death_started=seconds;
   return .45f+.55f*float(std::exp(-std::max(0.,seconds-death_started)/.7));
  }
  death_started=-1;
  const int warning=p.value("hp_warning",0), hp=p.value("hp",0);
  return low && warning>0 && hp<warning ? .12f+.18f*(1-resource_fraction(hp,warning)) : 0;
 }
};

static bool matches(std::string text, std::string term) {
 auto lower = [](unsigned char c) { return static_cast<char>(std::tolower(c)); };
 std::transform(text.begin(), text.end(), text.begin(), lower);
 std::transform(term.begin(), term.end(), term.begin(), lower);
 return text.find(term) != std::string::npos;
}
#include "ui_theme.h"
#include "engine_options.h"
#include "game_tuning.h"
#include "keybinding_editor.h"
#include "audio_player.h"
#include "inventory_changes.h"
#include "level_feedback.h"
#include "floor_items.h"

static std::string utf8(unsigned c) {
 std::string s;
 if (c < 128) s += char(c ? c : ' ');
 else if (c < 2048) { s += char(192 | (c >> 6)); s += char(128 | (c & 63)); }
 else if (c < 65536) { s += char(224 | (c >> 12)); s += char(128 | ((c >> 6) & 63)); s += char(128 | (c & 63)); }
 else { s += char(240 | (c >> 18)); s += char(128 | ((c >> 12) & 63)); s += char(128 | ((c >> 6) & 63)); s += char(128 | (c & 63)); }
 return s;
}
static ImU32 color(int index) {
 static const unsigned char colors[][3] = {
  {12,15,20},{235,236,235},{155,160,169},{237,146,66},
  {202,66,64},{60,175,99},{71,109,205},{149,106,66},
  {95,101,114},{245,245,245},{170,88,186},{237,215,95},
  {250,113,106},{139,228,153},{122,186,244},{199,158,104},
  {135,76,166},{157,123,178},{72,170,170},{193,187,147},
  {223,126,214},{95,109,158},{116,148,170},{162,180,191},
  {86,104,78},{149,188,146},{233,184,158},{209,167,105}
 };
 const auto &c = colors[std::max(0,index) % std::size(colors)];
 return IM_COL32(c[0],c[1],c[2],255);
}
// Native text follows the theme; dungeon glyphs retain Angband's palette.
static ImU32 ui_color(int index) {
 auto ink=ImGui::ColorConvertU32ToFloat4(color(index));
 const auto bg=ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
 if(AnybandUITheme::light_surface()) {
  auto luminance=[](ImVec4 v) { auto linear=[](float x) { return x<=.04045f?x/12.92f:std::pow((x+.055f)/1.055f,2.4f); }; return .2126f*linear(v.x)+.7152f*linear(v.y)+.0722f*linear(v.z); };
  for(int i=0;i<30 && (luminance(bg)+.05f)/(luminance(ink)+.05f)<4.5f;++i) { ink.x*=.9f; ink.y*=.9f; ink.z*=.9f; }
 }
 return ImGui::ColorConvertFloat4ToU32(ink);
}
#include "scene_transitions.h"
struct Connection {
 SceneTransitions transitions;
 InventoryChanges inventory_changes;
 LevelFeedback level_feedback;
 std::vector<std::string> sound_cues;
 RenderGrid game_grid;
 std::unique_ptr<BackendReader> reader;
 std::unique_ptr<SDL_Process,decltype(&SDL_DestroyProcess)> process{nullptr,SDL_DestroyProcess};
 std::string outgoing, diagnostic, menu_error, character_save, replay_save;
 std::deque<json> messages;
 std::deque<json> combat_events, projectile_events, motion_events;
 json previous_messages = json::array();
 json capabilities = json::object();
 json comparisons = json::object();
 json item_rules=nullptr;
 json run_report=nullptr;
 bool postgame_finished=false;
 json options_result=nullptr,options_saved=nullptr;
 json tuning_result=nullptr,tuning_saved=nullptr;
 json bindings_result=nullptr,bindings_saved=nullptr;
 std::string bindings_request;
 std::string options_request;
 json knowledge_list=nullptr,knowledge_detail=nullptr;
 json creature_detail=nullptr;
 int creature_race=-1;
 std::string creature_request;
 std::string knowledge_list_request,knowledge_detail_request;
 json route=json::object(), travel=json::object();
 std::string route_request, blast_request;
 std::string journal_request;
 json journal_data=nullptr;
 json blast=json::object();
 Uint64 blast_sent=0;
 Uint64 route_sent=0;
 json save_change_requests=json::object(), save_changes=json::array();
 std::map<std::string,std::string> comparison_requests;
 std::map<std::string,std::string> requests;
 unsigned long next = 0;
 bool connected = false, negotiated = false, busy = false, close_requested = false, closed = false;
 bool pickup_travel=false, resting=false, saving=false;
 std::string loading;
 int camera_sent=-1, tiles_sent=-1, viewport_width_sent=-1, viewport_height_sent=-1;
 Uint64 viewport_sent_at=0;
 bool return_to_menu = false, restart_ready = false, close_confirmed = false;
 json state = json::object(), prompt = json::object(), pending_prompt = json::object(), commands = json::array(), saves = json::array(), catalog = json::object();
 Connection()=default;
 Connection(const Connection&)=delete;
 Connection& operator=(const Connection&)=delete;
 Connection(Connection&&)=default;
 Connection& operator=(Connection&&)=default; // Reader joins before replacing its process.
 void close_process() {
  reader.reset(); // Join before SDL closes the worker's pipe handles.
  process.reset();
 }
 ~Connection() { close_process(); }
 void notice(const std::string &text) {
  messages.push_front({{"text","[SYSTEM] " + text},{"count",1},{"system",true}});
  if(messages.size()>400) messages.pop_back();
 }
 void update_messages(const json &latest) {
  size_t added=latest.size();
  for(size_t i=0;i<latest.size();++i) {
   size_t overlap=std::min(latest.size()-i,previous_messages.size());
   if(!overlap) break;
   bool same=true;
   for(size_t j=0;j<overlap;++j)
    if(latest[i+j].value("text","")!=previous_messages[j].value("text","") ||
       latest[i+j].value("category",0)!=previous_messages[j].value("category",0)) { same=false; break; }
   if(same) { added=i; break; }
  }
  // Angband coalesces consecutive repeats into the newest message.
  if(added<latest.size()) for(auto &m:messages) if(!m.value("system",false)) {
   m["count"]=latest[added].value("count",1); break;
  }
  for(size_t i=added;i>0;--i) messages.push_front(latest[i-1]);
  while(messages.size()>400) messages.pop_back();
  previous_messages=latest;
 }
 void save(bool leave=false, bool menu=false) {
  if(!ready()) return;
  close_requested=leave; return_to_menu=menu; saving=true;
  send(leave?"session.close":"session.save"); busy=true;
 }
 std::string send(const std::string &method, json params = json::object()) {
  if(method=="session.new" || method=="session.load" || method=="session.replay") { inventory_changes.reset(); level_feedback.reset(); transitions.reset(); character_save=params.value("save",""); params["native_birth"]=capabilities.value("interaction.birth",0)>0; }
  if (!connected) return "";
  if(method=="session.new") loading="Starting character creation...";
  else if(method=="session.load" || method=="session.replay") loading="Loading character...";
  else if(method=="birth.action" && params.value("action","")=="accept") loading="Starting game...";
  if(transitions.kind!=SceneTransitions::Kind::Death &&
     (method=="terminal.input" || method=="command.execute" || method=="dungeon.click" || method=="dungeon.pickup" || method=="dungeon.terrain" || method=="store.leave")) transitions.dismiss();
  auto id = "r" + std::to_string(++next);
  params["session_id"] = "session-1";
  outgoing += json{{"kind","request"},{"id",id},{"method",method},{"params",params}}.dump() + "\n";
  requests[id] = method;
  if(method=="saves.rename" || method=="saves.delete") save_change_requests[id]={{"save",params.value("save","")},{"name",params.value("name","")}};
  return id;
 }
 bool start(const std::string &exe, const std::string &data, const std::string &user) {
  const char *args[] = {exe.c_str(),"--data-dir",data.c_str(),"--user-dir",user.c_str(),nullptr};
  SDL_PropertiesID properties = SDL_CreateProperties();
  SDL_SetPointerProperty(properties, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, const_cast<char**>(args));
  SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, SDL_PROCESS_STDIO_APP);
  SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_APP);
  SDL_SetNumberProperty(properties, SDL_PROP_PROCESS_CREATE_STDERR_NUMBER, SDL_PROCESS_STDIO_APP);
  SDL_SetBooleanProperty(properties, SDL_PROP_PROCESS_CREATE_BACKGROUND_BOOLEAN, true);
  process.reset(SDL_CreateProcessWithProperties(properties)); SDL_DestroyProperties(properties);
  if (!process) { menu_error=SDL_GetError(); notice(menu_error); return false; }
  auto errors=static_cast<SDL_IOStream*>(SDL_GetPointerProperty(SDL_GetProcessProperties(process.get()),SDL_PROP_PROCESS_STDERR_POINTER,nullptr));
  reader=std::make_unique<BackendReader>(SDL_GetProcessOutput(process.get()),errors);
  connected = true; loading="Starting AnybandUI...";
  send("hello",{{"protocols",json::array({{{"major",0},{"minor",1}}})},{"max_frame_bytes",4194304},{"native_inventory",true},{"native_equipment",true}});
  return true;
 }
 bool inventory_requested=false, equipment_requested=false;
 void receive(json j) {
  if (j.value("kind","") == "event") {
   auto name = j.value("event","");
   if(name=="motion.feedback") {
    auto feedback=std::move(j.at("data")); feedback["received"]=double(SDL_GetTicksNS())/1e9;
    if(motion_events.size()>=16) motion_events.pop_front(); motion_events.push_back(std::move(feedback)); return;
   }
   if(name=="projectile.feedback") {
    auto feedback=std::move(j.at("data")); feedback["received"]=double(SDL_GetTicksNS())/1e9;
    if(projectile_events.size()>=16) projectile_events.pop_front(); projectile_events.push_back(std::move(feedback)); return;
   }
   if(name=="combat.feedback") {
    auto feedback=std::move(j.at("data")); feedback["received"]=double(SDL_GetTicksNS())/1e9;
    if(combat_events.size()>=64) combat_events.pop_front(); combat_events.push_back(std::move(feedback)); return;
   }
   if(name=="equipment.open") { equipment_requested=true; return; }
   if(name=="inventory.open") { inventory_requested=true; return; }
   if(name=="activity.changed") { resting=j.at("data").value("resting",false); return; }
   if(name=="sound.play") {
    auto cue=AudioPlayer::engine_cue(j.at("data").value("name",""));
    if(!cue.empty() && sound_cues.size()<16) sound_cues.push_back(cue);
    return;
   }
   if(name=="state.changed" && j.at("data").contains("run")) {
    loading.clear();
    if(run_report.is_null()) {
     run_report=j["data"]["run"];
     if(!run_report.value("winner",false) && !run_report.value("retired",false)) transitions.death(state,game_grid,double(SDL_GetTicksNS())/1e9);
     prompt=json::object(); pending_prompt=json::object();
     if(j["data"].value("phase","")=="dead") send("run.finish");
    }
    postgame_finished=j["data"].value("phase","")=="finished";
    busy=false; return;
   }
   if (name == "state.changed") { if(j.at("data").value("phase","")=="playing" || j.at("data").contains("birth")) loading.clear(); replay_save.clear(); transitions.observe(j.at("data"),game_grid,double(SDL_GetTicksNS())/1e9); state = std::move(j.at("data")); inventory_changes.update(state); level_feedback.update(state,double(SDL_GetTicksNS())/1e9); resting=false; comparisons=json::object(); item_rules=nullptr; game_grid.update(state); update_messages(state.at("messages")); busy = false; pickup_travel=false; }
   if(name=="knowledge.changed" && creature_race>=0 && j["data"].value("category","")=="creatures" && j["data"].value("id",-1)==creature_race) creature_detail=j["data"];
   if(name=="travel.changed") {
    travel=j.at("data");
    const auto label=travel.value("label","");
    if(travel.value("interrupted",false) && !label.empty()) notice(label);
   }
   if (name == "prompt.requested") { loading.clear(); prompt = j.at("data"); busy = false; pickup_travel=false; }
   return;
  }
  auto id = j.value("id",""); auto it = requests.find(id);
  if (it == requests.end()) return;
  auto method = it->second; requests.erase(it);
  if(method=="keybindings.get") {
   if(id==bindings_request) bindings_result=j.contains("error")?json{{"error",j["error"].value("message","Bindings unavailable.")}}:j["result"];
   return;
  }
  if(method=="keybindings.set") {
   bindings_saved=j.contains("error")?json{{"error",j["error"].value("message","Bindings could not be saved.")}}:j["result"];
   busy=false; return;
  }
  if(method=="dungeon.camera" || method=="dungeon.tiles" || method=="dungeon.viewport") {
   busy=false;
   if(j.contains("error")) {
    if(j["error"].value("code","")=="busy") { if(method=="dungeon.tiles") tiles_sent=-1; else if(method=="dungeon.viewport") viewport_width_sent=viewport_height_sent=-1; else camera_sent=-1; }
    else notice(j["error"].value("message","Camera mode could not be changed."));
   }
   return;
  }
  if(method=="journal.get") {
   if(id==journal_request) {
    journal_data=j.contains("error")?json{{"error",j["error"].value("message","Journal unavailable.")}}:j["result"];
    journal_request.clear();
   }
   return;
  }
  if(method=="tuning.get" || method=="tuning.set") {
   auto &destination=method=="tuning.get"?tuning_result:tuning_saved;
   destination=j.contains("error")?json{{"error",j["error"].value("message","Game tuning unavailable.")}}:j["result"];
   return;
  }
  if(method=="options.get") {
   if(id==options_request) options_result=j.contains("error")?json{{"error",j["error"].value("message","Options unavailable.")}}:j["result"];
   return;
  }
  if(method=="options.set") {
   options_saved=j.contains("error")?json{{"error",j["error"].value("message","Options could not be applied.")}}:j["result"];
   if(j.contains("error")) busy=false;
   return;
  }
  if(method=="knowledge.get" && id==creature_request) {
   creature_request.clear();
   creature_detail=j.contains("error")?json{{"error",j["error"].value("message","Creature recall unavailable.")}}:j["result"];
   return;
  }
  if(method=="knowledge.list" || method=="knowledge.get") {
   auto &pending=method=="knowledge.list"?knowledge_list_request:knowledge_detail_request;
   if(id==pending) {
    auto &result=method=="knowledge.list"?knowledge_list:knowledge_detail;
    result=j.contains("error")?json{{"error",j["error"].value("message","Knowledge unavailable.")}}:j["result"];
    if(result.contains("entries")) std::sort(result["entries"].begin(),result["entries"].end(),[](const json &a,const json &b) { return a.value("name","")<b.value("name",""); });
    pending.clear();
   }
   return; // Read-only replies never release the engine command lock.
  }
  if(method=="targeting.blast") {
   if(id==blast_request) {
    blast_request.clear();
    if(!j.contains("error") && j["result"].value("context","")==state.value("context","")) blast=j["result"];
   }
   return; // Read-only preview replies never release the command lock.
  }
  if(method=="dungeon.route") {
   route_request.clear();
   if(!j.contains("error") && j["result"].value("context","")==state.value("context","")) route=j["result"];
   return; // Preview replies must never unblock actions or show stale-query errors.
  }
  if(save_change_requests.contains(id)) {
   if(!j.contains("error")) save_changes.push_back(save_change_requests[id]);
   save_change_requests.erase(id);
  }
  if(method=="item.compare") {
   auto request=comparison_requests.find(id);
   if(request!=comparison_requests.end()) {
    const auto item=request->second; comparison_requests.erase(request);
    if(comparisons.contains(item)) {
     if(j.contains("error")) comparisons[item]={{"error",j["error"].value("message","Comparison unavailable.")}};
     else if(j["result"].value("revision","")==state.value("revision","")) comparisons[item]=j["result"];
    }
   }
   return;
  }
  if (j.contains("error")) {
   if(method=="hello" || method=="saves.list" || method=="session.new" || method=="session.load" || method=="session.replay" || method=="birth.action") loading.clear();
   const auto error=j["error"].value("message","Request failed"); notice(error); busy = false; pickup_travel=false;
   if(method=="session.save" || method=="session.close") saving=false;
   if(!state.contains("terminal") || method=="birth.action" || method=="birth.cancel") menu_error=error;
   if(method=="session.close" || method=="birth.cancel") { close_requested=false; return_to_menu=false; }
   if(method=="session.replay") { replay_save.clear(); send("saves.list"); }
   if(method == "prompt.reply") { prompt = pending_prompt; pending_prompt = json::object(); }
   return;
  }
  if(method=="birth.action") menu_error.clear();
  const auto &result = j.at("result");
  if (method == "hello") {
   capabilities=result.value("capabilities",json::object());
   negotiated = true;
   if(!replay_save.empty()) { send("session.replay",{{"save",replay_save}}); busy=true; }
   else send("saves.list");
   send("commands.list");
  } else if (method == "saves.list") {
   saves = result;
   if(loading=="Starting AnybandUI...") loading.clear();
   std::stable_sort(saves.begin(),saves.end(),[](const json &a,const json &b) { return a.value("modified",0.)>b.value("modified",0.); });
   busy=false;
  }
  else if (method == "saves.rename" || method == "saves.delete") { menu_error.clear(); send("saves.list"); }
  else if (method == "commands.list") commands = result;
  else if (method == "catalog.get") catalog = result;
  else if (method == "item.rules.list") item_rules=result;
  else if (method == "session.new" || method == "session.load" || method=="session.replay") { menu_error.clear(); send("catalog.get"); }
  else if (method == "session.save") { saving=false; busy = false; notice("Game saved."); }
  else if (method == "session.close" || method == "birth.cancel") { close_confirmed=true; closed = !return_to_menu && !saving; busy = false; }
  else if (method == "prompt.reply") pending_prompt = json::object();
 }
 void flush_input() {
  if(connected && !outgoing.empty()) {
   const auto n=SDL_WriteIO(SDL_GetProcessInput(process.get()),outgoing.data(),outgoing.size());
   outgoing.erase(0,n);
  }
 }
 void process_stopped(int exit_code) {
  if(saving && close_confirmed && !return_to_menu && exit_code==0) closed=true;
  loading.clear(); saving=false; connected=false; busy=false; pickup_travel=false; resting=false;
  // Death/post-game screens remain interactive until the engine reports that
  // play_game completed. A crash must not masquerade as a normal game ending.
  const bool finished=postgame_finished || state.value("phase","")=="finished";
  if(exit_code==0 && !closed && ((close_confirmed && return_to_menu) || finished)) restart_ready=true;
  else if(!closed) {
   notice("Backend stopped (" + std::to_string(exit_code) + "). " + diagnostic);
   if(!state.contains("terminal")) menu_error="Backend stopped. Restart AnybandUI to try again.";
  }
 }
 void poll() {
  if (!connected) return;
  int exit_code=0;
  const bool exited=SDL_WaitProcess(process.get(),false,&exit_code);
  auto batch=reader->take();
  diagnostic+=batch.diagnostic;
  if(diagnostic.size()>65536) diagnostic.erase(0,diagnostic.size()-65536);
  try { for(auto &frame:batch.frames) receive(std::move(frame)); }
  catch(const std::exception &e) { batch.error=e.what(); }
  if(!batch.error.empty()) { notice("Invalid backend message: "+batch.error); connected=false; saving=false; loading.clear(); return; }
  if(!exited) flush_input();
  // EOF must be drained by the reader before interpreting the final game state.
  else if(batch.finished) process_stopped(exit_code);
 }
 bool has_prompt() const { return !prompt.empty() || !pending_prompt.empty(); }
 bool ready() const { return run_report.is_null() && connected && !busy && !saving && !has_prompt() && !state.value("message_pending",false) && state.value("readiness","") == "ready"; }
 bool can_view_character() const {
  return run_report.is_null() && !has_prompt() &&
   state.contains("player") && state["player"].contains("character_sheet");
 }
 const char *save_unavailable_reason() const {
  return state.value("message_pending",false)?"Finish the waiting message before saving.":
   "Finish the current action or menu before saving.";
 }
 bool native_targeting() const { return capabilities.value("interaction.targeting",0)>0; }
 bool mouse_movement() const { return capabilities.value("interaction.mouse",0)>0; }
 bool key(const json &k) {
  if(connected && resting && !has_prompt() && k=="escape") {
   send("rest.cancel",{{"context",state.value("context","")}}); return true;
  }
  if(connected && busy && pickup_travel && !has_prompt() && k=="escape") {
   send("terminal.input",{{"context",state.value("context","")},{"key","escape"}});
   pickup_travel=false; return true;
  }
  if (!connected || busy || has_prompt() || state.empty()) return false;
  send("terminal.input",{{"context",state.value("context","")},{"key",k}}); busy = true;
  return true;
 }
 void target(const std::string &method,json params=json::object()) {
  if(!connected || busy || has_prompt()) return;
  params["context"]=state.value("context",""); send(method,std::move(params)); busy=true;
  pickup_travel=method=="dungeon.pickup" || method=="dungeon.terrain" || method=="dungeon.click";
 }
 void preview_blast(int x,int y) {
  if(!connected || busy || has_prompt() || state.value("blast_radius",0)<=0 ||
     capabilities.value("targeting.blast",0)==0 || !blast_request.empty()) return;
  const auto context=state.value("context","");
  if(blast.value("context","")==context && blast.value("x",-1)==x && blast.value("y",-1)==y) return;
  const auto now=SDL_GetTicksNS(); if(now-blast_sent<50000000) return;
  blast_sent=now; blast_request=send("targeting.blast",{{"context",context},{"x",x},{"y",y}});
 }
 void preview_route(int x,int y) {
  if(!ready() || capabilities.value("interaction.route",0)==0 || !route_request.empty()) return;
  const auto context=state.value("context","");
  if(route.value("context","")==context && route.value("x",-1)==x && route.value("y",-1)==y) return;
  const auto now=SDL_GetTicksNS(); if(now-route_sent<120000000) return;
  route_sent=now; route_request=send("dungeon.route",{{"context",context},{"x",x},{"y",y}});
 }
 void command(const std::string &id, const std::string &item = "", const std::string &spell = "") {
  if (!ready()) return;
  json params={{"revision",state.value("revision","")},{"command",id},{"item",item}};
  if(!spell.empty()) params["spell"]=spell;
  send("command.execute",params); busy = true;
 }
 void answer(json v) {
  send("prompt.reply",{{"prompt_id",prompt.value("prompt_id","")},{"value",v}});
  pending_prompt = prompt;
  prompt = json::object(); busy = true;
 }
};

static std::string display_label(std::string label) {
 std::replace(label.begin(),label.end(),'_',' ');
 if(!label.empty()) label[0]=char(std::toupper(static_cast<unsigned char>(label[0])));
 return label;
}
static void properties(const json &value) {
 if (!value.is_object()) return;
 for (auto it=value.begin(); it!=value.end(); ++it) {
  if (it.value().is_primitive()) {
   if(it.value().is_boolean()) {
    const bool checked=it.value().get<bool>();
    ImGui::Text("%s:",display_label(it.key()).c_str()); ImGui::SameLine();
    const auto p=ImGui::GetCursorScreenPos(); const float s=ImGui::GetFontSize();
    const auto ink=ImGui::GetColorU32(ImGuiCol_Text); auto draw=ImGui::GetWindowDrawList();
    const float stroke=std::max(1.f,s*.09f);
    if(checked) {
     draw->AddLine(ImVec2(p.x+s*.15f,p.y+s*.52f),ImVec2(p.x+s*.4f,p.y+s*.77f),ink,stroke);
     draw->AddLine(ImVec2(p.x+s*.4f,p.y+s*.77f),ImVec2(p.x+s*.88f,p.y+s*.23f),ink,stroke);
    } else {
     draw->AddLine(ImVec2(p.x+s*.23f,p.y+s*.23f),ImVec2(p.x+s*.77f,p.y+s*.77f),ink,stroke);
     draw->AddLine(ImVec2(p.x+s*.23f,p.y+s*.77f),ImVec2(p.x+s*.77f,p.y+s*.23f),ink,stroke);
    }
    ImGui::Dummy(ImVec2(s,s));
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("%s",checked?"Yes":"No");
    continue;
   }
   const std::string v = it.value().is_boolean() ? (it.value().get<bool>()?"Yes":"No") :
    (it.value().is_string() ? it.value().get<std::string>() : it.value().dump());
   ImGui::TextWrapped("%s: %s",display_label(it.key()).c_str(),v.c_str());
  }
 }
}
#include "character_overview.h"
#include "knowledge_browser.h"
#include "map_overview.h"
#include "dungeon_feedback.h"
#include "combat_feedback.h"
#include "projectile_feedback.h"
#include "motion_feedback.h"
#include "blast_preview.h"
#include "monster_feedback.h"
#include "item_glow.h"
#include "scene_presence.h"
#include "dungeon_tooltip.h"
#include "rest_dialog.h"
#include "quantity_picker.h"
#include "message_history.h"
#include "birth_panel.h"
#include "quickbar.h"
#include "spell_panel.h"
#include "item_description.h"
#include "item_comparison.h"
#include "item_rules.h"
#include "store_panel.h"
#include "character_select.h"
#include "equipment_portrait.h"
#include "character_sheet.h"
#include "run_journal.h"
#include "run_history.h"
struct UI {
 Connection &c;
 RunHistory run_history;
 RunJournal run_journal;
 CharacterSelect character_select;
 std::string pending_replay;
 bool run_report_started=false;
 bool showing_postgame=false;
 bool scene_animation=true, draft_scene_animation=true;
 json replay_profile=json::object();
 bool replay_prompt=false,run_ui_reset=false,quit_after_run=false;
 BirthPanel birth_panel;
 Quickbar quickbar;
 bool quickbar_enabled=false, draft_quickbar_enabled=false;
 bool quickbar_held[10]{};
 char quickbar_text=0;
 bool open_character_sheet=false;
 KnowledgeBrowser knowledge_browser;
 MapOverview map_overview;
 DungeonCamera dungeon_camera;
 DungeonCameraSettings camera_settings,draft_camera;
 bool inscription_edit=false;
 ItemRules item_rules_panel;
 StorePanel store_panel;
 SpellPanel spell_panel;
 bool was_store=false;
 float scale = 1.0f, game_fraction = .72f;
 bool fullscreen=false, draft_fullscreen=false;
 CombatFeedback combat_feedback;
 ProjectileFeedback projectile_feedback;
 MotionFeedback motion_feedback;
 bool movement_animation=true, draft_movement_animation=true, blink_animation=true, draft_blink_animation=true;
 bool projectile_animation=true, draft_projectile_animation=true;
 bool combat_animation=true, draft_combat_animation=true;
 bool item_glow=true, draft_item_glow=true;
 bool sleep_animation=true, draft_sleep_animation=true;
 bool fear_animation=true, draft_fear_animation=true;
 bool level_animation=true, draft_level_animation=true;
 bool low_animation=true, death_animation=true, draft_low_animation=true, draft_death_animation=true;
 PresenceSettings presence_settings,draft_presence; ScenePresence presence;
 int settings_page=0;
 int crt=0, draft_crt=0; // Persisted IDs: 0 off, 1 dungeon, 2 full, 3 main window.
 int crt_strength=1, draft_crt_strength=1;
 AudioSettings audio_settings{}, draft_audio_settings{};
 AudioPlayer audio;
 CrtSettings crt_settings{}, draft_crt_settings{};
 TilesetLibrary tiles;
 int tileset=0,draft_tileset=0;
 FontLibrary *font_library=nullptr;
 FontSettings font_settings,draft_fonts;
 ThemeSettings theme_settings,draft_theme;
 WorkspaceLayout layout;
 float draft_scale=1.f;
 std::string settings_error;
 EngineOptions engine_options;
 GameTuning game_tuning;
 bool saving_tuning=false;
 KeybindingEditor keybinding_editor;
 bool saving_bindings=false, settings_open=false;
 bool settings_saving=false;
 ImDrawList *game_draw_list=nullptr;
 ImVec2 game_pos{},game_size{};
 ImVec2 dungeon_surface_pos{},dungeon_surface_size{};
 bool quit_dialog = false;
 bool inventory_window_open=false, inventory_window_drawing=false;
 int inventory_category=0;
 std::string inventory_selected,inventory_action,inventory_action_item;
 char inventory_filter[128]{};
 bool grid_focus = false, focus_requested = false, window_active = true;
 bool return_from_prompt = false;
 bool message_search_open = false;
 bool targeting_was_active=false;
 bool proceed_with_click=false, draft_proceed_with_click=false;
 bool click_exits_look=false, draft_click_exits_look=false;
 bool quick_targeting=false, draft_quick_targeting=false;
 bool select_creatures_tab=false, select_spells_tab=false;
 DungeonTooltip dungeon_tooltip;
 int grid_menu_x=0,grid_menu_y=0;
 std::string grid_menu_context;
 float display_scale = 1.f;
 ImGuiStyle base_style;
 char item_filter[128]{}, message_filter[128]{}, command_filter[128]{}, save_name[65] = "Adventurer";
 char prompt_text[4096]{};
 RestDialog rest_dialog;
 MessageHistory message_history;
 std::string last_prompt, selected, settings_path, prompt_item;
 std::string managed_save;
 char renamed_save[65]{};
 std::vector<json> keys;
 void load_settings() {
  try { std::ifstream in(settings_path); if (!in) return; json j; in >> j;
   tileset=std::clamp(j.value("tileset",0),0,6);
   camera_settings.load(j.value("camera",json::object()));
   font_settings.load(j.value("fonts",json::object()));
   theme_settings.load(j.value("theme",json::object()));
   layout.load(j.value("layout",json::object()));
   scale=std::clamp(j.value("scale",1.f),0.75f,1.5f);
   game_fraction=std::clamp(j.value("game_fraction",.72f),.2f,.9f);
   fullscreen=j.value("fullscreen",false);
   proceed_with_click=j.value("proceed_with_click",false);
   click_exits_look=j.value("click_exits_look",false);
   quick_targeting=j.value("quick_targeting",false);
   quickbar_enabled=j.value("quickbar_enabled",false);
   quickbar.load(j.value("quickbar_profiles",json::object()));
   audio_settings.load(j.value("audio",json::object()));
   movement_animation=j.value("movement_animation",true); blink_animation=j.value("blink_animation",true);
   projectile_animation=j.value("projectile_animation",true);
   combat_animation=j.value("combat_animation",true);
   item_glow=j.value("item_glow",true);
   presence_settings.load(j.value("presence",json::object()));
   sleep_animation=j.value("sleep_animation",true);
   fear_animation=j.value("fear_animation",true);
   level_animation=j.value("level_animation",true);
   scene_animation=j.value("scene_animation",true);
   low_animation=j.value("low_health_animation",true); death_animation=j.value("death_animation",true);
   crt=std::clamp(j.value("crt",0),0,3);
   crt_strength=std::clamp(j.value("crt_strength",1),-1,3);
   crt_settings=CrtSettings(crt_strength);
   crt_settings.parts[Hum].enabled=j.value("hum_bar",false);
   crt_settings.parts[Ghost].enabled=false; // New effects remain opt-in for legacy preferences.
   if(j.contains("crt_components")) crt_settings.load(j.at("crt_components"));
  } catch (...) { c.notice("Settings could not be read; using defaults."); }
 }
 bool write_settings(float zoom,bool full,int effect,int strength,const CrtSettings &settings,bool low,bool death,bool proceed,bool exit_look,bool quick,bool bar,const AudioSettings *sound=nullptr,const bool *combat=nullptr,const bool *sleep=nullptr,const bool *fear=nullptr,const bool *level=nullptr,const bool *projectiles=nullptr,const bool *movement=nullptr,const bool *blink=nullptr,const bool *scene=nullptr,const FontSettings *fonts=nullptr,const ThemeSettings *theme=nullptr,const bool *glow=nullptr,const DungeonCameraSettings *camera=nullptr,const PresenceSettings *presence_options=nullptr,const int *tile_choice=nullptr) {
  const std::string temporary=settings_path+".tmp";
  std::ofstream out(temporary);
  out << json{{"tileset",tile_choice?*tile_choice:tileset},{"presence",(presence_options?*presence_options:presence_settings).serialize()},{"camera",(camera?*camera:camera_settings).serialize()},{"item_glow",glow?*glow:item_glow},{"theme",(theme?*theme:theme_settings).serialize()},{"layout",layout.serialize()},{"fonts",(fonts?*fonts:font_settings).serialize()},{"scene_animation",scene?*scene:scene_animation},{"movement_animation",movement?*movement:movement_animation},{"blink_animation",blink?*blink:blink_animation},{"projectile_animation",projectiles?*projectiles:projectile_animation},{"audio",(sound?*sound:audio_settings).serialize()},{"scale",zoom},{"game_fraction",game_fraction},{"fullscreen",full},{"crt",effect},{"crt_strength",strength},{"crt_components",settings.serialize()},{"level_animation",level?*level:level_animation},{"fear_animation",fear?*fear:fear_animation},{"sleep_animation",sleep?*sleep:sleep_animation},{"combat_animation",combat?*combat:combat_animation},{"low_health_animation",low},{"death_animation",death},{"proceed_with_click",proceed},{"click_exits_look",exit_look},{"quick_targeting",quick},{"quickbar_enabled",bar},{"quickbar_profiles",quickbar.profiles}}.dump(2);
  out.close();
  return bool(out) && SDL_RenamePath(temporary.c_str(),settings_path.c_str());
 }
 void save_settings() {
  if(!write_settings(scale,fullscreen,crt,crt_strength,crt_settings,low_animation,death_animation,proceed_with_click,click_exits_look,quick_targeting,quickbar_enabled)) c.notice("Settings could not be saved.");
 }
 void begin_settings() {
  game_tuning.reset(); saving_tuning=false; c.tuning_result=nullptr; c.tuning_saved=nullptr;
  if(c.negotiated && c.capabilities.value("tuning",0)>0) c.send("tuning.get");
  else { game_tuning.loaded=true; game_tuning.error="Game tuning needs a connected, supported backend."; }
  draft_tileset=tileset;
  draft_camera=camera_settings;
  draft_fonts=font_settings; draft_theme=theme_settings;
  draft_audio_settings=audio_settings;
  engine_options.reset(); settings_saving=false; saving_bindings=false;
  keybinding_editor.reset(); c.bindings_result=nullptr; c.bindings_saved=nullptr; c.bindings_request.clear();
  if(c.capabilities.value("keybindings",0)>0 && c.state.contains("player")) c.bindings_request=c.send("keybindings.get");
  else { keybinding_editor.loaded=true; keybinding_editor.error="Keybindings are available after starting a character with a supported backend."; }
  c.options_result=nullptr; c.options_saved=nullptr; c.options_request.clear();
  if(c.capabilities.value("options",0)>0 && c.state.contains("player")) c.options_request=c.send("options.get");
  else { engine_options.loaded=true; engine_options.error="Angband options are available after starting a character with a supported backend."; }
  draft_proceed_with_click=proceed_with_click;
  draft_click_exits_look=click_exits_look;
  draft_quick_targeting=quick_targeting;
  draft_quickbar_enabled=quickbar_enabled;
  draft_projectile_animation=projectile_animation;
  draft_movement_animation=movement_animation; draft_blink_animation=blink_animation;
  draft_combat_animation=combat_animation;
  draft_item_glow=item_glow; draft_presence=presence_settings;
  draft_sleep_animation=sleep_animation;
  draft_fear_animation=fear_animation;
  draft_level_animation=level_animation;
  draft_scene_animation=scene_animation;
  draft_low_animation=low_animation; draft_death_animation=death_animation;
  draft_scale=scale; draft_fullscreen=fullscreen; draft_crt=crt; settings_error.clear();
  draft_crt_strength=crt_strength; draft_crt_settings=crt_settings;
 }
 bool apply_settings(SDL_Window *window) {
  if(draft_fullscreen!=fullscreen && !SDL_SetWindowFullscreen(window,draft_fullscreen)) {
   settings_error=SDL_GetError(); return false;
  }
  if(!write_settings(draft_scale,draft_fullscreen,draft_crt,draft_crt_strength,draft_crt_settings,draft_low_animation,draft_death_animation,draft_proceed_with_click,draft_click_exits_look,draft_quick_targeting,draft_quickbar_enabled,&draft_audio_settings,&draft_combat_animation,&draft_sleep_animation,&draft_fear_animation,&draft_level_animation,&draft_projectile_animation,&draft_movement_animation,&draft_blink_animation,&draft_scene_animation,&draft_fonts,&draft_theme,&draft_item_glow,&draft_camera,&draft_presence,&draft_tileset)) {
   if(draft_fullscreen!=fullscreen) SDL_SetWindowFullscreen(window,fullscreen);
   settings_error="Settings could not be saved. Please try again."; return false;
  }
  if(camera_settings.enabled!=draft_camera.enabled || camera_settings.follow!=draft_camera.follow) dungeon_camera.reset();
  if(camera_settings.fixed_size!=draft_camera.fixed_size || camera_settings.fixed_width!=draft_camera.fixed_width) dungeon_camera.zoom=1;
  camera_settings=draft_camera; tileset=draft_tileset;
  font_settings=draft_fonts; theme_settings=draft_theme;
  audio_settings=draft_audio_settings; audio.configure(audio_settings,window_active);
  projectile_animation=draft_projectile_animation;
  movement_animation=draft_movement_animation; blink_animation=draft_blink_animation;
  combat_animation=draft_combat_animation;
  item_glow=draft_item_glow; presence_settings=draft_presence;
  sleep_animation=draft_sleep_animation;
  fear_animation=draft_fear_animation;
  level_animation=draft_level_animation;
  scene_animation=draft_scene_animation;
  low_animation=draft_low_animation; death_animation=draft_death_animation;
  proceed_with_click=draft_proceed_with_click;
  click_exits_look=draft_click_exits_look;
  quick_targeting=draft_quick_targeting;
  quickbar_enabled=draft_quickbar_enabled;
  scale=draft_scale; fullscreen=draft_fullscreen; crt=draft_crt;
  crt_strength=draft_crt_strength; crt_settings=draft_crt_settings;
  return true;
 }
 bool save_next_settings(SDL_Window *window) {
  if(keybinding_editor.command>=0) { settings_error="Finish or cancel the pending key capture first."; return false; }
  if((keybinding_editor.changed() || !engine_options.changes().empty()) && (!c.ready() || c.state.value("phase","")!="playing")) {
   settings_error="Return to normal play before changing keybindings or Angband options."; return false;
  }
  settings_error.clear();
  if(game_tuning.changed()) {
   if(!c.connected) { settings_error="Reconnect the backend before saving game tuning."; return false; }
   c.tuning_saved=nullptr; c.send("tuning.set",{{"revision",game_tuning.revision},{"values",game_tuning.values}});
   settings_saving=saving_tuning=true; return false;
  }
  if(keybinding_editor.changed()) {
   c.bindings_saved=nullptr; c.options_saved=nullptr;
   c.send("keybindings.set",{{"revision",keybinding_editor.revision},{"bindings",keybinding_editor.bindings}});
   c.busy=true; settings_saving=saving_bindings=true; return false;
  }
  if(!engine_options.changes().empty()) {
   c.options_saved=nullptr; c.send("options.set",{{"context",engine_options.context},{"values",engine_options.changes()}});
   c.busy=true; settings_saving=true; return false;
  }
  return apply_settings(window);
 }
 void settings_window(SDL_Window *window) {
  auto vp=ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x+vp->WorkSize.x*.5f,vp->WorkPos.y+vp->WorkSize.y*.5f),ImGuiCond_Appearing,ImVec2(.5f,.5f));
  ImGui::SetNextWindowSize(ImVec2(std::min(vp->WorkSize.x-24.f,ImGui::GetFontSize()*62),
   std::min(vp->WorkSize.y-24.f,ImGui::GetFontSize()*40)),ImGuiCond_Appearing);
  settings_open=ImGui::BeginPopupModal("Settings",nullptr,ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoSavedSettings);
  if(settings_open) {
   if(!engine_options.loaded && !c.options_result.is_null()) engine_options.load(c.options_result);
   if(!keybinding_editor.loaded && !c.bindings_result.is_null()) keybinding_editor.load(c.bindings_result);
   if(!game_tuning.loaded && !c.tuning_result.is_null()) game_tuning.load(c.tuning_result);
   if(settings_saving && !c.connected) {
    settings_saving=saving_bindings=saving_tuning=false; settings_error="Backend disconnected. Changes could not be confirmed.";
   }
   if(saving_tuning && !c.tuning_saved.is_null()) {
    settings_saving=saving_tuning=false;
    if(c.tuning_saved.contains("error")) settings_error=c.tuning_saved.value("error","");
    else { game_tuning.load(c.tuning_saved); if(save_next_settings(window)) ImGui::CloseCurrentPopup(); }
   }
   else if(saving_bindings && !c.bindings_saved.is_null()) {
    settings_saving=saving_bindings=false;
    if(c.bindings_saved.contains("error")) settings_error=c.bindings_saved.value("error","");
    else { keybinding_editor.load(c.bindings_saved); if(save_next_settings(window)) ImGui::CloseCurrentPopup(); }
   }
   else if(settings_saving && !saving_tuning && !saving_bindings && !c.options_saved.is_null()) {
    settings_saving=false;
    if(c.options_saved.contains("error")) settings_error=c.options_saved.value("error","");
    else { engine_options.original=engine_options.values; if(save_next_settings(window)) ImGui::CloseCurrentPopup(); }
   }
   ImGui::BeginDisabled(settings_saving);
   const float footer=ImGui::GetFrameHeightWithSpacing()+ImGui::GetStyle().ItemSpacing.y+
    (settings_error.empty()?0:ImGui::CalcTextSize(settings_error.c_str(),nullptr,false,ImGui::GetContentRegionAvail().x).y+ImGui::GetStyle().ItemSpacing.y);
   const char *pages[]={"Interaction","Keyboard","Game rules","Display","Theme","Fonts","CRT effects","Animations","Audio","Game tuning"};
   const char *descriptions[]={"Mouse controls and shortcuts.","Keybindings and command shortcuts.","Angband preferences for the current character.","Window mode, interface size and dungeon camera.","Interface colours, contrast and styling.","Interface and dungeon fonts.","CRT effects and intensity.","Movement, combat and environmental effects.","Sound effects and volume.","Game constants. Changes apply on the next launch."};
   ImGui::BeginChild("Settings body",ImVec2(0,-footer),ImGuiChildFlags_None,ImGuiWindowFlags_NoScrollbar);
   const bool sidebar=ImGui::GetContentRegionAvail().x>ImGui::GetFontSize()*40;
   if(sidebar) {
    ImGui::BeginChild("Settings navigation",ImVec2(ImGui::GetFontSize()*12,0),ImGuiChildFlags_Borders);
    for(int i:{0,1,2,9,3,4,5,6,7,8}) {
     if(i==0 || i==3 || i==8) {
      if(i) ImGui::Spacing();
      ImGui::TextDisabled("%s",i==0?"PLAY":i==3?"PRESENTATION":"SOUND"); ImGui::Separator();
     }
     if(ImGui::Selectable(pages[i],settings_page==i,0,ImVec2(0,ImGui::GetFrameHeight()))) settings_page=i;
    }
    const char *save_note="Changes apply when you save.";
    const float note_height=ImGui::CalcTextSize(save_note,nullptr,false,ImGui::GetContentRegionAvail().x).y;
    ImGui::SetCursorPosY(ImGui::GetCursorPosY()+std::max(ImGui::GetStyle().ItemSpacing.y,ImGui::GetContentRegionAvail().y-note_height));
    ImGui::PushStyleColor(ImGuiCol_Text,ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s",save_note);
    ImGui::PopStyleColor();
    ImGui::EndChild(); ImGui::SameLine();
   } else {
    ImGui::SetNextItemWidth(-1); ImGui::Combo("##Settings page",&settings_page,pages,10);
   }
   ImGui::PushID(settings_page);
   ImGui::BeginChild("Settings contents",ImVec2(0,0));
   AnybandUITheme::section(pages[settings_page]);
   ImGui::PushTextWrapPos(0); ImGui::TextDisabled("%s",descriptions[settings_page]); ImGui::PopTextWrapPos(); ImGui::Spacing();
   bool editing_bindings=false;
   {
    if(settings_page==9) game_tuning.draw();
    if(settings_page==3) {
     AnybandUITheme::section("Dungeon artwork");
     ImGui::SetNextItemWidth(-1);
     if(ImGui::BeginCombo("##Tileset",TilesetLibrary::sets[draft_tileset].name)) {
      for(int i=0;i<7;++i) if(ImGui::Selectable(TilesetLibrary::sets[i].name,draft_tileset==i)) draft_tileset=i;
      ImGui::EndCombo();
     }
     if(draft_tileset) {
      if(tiles.load(draft_tileset)) {
       tiles.preview(draft_tileset);
       ImGui::TextWrapped("Tiles replace dungeon lettering. Choose a cell size below, or zoom with the free camera.");
      } else ImGui::TextWrapped("%s",tiles.atlases[draft_tileset].error.c_str());
     } else ImGui::TextWrapped("Classic lettering, using your chosen dungeon font.");
     ImGui::TextDisabled("Unmapped artwork falls back to ASCII.");
     ImGui::Spacing();
     AnybandUITheme::section("Dungeon camera");
     ImGui::Checkbox("Free dungeon camera",&draft_camera.enabled);
     ImGui::TextWrapped("Explore the whole known level at your own scale. Unexplored areas remain hidden.");
     ImGui::BeginDisabled(!draft_camera.enabled);
     ImGui::Checkbox("Keep player centered",&draft_camera.follow);
     ImGui::EndDisabled();
     if(!draft_camera.enabled) {
      ImGui::SameLine(); ImGui::TextDisabled("(i)");
      if(ImGui::IsItemHovered()) ImGui::SetTooltip("For camera centering without a free camera, see \"Game rules -> Display -> Center map continuously\".");
     }
     ImGui::TextUnformatted("Glyph / tile size"); ImGui::SetNextItemWidth(-1);
     const char *automatic_size=draft_camera.enabled?"Default":"Auto-fit";
     const std::string size_label=draft_camera.custom_size?"Custom":draft_camera.fixed_size?
      (draft_camera.fixed_width?std::to_string(draft_camera.fixed_width)+" x ":"")+std::to_string(draft_camera.fixed_size)+" px":automatic_size;
     if(ImGui::BeginCombo("##Fixed cell size",size_label.c_str())) {
      if(ImGui::Selectable(automatic_size,!draft_camera.custom_size && !draft_camera.fixed_size)) { draft_camera.fixed_size=draft_camera.fixed_width=0; draft_camera.custom_size=false; }
      for(const auto dimensions:{ImVec2(8,8),ImVec2(8,16),ImVec2(12,20),ImVec2(16,16),ImVec2(16,24),ImVec2(16,32),ImVec2(24,24),ImVec2(24,32),ImVec2(32,32),ImVec2(32,48),ImVec2(48,48)}) {
       const int width=int(dimensions.x),height=int(dimensions.y);
       const auto label=std::to_string(width)+" x "+std::to_string(height)+" px";
       if(ImGui::Selectable(label.c_str(),!draft_camera.custom_size && draft_camera.fixed_width==width && draft_camera.fixed_size==height)) { draft_camera.fixed_width=width; draft_camera.fixed_size=height; draft_camera.custom_size=false; }
      }
      if(ImGui::Selectable("Custom",draft_camera.custom_size)) {
       draft_camera.custom_size=true;
       if(!draft_camera.fixed_size) draft_camera.fixed_size=24;
       if(!draft_camera.fixed_width) draft_camera.fixed_width=draft_tileset?draft_camera.fixed_size:std::clamp(int(draft_camera.fixed_size*.6f),8,64);
      }
      ImGui::EndCombo();
     }
     if(draft_camera.custom_size) {
      ImGui::TextUnformatted("Width"); ImGui::SetNextItemWidth(-1); ImGui::InputInt("##Custom cell width",&draft_camera.fixed_width);
      ImGui::TextUnformatted("Height"); ImGui::SetNextItemWidth(-1); ImGui::InputInt("##Custom cell height",&draft_camera.fixed_size);
      draft_camera.fixed_width=std::clamp(draft_camera.fixed_width,8,64); draft_camera.fixed_size=std::clamp(draft_camera.fixed_size,8,64);
     }
     ImGui::TextDisabled("%s",draft_camera.enabled?"Cell size at 100% zoom. Zoom preserves the chosen proportions.":"Width x height in pixels. Smaller cells show more dungeon.");
     ImGui::TextDisabled("Middle-drag to pan. Scroll to zoom.");
     ImGui::TextWrapped("Panning pauses following. Return to player resumes it. Changing floors recenters the view.");
     ImGui::Spacing(); AnybandUITheme::section("Window");
     ImGui::Checkbox("Fullscreen",&draft_fullscreen);
     ImGui::Spacing(); ImGui::TextUnformatted("UI scale"); ImGui::SetNextItemWidth(-1);
     char zoom[16]; SDL_snprintf(zoom,sizeof(zoom),"%.0f%%",draft_scale*100);
     if(ImGui::BeginCombo("##UI scale",zoom)) {
      for(float value:{.75f,1.f,1.25f,1.5f}) {
       char label[16]; SDL_snprintf(label,sizeof(label),"%.0f%%",value*100);
       if(ImGui::Selectable(label,draft_scale==value)) draft_scale=value;
      }
      ImGui::EndCombo();
     }

    }
    if(settings_page==4) {
     AnybandUITheme::editor(draft_theme);
    }
    if(settings_page==5) {
     if(font_library) {
      AnybandUITheme::section("Typefaces");
      font_library->picker("Interface font",draft_fonts.interface_font);
      font_library->picker("Dungeon font",draft_fonts.dungeon_font,true);
      if(ImGui::Button("Restore default fonts")) draft_fonts=FontSettings{};
      ImGui::TextDisabled("Preview only until Save and Close.");
      ImGui::Spacing();
      ImGui::BeginChild("Font preview",ImVec2(0,ImGui::GetTextLineHeight()*13),ImGuiChildFlags_Borders);
      font_library->preview(draft_fonts);
      if(crt==1) { game_draw_list=ImGui::GetWindowDrawList(); game_pos=ImGui::GetWindowPos(); game_size=ImGui::GetWindowSize(); }
      ImGui::EndChild();
     }

    }
    if(settings_page==0) {
     AnybandUITheme::section("Shortcuts");
     ImGui::Checkbox("Quick-action bar",&draft_quickbar_enabled);
     if(ImGui::IsItemHovered()) ImGui::SetTooltip("Ten slots using top-row 1-0. Numpad movement is unchanged. Right-click a slot, item, spell or command to assign.");
     ImGui::Spacing(); AnybandUITheme::section("Mouse & targeting");
     ImGui::Checkbox("Proceed with click",&draft_proceed_with_click);
     ImGui::TextWrapped("Left-click the game view to continue when messages are waiting.");
     ImGui::Spacing(); ImGui::Checkbox("Quick targeting",&draft_quick_targeting);
     if(ImGui::IsItemHovered()) ImGui::SetTooltip("Click to confirm a target and continue casting, shooting or another aimed action.");
     ImGui::Spacing(); ImGui::Checkbox("Click exits look",&draft_click_exits_look);
     ImGui::TextWrapped("Resume normal click movement when leaving look mode.");

    }
    if(settings_page==2) {
     engine_options.draw();

    }
    if(settings_page==1) { editing_bindings=true; keybinding_editor.draw();  }
    if(settings_page==8) {
     ImGui::Checkbox("Sound enabled",&draft_audio_settings.enabled);
     ImGui::Spacing(); AnybandUITheme::section("Mix");
     ImGui::BeginDisabled(!draft_audio_settings.enabled);
     auto volume=[](const char *label,float &value) {
      float percent=value*100;
      if(ImGui::SliderFloat(label,&percent,0,100,"%.0f%%",ImGuiSliderFlags_AlwaysClamp)) value=percent/100;
     };
     volume("Master volume",draft_audio_settings.master);
     volume("Gameplay",draft_audio_settings.gameplay);
     volume("Interface",draft_audio_settings.interface_volume);
     ImGui::EndDisabled();
     ImGui::Spacing(); ImGui::TextWrapped("Audio mutes when AnybandUI and its detached windows are unfocused.");
     if(!audio.error.empty()) ImGui::TextWrapped("Audio unavailable: %s",audio.error.c_str());

    }
    if(settings_page==7) {
     AnybandUITheme::section("Atmosphere & milestones");
     ImGui::Checkbox("Low health glitch",&draft_low_animation);
     if(ImGui::IsItemHovered()) ImGui::SetTooltip("Distort the CRT image below the low-hitpoint warning threshold. Requires CRT effects.");
     ImGui::Checkbox("Scene transitions",&draft_scene_animation);
     if(ImGui::IsItemHovered()) ImGui::SetTooltip("Floor changes, shops, character loading and the final death transition.");
     ImGui::Checkbox("Level up flourish",&draft_level_animation);
     ImGui::Spacing(); AnybandUITheme::section("Movement & combat");
     ImGui::Checkbox("Monster walking",&draft_movement_animation);
     ImGui::Checkbox("Teleport ripples",&draft_blink_animation);
     ImGui::Checkbox("Projectiles and spells",&draft_projectile_animation);
     ImGui::Checkbox("Combat feedback",&draft_combat_animation);
     ImGui::Spacing(); AnybandUITheme::section("Dungeon indicators");
     ImGui::Checkbox("Ground item glow",&draft_item_glow);
     if(ImGui::IsItemHovered()) ImGui::SetTooltip("Soft glows for known artifacts, runes and curses on visible ground items. Works with CRT effects off.");
     ImGui::Checkbox("Unique enemy auras",&draft_presence.uniques);
     if(ImGui::IsItemHovered()) ImGui::SetTooltip("Broken halos for visible uniques; Morgoth has a crimson corona. Arrival, waking and injury intensify the effect.");
     ImGui::Checkbox("Stairway glow",&draft_presence.stairs);
     ImGui::Checkbox("Hazardous terrain effects",&draft_presence.terrain);
     ImGui::Checkbox("Recall gathering effect",&draft_presence.recall);
     ImGui::Checkbox("Sleeping monsters",&draft_sleep_animation);
     ImGui::Checkbox("Frightened monsters",&draft_fear_animation);


    }
    if(settings_page==6) {
     ImGui::Spacing(); ImGui::TextUnformatted("Apply effects to"); ImGui::SetNextItemWidth(-1);
     const char *effects[]={"Off","Dungeon Only","Main Window Only","Full"};
     const int scope_ids[]={0,1,3,2};
     int choice=draft_crt==2?3:draft_crt==3?2:draft_crt;
     if(ImGui::Combo("##CRT Effects",&choice,effects,4)) draft_crt=scope_ids[choice];
     ImGui::Spacing(); ImGui::TextUnformatted("Effect Strength"); ImGui::SetNextItemWidth(-1);
     const char *strengths[]={"Subtle","Classic","Vivid","Zero Cool"};
     if(ImGui::BeginCombo("##CRT Effects Strength",draft_crt_strength<0?"Custom":strengths[draft_crt_strength])) {
      for(int i=0;i<4;++i) if(ImGui::Selectable(strengths[i],draft_crt_strength==i)) {
       const int lines=draft_crt_settings.raster_lines,mask=draft_crt_settings.mask;
       draft_crt_strength=i; draft_crt_settings=CrtSettings(i);
       draft_crt_settings.raster_lines=lines; draft_crt_settings.mask=mask; draft_crt_settings.tube_preset=-1;
      }
      ImGui::EndCombo();
     }
     ImGui::TextWrapped("Presets reset all effect sliders and switches.");
     ImGui::Spacing();
     if(ImGui::CollapsingHeader("Tube & phosphor layout")) {
     ImGui::Spacing(); ImGui::TextUnformatted("Tube preset"); ImGui::SetNextItemWidth(-1);
     const char *tubes[]={"Desktop Monitor","Shadow-mask Monitor","Soft Terminal"};
     if(ImGui::BeginCombo("##Tube",draft_crt_settings.tube_preset<0?"Custom":tubes[draft_crt_settings.tube_preset])) {
      for(int i=0;i<3;++i) if(ImGui::Selectable(tubes[i],draft_crt_settings.tube_preset==i)) { draft_crt_settings.tube(i); draft_crt_strength=-1; }
      ImGui::EndCombo();
     }
     ImGui::TextUnformatted("Simulated raster lines"); ImGui::SetNextItemWidth(-1);
     bool automatic_raster=draft_crt_settings.raster_lines==0;
     if(ImGui::Checkbox("Match window resolution",&automatic_raster)) {
      draft_crt_settings.raster_lines=automatic_raster?0:480; draft_crt_settings.tube_preset=-1; draft_crt_strength=-1;
     }
     if(!automatic_raster && ImGui::SliderInt("##Raster",&draft_crt_settings.raster_lines,240,1200)) { draft_crt_settings.tube_preset=-1; draft_crt_strength=-1; }
     ImGui::TextUnformatted("Phosphor layout"); ImGui::SetNextItemWidth(-1);
     const char *masks[]={"Delta RGB dots","Aperture grille","Slot mask"};
     if(ImGui::Combo("##Mask",&draft_crt_settings.mask,masks,3)) { draft_crt_settings.tube_preset=-1; draft_crt_strength=-1; }
     }
     ImGui::Spacing(); ImGui::Separator();
     const std::vector<std::pair<const char*,std::vector<int>>> groups={
      {"Light & glow",{Glow,Bloom,Glass}}, {"Screen texture",{Scanlines,Dots,Beam}},
      {"Geometry & focus",{Barrel,Edges,Fringe,Focus}}, {"Signal & interference",{Hum,Ghost,Interference}}};
     for(const auto &group:groups) if(ImGui::CollapsingHeader(group.first,ImGuiTreeNodeFlags_DefaultOpen)) {
      for(int i:group.second) {
      auto &control=draft_crt_settings.parts[i];
      ImGui::PushID(i); ImGui::Spacing();
      if(ImGui::Checkbox(crt_labels[i],&control.enabled)) { draft_crt_strength=-1; draft_crt_settings.tube_preset=-1; }
      ImGui::BeginDisabled(!control.enabled); ImGui::SetNextItemWidth(-1);
      if(ImGui::SliderFloat("##Amount",&control.value,0.f,100.f,"%.0f%%",ImGuiSliderFlags_AlwaysClamp)) { draft_crt_strength=-1; draft_crt_settings.tube_preset=-1; }
      ImGui::EndDisabled(); ImGui::PopID();
      }
     }

    }

   }
   if(!editing_bindings && keybinding_editor.listening) { keybinding_editor.listening=false; keybinding_editor.command=-1; }
   ImGui::EndChild(); ImGui::PopID(); ImGui::EndChild();
   if(!settings_error.empty()) ImGui::TextWrapped("%s",settings_error.c_str());
   if(ImGui::Button("Cancel")||(!keybinding_editor.listening && !settings_saving && ImGui::IsKeyPressed(ImGuiKey_Escape))) ImGui::CloseCurrentPopup();
   ImGui::SameLine();
   const float button_width=ImGui::CalcTextSize("Save and Close").x+2*ImGui::GetStyle().FramePadding.x;
   ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(),ImGui::GetWindowWidth()-ImGui::GetStyle().WindowPadding.x-button_width));
   if(ImGui::Button("Save and Close") && save_next_settings(window)) ImGui::CloseCurrentPopup();
   ImGui::EndDisabled();
   ImGui::EndPopup();
  }
 }
 void activate_quickbar(int slot) {
  quickbar.profile=c.character_save;
  const auto action=Quickbar::resolve(quickbar.slots()[slot],c);
  if(!action.reason.empty()) return;
  keys.clear();
  if(!action.spell.empty()) { c.command(action.command,action.item,action.spell); focus_game(); }
  else execute(action.command,action.item);
 }
 bool quickbar_event(const SDL_Event &e) {
  if(e.type==SDL_EVENT_WINDOW_FOCUS_LOST) { for(auto &held:quickbar_held) held=false; quickbar_text=0; }
  if(e.type==SDL_EVENT_TEXT_INPUT) {
   const char expected=quickbar_text; quickbar_text=0;
   if(expected) return true; // Consume the text paired with the captured physical key, on any keyboard layout.
  }
  if(e.type!=SDL_EVENT_KEY_DOWN && e.type!=SDL_EVENT_KEY_UP) return false;
  const int slot=int(e.key.scancode)-int(SDL_SCANCODE_1);
  if(e.type==SDL_EVENT_KEY_DOWN) quickbar_text=0;
  if(slot<0 || slot>=10) return false; // Keypad has distinct physical scancodes.
  if(e.type==SDL_EVENT_KEY_UP) { bool held=quickbar_held[slot]; quickbar_held[slot]=false; return held; }
  if(quickbar_held[slot] || (quickbar_enabled && owns_keyboard() && !ImGui::GetIO().WantTextInput && Quickbar::normal_play(c) &&
    !(e.key.mod&(SDL_KMOD_CTRL|SDL_KMOD_SHIFT|SDL_KMOD_ALT|SDL_KMOD_GUI)))) {
   quickbar_text=slot==9?'0':char('1'+slot);
   if(!quickbar_held[slot] && !e.key.repeat) activate_quickbar(slot);
   quickbar_held[slot]=true;
   return true;
  }
  return false;
 }
 void focus_game() { focus_requested=true; keys.clear(); }
 bool proceed_click() {
  if(!proceed_with_click || !c.state.value("message_pending",false)) return false;
  if(!c.key(32)) return false;
  focus_game(); return true;
 }
 void execute(const std::string &id,const std::string &item="") {
  if(inventory_window_drawing && c.ready()) { inventory_action=id; inventory_action_item=item; return; }
  if(id=="core.knowledge" && c.capabilities.value("knowledge",0)>0 && c.state.value("phase","")=="playing") { keys.clear(); knowledge_browser.open(c); return; }
  if(id=="core.inscribe" && c.ready()) inscription_edit=true;
  if(id=="core.character" && c.can_view_character()) { keys.clear(); open_character_sheet=true; return; }
  if(c.native_targeting() && (id=="core.look" || id=="core.target")) c.target("targeting.begin",{{"mode",id=="core.look"?"look":"target"}});
  else c.command(id,item);
  focus_game();
 }
 bool owns_keyboard() const {
  return !c.saving && c.loading.empty() && !layout.editing && c.run_report.is_null() && c.state.contains("terminal") && !c.state.contains("birth") && !c.state.contains("store") && grid_focus && window_active && c.prompt.empty() && c.pending_prompt.empty()
   && !quit_dialog && !ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId);
 }
 void prepare_frame(SDL_Window *window) {
  display_scale=SDL_GetWindowDisplayScale(window);
  if(display_scale<=0) display_scale=1.f;
  // Rebuild from the unscaled style; repeated changes must not accumulate rounding.
  ImGui::GetStyle()=base_style;
  AnybandUITheme::configure(theme_settings);
  ImGui::GetStyle().ScaleAllSizes(display_scale*scale);
  ImGui::GetStyle().FontScaleDpi=display_scale;
  ImGui::GetStyle().FontScaleMain=scale;
  if(owns_keyboard()) ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
  else ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  if(!c.prompt.empty()) return_from_prompt=true;
  else if(return_from_prompt && c.pending_prompt.empty()) { return_from_prompt=false; focus_game(); }
 }
 void launcher() {
  if(replay_prompt && c.negotiated && !c.busy) { replay_prompt=false; ImGui::OpenPopup("New character"); }
  const int choice=character_select.draw(c.saves,c.negotiated && !c.busy);
  bool rename_clicked=choice==2,delete_clicked=choice==3;
  if(choice==1) {
   const auto it=std::find_if(c.saves.begin(),c.saves.end(),[&](const json &save){return save.value("id","")==character_select.selected;});
   if(it!=c.saves.end()) {
    c.menu_error.clear(); c.send(it->value("dead",false)?"session.replay":"session.load",{{"save",character_select.selected}}); c.busy=true; focus_game();
   }
  }
  if(rename_clicked || delete_clicked) {
   managed_save=character_select.selected; SDL_strlcpy(renamed_save,managed_save.c_str(),sizeof(renamed_save)); c.menu_error.clear();
  }
  if(choice==4) { c.menu_error.clear(); ImGui::OpenPopup("New character"); }
  if(choice==5) {
   run_history.directory=fs::path(settings_path).parent_path()/"run-history";
   run_history.load(); run_history.browsing=true; run_history.current=nullptr;
  }
  if(rename_clicked) ImGui::OpenPopup("Rename save");
  if(delete_clicked) ImGui::OpenPopup("Delete save");
  if(ImGui::BeginPopupModal("Rename save",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
   ImGui::Text("Rename %s",managed_save.c_str());
   if(ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
   const bool entered=ImGui::InputText("Save name",renamed_save,sizeof(renamed_save),ImGuiInputTextFlags_EnterReturnsTrue);
   const std::string name=renamed_save;
   const bool valid=!name.empty() && name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")==std::string::npos;
   bool exists=false; for(const auto &s:c.saves) if(s.value("id","")==name) exists=true;
   if(!valid) ImGui::TextUnformatted("Use letters, numbers, hyphens or underscores.");
   else if(exists && name!=managed_save) ImGui::TextUnformatted("That save name is already in use.");
   ImGui::BeginDisabled(!valid||exists||c.busy||!c.connected);
   if(ImGui::Button("Rename") || (entered&&valid&&!exists&&!c.busy&&c.connected)) {
    c.send("saves.rename",{{"save",managed_save},{"name",name}}); c.busy=true; ImGui::CloseCurrentPopup();
   }
   ImGui::EndDisabled(); ImGui::SameLine();
   if(ImGui::Button("Cancel")||ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
   ImGui::EndPopup();
  }
  if(ImGui::BeginPopupModal("Delete save",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
   ImGui::Text("Permanently delete %s?",managed_save.c_str());
   ImGui::TextUnformatted("This cannot be undone.");
   ImGui::BeginDisabled(c.busy||!c.connected);
   if(ImGui::Button("Delete save")) { c.send("saves.delete",{{"save",managed_save}}); c.busy=true; ImGui::CloseCurrentPopup(); }
   ImGui::EndDisabled(); ImGui::SameLine();
   if(ImGui::Button("Cancel")||ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
   ImGui::EndPopup();
  }
  if(!c.menu_error.empty()) ImGui::TextWrapped("[SYSTEM] %s",c.menu_error.c_str());
  if(ImGui::BeginPopupModal("New character",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
   ImGui::TextUnformatted("Save name");
   if(ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
   const bool entered=ImGui::InputText("##save-name",save_name,sizeof(save_name),ImGuiInputTextFlags_EnterReturnsTrue);
   const std::string name=save_name;
   const bool valid=!name.empty() && name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")==std::string::npos;
   bool exists=false; for(const auto &s:c.saves) if(s.value("id","")==name) exists=true;
   if(!valid) ImGui::TextUnformatted("Use letters, numbers, hyphens or underscores.");
   else if(exists) ImGui::TextUnformatted("That save name is already in use.");
   ImGui::BeginDisabled(!valid||exists||!c.negotiated||c.busy);
   if(ImGui::Button("Create") || (entered&&valid&&!exists&&c.negotiated&&!c.busy)) {
    c.menu_error.clear(); c.send("session.new",{{"save",name},{"race",replay_profile.value("race","")},{"class",replay_profile.value("class","")}}); replay_profile=json::object(); c.busy=true; focus_game(); ImGui::CloseCurrentPopup();
   }
   ImGui::EndDisabled(); ImGui::SameLine();
   if(ImGui::Button("Cancel")||ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
   ImGui::EndPopup();
  }
 }
 void grid(float height) {
  if (!c.state.contains("terminal")) { launcher(); return; }
  ImGui::PushStyleColor(ImGuiCol_Border,owns_keyboard()?AnybandUITheme::green():ImVec4(.16f,.24f,.24f,1));
  ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize,display_scale);
  ImGui::BeginChild("Dungeon",ImVec2(0,height),ImGuiChildFlags_Borders,
   ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);
  ImGui::SetScrollX(0); ImGui::SetScrollY(0);
  if(focus_requested && c.prompt.empty() && !ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId)) {
   ImGui::SetWindowFocus(); grid_focus=true; focus_requested=false;
  }
  if(ImGui::IsWindowHovered() && ImGui::IsMouseClicked(0)) grid_focus=true;
  const auto &grid=c.game_grid;
  const size_t columns=std::max(size_t(1),grid.width);
  const bool free_camera=grid.semantic && c.state["dungeon"].value("full_level",false);
  if(free_camera) {
   dungeon_camera.update(c.state["dungeon"],c.state["player"],camera_settings.follow && !c.state.contains("targeting"));
   if(ImGui::GetContentRegionAvail().x>ImGui::GetFontSize()*25) {
    ImGui::TextDisabled("%s",camera_settings.follow && !dungeon_camera.paused?"Following player":"Free camera");
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("Middle-drag to pan. Scroll to zoom.\nPanning pauses following; Return to player resumes it.");
    ImGui::SameLine();
   }
   if(ImGui::SmallButton("-##camera")) dungeon_camera.zoom=std::max(.35f,dungeon_camera.zoom/1.15f);
   ImGui::SameLine();
   if(ImGui::SmallButton("+##camera")) dungeon_camera.zoom=std::min(4.f,dungeon_camera.zoom*1.15f);
   ImGui::SameLine();
   if(ImGui::SmallButton("Return to player")) { dungeon_camera.return_to_player(c.state["player"]); focus_game(); }
  }
  const auto start=ImGui::GetCursorScreenPos();
  const auto available=ImGui::GetContentRegionAvail();
  const ImVec2 viewport(std::max(1.f,available.x),std::max(1.f,available.y));
  dungeon_surface_pos=start; dungeon_surface_size=viewport;
  // Fit the complete semantic viewport (or fallback terminal) without scrolling.
  auto *dungeon_font=font_library?font_library->get(font_settings.dungeon()):ImGui::GetFont();
  const bool tiled=grid.semantic && tileset>0 && grid.tileset==tileset && tiles.load(tileset);
  const float cell_ratio=tiled?1.12f:(font_library?font_library->cell_ratio(font_settings.dungeon()):.60f);
  float pixels=std::min(
   std::max(.01f,viewport.x-2)/(float(columns)*cell_ratio),
   std::max(.01f,viewport.y-2)/(float(std::max(size_t(1),grid.height))*1.12f));
  const bool fixed_cells=!camera_settings.enabled && camera_settings.fixed_size>0 && c.capabilities.value("presentation.viewport",0)>0;
  if(c.capabilities.value("presentation.viewport",0)>0 && c.ready() && !layout.editing && SDL_GetTicks()-c.viewport_sent_at>=200) {
   const float cell_height=float(camera_settings.fixed_size),cell_width=camera_settings.fixed_width>0?float(camera_settings.fixed_width):cell_height*cell_ratio/1.12f;
   const int wanted_w=fixed_cells?std::clamp(int(std::max(1.f,viewport.x-2)/cell_width),9,240):0;
   const int wanted_h=fixed_cells?std::clamp(int(std::max(1.f,viewport.y-2)/cell_height),5,128):0;
   if(wanted_w!=c.viewport_width_sent || wanted_h!=c.viewport_height_sent) {
    c.send("dungeon.viewport",{{"width",wanted_w},{"height",wanted_h}});
    c.viewport_width_sent=wanted_w; c.viewport_height_sent=wanted_h; c.viewport_sent_at=SDL_GetTicks(); c.busy=true;
   }
  }
  if(fixed_cells && grid.semantic && !free_camera) pixels=std::min(pixels,float(camera_settings.fixed_size)/1.12f);
  ImGui::InvisibleButton("Dungeon keyboard surface",viewport,ImGuiButtonFlags_EnableNav|ImGuiButtonFlags_MouseButtonLeft|ImGuiButtonFlags_MouseButtonMiddle);
  float cw=pixels*cell_ratio, ch=pixels*1.12f;
  if(free_camera) {
   const auto &io=ImGui::GetIO();
   const float base=22.f*display_scale;
   const float base_h=camera_settings.fixed_size>0?float(camera_settings.fixed_size):base*1.12f;
   const float base_w=camera_settings.fixed_size>0 && camera_settings.fixed_width>0?float(camera_settings.fixed_width):base_h*cell_ratio/1.12f;
   if(ImGui::IsItemHovered() && io.MouseWheel!=0)
    dungeon_camera.zoom_at(io.MouseWheel,io.MousePos.x-start.x,io.MousePos.y-start.y,viewport.x,viewport.y,base_w,base_h,camera_settings.follow && !dungeon_camera.paused);
   dungeon_camera.dragging=ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Middle);
   if(dungeon_camera.dragging) {
    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
    dungeon_camera.pan(io.MouseDelta.x,io.MouseDelta.y,base_w*dungeon_camera.zoom,base_h*dungeon_camera.zoom);
   }
   cw=base_w*dungeon_camera.zoom; ch=base_h*dungeon_camera.zoom;
   const float glyph_ratio=font_library?font_library->cell_ratio(font_settings.dungeon()):.60f;
   pixels=std::min(ch/1.12f,cw/glyph_ratio);
   dungeon_camera.reveal_target(c.state,viewport.x/cw,viewport.y/ch);
  }
  if(fixed_cells && grid.semantic && !free_camera) {
   const float wanted_h=float(camera_settings.fixed_size);
   const float wanted_w=camera_settings.fixed_width>0?float(camera_settings.fixed_width):wanted_h*cell_ratio/1.12f;
   const float fit=std::min({1.f,std::max(.01f,viewport.x-2)/(columns*wanted_w),std::max(.01f,viewport.y-2)/(std::max(size_t(1),grid.height)*wanted_h)});
   cw=wanted_w*fit; ch=wanted_h*fit;
   const float glyph_ratio=font_library?font_library->cell_ratio(font_settings.dungeon()):.60f;
   pixels=std::min(ch/1.12f,cw/glyph_ratio);
  }
  const ImVec2 size(cw*float(columns),ch*float(grid.height));
  const ImVec2 origin=free_camera?ImVec2(start.x+viewport.x*.5f-dungeon_camera.x*cw,start.y+viewport.y*.5f-dungeon_camera.y*ch):
   ImVec2(start.x+(viewport.x-size.x)*.5f,start.y+(viewport.y-size.y)*.5f);
  if (ImGui::IsItemClicked()) grid_focus = true;
  if(ImGui::IsItemClicked()) proceed_click();
  if(ImGui::IsItemFocused()) grid_focus=true;
  auto draw=ImGui::GetWindowDrawList();
  game_draw_list=draw; game_pos=ImGui::GetWindowPos(); game_size=ImGui::GetWindowSize();
  draw->AddRectFilled(start,ImVec2(start.x+viewport.x,start.y+viewport.y),AnybandUITheme::dungeon_colour(color(0)));
  draw->PushClipRect(start,ImVec2(start.x+viewport.x,start.y+viewport.y),true);
  const int x0=std::clamp(int(std::floor((start.x-origin.x)/cw))-1,0,int(grid.width));
  const int y0=std::clamp(int(std::floor((start.y-origin.y)/ch))-1,0,int(grid.height));
  const int x1=std::clamp(int(std::ceil((start.x+viewport.x-origin.x)/cw))+1,0,int(grid.width));
  const int y1=std::clamp(int(std::ceil((start.y+viewport.y-origin.y)/ch))+1,0,int(grid.height));
  auto tile_layer=[&](int x,int y,int layer,ImVec2 offset) {
   const auto &v=c.state["dungeon"]; const auto &t=grid.tiles[y*grid.width+x];
   const ImVec2 at(origin.x+(x+offset.x)*cw,origin.y+(y+offset.y)*ch);
   if(!tiles.tile(draw,tileset,t[layer*2],t[layer*2+1],at,{cw,ch})) {
    const auto &a=v["cells"][y][x]; unsigned glyph=a[layer*2];
    if(glyph && glyph!=' ') draw->AddText(dungeon_font,pixels,at,AnybandUITheme::dungeon_colour(color(a[layer*2+1].get<int>())),utf8(glyph).c_str());
   }
  };
  if(tiled) for(int y=y0;y<y1;++y) for(int x=x0;x<x1;++x) tile_layer(x,y,0,{0,0});
  if(grid.semantic) presence.draw(draw,c.state,c.catalog,origin,size,cw,ch,double(SDL_GetTicksNS())/1e9,presence_settings,font_library?font_library->tengwar:nullptr);
  if(grid.semantic && item_glow) ItemGlow::draw(draw,c.state["dungeon"],origin,size,cw,ch,double(SDL_GetTicksNS())/1e9);
  if(tiled) {
   for(int layer=1;layer<4;++layer) for(int y=y0;y<y1;++y) for(int x=x0;x<x1;++x) {
    ImVec2 offset(0,0);
    const auto &v=c.state["dungeon"]; const auto &a=v["cells"][y][x];
    if(layer==3 && a[6].get<int>() && !a[11].get<int>() && !a[12].get<int>())
     offset=motion_feedback.offset(x+v.value("x",0),y+v.value("y",0),double(SDL_GetTicksNS())/1e9);
    tile_layer(x,y,layer,offset);
   }
  } else {
  for(int y=y0;y<y1;++y) for(int x=x0;x<x1;++x) {
   const auto &cell=grid.cells[y*grid.width+x];
   if(cell.glyph && cell.glyph!=' ') {
    const ImVec2 at(origin.x+float(x)*cw+std::max(0.f,cw-pixels*cell_ratio)*.5f,origin.y+float(y)*ch+std::max(0.f,ch-pixels*1.12f)*.5f);
    ImVec2 displacement(0,0);
    if(grid.semantic && !motion_feedback.walks.empty()) {
     const auto &view=c.state["dungeon"]; const auto &layers=view["cells"][y][x];
     if(layers[6].get<int>() && !layers[11].get<int>() && !layers[12].get<int>())
      displacement=motion_feedback.offset(int(x)+view.value("x",0),int(y)+view.value("y",0),double(SDL_GetTicksNS())/1e9);
     if(displacement.x || displacement.y) {
      unsigned glyph=0; int ink=0;
      for(int layer=0;layer<3;++layer) if(layers[layer*2].get<unsigned>()) { glyph=layers[layer*2]; ink=layers[layer*2+1]; }
      if(glyph) draw->AddText(dungeon_font,pixels,at,AnybandUITheme::dungeon_colour(color(ink)),utf8(glyph).c_str());
     }
    }
    draw->AddText(dungeon_font,pixels,{at.x+displacement.x*cw,at.y+displacement.y*ch},AnybandUITheme::dungeon_colour(color(cell.color)),utf8(cell.glyph).c_str());
   }
  }
  }
  if(grid.semantic) {
   const auto &view=c.state["dungeon"];
   const int ox=view.value("x",0),oy=view.value("y",0);
   auto outline=[&](int x,int y,ImU32 ink,float thickness) {
    x-=ox; y-=oy;
    if(x<0 || y<0 || size_t(x)>=grid.width || size_t(y)>=grid.height) return;
    const ImVec2 at(origin.x+x*cw,origin.y+y*ch);
    draw->AddRect(at,ImVec2(at.x+cw,at.y+ch),ink,0,0,thickness);
   };
   int x=0,y=0;
   const auto mouse=ImGui::GetMousePos();
   const bool hovered=ImGui::IsItemHovered() && !(free_camera && dungeon_camera.dragging) && grid_cell_at(mouse.x-origin.x,mouse.y-origin.y,cw,ch,grid.width,grid.height,x,y);
   x+=ox; y+=oy;
   // A direction prompt is already an aiming interaction. Preview its mouse
   // location locally, without entering another engine mode or sending input.
   const bool mouse_target=hovered && !c.state.value("message_pending",false) &&
    (c.state.value("aiming",false) || (quick_targeting && c.state.contains("targeting") && c.state["targeting"].value("mode","")=="target"));
   auto target_box=[&](int wx,int wy) {
    if(c.state.contains("player")) {
     const auto &p=c.state["player"];
     const int tx=wx-ox,ty=wy-oy;
     ImVec2 a,b;
     if(tx>=0 && ty>=0 && size_t(tx)<grid.width && size_t(ty)<grid.height &&
        target_connector(ImVec2(origin.x+(p.value("x",0)-ox+.5f)*cw,origin.y+(p.value("y",0)-oy+.5f)*ch),
         ImVec2(origin.x+(tx+.5f)*cw,origin.y+(ty+.5f)*ch),cw,ch,a,b)) {
      draw->PushClipRect(origin,ImVec2(origin.x+size.x,origin.y+size.y),true);
      draw->AddLine(a,b,IM_COL32(255,225,125,135),std::max(1.f,display_scale));
      draw->PopClipRect();
     }
    }
    outline(wx,wy,IM_COL32(255,225,125,255),2.f*display_scale);
   };
   const auto &io=ImGui::GetIO();
   const bool routing=hovered && c.ready() && !c.state.contains("targeting") && !c.state.value("aiming",false) && !c.state.value("direction_prompt",false) && !c.state.value("message_pending",false) && !io.KeyShift && !io.KeyCtrl && !io.KeyAlt;
   if(routing) c.preview_route(x,y);
   DungeonFeedback::route(c,draw,origin,size,cw,ch,ox,oy,routing,x,y);
   if(c.state.value("blast_radius",0)>0 && !c.state.value("message_pending",false)) {
    int bx=x,by=y;
    bool previewing=mouse_target;
    if(!previewing && c.state.contains("targeting") && c.state["targeting"].value("mode","")=="target") {
     bx=c.state["targeting"].value("x",0); by=c.state["targeting"].value("y",0); previewing=true;
    }
    if(previewing) {
     c.preview_blast(bx,by);
     if(BlastPreview::matches(c.blast,c.state,bx,by)) BlastPreview::draw(draw,c.blast,origin,size,cw,ch,ox,oy,display_scale);
    }
   }
   if(sleep_animation || fear_animation) MonsterFeedback::draw(draw,c.state,origin,size,cw,ch,double(SDL_GetTicksNS())/1e9,sleep_animation,fear_animation);
   motion_feedback.draw(draw,view,origin,cw,ch,double(SDL_GetTicksNS())/1e9);
   projectile_feedback.draw(draw,origin,size,cw,ch,ox,oy,double(SDL_GetTicksNS())/1e9);
   combat_feedback.draw(draw,origin,size,cw,ch,ox,oy,double(SDL_GetTicksNS())/1e9);
   if(mouse_target) {
    target_box(x,y);
   } else if(c.state.contains("targeting")) {
    const auto &t=c.state["targeting"];
    for(const auto &point:t["path"]) {
     const int px=point[0].get<int>()-ox,py=point[1].get<int>()-oy;
     if(px>=0 && py>=0 && size_t(px)<grid.width && size_t(py)<grid.height)
      draw->AddCircleFilled(ImVec2(origin.x+(px+.5f)*cw,origin.y+(py+.5f)*ch),std::max(1.f,display_scale),IM_COL32(240,205,100,190));
    }
    target_box(t.value("x",0),t.value("y",0));
   } else if(c.state.contains("selected_target")) {
    const auto &t=c.state["selected_target"];
    outline(t.value("x",0),t.value("y",0),IM_COL32(210,175,85,200),display_scale);
   }
   const bool tooltip_allowed=hovered && c.ready() && window_active && !c.state.contains("targeting") &&
    !c.state.value("aiming",false) && !c.state.value("direction_prompt",false) && !c.state.value("message_pending",false) &&
    !ImGui::IsAnyMouseDown() && !ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId|ImGuiPopupFlags_AnyPopupLevel);
   if(dungeon_tooltip.dwell(tooltip_allowed,c.state.value("context",""),x,y,ImGui::GetTime())) dungeon_tooltip.draw(c.state,c.catalog);
   if(hovered && !c.state.value("message_pending",false)) {
    if(!mouse_target) outline(x,y,IM_COL32(140,185,220,190),display_scale);
    if(ImGui::IsMouseClicked(0) && c.native_targeting() && !c.busy && !c.state.value("message_pending",false)) {
     const bool active=c.state.contains("targeting") || c.state.value("aiming",false) || c.state.value("direction_prompt",false);
     if(active || (c.ready() && c.mouse_movement())) {
      const bool exit_look=click_exits_look && c.state.contains("targeting") && c.state["targeting"].value("mode","")=="look";
      c.target(active && !exit_look?"targeting.select":"dungeon.click",{{"x",x},{"y",y},{"exit_look",exit_look},{"confirm",quick_targeting},{"shift",io.KeyShift},{"control",io.KeyCtrl},{"alt",io.KeyAlt}});
      focus_game();
     }
    }
    if(ImGui::IsMouseClicked(1) && c.native_targeting() && !c.state.value("message_pending",false)) {
     grid_menu_x=x; grid_menu_y=y; grid_menu_context=c.state.value("context","");
     keys.clear(); ImGui::OpenPopup("Dungeon actions");
    }
   }
  }
  if(ImGui::BeginPopup("Dungeon actions")) {
   const bool active=c.state.contains("targeting") || c.state.value("aiming",false) || c.state.value("direction_prompt",false);
   const bool usable=!c.busy && grid_menu_context==c.state.value("context","") && c.state.contains("dungeon") && !c.state.value("message_pending",false);
   auto choose=[&](const char *label,const char *method,json extra=json::object()) {
    if(ImGui::MenuItem(label)) {
     extra["x"]=grid_menu_x; extra["y"]=grid_menu_y;
     c.target(method,std::move(extra)); focus_game();
    }
   };
   ImGui::BeginDisabled(!usable);
   if(active) {
    choose("Select tile","targeting.select");
    if(ImGui::MenuItem("Cancel")) { c.target("targeting.control",{{"operation","cancel"}}); focus_game(); }
   } else {
    ImGui::BeginDisabled(!c.ready());
    ImGui::BeginDisabled(!c.mouse_movement());
    choose("Move here","dungeon.click");
    ImGui::EndDisabled();
    ImGui::Separator();
    bool pickup=false;
    if(c.capabilities.value("interaction.pickup",0)>0 && c.state.contains("dungeon")) {
     const auto &view=c.state["dungeon"];
     const int x=grid_menu_x-view.value("x",0), y=grid_menu_y-view.value("y",0);
     if(x>=0 && y>=0 && x<view.value("width",0) && y<view.value("height",0))
      pickup=view["cells"][y][x][4].get<int>()!=0;
    }
    if(pickup) choose("Pick up","dungeon.pickup");
    bool contextual=pickup;
    if(c.capabilities.value("interaction.terrain",0)>0 && c.state.contains("terrain_actions"))
     for(const auto &entry:c.state["terrain_actions"]) if(entry.value("x",-1)==grid_menu_x && entry.value("y",-1)==grid_menu_y) {
      const std::string action=entry.value("action","");
      if(action=="tunnel" || action=="up" || action=="down" || action=="disarm" || action=="open" || action=="close") {
       contextual=true;
       choose(action=="tunnel"?"Tunnel":action=="up"?"Go up":action=="down"?"Go down":
        action=="disarm"?"Disarm":action=="open"?"Open":"Close","dungeon.terrain",{{"action",action}});
      }
     }
    if(c.capabilities.value("knowledge",0)>0 && c.state.contains("monsters"))
     for(const auto &monster:c.state["monsters"]) if(monster.value("visible",false) && monster.value("x",-1)==grid_menu_x && monster.value("y",-1)==grid_menu_y && monster.contains("race_id")) {
      contextual=true;
      if(ImGui::MenuItem("Inspect")) inspect_creature(monster.value("race_id",-1));
      break;
     }
    if(contextual) ImGui::Separator();
    choose("Look","targeting.begin",{{"mode","look"}});
    choose("Target","targeting.set");
    ImGui::EndDisabled();
   }
   ImGui::EndDisabled(); ImGui::EndPopup();
  }
  if(!grid.semantic) dungeon_tooltip.reset();
  if(!grid.semantic && c.state.contains("cursor")) {
   const auto &cursor=c.state["cursor"];
   const int x=cursor.value("x",-1), y=cursor.value("y",-1);
   if(cursor.value("visible",false) && x>=0 && y>=0 && size_t(y)<grid.height && size_t(x)<columns) {
    const ImVec2 p(origin.x+x*cw,origin.y+y*ch);
    draw->AddRect(p,ImVec2(p.x+cw,p.y+ch),IM_COL32(255,225,125,255),0,0,std::max(1.f,display_scale));
   }
  }

  draw->PopClipRect();
  c.transitions.dungeon(draw,start,viewport,double(SDL_GetTicksNS())/1e9,CharacterSelect::accent(c.character_save),dungeon_font,cell_ratio);
  if(grid.semantic) c.transitions.remember_camera(free_camera,{origin.x-start.x,origin.y-start.y},pixels,cw,ch);
  DungeonFeedback::ribbon(c,draw,start,viewport,proceed_with_click);

  if(!ImGui::IsWindowFocused()) grid_focus=false;
  ImGui::EndChild(); ImGui::PopStyleVar(); ImGui::PopStyleColor();
 }
 void character() {
  if(c.state.contains("player") && CharacterOverview::draw(c.state["player"],c.can_view_character(),level_animation?&c.level_feedback:nullptr,double(SDL_GetTicksNS())/1e9,layout.heading(WorkspaceLayout::Character))) execute("core.character");
 }
 void tile_details(int x,int y,bool full) {
  ImGui::Text("Tile %d, %d",x,y);
  const auto &view=c.state["dungeon"];
  const int vx=x-view.value("x",0),vy=y-view.value("y",0);
  if(vx>=0 && vy>=0 && vx<view.value("width",0) && vy<view.value("height",0)) {
   const int feature=view["cells"][vy][vx][8];
   if(c.catalog.contains("features") && feature>=0 && size_t(feature)<c.catalog["features"].size())
    ImGui::TextWrapped("%s",display_label(c.catalog["features"][feature].value("name","Unknown terrain")).c_str());
   if(view["cells"][vy][vx][11].get<int>()) ImGui::TextUnformatted("Hallucinating");
  }
  if(c.state.contains("player") && c.state["player"].value("x",-1)==x && c.state["player"].value("y",-1)==y)
   ImGui::TextUnformatted(c.state["player"].value("name","You").c_str());
  if(c.state.contains("monsters")) for(const auto &m:c.state["monsters"]) {
   if(m.value("x",-1)!=x || m.value("y",-1)!=y || !m.value("visible",false)) continue;
   ImGui::TextWrapped("%s",display_label(m.value("name","")).c_str());
   ImGui::Text("HP %d / %d",m.value("hp",0),m.value("max_hp",0));
   if(m.contains("condition")) ImGui::TextWrapped("%s",display_label(m.value("condition","")).c_str());
   else if(m.value("asleep",false)) ImGui::TextUnformatted("Asleep");
  }
  if(c.state.contains("items")) for(const auto &o:c.state["items"]) {
   if(o.value("location","")!="Floor" || o.value("x",-1)!=x || o.value("y",-1)!=y) continue;
   ImGui::PushStyleColor(ImGuiCol_Text,ui_color(o.value("name_color",1)));
   ImGui::TextWrapped("%s",o.value("label","").c_str());
   ImGui::PopStyleColor();
   if(full) ImGui::TextWrapped("%s",o.value("description","").c_str());
  }
 }
 void targeting_panel() {
  if(c.state.value("blast_radius",0)>0) {
   ImGui::TextColored(ImVec4(.96f,.75f,.32f,1),"Blast radius %d",c.state.value("blast_radius",0));
   if(ImGui::IsItemHovered()) ImGui::SetTooltip("Amber outline: expected blast area, based on known terrain.\nUnseen changes may affect the actual blast.");
  }
  const bool aiming=c.state.value("aiming",false);
  auto action=[&](const char *label,const char *operation) {
   if(ImGui::Button(label)) { c.target("targeting.control",{{"operation",operation}}); focus_game(); }
  };
  ImGui::BeginDisabled(c.busy);
  if(c.state.value("direction_prompt",false)) {
   ImGui::TextWrapped("Choose a direction or click a tile. Escape cancels.");
   action("Cancel","cancel");
  } else if(aiming) {
   ImGui::TextWrapped("Choose a direction, or click a tile to aim. Escape cancels.");
   action("Choose target","target"); ImGui::SameLine(); action("Cancel","cancel");
  } else {
   const auto &t=c.state["targeting"];
   ImGui::TextUnformatted(t.value("mode","")=="look"?"Looking":"Targeting");
   ImGui::TextUnformatted(t.value("interesting",false)?"Interesting tiles":"Free cursor");
   ImGui::BeginDisabled(!t.value("can_confirm",false)); action("Set target [t]","confirm"); ImGui::EndDisabled();
   ImGui::SameLine(); action("Cancel [Esc]","cancel");
   action("Previous [-]","previous"); ImGui::SameLine(); action("Next [+]","next");
   action("Free [o]","free"); ImGui::SameLine(); action("Interesting [m]","interesting");
   ImGui::TextWrapped("Directions move the cursor. Click selects a tile; t confirms. r opens recall.");
   ImGui::Separator(); tile_details(t.value("x",0),t.value("y",0),true);
  }
  ImGui::EndDisabled();
 }
 static const char *item_action_label(const std::string &id) {
  return id=="core.browse"?"Browse spells":id=="core.wield"?"Wield / wear":id=="core.use"?"Use":id=="core.quaff"?"Quaff":id=="core.read"?"Read":id=="core.eat"?"Eat":id=="core.fire"?"Fire":id=="core.throw"?"Throw":id=="core.takeoff"?"Take off":id=="core.drop"?"Drop":"Inscribe";
 }
 void floor_items() {
  const auto items=FloorItems::collect(c.state);
  if(items.empty()) return;
  AnybandUITheme::section("On this tile");
  // Bound large piles so equipment and inspection remain within easy reach.
  const float height=std::min(3.f,float(items.size()))*(ImGui::GetFrameHeight()+2*ImGui::GetStyle().CellPadding.y);
  if(ImGui::BeginTable("Floor items",3,ImGuiTableFlags_ScrollY|ImGuiTableFlags_RowBg,ImVec2(0,height))) {
   ImGui::TableSetupColumn("Item",ImGuiTableColumnFlags_WidthStretch);
   ImGui::TableSetupColumn("Qty",ImGuiTableColumnFlags_WidthFixed,ImGui::GetFontSize()*2.5f);
   ImGui::TableSetupColumn("Action",ImGuiTableColumnFlags_WidthFixed,ImGui::CalcTextSize("Pick up").x+2*ImGui::GetStyle().FramePadding.x);
   for(const auto *item:items) {
    const auto &o=*item; const auto id=o.value("id","");
    ImGui::PushID(id.c_str()); ImGui::TableNextRow(); ImGui::TableNextColumn();
    ImGui::PushStyleColor(ImGuiCol_Text,ui_color(o.value("name_color",1)));
    if(AnybandUITheme::table_choice(o.value("label","").c_str(),selected==id,ImGuiSelectableFlags_None,"",ImGui::GetFrameHeight())) selected=id;
    ImGui::PopStyleColor();
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("%s",o.value("label","").c_str());
    ImGui::TableNextColumn(); ImGui::AlignTextToFramePadding(); ImGui::Text("%d",o.value("quantity",1));
    ImGui::TableNextColumn();
    ImGui::BeginDisabled(!c.ready() || !o.value("can_pickup",false));
    if(ImGui::Button("Pick up")) execute("core.pickup",id);
    ImGui::EndDisabled();
    if(!o.value("can_pickup",false) && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("You cannot carry this item right now.");
    ImGui::PopID();
   }
   ImGui::EndTable();
  }
  ImGui::Spacing();
 }
 void items() {
  floor_items();
  if(ImGui::BeginTabBar("Item categories",ImGuiTabBarFlags_DrawSelectedOverline)) {
   const char *tabs[]={"Pack","Equipment","Quiver"};
   for(int category=0;category<3;++category) if(ImGui::BeginTabItem(tabs[category])) {
    ImGui::PushID(tabs[category]); items_category(category); ImGui::PopID();
    ImGui::EndTabItem();
   }
   ImGui::EndTabBar();
  }
 }
 void items_category(int category,bool item_window=false) {
  ImGui::SetNextItemWidth(-1); ImGui::InputTextWithHint("##items","Search items",item_filter,sizeof(item_filter));
  if(!c.state.contains("items")) return;
  std::vector<const json*> values;
  for(const auto &o:c.state["items"]) {
   const auto location=o.value("location","");
   const bool equipment=location!="Pack" && location!="Quiver" && location!="Floor" && location!="Store" && location!="Home";
   if(category==0 ? location!="Pack" : category==2 ? location!="Quiver" : !equipment) continue;
   values.push_back(&o);
  }
  std::stable_sort(values.begin(),values.end(),[](const json *a,const json *b){return a->value("location","")<b->value("location","");});
  if(item_window && values.empty()) ImGui::TextDisabled("%s",category==1?"Nothing equipped.":"Your pack is empty.");
  const auto visible_count=std::count_if(values.begin(),values.end(),[&](const json *o) { return matches(o->value("label",""),item_filter); });
  const float row_height=ImGui::GetTextLineHeight()+2*ImGui::GetStyle().CellPadding.y;
  const float list_height=row_height*float(std::clamp(int(visible_count)+1,2,11));
  if(ImGui::BeginTable("items",category==1?3:2,ImGuiTableFlags_Resizable|ImGuiTableFlags_RowBg|ImGuiTableFlags_ScrollY,ImVec2(0,item_window?std::min(list_height,std::max(row_height*2,ImGui::GetContentRegionAvail().y*.42f)):list_height))) {
   ImGui::TableSetupColumn("Item",ImGuiTableColumnFlags_WidthStretch,1.f);
   if(category==1) ImGui::TableSetupColumn("Slot",ImGuiTableColumnFlags_WidthFixed,ImGui::GetFontSize()*6.f);
   ImGui::TableSetupColumn("Qty",ImGuiTableColumnFlags_WidthFixed,ImGui::GetFontSize()*2.5f);
   ImGui::TableSetupScrollFreeze(0,1); ImGui::TableHeadersRow();
   for(const auto *item:values) {
    const auto &o=*item;
    std::string label=o.value("label","");
    if(!matches(label,item_filter)) continue;
    auto id=o.value("id",""); ImGui::PushID(id.c_str());
    ImGui::TableNextRow(); ImGui::TableNextColumn();
    ImGui::PushStyleColor(ImGuiCol_Text,ui_color(o.value("name_color",1)));
    const auto *change=c.inventory_changes.find(o);
    const std::string badge=change?(change->fresh?"NEW":"+"+std::to_string(change->amount)):"";
    if(change) ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1,IM_COL32(67,125,85,65));
    if(AnybandUITheme::table_choice(label.c_str(),selected==id,ImGuiSelectableFlags_SpanAllColumns,badge.c_str())) {
     selected=id; c.inventory_changes.acknowledge(o);
    }
    // A deliberate hover reads the item without requiring a click. Keep the
    // badge for this frame so acknowledging cannot invalidate a borrowed pointer.
    const bool read=ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal|ImGuiHoveredFlags_NoSharedDelay) && !ImGui::GetDragDropPayload();
    if(read) c.inventory_changes.acknowledge(o);
    ImGui::PopStyleColor();
    if(quickbar_enabled) quickbar.drag_source(Quickbar::default_item_binding(o));
    if(ImGui::BeginPopupContextItem("Item actions")) {
     selected=id; c.inventory_changes.acknowledge(o);
     ImGui::TextDisabled("%s",label.c_str()); ImGui::Separator();
     ImGui::BeginDisabled(!c.ready());
     for(const auto &action:o.value("actions",json::array())) {
      const auto command=action.get<std::string>();
      if(ImGui::MenuItem(item_action_label(command))) execute(command,id);
     }
     item_rules_panel.context(c,o);
     ImGui::EndDisabled();
     if(quickbar_enabled) quickbar.item_menu(o);
     ImGui::EndPopup();
    }

    if(ImGui::IsItemHovered() && !ImGui::GetDragDropPayload()) {
     ImGui::BeginTooltip(); ImGui::TextUnformatted(label.c_str());
     if(!badge.empty()) ImGui::TextColored(AnybandUITheme::green(),"%s",badge=="NEW"?"Newly acquired":(badge+" acquired").c_str());
     const auto inscription=o.value("inscription","");
     if(!inscription.empty()) ImGui::Text("Inscription: %s",inscription.c_str());
     ImGui::EndTooltip();
    }
    if(category==1) { ImGui::TableNextColumn(); ImGui::TextUnformatted(display_label(o.value("location","")).c_str()); }
    ImGui::TableNextColumn(); ImGui::Text("%d",o.value("quantity",0)); ImGui::PopID();
   }
   ImGui::EndTable();
  }
  if(!item_window) for(const auto *item:FloorItems::collect(c.state)) values.push_back(item);
  for(const auto *item:values) if(item->value("id","")==selected) {
   const auto &o=*item;
   AnybandUITheme::section("Inspection");
   ImGui::BeginDisabled(!c.ready());
   bool first=true;
   if(o.value("on_player_tile",false)) {
    ImGui::BeginDisabled(!o.value("can_pickup",false));
    if(ImGui::Button("Pick up")) execute("core.pickup",selected);
    ImGui::EndDisabled(); first=false;
   }
   for(const auto &action:o.value("actions",json::array())) {
    const std::string id=action.get<std::string>();
    const char *label=item_action_label(id);
    const float right=ImGui::GetCursorScreenPos().x+ImGui::GetContentRegionAvail().x;
    if(!first && ImGui::GetItemRectMax().x+ImGui::GetStyle().ItemSpacing.x+ImGui::CalcTextSize(label).x+2*ImGui::GetStyle().FramePadding.x<right) ImGui::SameLine();
    first=false;
    if(ImGui::Button(label)) execute(id,selected);
   }
   ImGui::EndDisabled();
   ImGui::Spacing();
   ImGui::PushStyleColor(ImGuiCol_Text,ui_color(o.value("name_color",1)));
   ImGui::TextWrapped("%s",o.value("label","").c_str()); ImGui::PopStyleColor();
   ImGui::PushStyleVar(ImGuiStyleVar_CellPadding,ImVec2(ImGui::GetFontSize()*.4f,ImGui::GetFontSize()*.3f));
   if(ImGui::BeginTable("Item facts",2,ImGuiTableFlags_SizingStretchSame|ImGuiTableFlags_BordersInnerV|ImGuiTableFlags_RowBg)) {
    const auto known=o.value("player_known",json::object());
    for(auto it=known.begin();it!=known.end();++it) if(it.value().is_primitive()) {
     ImGui::TableNextColumn();
     properties(json::object({{it.key(),it.value()}}));
    }
    ImGui::TableNextRow(); ImGui::TableNextColumn();
    ImGui::TextDisabled("Quantity"); ImGui::SameLine(); ImGui::Text("%d",o.value("quantity",0));
    ImGui::TableNextColumn(); ImGui::TextDisabled("%s",category==1 && o.value("location","")!="Floor"?"Slot":"Location"); ImGui::SameLine();
    ImGui::TextWrapped("%s",display_label(o.value("location","")).c_str());
    ImGui::EndTable();
   }
   ImGui::PopStyleVar();
   const auto description=o.value("description","");
   ItemComparison::draw(c,o);
   if(!description.empty()) { ImGui::Spacing(); ItemDescription::draw(o); }
  }
 }
 // Browse the current snapshot without parking the engine in an item prompt.
 // Actions are dispatched only after closing this modal, so native quantity,
 // inscription and targeting flows can take ownership of input immediately.
 void inventory_window() {
  if((c.inventory_requested || c.equipment_requested) && c.ready()) {
   inventory_category=c.equipment_requested?1:0;
   c.equipment_requested=false; c.inventory_requested=false; inventory_window_open=true; keys.clear(); grid_focus=false;
   inventory_filter[0]=0; ImGui::OpenPopup(inventory_category==1?"Equipment":"Inventory");
  }
  if(!inventory_window_open) return;
  const auto vp=ImGui::GetMainViewport();
  ImGui::SetNextWindowPos({vp->WorkPos.x+vp->WorkSize.x*.5f,vp->WorkPos.y+vp->WorkSize.y*.5f},ImGuiCond_Appearing,{.5f,.5f});
  ImGui::SetNextWindowSize({std::min(vp->WorkSize.x-24,ImGui::GetFontSize()*52),std::min(vp->WorkSize.y-24,ImGui::GetFontSize()*34)},ImGuiCond_Appearing);
  bool keep_open=true;
  if(ImGui::BeginPopupModal(inventory_category==1?"Equipment":"Inventory",&keep_open,ImGuiWindowFlags_NoSavedSettings)) {
   inventory_action.clear(); inventory_action_item.clear();
   const auto included=[&](const json &o) {
    const auto location=o.value("location","");
    return inventory_category==0?location=="Pack":location!="Pack" && location!="Quiver" && location!="Floor" && location!="Store" && location!="Home";
   };
   bool selected_exists=false;
   for(const auto &o:c.state.value("items",json::array())) if(included(o) && o.value("id","")==inventory_selected) selected_exists=true;
   if(!selected_exists) {
    inventory_selected.clear();
    for(const auto &o:c.state.value("items",json::array())) if(included(o)) { inventory_selected=o.value("id",""); break; }
   }
   AnybandUITheme::section(inventory_category==1?"Equipped items":"Pack");
   ImGui::BeginChild("Item browser",{0,-ImGui::GetFrameHeightWithSpacing()});
   std::swap(selected,inventory_selected); std::swap(item_filter,inventory_filter);
   inventory_window_drawing=true; items_category(inventory_category,true); inventory_window_drawing=false;
   std::swap(selected,inventory_selected); std::swap(item_filter,inventory_filter);
   ImGui::EndChild();
   const bool close=ImGui::Button("Close") || ImGui::IsKeyPressed(ImGuiKey_Escape) || !keep_open || !inventory_action.empty() || !c.connected;
   if(close) { inventory_window_open=false; ImGui::CloseCurrentPopup(); focus_game(); }
   ImGui::EndPopup();
  }
  if(!keep_open && inventory_window_open) { inventory_window_open=false; focus_game(); }
  if(!inventory_action.empty()) {
   const auto command=inventory_action,item=inventory_action_item; inventory_action.clear(); inventory_action_item.clear(); execute(command,item);
  }
 }
 void inspect_creature(int race) {
  if(race<0 || c.capabilities.value("knowledge",0)<1) return;
  c.creature_race=race; c.creature_detail=nullptr; keys.clear();
  c.creature_request=c.send("knowledge.get",{{"category","creatures"},{"id",race},{"watch",c.capabilities.value("knowledge.watch",0)>0}});
  select_creatures_tab=true;
 }
 void creatures() {
  if(c.creature_race>=0) {
   if(ImGui::Button("Back to creatures")) { if(c.capabilities.value("knowledge.watch",0)>0) c.send("knowledge.unwatch"); c.creature_race=-1; c.creature_detail=nullptr; c.creature_request.clear(); }
   else {
    ImGui::Spacing();
    if(c.creature_detail.is_null()) ImGui::TextDisabled("Loading creature recall...");
    else if(c.creature_detail.contains("error")) ImGui::TextWrapped("%s",c.creature_detail.value("error","").c_str());
    else KnowledgeBrowser::details(c.creature_detail);
    return;
   }
  }
  if(!c.state.contains("monsters")) return;
  bool any=false;
  for(const auto &m:c.state["monsters"]) {
   if(!m.value("visible",false)) continue;
   any=true; ImGui::PushID(m.value("id","").c_str());
   const bool can_inspect=c.capabilities.value("knowledge",0)>0 && m.contains("race_id");
   ImGui::PushStyleColor(ImGuiCol_Text,ui_color(m.value("color",1)));
   if(ImGui::Selectable(display_label(m.value("name","")).c_str()) && can_inspect) inspect_creature(m.value("race_id",-1));
   ImGui::PopStyleColor();
   if(ImGui::IsItemHovered()) ImGui::SetTooltip("%s\nPosition %d, %d%s",m.value("asleep",false)?"Asleep":"Awake",m.value("x",0),m.value("y",0),can_inspect?"\nClick to inspect":"");
   if(can_inspect && ImGui::BeginPopupContextItem("Creature actions")) {
    if(ImGui::MenuItem("Inspect")) inspect_creature(m.value("race_id",-1));
    ImGui::EndPopup();
   }
   ImGui::PopID();
  }
  if(!any) ImGui::TextDisabled("No creatures in sight.");
 }
 void minimap() {
  if(map_overview.draw(c.state,c.catalog)) { grid_focus=false; keys.clear(); }
 }
 bool item_selection_prompt(bool fresh) {
  std::vector<const json*> rows;
  if(c.state.contains("items")) for(const auto &item:c.state["items"]) {
   if(item.value("location","")=="Floor") {
    const auto &player=c.state["player"];
    if(item.value("x",-1)!=player.value("x",-2) || item.value("y",-1)!=player.value("y",-2)) continue;
   }
   rows.push_back(&item);
  }
  auto option_for=[&](const std::string &id)->const json* {
   for(const auto &option:c.prompt["choices"]) if(option.value("item_id","")==id) return &option;
   return nullptr;
  };
  rows.erase(std::remove_if(rows.begin(),rows.end(),[&](const json *item){ return !option_for(item->value("id","")); }),rows.end());
  if(fresh) {
   prompt_item.clear();
   for(const auto *item:rows) if(option_for(item->value("id",""))) { prompt_item=item->value("id",""); break; }
  }
  int selected_row=0;
  for(size_t i=0;i<rows.size();++i) if(rows[i]->value("id","")==prompt_item) selected_row=int(i);
  if(!rows.empty()) {
   if(ImGui::IsKeyPressed(ImGuiKey_UpArrow)) selected_row=std::max(0,selected_row-1);
   if(ImGui::IsKeyPressed(ImGuiKey_DownArrow)) selected_row=std::min(int(rows.size())-1,selected_row+1);
   prompt_item=rows[selected_row]->value("id","");
  }
  std::string answer;
  ImGui::TextDisabled("Double-click an item or press Enter to choose it.");
  const float height=std::min(ImGui::GetTextLineHeightWithSpacing()*float(std::clamp(int(rows.size())+1,3,16)),ImGui::GetMainViewport()->WorkSize.y*.48f);
  if(ImGui::BeginTable("Item choices",4,ImGuiTableFlags_RowBg|ImGuiTableFlags_Resizable|ImGuiTableFlags_ScrollY,ImVec2(0,height))) {
   ImGui::TableSetupColumn("Key",ImGuiTableColumnFlags_WidthFixed,ImGui::GetFontSize()*2);
   ImGui::TableSetupColumn("Item",ImGuiTableColumnFlags_WidthStretch);
   ImGui::TableSetupColumn("Location"); ImGui::TableSetupColumn("Qty"); ImGui::TableSetupScrollFreeze(0,1); ImGui::TableHeadersRow();
   for(const auto *item:rows) {
    const auto id=item->value("id",""); const auto *option=option_for(id);
    ImGui::PushID(id.c_str()); ImGui::TableNextRow(); ImGui::TableNextColumn();
    if(!option) ImGui::PushStyleColor(ImGuiCol_Text,ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextUnformatted(option?option->value("shortcut","").c_str():""); ImGui::TableNextColumn();
    ImGui::PushStyleColor(ImGuiCol_Text,option?ui_color(item->value("name_color",1)):ImGui::GetColorU32(ImGuiCol_TextDisabled));
    if(AnybandUITheme::table_choice(item->value("label","").c_str(),prompt_item==id,ImGuiSelectableFlags_SpanAllColumns|ImGuiSelectableFlags_AllowDoubleClick)) {
     prompt_item=id;
     if(option && ImGui::IsMouseDoubleClicked(0)) answer=option->value("id","");
    }
    ImGui::PopStyleColor();
    if(prompt_item==id && (fresh || ImGui::IsKeyPressed(ImGuiKey_UpArrow) || ImGui::IsKeyPressed(ImGuiKey_DownArrow))) ImGui::SetScrollHereY();
    ImGui::TableNextColumn(); ImGui::TextUnformatted(display_label(item->value("location","")).c_str());
    ImGui::TableNextColumn(); ImGui::Text("%d",item->value("quantity",0));
    if(!option) ImGui::PopStyleColor(); ImGui::PopID();
   }
   ImGui::EndTable();
  }
  // Inventory/equipment can reuse a letter. Only unambiguous shortcuts select;
  // arrows and Enter always work across the complete unified list.
  for(const auto &option:c.prompt["choices"]) {
   const auto key=option.value("shortcut",""); int matches=0;
   if(key.size()!=1 || key[0]<'a' || key[0]>'z') continue;
   for(const auto &other:c.prompt["choices"]) if(other.value("shortcut","")==key) ++matches;
   if(matches==1 && ImGui::IsKeyPressed(ImGuiKey(ImGuiKey_A+key[0]-'a'))) answer=option.value("id","");
  }
  ImGui::BeginChild("Choice inspection",ImVec2(0,ImGui::GetTextLineHeightWithSpacing()*5));
  for(const auto *item:rows) if(item->value("id","")==prompt_item) {
   ImGui::TextWrapped("%s",item->value("description","").c_str());
   if(!option_for(prompt_item)) ImGui::TextDisabled("Not available for this action.");
  }
  ImGui::EndChild();
  const auto *chosen=option_for(prompt_item);
  ImGui::BeginDisabled(!chosen);
  if(ImGui::Button("Choose") || (chosen && ImGui::IsKeyPressed(ImGuiKey_Enter))) answer=chosen->value("id","");
  ImGui::EndDisabled(); ImGui::SameLine();
  if(!answer.empty()) { c.answer(answer); return true; }
  return false;
 }
 QuantityPicker quantity_picker;
 void prompts() {
  if(c.prompt.empty()) return;
  auto id=c.prompt.value("prompt_id","");
  const bool fresh=id!=last_prompt;
  const bool rest_selection=c.prompt.value("selection_kind","")=="rest";
  const bool item_selection=c.prompt.value("selection_kind","")=="item";
  const bool spell_selection=c.prompt.value("selection_kind","")=="spell";
  if(fresh) { last_prompt=id; SDL_strlcpy(prompt_text,c.prompt.value("initial","").c_str(),sizeof(prompt_text)); }
  const bool editing_inscription=inscription_edit && c.prompt.value("type","")=="text";
  const bool quantity_selection=c.prompt.value("type","")=="quantity";
  const char *prompt_title=quantity_selection?"Choose quantity":rest_selection?"Rest":editing_inscription?"Item inscription":"Angband asks";
  if(!ImGui::IsPopupOpen(prompt_title)) ImGui::OpenPopup(prompt_title);
  if(quantity_selection) ImGui::SetNextWindowSize(ImVec2(std::min(ImGui::GetMainViewport()->WorkSize.x-24,ImGui::GetFontSize()*30),0),ImGuiCond_Always);
  if(item_selection || spell_selection) ImGui::SetNextWindowSize(ImVec2(std::min(ImGui::GetMainViewport()->WorkSize.x-24,ImGui::GetFontSize()*48),0),ImGuiCond_Always);
  if(ImGui::BeginPopupModal(prompt_title,nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
   if(!rest_selection) ImGui::TextWrapped("%s",c.prompt.value("text","").c_str());
   auto type=c.prompt.value("type",""); bool answered=false;
   if(rest_selection) { answered=rest_dialog.draw(c,fresh); }
   else if(quantity_selection) { answered=quantity_picker.draw(c,fresh); }
   else if(type=="confirmation") {
    if(ImGui::Button("Yes")) { c.answer(true); answered=true; } ImGui::SameLine();
    if(ImGui::Button("No")) { c.answer(false); answered=true; }
   } else if(type=="choice" && spell_selection) {
    answered=spell_panel.prompt(c,fresh);
   } else if(type=="choice" && item_selection) {
    answered=item_selection_prompt(fresh);
   } else if(type=="choice") {
    for (const auto &option:c.prompt.value("choices",json::array())) {
     if(ImGui::Selectable(option.value("label","").c_str())) { c.answer(option.at("id")); answered=true; break; }
    }
   } else {
    if(editing_inscription) {
     ImGui::TextDisabled("Current: %s",c.prompt.value("initial","").c_str());
     ItemRules::inscription_help();
    }
    if(fresh) ImGui::SetKeyboardFocusHere();
    const bool submitted=ImGui::InputText("##answer",prompt_text,std::min(sizeof(prompt_text),size_t(std::max(0,c.prompt.value("maximum",4095)))+1),ImGuiInputTextFlags_EnterReturnsTrue);
    if(ImGui::Button(editing_inscription?"Save inscription":"OK") || submitted) {
     c.answer(std::string(prompt_text)); answered=true;
    }
   }
   if(type!="choice") ImGui::SameLine();
   if(!answered && !spell_selection && (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape))) { inscription_edit=false; c.answer(nullptr); answered=true; }
   if(answered) { if(editing_inscription) inscription_edit=false; ImGui::CloseCurrentPopup(); } ImGui::EndPopup();
  }
 }
 void messages_panel() {
   // At strip height the newest message replaces all panel chrome. Ignore the
   // search filter here: a collapsed log should never conceal a new message.
   if(ImGui::GetWindowHeight()<ImGui::GetFrameHeightWithSpacing()+ImGui::GetTextLineHeightWithSpacing()) {
    ImGui::SetScrollY(0);
    ImGui::SetCursorPosY(ImGui::GetScrollY());
    const bool waiting=c.state.value("message_pending",false);
    std::string full=c.messages.empty()?"No messages yet.":c.messages.front().value("text","");
    if(!c.messages.empty() && c.messages.front().value("count",1)>1) full+=" (x"+std::to_string(c.messages.front().value("count",1))+")";
    std::string text=full;
    std::replace(text.begin(),text.end(),'\n',' '); std::replace(text.begin(),text.end(),'\r',' ');
    const auto pos=ImGui::GetCursorScreenPos();
    const float width=std::max(1.f,ImGui::GetContentRegionAvail().x),height=ImGui::GetWindowHeight();
    const float pad=ImGui::GetFontSize()*.65f;
    ImGui::InvisibleButton("Latest message",{width,height});
    if(ImGui::IsItemClicked()) { if(!waiting || !proceed_click()) { keys.clear(); message_history.open=true; } }
    if(ImGui::BeginPopupContextItem("Compact messages")) {
     if(ImGui::MenuItem("Message history")) { keys.clear(); message_history.open=true; }
     ImGui::EndPopup();
    }
    if(ImGui::IsItemHovered()) {
     ImGui::BeginTooltip(); ImGui::PushTextWrapPos(ImGui::GetFontSize()*36);
     ImGui::TextUnformatted(full.c_str()); ImGui::TextDisabled("%s",waiting && proceed_with_click?"Click to continue. Right-click for history.":"Click for message history. Expand the panel for the full log.");
     ImGui::PopTextWrapPos(); ImGui::EndTooltip();
    }
    const float available=std::max(1.f,width-pad*2);
    if(ImGui::CalcTextSize(text.c_str()).x>available) {
     // Remove complete UTF-8 characters when reserving room for the ellipsis.
     while(!text.empty() && ImGui::CalcTextSize((text+"...").c_str()).x>available) {
      size_t last=text.size()-1; while(last>0 && (static_cast<unsigned char>(text[last])&0xc0)==0x80) --last;
      text.resize(last);
     }
     text+="...";
    }
    auto *draw=ImGui::GetWindowDrawList();
    const auto ink=waiting?ImGui::GetColorU32(ImVec4(1,.73f,.3f,1)):ImGui::GetColorU32(ImGuiCol_Text);
    draw->PushClipRect(pos,{pos.x+width,pos.y+height},true);
    draw->AddLine({pos.x+1,pos.y+3},{pos.x+1,pos.y+height-3},waiting?ink:ImGui::GetColorU32(AnybandUITheme::green()),2);
    draw->AddText({pos.x+pad,pos.y+std::max(0.f,(height-ImGui::GetTextLineHeight())*.5f)},ink,text.c_str());
    draw->PopClipRect(); return;
   }
   AnybandUITheme::panel();
   ImGui::Indent(ImGui::GetFontSize()*.65f);
   if(layout.heading(WorkspaceLayout::Messages)) { ImGui::AlignTextToFramePadding(); ImGui::TextColored(AnybandUITheme::green(),"Messages"); ImGui::SameLine(); }
   if(c.state.value("message_pending",false)) { ImGui::TextColored(ImVec4(1,.73f,.3f,1),"WAITING"); ImGui::SameLine(); }
   const float icon=ImGui::GetFrameHeight();
   const auto icon_pos=ImGui::GetCursorScreenPos();
   bool focus_search=false;
   if(ImGui::InvisibleButton("Search messages",ImVec2(icon,icon),ImGuiButtonFlags_EnableNav)) {
    message_search_open=!message_search_open; focus_search=message_search_open;
    if(!message_search_open) message_filter[0]=0;
   }
   auto draw=ImGui::GetWindowDrawList();
   const ImU32 ink=ImGui::GetColorU32(ImGui::IsItemHovered()?ImGuiCol_ButtonHovered:ImGuiCol_Text);
   draw->AddCircle(ImVec2(icon_pos.x+icon*.42f,icon_pos.y+icon*.42f),icon*.22f,ink,0,std::max(1.f,icon*.06f));
   draw->AddLine(ImVec2(icon_pos.x+icon*.58f,icon_pos.y+icon*.58f),ImVec2(icon_pos.x+icon*.8f,icon_pos.y+icon*.8f),ink,std::max(1.f,icon*.06f));
   if(ImGui::IsItemHovered()) ImGui::SetTooltip("%s",message_search_open?"Close search":"Search messages");
   ImGui::SameLine();
   if(ImGui::SmallButton("History")) { keys.clear(); message_history.open=true; }
   if(message_search_open) {
    ImGui::SameLine(); ImGui::SetNextItemWidth(std::max(1.f,ImGui::GetContentRegionAvail().x));
    if(focus_search) ImGui::SetKeyboardFocusHere();
    ImGui::InputTextWithHint("##messages","Search messages",message_filter,sizeof(message_filter));
   }
   ImGui::Unindent(ImGui::GetFontSize()*.65f);
   for(const auto &m:c.messages) {
    auto text=m.value("text",""); if(!matches(text,message_filter)) continue;
    const auto a=ImGui::GetCursorScreenPos();
    ImGui::Indent(ImGui::GetFontSize()*.65f);
    ImGui::TextWrapped("%s%s",text.c_str(),m.value("count",1)>1?(" (x"+std::to_string(m.value("count",1))+")").c_str():"");
    const auto bottom=ImGui::GetItemRectMax().y; ImGui::Unindent(ImGui::GetFontSize()*.65f);
    const auto group=m.value("group","");
    const auto ink=m.value("system",false)?IM_COL32(100,160,185,180):group=="combat"?IM_COL32(200,143,95,180):group=="loot"?IM_COL32(177,169,98,180):IM_COL32(91,138,113,160);
    ImGui::GetWindowDrawList()->AddLine(ImVec2(a.x+1,a.y+3),ImVec2(a.x+1,bottom-2),ink,2);
   }
 }
 void commands_panel() {
     ImGui::InputTextWithHint("##commands","Search commands",command_filter,sizeof(command_filter));
     ImGui::BeginDisabled(!c.ready());
     for(const auto &cmd:c.commands) {
      auto label=cmd.value("label",""); if(!matches(label,command_filter)) continue;
      ImGui::PushID(cmd.value("id","").c_str());
      if(ImGui::Selectable(label.c_str())) execute(cmd.value("id",""));
      if(quickbar_enabled && ImGui::BeginPopupContextItem("Command actions")) { quickbar.assign_menu(Quickbar::command_binding(cmd)); ImGui::EndPopup(); }
      ImGui::PopID();
     }
     ImGui::EndDisabled(); }
 bool workspace_panel_available(int panel) const {
  if(layout.editing) return true;
  if(panel==WorkspaceLayout::Target) return c.state.contains("targeting") || c.state.value("aiming",false) || c.state.value("direction_prompt",false);
  if(panel==WorkspaceLayout::Quickbar) return quickbar_enabled && c.state.value("phase","")=="playing";
  if(panel==WorkspaceLayout::Spells) return c.capabilities.value("spells",0)>0 && c.state.contains("player") && c.state["player"].value("spellcasting",false);
  return true;
 }
 void workspace_panel(int panel) {
  switch(panel) {
   case WorkspaceLayout::Dungeon: grid(std::max(1.f,ImGui::GetContentRegionAvail().y)); break;
   case WorkspaceLayout::Character: character(); break;
   case WorkspaceLayout::Status:
    if(c.state.contains("player")) {
     const auto &p=c.state["player"];
     if(layout.heading(WorkspaceLayout::Status)) AnybandUITheme::section("Status effects",false);
     if(StatusEffects::active(p).empty() && p.value("study",0)<=0) ImGui::TextDisabled("No active effects");
     else StatusEffects::draw(p,c.capabilities.value("spells",0)>0 && p.value("spellcasting",false)?&select_spells_tab:nullptr,level_animation?c.level_feedback.intensity(double(SDL_GetTicksNS())/1e9):0);
    }
    break;
   case WorkspaceLayout::TrackedCreature: if(c.state.contains("player")) CharacterOverview::tracked(c.state["player"],layout.heading(WorkspaceLayout::TrackedCreature)); break;
   case WorkspaceLayout::DungeonDetails: if(c.state.contains("player")) CharacterOverview::dungeon(c.state["player"],layout.heading(WorkspaceLayout::DungeonDetails)); break;
   case WorkspaceLayout::Messages: messages_panel(); break;
   case WorkspaceLayout::Inventory: items(); break;
   case WorkspaceLayout::Spells: if(spell_panel.draw(c,quickbar_enabled?&quickbar:nullptr)) focus_game(); break;
   case WorkspaceLayout::Creatures: creatures(); break;
   case WorkspaceLayout::Map: minimap(); break;
   case WorkspaceLayout::Commands: commands_panel(); break;
   case WorkspaceLayout::More:
     if(c.capabilities.value("journal",0)>0 && ImGui::Button("Run journal")) { keys.clear(); run_journal.open=true; }
     if(c.capabilities.value("item.rules",0)>0 && ImGui::Button("Item rules")) item_rules_panel.open=true;
    break;
   case WorkspaceLayout::Target: targeting_panel(); break;
   case WorkspaceLayout::Quickbar: {
    int slot=quickbar.draw(c); if(slot>=0) activate_quickbar(slot); break;
   }
  }
 }
 void draw(SDL_Window *window) {
  if(c.negotiated && !c.busy && c.capabilities.value("presentation.tiles",0)>0 &&
     c.tiles_sent!=tileset && (c.ready() || !c.state.contains("player"))) {
   c.send("dungeon.tiles",{{"id",tileset}}); c.tiles_sent=tileset; c.busy=true;
  }
  if(c.negotiated && !c.busy && c.capabilities.value("presentation.camera",0)>0 &&
     c.camera_sent!=int(camera_settings.enabled) && (c.ready() || !c.state.contains("player"))) {
   c.send("dungeon.camera",{{"enabled",camera_settings.enabled}});
   c.camera_sent=int(camera_settings.enabled); c.busy=true;
  }
  if(!c.state.contains("player")) dungeon_camera.reset();
  const double scene_now=double(SDL_GetTicksNS())/1e9;
  if(!scene_animation || (c.transitions.kind!=SceneTransitions::Kind::Death && !c.transitions.active(scene_now))) c.transitions.dismiss();
  // Drawing never owns input. A deliberate click/key can skip the flourish.
  if(c.transitions.kind!=SceneTransitions::Kind::Death && scene_now-c.transitions.started>.06 &&
     (ImGui::IsMouseClicked(0) || ImGui::IsMouseClicked(1) || !keys.empty())) c.transitions.dismiss();
  motion_feedback.update(c.motion_events,c.state,movement_animation,blink_animation,double(SDL_GetTicksNS())/1e9);
  projectile_feedback.update(c.projectile_events,c.state,projectile_animation,double(SDL_GetTicksNS())/1e9);
  combat_feedback.update(c.combat_events,c.state,combat_animation,double(SDL_GetTicksNS())/1e9);
  quickbar.sync_saves(c.save_changes);
  quickbar.profile=c.character_save;
  game_draw_list=nullptr; showing_postgame=false;
  auto vp=ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(vp->WorkPos); ImGui::SetNextWindowSize(vp->WorkSize);
  ImGui::Begin("AnybandUI",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoSavedSettings);
  const bool ending=!c.run_report.is_null();
  if(ending && !run_ui_reset) {
   c.transitions.restore_colour(scene_now);
   ImGui::ClosePopupsOverWindow(nullptr,false);
   open_character_sheet=false; quickbar.open_customize=false;
   inventory_window_open=false; c.inventory_requested=false; c.equipment_requested=false;
   knowledge_browser.request_open=false; item_rules_panel.open=item_rules_panel.auto_open=false;
   quit_dialog=false; run_ui_reset=true;
  }
  if(!ending) run_ui_reset=false;
  if(ending || run_history.browsing) {
   showing_postgame=ending;
   grid_focus=false; keys.clear();
   if(ending && !c.connected && !c.postgame_finished)
    ImGui::TextWrapped("The engine stopped before confirming its final save. The run summary has been retained.");
   const int action=run_history.draw(!ending || !c.connected);
   if(action) {
    if(action==2) {
     if(ending && !run_history.archived) pending_replay=c.character_save;
     else { replay_profile=run_history.current.value("player",json::object()); replay_prompt=true; }
    }
    if(run_history.archived && action==1) { run_history.current=nullptr; run_history.archived=false; }
    else {
     run_history.current=nullptr; run_history.browsing=false;
     if(ending) { c.run_report=nullptr; c.restart_ready=true; run_report_started=false; c.transitions.reset(); }
    }
   }
   if(ending) prompts();
   ImGui::End(); return;
  }
  if(!c.replay_save.empty() && !c.state.contains("terminal")) {
   ImGui::TextUnformatted("Preparing your next character..."); ImGui::End(); return;
  }
  // Freeze the death transition without dimming it, but restore normal
  // disabled styling immediately for controls nested inside this scope.
  ImGui::PushStyleVar(ImGuiStyleVar_DisabledAlpha,1.f); ImGui::BeginDisabled(ending || c.saving || !c.loading.empty()); ImGui::PopStyleVar();
  const bool in_game=c.state.contains("terminal");
  if(in_game && !c.state.contains("birth")) {
   ImGui::BeginDisabled(!c.ready());
   if(ImGui::Button("Save and...")) ImGui::OpenPopup("Save menu");
   ImGui::EndDisabled();
   if(!c.ready() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s",c.save_unavailable_reason());
   if(ImGui::BeginPopup("Save menu")) {
    ImGui::BeginDisabled(!c.ready());
    if(ImGui::MenuItem("Save and continue")) { c.save(); focus_game(); }
    if(ImGui::MenuItem("Save and return to main menu")) c.save(true,true);
    if(ImGui::MenuItem("Save and quit")) c.save(true);
    ImGui::EndDisabled();
    if(!c.ready()) ImGui::TextUnformatted(c.save_unavailable_reason());
    ImGui::EndPopup();
   }
   ImGui::SameLine();
  }
  if(c.resting) {
   ImGui::TextColored(ImVec4(.52f,.80f,.66f,1),"Resting..."); ImGui::SameLine();
   if(ImGui::Button("Stop resting")) { c.key("escape"); focus_game(); }
   ImGui::SameLine();
  }
  const float settings_width=ImGui::CalcTextSize("Settings").x+2*ImGui::GetStyle().FramePadding.x;
  const float knowledge_width=ImGui::CalcTextSize("Knowledge").x+2*ImGui::GetStyle().FramePadding.x;
  const float layout_width=ImGui::CalcTextSize("Layout").x+2*ImGui::GetStyle().FramePadding.x;
  ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(),ImGui::GetWindowSize().x-ImGui::GetStyle().WindowPadding.x-settings_width-knowledge_width-layout_width-2*ImGui::GetStyle().ItemSpacing.x));
  const bool was_editing=layout.editing;
  layout.menu(in_game && c.state.value("phase","")=="playing" && !c.state.contains("store"),[&](int panel) { return workspace_panel_available(panel); });
  if(layout.editing) { keys.clear(); grid_focus=false; }
  if(!layout.editing && (was_editing || layout.dirty)) focus_game();
  ImGui::SameLine();
  ImGui::BeginDisabled(!c.connected || c.capabilities.value("knowledge",0)<1 || c.state.value("phase","")!="playing");
  if(ImGui::Button("Knowledge")) { keys.clear(); knowledge_browser.open(c); }
  ImGui::EndDisabled(); ImGui::SameLine();
  if(ImGui::Button("Settings")) {
   begin_settings();
   ImGui::OpenPopup("Settings");
   }
  settings_window(window);
  if(!in_game) launcher();
  const auto phase=c.state.value("phase","launcher");
  const bool in_store=in_game && c.state.contains("store");
  if(in_store) { grid_focus=false; keys.clear(); store_panel.draw(c); }
  else if(was_store) { focus_game(); store_panel.last_name.clear(); }
  was_store=in_store;
  const bool creating_character=in_game && (phase=="birth" || phase=="launcher");
  if(!in_game || creating_character) presence=ScenePresence{};
  if(creating_character) {
   if(c.state.contains("birth")) { grid_focus=false; keys.clear(); birth_panel.draw(c); }
   else grid(std::max(1.f,ImGui::GetContentRegionAvail().y));
  } else birth_panel.initialized=false;
  if(in_game && !creating_character && !in_store) {
   const bool targeting_active=c.state.contains("targeting") || c.state.value("aiming",false) || c.state.value("direction_prompt",false);
   if(targeting_active && !targeting_was_active) layout.reveal(WorkspaceLayout::Target,false);
   if(select_spells_tab) { layout.reveal(WorkspaceLayout::Spells); select_spells_tab=false; }
   if(select_creatures_tab) { layout.reveal(WorkspaceLayout::Creatures); select_creatures_tab=false; }
   layout.compact_height=[&](int panel,float width) {
    if(!c.state.contains("player")) return 0.f;
    const auto &p=c.state["player"];
    if(panel==WorkspaceLayout::Character) return CharacterOverview::height(p,width,layout.heading(panel));
    if(panel==WorkspaceLayout::Status) return StatusEffects::height(p,width,layout.heading(panel));
    return panel==WorkspaceLayout::DungeonDetails?CharacterOverview::dungeon_height(p,width,layout.heading(panel)):0.f;
   };
   layout.tracker_content_height=CharacterOverview::tracked_height(layout.heading(WorkspaceLayout::TrackedCreature));
   layout.quickbar_content_height=Quickbar::height()-ImGui::GetStyle().ItemSpacing.y;
   const bool editing_before_draw=layout.editing;
   layout.draw([&](int panel) { return workspace_panel_available(panel) && !(layout.native_windows_available && layout.detached[panel].open);
   },[&](int panel) {
    if(panel==WorkspaceLayout::Spells && (!c.state.contains("player") || !c.state["player"].value("spellcasting",false))) ImGui::TextWrapped("Spells appear here for spellcasting characters.");
    else if(panel==WorkspaceLayout::Target && !targeting_active) ImGui::TextWrapped("Look and targeting details appear here when needed.");
    else if(panel==WorkspaceLayout::Quickbar && !quickbar_enabled) ImGui::TextWrapped("Enable the quickbar in Gameplay settings to use this panel.");
    else workspace_panel(panel);
   });
   if(editing_before_draw && !layout.editing) focus_game();
   targeting_was_active=targeting_active;
   if(layout.editing) { grid_focus=false; keys.clear(); }
  }
  if(layout.dirty && !layout.editing) { layout.dirty=false; if(!settings_path.empty()) save_settings(); }
  if(!ending) {
  inventory_window();
  if(quickbar.customize_window()) focus_game();
  if(quickbar.dirty) { quickbar.dirty=false; save_settings(); }
  if(message_history.draw(c)) focus_game();
  if(run_journal.draw(c)) focus_game();
  if(item_rules_panel.draw(c)) focus_game();
  if(knowledge_browser.draw(c)) focus_game();
  if(c.state.contains("player")) {
   if(CharacterSheet::draw(c.state["player"],open_character_sheet,c.state["items"],c.state["slots"],c.character_save)) focus_game();
  }
  open_character_sheet=false;
  if(quit_dialog) { ImGui::OpenPopup("Close game"); quit_dialog=false; }
  if(ImGui::BeginPopupModal("Close game",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
   ImGui::TextWrapped("%s",c.ready()?"Save this character and close AnybandUI?":c.save_unavailable_reason());
   ImGui::BeginDisabled(!c.ready());
   if(ImGui::Button("Save and quit")) { c.save(true); ImGui::CloseCurrentPopup(); }
   ImGui::EndDisabled(); ImGui::SameLine();
   if(ImGui::Button("Continue playing")) { ImGui::CloseCurrentPopup(); focus_game(); }
   if(!c.state.contains("player") || !c.connected) if(ImGui::Button("Close")) { c.closed=true; if(c.process) SDL_KillProcess(c.process.get(),true); }
   ImGui::EndPopup();
  }
  }
  ImGui::EndDisabled();
  if(!ending && !c.saving && c.loading.empty()) prompts();
  save_progress(); ImGui::End();
  dispatch_keys();
 }
 void save_progress() {
  const bool active=c.saving || !c.loading.empty();
  if(active) { keys.clear(); ImGui::OpenPopup("Progress###Save progress"); }
  const auto *viewport=ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(viewport->GetCenter(),ImGuiCond_Always,{.5f,.5f});
  if(ImGui::BeginPopupModal("Progress###Save progress",nullptr,ImGuiWindowFlags_AlwaysAutoResize|ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoSavedSettings)) {
   if(!active) ImGui::CloseCurrentPopup();
   else {
    const float radius=ImGui::GetFontSize()*.65f,pad=ImGui::GetStyle().FramePadding.y;
    const auto pos=ImGui::GetCursorScreenPos();
    auto *draw=ImGui::GetWindowDrawList(); const ImVec2 center(pos.x+radius+pad,pos.y+radius+pad);
    draw->AddCircle(center,radius,ImGui::GetColorU32(ImGuiCol_Border),32,2);
    const float phase=float(std::fmod(ImGui::GetTime()*5.,6.2831853));
    draw->PathArcTo(center,radius,phase,phase+4.4f,32);
    draw->PathStroke(ImGui::GetColorU32(AnybandUITheme::green()),0,2.5f);
    ImGui::Dummy({2*(radius+pad),2*(radius+pad)}); ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::TextUnformatted(!c.saving?c.loading.c_str():c.close_requested?(c.close_confirmed?"Closing game...":c.return_to_menu?"Saving and returning to menu...":"Saving and quitting..."):"Saving game...");
    ImGui::TextDisabled("Please wait"); ImGui::EndGroup();
   }
   ImGui::EndPopup();
  }
 }
 void dispatch_keys() {
  if(!owns_keyboard() || ImGui::GetIO().WantTextInput || !c.prompt.empty() || !c.connected) { keys.clear(); return; }
  // Keep short bursts while a game turn is in flight, instead of discarding
  // actual presses. Never allow a long movement backlog or spill into prompts.
  if(keys.size()>4) keys.resize(4);
  if(!keys.empty() && c.key(keys.front())) {
   keys.erase(keys.begin());
   if(c.state.value("readiness","")!="ready") keys.clear();
   c.flush_input();
  }
 }
};

#include "detached_panels.h"

#ifndef ANYBANDUI_CLIENT_TEST
int main(int argc,char **argv) {
 const char *base=SDL_GetBasePath();
 auto paths=RuntimePaths::discover(base?base:".",ANYBANDUI_DATA_DIR,ANYBANDUI_FONT_FILE);
 std::string user_override;
 bool check_assets=false;
 for(int i=1;i<argc;++i) {
  const std::string arg=argv[i];
  if(arg=="--check-assets") { check_assets=true; continue; }
  if((arg!="--backend" && arg!="--data-dir" && arg!="--user-dir") || i+1>=argc) {
   SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,"AnybandUI","Expected --backend PATH, --data-dir PATH, or --user-dir PATH.",nullptr); return 2;
  }
  const std::string value=argv[++i];
  if(arg=="--backend") paths.backend=value;
  else if(arg=="--data-dir") paths.data=value;
  else user_override=value;
 }
 const auto missing=paths.missing();
 if(check_assets) {
  std::cout<<"backend="<<paths.backend.string()<<"\ndata="<<paths.data.string()<<"\nfont="<<paths.font.string()<<"\naudio="<<paths.audio.string()<<"\n";
  for(const auto &path:missing) std::cerr<<"Missing: "<<path<<"\n";
  return missing.empty()?0:1;
 }
 // Report missing required assets before font loading can assert or the engine exits.
 if(!fs::is_regular_file(paths.font) || !fs::is_regular_file(paths.backend) || !fs::is_regular_file(paths.data/"gamedata"/"constants.txt")) {
  std::string message="Some game files are missing. Extract the entire archive before launching.\n";
  for(const auto &path:missing) message+="\n"+path;
  SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,"AnybandUI",message.c_str(),nullptr); return 1;
 }
 if(!SDL_Init(SDL_INIT_VIDEO|SDL_INIT_GAMEPAD)) return 1;
 const float dpi=SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
 SDL_Rect bounds{}; SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(), &bounds);
 const int width=std::min(int(1280*dpi),std::max(640,bounds.w-80));
 const int height=std::min(int(800*dpi),std::max(480,bounds.h-80));
 SDL_Window *window=SDL_CreateWindow("AnybandUI",width,height,SDL_WINDOW_RESIZABLE|SDL_WINDOW_HIGH_PIXEL_DENSITY);
 if(window) SDL_SetWindowPosition(window,SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED);
 SDL_GPUDevice *gpu=SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV|SDL_GPU_SHADERFORMAT_DXIL|SDL_GPU_SHADERFORMAT_METALLIB|SDL_GPU_SHADERFORMAT_MSL,false,nullptr);
 if(!window || !gpu || !SDL_ClaimWindowForGPUDevice(gpu,window)) {
  SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,"AnybandUI",SDL_GetError(),window); return 1;
 }
 SDL_SetGPUSwapchainParameters(gpu,window,SDL_GPU_SWAPCHAINCOMPOSITION_SDR,SDL_GPU_PRESENTMODE_VSYNC);
 SDL_SetGPUAllowedFramesInFlight(gpu,1); // Bound presentation latency; still paced by vsync.
 IMGUI_CHECKVERSION(); ImGui::CreateContext();
 auto &io=ImGui::GetIO(); io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard|ImGuiConfigFlags_NavEnableGamepad;
 FontLibrary fonts; fonts.load(paths.font.parent_path());
 io.FontDefault=fonts.fonts[0];
 AnybandUITheme::apply();
 ImGui::GetStyle().FontSizeBase=18.f;
 ImGui_ImplSDL3_InitForSDLGPU(window);
 ImGui_ImplSDLGPU3_InitInfo info{}; info.Device=gpu; info.ColorTargetFormat=SDL_GetGPUSwapchainTextureFormat(gpu,window); info.MSAASamples=SDL_GPU_SAMPLECOUNT_1;
 ImGui_ImplSDLGPU3_Init(&info);
 Connection connection; UI ui{connection}; ui.font_library=&fonts; CrtRenderer crt_renderer;
 crt_renderer.initialize(gpu,info.ColorTargetFormat);
 std::string renderer_error;
 ui.base_style=ImGui::GetStyle();
 char *pref=SDL_GetPrefPath("AnybandUI","AnybandUI");
 std::string user=user_override.empty()?(pref?std::string(pref):std::string("anybandui-user")):user_override; SDL_free(pref);
 const std::string backend=paths.backend.string(),data=paths.data.string();
 ui.tiles.device=gpu; ui.tiles.directory=paths.data/"tiles";
 try { fs::create_directories(user); }
 catch(const fs::filesystem_error &error) {
  SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,"AnybandUI",("Cannot open the save/settings folder: "+std::string(error.what())).c_str(),window); return 1;
 }
 ui.settings_path=(fs::path(user)/"settings.json").string(); ui.load_settings();
 DetachedPanels detached(gpu,window,paths.font.parent_path());
 ui.layout.native_windows_available=true;
 if(!ui.audio.open(paths.audio)) connection.notice("Audio unavailable: "+ui.audio.error);
 ui.audio.configure(ui.audio_settings,true);
 if(ui.fullscreen && !SDL_SetWindowFullscreen(window,true)) { ui.fullscreen=false; connection.notice(std::string("Fullscreen unavailable: ")+SDL_GetError()); }
 std::string ini=(fs::path(user)/"layout.ini").string(); io.IniFilename=ini.c_str();
 connection.start(backend,data,user);
 SDL_StartTextInput(window);
 HealthGlitch health_glitch;
 auto poll_backend=[&] {
  const auto phase=connection.state.value("phase","");
  const auto readiness=connection.state.value("readiness","");
  connection.poll();
  ui.window_active=detached.focused();
  ui.audio.configure(ui.audio_settings,ui.window_active);
  for(const auto &cue:connection.sound_cues) ui.audio.play(cue);
  connection.sound_cues.clear();
  if(!connection.run_report.is_null() && !ui.run_report_started) {
   ui.run_history.directory=fs::path(ui.settings_path).parent_path()/"run-history";
   ui.run_history.begin(connection.run_report);
   ui.run_report_started=true;
   ui.keys.clear(); ui.grid_focus=false;
  }
  if(phase!=connection.state.value("phase","") || readiness!=connection.state.value("readiness","") || !connection.prompt.empty())
   ui.keys.clear(); // Buffered movement must not answer a newly opened menu/prompt.
 };
 while(!connection.closed) {
  // Pace first: sampling input and ImGui's clock before a blocking presentation
  // wait produces stale, uneven animation times even when GPU work is fast.
  if(!SDL_WaitForGPUSwapchain(gpu,window)) break;
  poll_backend();
  if(ui.quit_after_run && !connection.connected) { connection.closed=true; break; }
  if(connection.restart_ready && connection.run_report.is_null()) {
   ui.audio.clear();
   crt_renderer.reset_history();
   health_glitch=HealthGlitch{};
   // A saved return-to-menu or normal post-game completion has exited cleanly.
   connection.close_process();
   connection=Connection{};
   connection.replay_save=ui.pending_replay; ui.pending_replay.clear();
   ui.birth_panel.initialized=false; ui.dungeon_tooltip.reset();
   ui.grid_focus=ui.return_from_prompt=false;
   ui.focus_requested=!connection.replay_save.empty();
   ui.selected.clear(); ui.last_prompt.clear(); ui.keys.clear();
   ui.item_filter[0]=ui.command_filter[0]=ui.message_filter[0]=0;
   ui.message_search_open=false;
   ui.quit_dialog=false;
   connection.start(backend,data,user);
  }
  ui.prepare_frame(window); SDL_Event e;
  while(SDL_PollEvent(&e)) {
   if(detached.event(ui,e)) continue;
   if(e.type==SDL_EVENT_WINDOW_FOCUS_LOST) { ui.window_active=false; ui.keys.clear(); }
   if(e.type==SDL_EVENT_WINDOW_FOCUS_GAINED) ui.window_active=true;
   if(e.type==SDL_EVENT_MOUSE_BUTTON_DOWN) { ui.grid_focus=false; ui.keys.clear(); }
   if(e.type==SDL_EVENT_MOUSE_MOTION && ui.crt!=0 && (ui.crt>=2 || ui.game_draw_list) && crt_renderer.ready()) {
    auto vp=ImGui::GetMainViewport();
    CrtCurve curve(ui.crt>=2?vp->Pos:ui.game_pos,ui.crt>=2?vp->Size:ui.game_size,ui.crt_settings);
    if(e.motion.x>=curve.pos.x && e.motion.x<=curve.pos.x+curve.size.x && e.motion.y>=curve.pos.y && e.motion.y<=curve.pos.y+curve.size.y) {
     auto p=curve.map(ImVec2(e.motion.x,e.motion.y),true); e.motion.x=p.x; e.motion.y=p.y;
    }
   }
   if(ui.settings_open && ui.keybinding_editor.event(e)) {
    if(e.type==SDL_EVENT_KEY_UP) ImGui_ImplSDL3_ProcessEvent(&e);
    continue;
   }
   // Forward releases so the death acknowledgement cannot leave input held.
   if(connection.run_report.is_null() && ui.quickbar_event(e)) continue;
   ImGui_ImplSDL3_ProcessEvent(&e);
   if(e.type==SDL_EVENT_QUIT || (e.type==SDL_EVENT_WINDOW_CLOSE_REQUESTED && e.window.windowID==SDL_GetWindowID(window))) {
    if(connection.saving) continue;
    if(!connection.run_report.is_null()) { ui.quit_after_run=true; continue; }
    if(!connection.state.contains("terminal") || (!connection.run_report.is_null() && !connection.connected)) { connection.closed=true; if(connection.process) SDL_KillProcess(connection.process.get(),true); }
    else ui.quit_dialog=true;
   }
   if(e.type==SDL_EVENT_KEY_DOWN && ui.owns_keyboard()) {
    switch(e.key.key) {
     case SDLK_RETURN: ui.keys.push_back("enter"); break;
     case SDLK_ESCAPE: ui.keys.push_back("escape"); break;
     case SDLK_BACKSPACE: ui.keys.push_back("backspace"); break;
     case SDLK_TAB: ui.keys.push_back("tab"); break;
     case SDLK_UP: ui.keys.push_back("up"); break;
     case SDLK_DOWN: ui.keys.push_back("down"); break;
     case SDLK_LEFT: ui.keys.push_back("left"); break;
     case SDLK_RIGHT: ui.keys.push_back("right"); break;
     default: if(KeybindingEditor::function_key(e.key.key) && !(e.key.mod&(SDL_KMOD_CTRL|SDL_KMOD_SHIFT|SDL_KMOD_ALT|SDL_KMOD_GUI))) ui.keys.push_back(KeybindingEditor::function_key(e.key.key));
      else if((e.key.mod&SDL_KMOD_CTRL) && e.key.key>='a' && e.key.key<='z') ui.keys.push_back(int(e.key.key-'a'+1));
    }
   }
   if(e.type==SDL_EVENT_TEXT_INPUT && ui.owns_keyboard() && !(SDL_GetModState()&SDL_KMOD_CTRL)) {
    const char *p=e.text.text; while(*p) ui.keys.push_back(SDL_StepUTF8(&p,nullptr));
   }
  }
  // Give the engine the key before constructing this frame, so its work can
  // overlap UI rendering rather than starting only after it has finished.
  ui.dispatch_keys();
  poll_backend();
  detached.draw(ui);
  ImGui_ImplSDLGPU3_NewFrame(); ImGui_ImplSDL3_NewFrame();
  if(ui.crt!=0 && (ui.crt>=2 || ui.game_draw_list) && crt_renderer.ready() && SDL_GetMouseFocus()==window) {
   float x,y; SDL_GetMouseState(&x,&y);
   auto vp=ImGui::GetMainViewport();
   CrtCurve curve(ui.crt>=2?vp->Pos:ui.game_pos,ui.crt>=2?vp->Size:ui.game_size,ui.crt_settings);
   if(x>=curve.pos.x && x<=curve.pos.x+curve.size.x && y>=curve.pos.y && y<=curve.pos.y+curve.size.y) {
    auto p=curve.map(ImVec2(x,y),true); ImGui::GetIO().AddMousePosEvent(p.x,p.y);
   }
  }
  io.FontDefault=fonts.get(ui.font_settings.interface_font);
  ImGui::NewFrame(); ui.draw(window);
  ui.window_active=detached.focused();
  ui.audio.configure(ui.audio_settings,ui.window_active);
  if(!ui.grid_focus && !ImGui::GetIO().WantTextInput && ((ImGui::GetIO().MouseClicked[0] && GImGui->ActiveId && GImGui->ActiveIdIsJustActivated) || GImGui->NavActivateId)) ui.audio.play("ui");
  ImGui::Render();
  auto *render_data=ImGui::GetDrawData();
  connection.flush_input(); // Dispatch this frame's input before waiting for presentation.
  // ImGui may stop text input when one of its textboxes loses focus. The
  // game also needs SDL's layout-aware text events (including shifted keys).
  if(ui.owns_keyboard() && !SDL_TextInputActive(window)) SDL_StartTextInput(window);
  auto cmd=SDL_AcquireGPUCommandBuffer(gpu); SDL_GPUTexture *surface=nullptr; Uint32 surface_width=0,surface_height=0;
  if(!cmd) break;
  if(!SDL_WaitAndAcquireGPUSwapchainTexture(cmd,window,&surface,&surface_width,&surface_height)) { SDL_CancelGPUCommandBuffer(cmd); break; }
  if(surface) {
   CrtFrame frame;
   frame.scope=ui.crt>=2?2:ui.crt; frame.settings=ui.crt_settings;
   frame.game=ui.game_draw_list; frame.game_pos=ui.game_pos; frame.game_size=ui.game_size;
   frame.seconds=double(SDL_GetTicksNS())/1e9; frame.ui_scale=ui.scale*ui.display_scale; frame.session=connection.state.value("phase","");
   frame.desaturation=ui.scene_animation?connection.transitions.desaturation(frame.seconds):0;
   frame.retain_scene=ui.scene_animation;
   frame.hold_scene=ui.scene_animation && connection.transitions.hold_shop(frame.seconds);
   frame.scene_darkness=ui.scene_animation?connection.transitions.shop_darkness(frame.seconds):0;
   frame.health_glitch=health_glitch.update(connection.state,frame.seconds,ui.low_animation,false);
   if(ui.showing_postgame) frame.session="postgame";
   crt_renderer.render(cmd,surface,surface_width,surface_height,render_data,frame);
   if(renderer_error!=crt_renderer.error()) {
    renderer_error=crt_renderer.error(); if(!renderer_error.empty()) connection.notice(renderer_error);
   }
  }
  SDL_SubmitGPUCommandBuffer(cmd);
  if(SDL_GetWindowFlags(window)&SDL_WINDOW_MINIMIZED) SDL_Delay(20);
 }
 detached.shutdown();
 ui.tiles.shutdown(); ui.audio.close();
 SDL_WaitForGPUIdle(gpu); crt_renderer.shutdown(); ImGui_ImplSDLGPU3_Shutdown(); ImGui_ImplSDL3_Shutdown(); ImGui::DestroyContext();
 connection.close_process();
 SDL_ReleaseWindowFromGPUDevice(gpu,window); SDL_DestroyGPUDevice(gpu); SDL_DestroyWindow(window); SDL_Quit(); return 0;
}
#endif
