// Angband Deluxe desktop client. GPLv2. No engine headers or linked engine state.
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlgpu3.h"
#include "crt_renderer.h"
#include "dungeon_view.h"
#include "backend_reader.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
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
struct Connection {
 RenderGrid game_grid;
 std::unique_ptr<BackendReader> reader;
 std::unique_ptr<SDL_Process,decltype(&SDL_DestroyProcess)> process{nullptr,SDL_DestroyProcess};
 std::string outgoing, diagnostic, menu_error, character_save;
 std::deque<json> messages;
 json previous_messages = json::array();
 json capabilities = json::object();
 json comparisons = json::object();
 json item_rules=nullptr;
 json save_change_requests=json::object(), save_changes=json::array();
 std::map<std::string,std::string> comparison_requests;
 std::map<std::string,std::string> requests;
 unsigned long next = 0;
 bool connected = false, negotiated = false, busy = false, close_requested = false, closed = false;
 bool pickup_travel=false;
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
  close_requested=leave; return_to_menu=menu;
  send(leave?"session.close":"session.save"); busy=true;
 }
 void quit_without_saving() {
  if(!connected || busy || capabilities.value("debug.quit",0)<1) return;
  close_requested=true; return_to_menu=false;
  send("debug.quit"); busy=true;
 }
 std::string send(const std::string &method, json params = json::object()) {
  if(method=="session.new" || method=="session.load") { character_save=params.value("save",""); params["native_birth"]=capabilities.value("interaction.birth",0)>0; }
  if (!connected) return "";
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
  connected = true;
  send("hello",{{"protocols",json::array({{{"major",0},{"minor",1}}})},{"max_frame_bytes",1048576}});
  return true;
 }
 void receive(json j) {
  if (j.value("kind","") == "event") {
   auto name = j.value("event","");
   if (name == "state.changed") { state = std::move(j.at("data")); comparisons=json::object(); item_rules=nullptr; game_grid.update(state); update_messages(state.at("messages")); busy = false; pickup_travel=false; }
   if (name == "prompt.requested") { prompt = j.at("data"); busy = false; pickup_travel=false; }
   return;
  }
  auto id = j.value("id",""); auto it = requests.find(id);
  if (it == requests.end()) return;
  auto method = it->second; requests.erase(it);
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
   const auto error=j["error"].value("message","Request failed"); notice(error); busy = false; pickup_travel=false;
   if(!state.contains("terminal") || method=="birth.action" || method=="birth.cancel") menu_error=error;
   if(method=="session.close" || method=="debug.quit" || method=="birth.cancel") { close_requested=false; return_to_menu=false; }
   if(method == "prompt.reply") { prompt = pending_prompt; pending_prompt = json::object(); }
   return;
  }
  if(method=="birth.action") menu_error.clear();
  const auto &result = j.at("result");
  if (method == "hello") {
   capabilities=result.value("capabilities",json::object());
   negotiated = true; send("saves.list"); send("commands.list");
  } else if (method == "saves.list") { saves = result; busy=false; }
  else if (method == "saves.rename" || method == "saves.delete") { menu_error.clear(); send("saves.list"); }
  else if (method == "commands.list") commands = result;
  else if (method == "catalog.get") catalog = result;
  else if (method == "item.rules.list") item_rules=result;
  else if (method == "session.new" || method == "session.load") { menu_error.clear(); send("catalog.get"); }
  else if (method == "session.save") { busy = false; notice("Game saved."); }
  else if (method == "session.close" || method == "debug.quit" || method == "birth.cancel") { close_confirmed=true; closed = !return_to_menu; busy = false; }
  else if (method == "prompt.reply") pending_prompt = json::object();
 }
 void flush_input() {
  if(connected && !outgoing.empty()) {
   const auto n=SDL_WriteIO(SDL_GetProcessInput(process.get()),outgoing.data(),outgoing.size());
   outgoing.erase(0,n);
  }
 }
 void process_stopped(int exit_code) {
  connected=false; busy=false; pickup_travel=false;
  // Death/post-game screens remain interactive until the engine reports that
  // play_game completed. A crash must not masquerade as a normal game ending.
  const bool finished=state.value("phase","")=="finished";
  if(exit_code==0 && !closed && ((close_confirmed && return_to_menu) || finished)) restart_ready=true;
  else if(!closed) {
   notice("Backend stopped (" + std::to_string(exit_code) + "). " + diagnostic);
   if(!state.contains("terminal")) menu_error="Backend stopped. Restart Deluxe to try again.";
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
  if(!batch.error.empty()) { notice("Invalid backend message: "+batch.error); connected=false; return; }
  if(!exited) flush_input();
  // EOF must be drained by the reader before interpreting the final game state.
  else if(batch.finished) process_stopped(exit_code);
 }
 bool ready() const { return connected && !busy && prompt.empty() && state.value("readiness","") == "ready"; }
 bool native_targeting() const { return capabilities.value("interaction.targeting",0)>0; }
 bool mouse_movement() const { return capabilities.value("interaction.mouse",0)>0; }
 bool key(const json &k) {
  if(connected && busy && pickup_travel && prompt.empty() && k=="escape") {
   send("terminal.input",{{"context",state.value("context","")},{"key","escape"}});
   pickup_travel=false; return true;
  }
  if (!connected || busy || !prompt.empty() || state.empty()) return false;
  send("terminal.input",{{"context",state.value("context","")},{"key",k}}); busy = true;
  return true;
 }
 void target(const std::string &method,json params=json::object()) {
  if(!connected || busy || !prompt.empty()) return;
  params["context"]=state.value("context",""); send(method,std::move(params)); busy=true;
  pickup_travel=method=="dungeon.pickup" || method=="dungeon.terrain";
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
#include "character_sheet.h"
#include "birth_panel.h"
#include "quickbar.h"
#include "spell_panel.h"
#include "item_description.h"
#include "item_comparison.h"
#include "item_rules.h"
#include "store_panel.h"
struct UI {
 Connection &c;
 BirthPanel birth_panel;
 Quickbar quickbar;
 bool quickbar_enabled=false, draft_quickbar_enabled=false;
 bool quickbar_held[10]{};
 char quickbar_text=0;
 bool open_character_sheet=false;
 bool inscription_edit=false;
 ItemRules item_rules_panel;
 StorePanel store_panel;
 SpellPanel spell_panel;
 bool was_store=false;
 float scale = 1.0f, game_fraction = .72f;
 bool fullscreen=false, draft_fullscreen=false;
 bool low_animation=true, death_animation=true, draft_low_animation=true, draft_death_animation=true;
 int damage_amount=1;
 int crt=0, draft_crt=0;
 int crt_strength=1, draft_crt_strength=1;
 CrtSettings crt_settings{}, draft_crt_settings{};
 float draft_scale=1.f;
 std::string settings_error;
 ImDrawList *game_draw_list=nullptr;
 ImVec2 game_pos{},game_size{};
 float split_drag_y = 0.f, split_drag_fraction = .72f;
 bool quit_dialog = false;
 bool grid_focus = false, focus_requested = false, window_active = true;
 bool return_from_prompt = false;
 bool message_search_open = false;
 bool targeting_was_active=false;
 bool proceed_with_click=false, draft_proceed_with_click=false;
 bool click_exits_look=false, draft_click_exits_look=false;
 bool quick_targeting=false, draft_quick_targeting=false;
 int grid_menu_x=0,grid_menu_y=0;
 std::string grid_menu_context;
 float display_scale = 1.f;
 ImGuiStyle base_style;
 char item_filter[128]{}, message_filter[128]{}, command_filter[128]{}, save_name[65] = "Adventurer";
 char prompt_text[4096]{};
 std::string last_prompt, selected, settings_path, prompt_item;
 std::string managed_save;
 char renamed_save[65]{};
 std::vector<json> keys;
 void load_settings() {
  try { std::ifstream in(settings_path); if (!in) return; json j; in >> j;
   scale=std::clamp(j.value("scale",1.f),0.75f,1.5f);
   game_fraction=std::clamp(j.value("game_fraction",.72f),.2f,.9f);
   fullscreen=j.value("fullscreen",false);
   proceed_with_click=j.value("proceed_with_click",false);
   click_exits_look=j.value("click_exits_look",false);
   quick_targeting=j.value("quick_targeting",false);
   quickbar_enabled=j.value("quickbar_enabled",false);
   quickbar.load(j.value("quickbar_profiles",json::object()));
   low_animation=j.value("low_health_animation",true); death_animation=j.value("death_animation",true);
   crt=std::clamp(j.value("crt",0),0,2);
   crt_strength=std::clamp(j.value("crt_strength",1),-1,3);
   crt_settings=CrtSettings(crt_strength);
   crt_settings.parts[Hum].enabled=j.value("hum_bar",false);
   crt_settings.parts[Ghost].enabled=false; // New effects remain opt-in for legacy preferences.
   if(j.contains("crt_components")) crt_settings.load(j.at("crt_components"));
  } catch (...) { c.notice("Settings could not be read; using defaults."); }
 }
 bool write_settings(float zoom,bool full,int effect,int strength,const CrtSettings &settings,bool low,bool death,bool proceed,bool exit_look,bool quick,bool bar) {
  const std::string temporary=settings_path+".tmp";
  std::ofstream out(temporary);
  out << json{{"scale",zoom},{"game_fraction",game_fraction},{"fullscreen",full},{"crt",effect},{"crt_strength",strength},{"crt_components",settings.serialize()},{"low_health_animation",low},{"death_animation",death},{"proceed_with_click",proceed},{"click_exits_look",exit_look},{"quick_targeting",quick},{"quickbar_enabled",bar},{"quickbar_profiles",quickbar.profiles}}.dump(2);
  out.close();
  return bool(out) && SDL_RenamePath(temporary.c_str(),settings_path.c_str());
 }
 void save_settings() {
  if(!write_settings(scale,fullscreen,crt,crt_strength,crt_settings,low_animation,death_animation,proceed_with_click,click_exits_look,quick_targeting,quickbar_enabled)) c.notice("Settings could not be saved.");
 }
 void begin_settings() {
  draft_proceed_with_click=proceed_with_click;
  draft_click_exits_look=click_exits_look;
  draft_quick_targeting=quick_targeting;
  draft_quickbar_enabled=quickbar_enabled;
  draft_low_animation=low_animation; draft_death_animation=death_animation;
  draft_scale=scale; draft_fullscreen=fullscreen; draft_crt=crt; settings_error.clear();
  draft_crt_strength=crt_strength; draft_crt_settings=crt_settings;
 }
 bool apply_settings(SDL_Window *window) {
  if(draft_fullscreen!=fullscreen && !SDL_SetWindowFullscreen(window,draft_fullscreen)) {
   settings_error=SDL_GetError(); return false;
  }
  if(!write_settings(draft_scale,draft_fullscreen,draft_crt,draft_crt_strength,draft_crt_settings,draft_low_animation,draft_death_animation,draft_proceed_with_click,draft_click_exits_look,draft_quick_targeting,draft_quickbar_enabled)) {
   if(draft_fullscreen!=fullscreen) SDL_SetWindowFullscreen(window,fullscreen);
   settings_error="Settings could not be saved. Please try again."; return false;
  }
  low_animation=draft_low_animation; death_animation=draft_death_animation;
  proceed_with_click=draft_proceed_with_click;
  click_exits_look=draft_click_exits_look;
  quick_targeting=draft_quick_targeting;
  quickbar_enabled=draft_quickbar_enabled;
  scale=draft_scale; fullscreen=draft_fullscreen; crt=draft_crt;
  crt_strength=draft_crt_strength; crt_settings=draft_crt_settings;
  return true;
 }
 void settings_window(SDL_Window *window) {
  auto vp=ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x+vp->WorkSize.x*.5f,vp->WorkPos.y+vp->WorkSize.y*.5f),ImGuiCond_Appearing,ImVec2(.5f,.5f));
  ImGui::SetNextWindowSize(ImVec2(std::min(vp->WorkSize.x-24.f,ImGui::GetFontSize()*28),
   std::min(vp->WorkSize.y-24.f,ImGui::GetFontSize()*34)),ImGuiCond_Appearing);
  if(ImGui::BeginPopupModal("Settings",nullptr,ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoSavedSettings)) {
   const float footer=ImGui::GetFrameHeightWithSpacing()+ImGui::GetStyle().ItemSpacing.y;
   ImGui::BeginChild("Settings contents",ImVec2(0,-footer));
   if(ImGui::BeginTabBar("Settings tabs")) {
    if(ImGui::BeginTabItem("Graphics")) {
     ImGui::Spacing(); ImGui::Checkbox("Fullscreen",&draft_fullscreen);
     ImGui::Spacing(); ImGui::TextUnformatted("UI scale"); ImGui::SetNextItemWidth(-1);
     char zoom[16]; SDL_snprintf(zoom,sizeof(zoom),"%.0f%%",draft_scale*100);
     if(ImGui::BeginCombo("##UI scale",zoom)) {
      for(float value:{.75f,1.f,1.25f,1.5f}) {
       char label[16]; SDL_snprintf(label,sizeof(label),"%.0f%%",value*100);
       if(ImGui::Selectable(label,draft_scale==value)) draft_scale=value;
      }
      ImGui::EndCombo();
     }
     ImGui::EndTabItem();
    }
    if(ImGui::BeginTabItem("Gameplay")) {
     ImGui::Checkbox("Quick-action bar",&draft_quickbar_enabled);
     if(ImGui::IsItemHovered()) ImGui::SetTooltip("Ten slots using top-row 1-0. Numpad movement is unchanged. Right-click a slot, item, spell or command to assign.");
     ImGui::Spacing();
     ImGui::Spacing(); ImGui::Checkbox("Proceed with click",&draft_proceed_with_click);
     ImGui::TextWrapped("Left-click the game view to continue at - more -.");
     ImGui::Spacing(); ImGui::Checkbox("Quick targeting",&draft_quick_targeting);
     if(ImGui::IsItemHovered()) ImGui::SetTooltip("Click to confirm a target and continue casting, shooting or another aimed action.");
     ImGui::Spacing(); ImGui::Checkbox("Click exits look",&draft_click_exits_look);
     ImGui::EndTabItem();
    }
    if(ImGui::BeginTabItem("Animations")) {
     ImGui::Spacing(); ImGui::Checkbox("Low Health Animation",&draft_low_animation);
     ImGui::Checkbox("Death Animation",&draft_death_animation);
     ImGui::EndTabItem();
    }
    if(ImGui::BeginTabItem("CRT effects")) {
     ImGui::Spacing(); ImGui::TextUnformatted("Effects Enabled"); ImGui::SetNextItemWidth(-1);
     const char *effects[]={"Off","Game Window Only","Full"};
     ImGui::Combo("##CRT Effects",&draft_crt,effects,3);
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
     ImGui::Spacing(); ImGui::TextUnformatted("Effect Strength"); ImGui::SetNextItemWidth(-1);
     const char *strengths[]={"Subtle","Classic","Deluxe","Zero Cool"};
     if(ImGui::BeginCombo("##CRT Effects Strength",draft_crt_strength<0?"Custom":strengths[draft_crt_strength])) {
      for(int i=0;i<4;++i) if(ImGui::Selectable(strengths[i],draft_crt_strength==i)) {
       const int lines=draft_crt_settings.raster_lines,mask=draft_crt_settings.mask;
       draft_crt_strength=i; draft_crt_settings=CrtSettings(i);
       draft_crt_settings.raster_lines=lines; draft_crt_settings.mask=mask; draft_crt_settings.tube_preset=-1;
      }
      ImGui::EndCombo();
     }
     ImGui::TextWrapped("Presets reset all effect sliders and switches.");
     ImGui::Spacing(); ImGui::Separator();
     for(int i=0;i<CrtPartCount;++i) {
      auto &control=draft_crt_settings.parts[i];
      ImGui::PushID(i); ImGui::Spacing();
      if(ImGui::Checkbox(crt_labels[i],&control.enabled)) { draft_crt_strength=-1; draft_crt_settings.tube_preset=-1; }
      ImGui::BeginDisabled(!control.enabled); ImGui::SetNextItemWidth(-1);
      if(ImGui::SliderFloat("##Amount",&control.value,0.f,100.f,"%.0f%%",ImGuiSliderFlags_AlwaysClamp)) { draft_crt_strength=-1; draft_crt_settings.tube_preset=-1; }
      ImGui::EndDisabled(); ImGui::PopID();
     }
     ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
   }
   if(!settings_error.empty()) { ImGui::Spacing(); ImGui::TextWrapped("%s",settings_error.c_str()); }
   ImGui::EndChild();
   if(ImGui::Button("Cancel")||ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
   ImGui::SameLine();
   const float button_width=ImGui::CalcTextSize("Save and Close").x+2*ImGui::GetStyle().FramePadding.x;
   ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(),ImGui::GetWindowWidth()-ImGui::GetStyle().WindowPadding.x-button_width));
   if(ImGui::Button("Save and Close")) {
    if(apply_settings(window)) ImGui::CloseCurrentPopup();
   }
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
  if(id=="core.inscribe" && c.ready()) inscription_edit=true;
  if(id=="core.character" && c.state.contains("player") && c.state["player"].contains("character_sheet")) { keys.clear(); open_character_sheet=true; return; }
  if(c.native_targeting() && (id=="core.look" || id=="core.target")) c.target("targeting.begin",{{"mode",id=="core.look"?"look":"target"}});
  else c.command(id,item);
  focus_game();
 }
 bool owns_keyboard() const {
  return c.state.contains("terminal") && !c.state.contains("birth") && !c.state.contains("store") && grid_focus && window_active && c.prompt.empty() && c.pending_prompt.empty()
   && !quit_dialog && !ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId);
 }
 void prepare_frame(SDL_Window *window) {
  display_scale=SDL_GetWindowDisplayScale(window);
  if(display_scale<=0) display_scale=1.f;
  // Rebuild from the unscaled style; repeated changes must not accumulate rounding.
  ImGui::GetStyle()=base_style;
  ImGui::GetStyle().ScaleAllSizes(display_scale*scale);
  ImGui::GetStyle().FontScaleDpi=display_scale;
  ImGui::GetStyle().FontScaleMain=scale;
  if(owns_keyboard()) ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
  else ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  if(!c.prompt.empty()) return_from_prompt=true;
  else if(return_from_prompt && c.pending_prompt.empty()) { return_from_prompt=false; focus_game(); }
 }
 void launcher() {
  const float heading_right=ImGui::GetCursorPosX()+ImGui::GetContentRegionAvail().x;
  ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("Characters");
  ImGui::SameLine();
  const float new_character_width=ImGui::CalcTextSize("New character").x+2*ImGui::GetStyle().FramePadding.x;
  ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(),heading_right-new_character_width));
  ImGui::BeginDisabled(!c.negotiated || c.busy);
  if (ImGui::Button("New character")) { c.menu_error.clear(); ImGui::OpenPopup("New character"); }
  ImGui::Separator();
  bool rename_clicked=false, delete_clicked=false;
  if(ImGui::BeginTable("Saved characters",3,ImGuiTableFlags_SizingStretchProp)) {
   ImGui::TableSetupColumn("Character",ImGuiTableColumnFlags_WidthStretch);
   ImGui::TableSetupColumn("Rename",ImGuiTableColumnFlags_WidthFixed);
   ImGui::TableSetupColumn("Delete",ImGuiTableColumnFlags_WidthFixed);
   for (const auto &s:c.saves) {
   std::string id=s.value("id","");
   ImGui::PushID(id.c_str()); ImGui::TableNextRow(); ImGui::TableNextColumn();
   if (ImGui::Selectable((id+" — "+s.value("description","")).c_str())) { c.menu_error.clear(); c.send("session.load",{{"save",id}}); c.busy=true; focus_game(); }
   ImGui::TableNextColumn();
   if(ImGui::Button("Rename")) { managed_save=id; SDL_strlcpy(renamed_save,id.c_str(),sizeof(renamed_save)); rename_clicked=true; c.menu_error.clear(); }
   ImGui::TableNextColumn();
   if(ImGui::Button("Delete")) { managed_save=id; delete_clicked=true; c.menu_error.clear(); }
   ImGui::PopID();
   }
   ImGui::EndTable();
  }
  ImGui::EndDisabled();
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
    c.menu_error.clear(); c.send("session.new",{{"save",name}}); c.busy=true; focus_game(); ImGui::CloseCurrentPopup();
   }
   ImGui::EndDisabled(); ImGui::SameLine();
   if(ImGui::Button("Cancel")||ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
   ImGui::EndPopup();
  }
 }
 void grid(float height) {
  if (!c.state.contains("terminal")) { launcher(); return; }
  ImGui::PushStyleColor(ImGuiCol_Border,owns_keyboard()?ImVec4(.35f,.58f,.78f,1):ImVec4(.16f,.19f,.23f,1));
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
  const auto start=ImGui::GetCursorScreenPos();
  const auto available=ImGui::GetContentRegionAvail();
  const ImVec2 viewport(std::max(1.f,available.x),std::max(1.f,available.y));
  // Fit the complete semantic viewport (or fallback terminal) without scrolling.
  const float pixels=std::min(
   std::max(.01f,viewport.x-2)/(float(columns)*.60f),
   std::max(.01f,viewport.y-2)/(float(std::max(size_t(1),grid.height))*1.12f));
  const float cw=pixels*.60f, ch=pixels*1.12f;
  const ImVec2 size(cw*float(columns),ch*float(grid.height));
  const ImVec2 origin(start.x+(viewport.x-size.x)*.5f,start.y+(viewport.y-size.y)*.5f);
  ImGui::InvisibleButton("Dungeon keyboard surface",viewport,ImGuiButtonFlags_EnableNav);
  if (ImGui::IsItemClicked()) grid_focus = true;
  if(ImGui::IsItemClicked()) proceed_click();
  if(ImGui::IsItemFocused()) grid_focus=true;
  auto draw=ImGui::GetWindowDrawList();
  game_draw_list=draw; game_pos=ImGui::GetWindowPos(); game_size=ImGui::GetWindowSize();
  draw->AddRectFilled(start,ImVec2(start.x+viewport.x,start.y+viewport.y),IM_COL32(12,15,20,255));
  for(size_t y=0;y<grid.height;++y) for(size_t x=0;x<grid.width;++x) {
   const auto &cell=grid.cells[y*grid.width+x];
   if(cell.glyph && cell.glyph!=' ') {
    const ImVec2 at(origin.x+float(x)*cw,origin.y+float(y)*ch);
    draw->AddText(ImGui::GetFont(),pixels,at,color(cell.color),utf8(cell.glyph).c_str());
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
   const bool hovered=ImGui::IsItemHovered() && grid_cell_at(mouse.x-origin.x,mouse.y-origin.y,cw,ch,grid.width,grid.height,x,y);
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
   if(hovered) {
    if(!mouse_target) outline(x,y,IM_COL32(140,185,220,190),display_scale);
    ImGui::BeginTooltip(); ImGui::PushTextWrapPos(ImGui::GetFontSize()*26);
    tile_details(x,y,false); ImGui::PopTextWrapPos(); ImGui::EndTooltip();
    if(ImGui::IsMouseClicked(0) && c.native_targeting() && !c.busy && !c.state.value("message_pending",false)) {
     const bool active=c.state.contains("targeting") || c.state.value("aiming",false) || c.state.value("direction_prompt",false);
     if(active || (c.ready() && c.mouse_movement())) {
      const auto &io=ImGui::GetIO();
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
      if(action=="tunnel" || action=="up" || action=="down") {
       contextual=true;
       choose(action=="tunnel"?"Tunnel":action=="up"?"Go up":"Go down","dungeon.terrain",{{"action",action}});
      }
     }
    if(contextual) ImGui::Separator();
    choose("Look","targeting.begin",{{"mode","look"}});
    choose("Target","targeting.set");
    ImGui::EndDisabled();
   }
   ImGui::EndDisabled(); ImGui::EndPopup();
  }
  if(!grid.semantic && c.state.contains("cursor")) {
   const auto &cursor=c.state["cursor"];
   const int x=cursor.value("x",-1), y=cursor.value("y",-1);
   if(cursor.value("visible",false) && x>=0 && y>=0 && size_t(y)<grid.height && size_t(x)<columns) {
    const ImVec2 p(origin.x+x*cw,origin.y+y*ch);
    draw->AddRect(p,ImVec2(p.x+cw,p.y+ch),IM_COL32(255,225,125,255),0,0,std::max(1.f,display_scale));
   }
  }

  if(c.state.value("message_pending",false)) {
   const char *label="- more -";
   const ImVec2 text_size=ImGui::CalcTextSize(label);
   const float padding=6.f*display_scale, inset=8.f*display_scale;
   const ImVec2 at(start.x+viewport.x-inset-text_size.x-2*padding,start.y+inset);
   const ImVec2 end(start.x+viewport.x-inset,at.y+text_size.y+2*padding);
   draw->AddRectFilled(at,end,IM_COL32(0,0,0,255));
   draw->AddText(ImVec2(at.x+padding,at.y+padding),IM_COL32(255,255,255,255),label);
  }

  if(!ImGui::IsWindowFocused()) grid_focus=false;
  ImGui::EndChild(); ImGui::PopStyleVar(); ImGui::PopStyleColor();
 }
 void character() {
  if(c.state.contains("player") && CharacterOverview::draw(c.state["player"],c.ready())) execute("core.character");
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
   ImGui::PushStyleColor(ImGuiCol_Text,color(o.value("name_color",1)));
   ImGui::TextWrapped("%s",o.value("label","").c_str());
   ImGui::PopStyleColor();
   if(full) ImGui::TextWrapped("%s",o.value("description","").c_str());
  }
 }
 void targeting_panel() {
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
 void items() {
  if(ImGui::BeginTabBar("Item categories")) {
   const char *tabs[]={"Pack","Equipment","Quiver"};
   for(int category=0;category<3;++category) if(ImGui::BeginTabItem(tabs[category])) {
    ImGui::PushID(tabs[category]); items_category(category); ImGui::PopID();
    ImGui::EndTabItem();
   }
   ImGui::EndTabBar();
  }
 }
 void items_category(int category) {
  ImGui::InputTextWithHint("##items","Search items",item_filter,sizeof(item_filter));
  if(!c.state.contains("items")) return;
  std::vector<const json*> values;
  for(const auto &o:c.state["items"]) {
   const auto location=o.value("location","");
   const bool equipment=location!="Pack" && location!="Quiver" && location!="Floor" && location!="Store" && location!="Home";
   if(category==0 ? location!="Pack" : category==2 ? location!="Quiver" : !equipment) continue;
   values.push_back(&o);
  }
  std::stable_sort(values.begin(),values.end(),[](const json *a,const json *b){return a->value("location","")<b->value("location","");});
  if(ImGui::BeginTable("items",category==1?3:2,ImGuiTableFlags_Resizable|ImGuiTableFlags_RowBg|ImGuiTableFlags_ScrollY,ImVec2(0,ImGui::GetTextLineHeightWithSpacing()*10))) {
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
    ImGui::PushStyleColor(ImGuiCol_Text,color(o.value("name_color",1)));
    if(ImGui::Selectable(label.c_str(),selected==id,ImGuiSelectableFlags_SpanAllColumns)) selected=id;
    ImGui::PopStyleColor();
    if(ImGui::BeginPopupContextItem("Item actions")) {
     selected=id;
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

    if(ImGui::IsItemHovered()) {
     ImGui::BeginTooltip(); ImGui::TextUnformatted(label.c_str());
     const auto inscription=o.value("inscription","");
     if(!inscription.empty()) ImGui::Text("Inscription: %s",inscription.c_str());
     ImGui::EndTooltip();
    }
    if(category==1) { ImGui::TableNextColumn(); ImGui::TextUnformatted(display_label(o.value("location","")).c_str()); }
    ImGui::TableNextColumn(); ImGui::Text("%d",o.value("quantity",0)); ImGui::PopID();
   }
   ImGui::EndTable();
  }
  for(const auto *item:values) if(item->value("id","")==selected) {
   const auto &o=*item;
   ImGui::SeparatorText("Inspection");
   ImGui::BeginDisabled(!c.ready());
   bool first=true;
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
   ImGui::PushStyleColor(ImGuiCol_Text,color(o.value("name_color",1)));
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
    ImGui::TableNextColumn(); ImGui::TextDisabled("%s",category==1?"Slot":"Location"); ImGui::SameLine();
    ImGui::TextWrapped("%s",display_label(o.value("location","")).c_str());
    ImGui::EndTable();
   }
   ImGui::PopStyleVar();
   const auto description=o.value("description","");
   ItemComparison::draw(c,o);
   if(!description.empty()) { ImGui::Spacing(); ItemDescription::draw(o); }
  }
 }
 void creatures() {
  if(!c.state.contains("monsters")) return;
  for(const auto &m:c.state["monsters"]) {
   if(!m.value("visible",false)) continue;
   ImGui::PushID(m.value("id","").c_str());
   if(ImGui::TreeNode(m.value("name","").c_str())) {
    ImGui::Text("Position %d, %d",m.value("x",0),m.value("y",0));
    ImGui::TextUnformatted(m.value("asleep",false)?"Asleep":"Awake"); ImGui::TreePop();
   }
   ImGui::PopID();
  }
 }
 void minimap() {
  if(!c.state.contains("map") || !c.catalog.contains("features")) return;
  const auto &map=c.state["map"]["known"];
  if(map.empty()) return;
  auto origin=ImGui::GetCursorScreenPos(); float cell=std::max(1.f,ImGui::GetContentRegionAvail().x/float(map[0].size()));
  auto draw=ImGui::GetWindowDrawList();
  for(size_t y=0;y<map.size();++y) for(size_t x=0;x<map[y].size();++x) {
   int f=map[y][x]; if(!f) continue;
   int attr=f<int(c.catalog["features"].size())?c.catalog["features"][f].value("color",2):2;
   draw->AddRectFilled(ImVec2(origin.x+x*cell,origin.y+y*cell),ImVec2(origin.x+(x+1)*cell,origin.y+(y+1)*cell),color(attr));
  }
  if(c.state.contains("player")) {
   auto &p=c.state["player"]; draw->AddCircleFilled(ImVec2(origin.x+(p.value("x",0)+.5f)*cell,origin.y+(p.value("y",0)+.5f)*cell),std::max(2.f,cell),IM_COL32(255,100,100,255));
  }
  ImGui::Dummy(ImVec2(cell*map[0].size(),cell*map.size()));
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
    ImGui::PushStyleColor(ImGuiCol_Text,option?color(item->value("name_color",1)):ImGui::GetColorU32(ImGuiCol_TextDisabled));
    if(ImGui::Selectable(item->value("label","").c_str(),prompt_item==id,ImGuiSelectableFlags_SpanAllColumns|ImGuiSelectableFlags_AllowDoubleClick)) {
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
 void prompts() {
  if(c.prompt.empty()) return;
  auto id=c.prompt.value("prompt_id","");
  const bool fresh=id!=last_prompt;
  const bool item_selection=c.prompt.value("selection_kind","")=="item";
  const bool spell_selection=c.prompt.value("selection_kind","")=="spell";
  if(fresh) { last_prompt=id; SDL_strlcpy(prompt_text,c.prompt.value("initial","").c_str(),sizeof(prompt_text)); }
  const bool editing_inscription=inscription_edit && c.prompt.value("type","")=="text";
  const char *prompt_title=editing_inscription?"Item inscription":"Angband asks";
  if(!ImGui::IsPopupOpen(prompt_title)) ImGui::OpenPopup(prompt_title);
  if(item_selection || spell_selection) ImGui::SetNextWindowSize(ImVec2(std::min(ImGui::GetMainViewport()->WorkSize.x-24,ImGui::GetFontSize()*48),0),ImGuiCond_Always);
  if(ImGui::BeginPopupModal(prompt_title,nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
   ImGui::TextWrapped("%s",c.prompt.value("text","").c_str());
   auto type=c.prompt.value("type",""); bool answered=false;
   if(type=="confirmation") {
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
     if(type=="quantity") { try { c.answer(std::stoi(prompt_text)); answered=true; } catch(...) { c.notice("Enter a number."); } }
     else { c.answer(std::string(prompt_text)); answered=true; }
    }
   }
   if(type!="choice") ImGui::SameLine();
   if(!answered && !spell_selection && (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape))) { inscription_edit=false; c.answer(nullptr); answered=true; }
   if(answered) { if(editing_inscription) inscription_edit=false; ImGui::CloseCurrentPopup(); } ImGui::EndPopup();
  }
 }
 void draw(SDL_Window *window) {
  quickbar.sync_saves(c.save_changes);
  quickbar.profile=c.character_save;
  game_draw_list=nullptr;
  auto vp=ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(vp->WorkPos); ImGui::SetNextWindowSize(vp->WorkSize);
  ImGui::Begin("Angband Deluxe",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoSavedSettings);
  const bool in_game=c.state.contains("terminal");
  if(in_game && !c.state.contains("birth")) {
   if(ImGui::Button("Save and...")) ImGui::OpenPopup("Save menu");
   if(ImGui::BeginPopup("Save menu")) {
    ImGui::BeginDisabled(!c.ready());
    if(ImGui::MenuItem("Save and continue")) { c.save(); focus_game(); }
    if(ImGui::MenuItem("Save and return to main menu")) c.save(true,true);
    if(ImGui::MenuItem("Save and quit")) c.save(true);
    ImGui::EndDisabled();
    if(!c.ready()) ImGui::TextUnformatted("Return to normal play to save.");
    ImGui::EndPopup();
   }
   ImGui::SameLine();
  }
  const float settings_width=ImGui::CalcTextSize("Settings").x+2*ImGui::GetStyle().FramePadding.x;
  const float dev_width=ImGui::CalcTextSize("Dev tools").x+2*ImGui::GetStyle().FramePadding.x;
  ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(),ImGui::GetWindowSize().x-ImGui::GetStyle().WindowPadding.x-settings_width-dev_width-ImGui::GetStyle().ItemSpacing.x));
  ImGui::PushStyleColor(ImGuiCol_Button,ImVec4(.55f,.12f,.15f,1));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered,ImVec4(.72f,.19f,.22f,1));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive,ImVec4(.85f,.24f,.26f,1));
  if(ImGui::Button("Dev tools")) ImGui::OpenPopup("Developer tools");
  ImGui::PopStyleColor(3);
  bool open_damage=false;
  if(ImGui::BeginPopup("Developer tools")) {
   if(ImGui::MenuItem("Inflict damage on player",nullptr,false,c.ready() && c.state.value("phase","")=="playing")) open_damage=true;
   ImGui::Separator();
   if(ImGui::MenuItem("Quit without saving",nullptr,false,c.connected && !c.busy && c.capabilities.value("debug.quit",0)>0)) c.quit_without_saving();
   ImGui::EndPopup();
  }
  if(open_damage) { damage_amount=1; ImGui::OpenPopup("Inflict damage on player"); }
  if(ImGui::BeginPopupModal("Inflict damage on player",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
   ImGui::TextUnformatted("Damage to deal");
   if(ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
   ImGui::InputInt("##Damage",&damage_amount,1,10);
   ImGui::TextDisabled("1–30000 HP. Damage can kill your character.");
   ImGui::BeginDisabled(!c.ready() || damage_amount<1 || damage_amount>30000);
   if(ImGui::Button("Inflict damage")) {
    c.send("debug.damage",{{"amount",damage_amount}}); c.busy=true;
    ImGui::CloseCurrentPopup(); focus_game();
   }
   ImGui::EndDisabled(); ImGui::SameLine();
   if(ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
   ImGui::EndPopup();
  }
  ImGui::SameLine();
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
  if(creating_character) {
   if(c.state.contains("birth")) { grid_focus=false; keys.clear(); birth_panel.draw(c); }
   else grid(std::max(1.f,ImGui::GetContentRegionAvail().y));
  } else birth_panel.initialized=false;
  if(in_game && !creating_character && !in_store && ImGui::BeginTable("layout",2,ImGuiTableFlags_Resizable|ImGuiTableFlags_BordersInnerV)) {
   ImGui::TableSetupColumn("Game",ImGuiTableColumnFlags_WidthStretch,0.69f);
   ImGui::TableSetupColumn("Panels",ImGuiTableColumnFlags_WidthStretch,0.31f);
   ImGui::TableNextRow(); ImGui::TableNextColumn();
   ImGui::BeginChild("Game",ImVec2(0,0),ImGuiChildFlags_None,ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);
   ImGui::SetScrollX(0); ImGui::SetScrollY(0);
   const float divider_height=8.f*display_scale*scale;
   const bool show_quickbar=quickbar_enabled && phase=="playing";
   const float usable_height=std::max(1.f,ImGui::GetContentRegionAvail().y-divider_height-2*ImGui::GetStyle().ItemSpacing.y-(show_quickbar?Quickbar::height():0));
   const float min_fraction=std::max(.2f,std::min(.4f,4*ImGui::GetTextLineHeight()/usable_height));
   const float max_fraction=std::min(.9f,1-std::min(.4f,3*ImGui::GetTextLineHeight()/usable_height));
   const float fraction=std::clamp(game_fraction,min_fraction,max_fraction);
   grid(std::max(1.f,usable_height*fraction));
   if(show_quickbar) { ImGui::PushID("Quickbar"); int slot=quickbar.draw(c); if(slot>=0) activate_quickbar(slot); ImGui::PopID(); }
   if(c.state.contains("terminal")) {
    const auto divider=ImGui::GetCursorScreenPos();
    const float width=std::max(1.f,ImGui::GetContentRegionAvail().x);
    ImGui::InvisibleButton("Resize message panel",ImVec2(width,divider_height));
    if(ImGui::IsItemActivated()) { split_drag_y=ImGui::GetIO().MousePos.y; split_drag_fraction=fraction; }
    if(ImGui::IsItemActive()) game_fraction=std::clamp(split_drag_fraction+(ImGui::GetIO().MousePos.y-split_drag_y)/usable_height,min_fraction,max_fraction);
    if(ImGui::IsItemDeactivated()) save_settings();
    if(ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) { game_fraction=.72f; save_settings(); }
    const bool highlighted=ImGui::IsItemHovered()||ImGui::IsItemActive();
    if(highlighted) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("Drag to resize messages. Double-click to reset.");
    ImGui::GetWindowDrawList()->AddLine(ImVec2(divider.x,divider.y+divider_height*.5f),
     ImVec2(divider.x+width,divider.y+divider_height*.5f),
     ImGui::GetColorU32(highlighted?ImGuiCol_SeparatorHovered:ImGuiCol_Separator),display_scale);
   }
   ImGui::BeginChild("Message history");
   ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("Messages"); ImGui::SameLine();
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
   if(message_search_open) {
    ImGui::SameLine(); ImGui::SetNextItemWidth(std::max(1.f,ImGui::GetContentRegionAvail().x));
    if(focus_search) ImGui::SetKeyboardFocusHere();
    ImGui::InputTextWithHint("##messages","Search messages",message_filter,sizeof(message_filter));
   }
   for(const auto &m:c.messages) {
    auto text=m.value("text",""); if(matches(text,message_filter)) ImGui::TextWrapped("%s%s",text.c_str(),m.value("count",1)>1?(" (x"+std::to_string(m.value("count",1))+")").c_str():"");
   }
   ImGui::EndChild();
   ImGui::EndChild(); ImGui::TableNextColumn();
   ImGui::BeginChild("Panels",ImVec2(0,0),ImGuiChildFlags_None,ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);
   ImGui::SetScrollX(0); ImGui::SetScrollY(0);
   character();
   ImGui::Dummy(ImVec2(0,ImGui::GetTextLineHeight()*.45f));
   ImGui::Separator();
   const bool targeting_active=c.state.contains("targeting") || c.state.value("aiming",false) || c.state.value("direction_prompt",false);
   if(ImGui::BeginTabBar("panels")) {
    if(targeting_active && ImGui::BeginTabItem("Look / Target",nullptr,targeting_was_active?ImGuiTabItemFlags_None:ImGuiTabItemFlags_SetSelected)) {
     ImGui::BeginChild("Target content"); targeting_panel(); ImGui::EndChild(); ImGui::EndTabItem();
    }
    if(ImGui::BeginTabItem("Inventory")) { ImGui::BeginChild("Item content"); items(); ImGui::EndChild(); ImGui::EndTabItem(); }
    if(c.capabilities.value("spells",0)>0 && c.state.contains("player") && c.state["player"].value("spellcasting",false) && ImGui::BeginTabItem("Spells")) {
     ImGui::BeginChild("Spell content"); if(spell_panel.draw(c,quickbar_enabled?&quickbar:nullptr)) focus_game(); ImGui::EndChild(); ImGui::EndTabItem();
    }
    if(ImGui::BeginTabItem("Creatures")) { ImGui::BeginChild("Creature content"); creatures(); ImGui::EndChild(); ImGui::EndTabItem(); }
    if(ImGui::BeginTabItem("Map")) { ImGui::BeginChild("Map content"); minimap(); ImGui::EndChild(); ImGui::EndTabItem(); }
    if(ImGui::BeginTabItem("Commands")) {
     ImGui::BeginChild("Command content");
     ImGui::InputTextWithHint("##commands","Search commands",command_filter,sizeof(command_filter));
     ImGui::BeginDisabled(!c.ready());
     for(const auto &cmd:c.commands) {
      auto label=cmd.value("label",""); if(!matches(label,command_filter)) continue;
      ImGui::PushID(cmd.value("id","").c_str());
      if(ImGui::Selectable(label.c_str())) execute(cmd.value("id",""));
      if(quickbar_enabled && ImGui::BeginPopupContextItem("Command actions")) { quickbar.assign_menu(Quickbar::command_binding(cmd)); ImGui::EndPopup(); }
      ImGui::PopID();
     }
     ImGui::EndDisabled(); ImGui::EndChild(); ImGui::EndTabItem();
    }
    if(ImGui::BeginTabItem("More")) {
     ImGui::BeginChild("More content");
     if(c.capabilities.value("item.rules",0)>0 && ImGui::Button("Item rules")) item_rules_panel.open=true;
     ImGui::EndChild(); ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
   }
   targeting_was_active=targeting_active;
   ImGui::EndChild(); ImGui::EndTable();
  }
  if(quickbar.customize_window()) focus_game();
  if(quickbar.dirty) { quickbar.dirty=false; save_settings(); }
  if(item_rules_panel.draw(c)) focus_game();
  if(c.state.contains("player")) {
   if(CharacterSheet::draw(c.state["player"],open_character_sheet)) focus_game();
  }
  open_character_sheet=false;
  if(quit_dialog) { ImGui::OpenPopup("Close game"); quit_dialog=false; }
  if(ImGui::BeginPopupModal("Close game",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
   ImGui::TextWrapped(c.ready()?"Save this character and close Deluxe?":"Return to normal play to save. You can finish the current menu first.");
   ImGui::BeginDisabled(!c.ready());
   if(ImGui::Button("Save and quit")) { c.save(true); ImGui::CloseCurrentPopup(); }
   ImGui::EndDisabled(); ImGui::SameLine();
   if(ImGui::Button("Continue playing")) { ImGui::CloseCurrentPopup(); focus_game(); }
   if(!c.state.contains("player") || !c.connected) if(ImGui::Button("Close")) { c.closed=true; if(c.process) SDL_KillProcess(c.process.get(),true); }
   ImGui::EndPopup();
  }
  prompts(); ImGui::End();
  dispatch_keys();
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

#ifndef DELUXE_CLIENT_TEST
int main(int argc,char **argv) {
 if(!SDL_Init(SDL_INIT_VIDEO|SDL_INIT_GAMEPAD)) return 1;
 const float dpi=SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
 SDL_Rect bounds{}; SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(), &bounds);
 const int width=std::min(int(1280*dpi),std::max(640,bounds.w-80));
 const int height=std::min(int(800*dpi),std::max(480,bounds.h-80));
 SDL_Window *window=SDL_CreateWindow("Angband Deluxe",width,height,SDL_WINDOW_RESIZABLE|SDL_WINDOW_HIGH_PIXEL_DENSITY);
 if(window) SDL_SetWindowPosition(window,SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED);
 SDL_GPUDevice *gpu=SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV|SDL_GPU_SHADERFORMAT_DXIL|SDL_GPU_SHADERFORMAT_METALLIB|SDL_GPU_SHADERFORMAT_MSL,false,nullptr);
 if(!window || !gpu || !SDL_ClaimWindowForGPUDevice(gpu,window)) {
  SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,"Angband Deluxe",SDL_GetError(),window); return 1;
 }
 SDL_SetGPUSwapchainParameters(gpu,window,SDL_GPU_SWAPCHAINCOMPOSITION_SDR,SDL_GPU_PRESENTMODE_VSYNC);
 SDL_SetGPUAllowedFramesInFlight(gpu,1); // Bound presentation latency; still paced by vsync.
 IMGUI_CHECKVERSION(); ImGui::CreateContext();
 auto &io=ImGui::GetIO(); io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard|ImGuiConfigFlags_NavEnableGamepad;
 io.Fonts->AddFontFromFileTTF(DELUXE_FONT_FILE,18.f);
 ImGui::StyleColorsDark(); ImGui::GetStyle().WindowRounding=5;
 ImGui::GetStyle().FontSizeBase=18.f;
 ImGui_ImplSDL3_InitForSDLGPU(window);
 ImGui_ImplSDLGPU3_InitInfo info{}; info.Device=gpu; info.ColorTargetFormat=SDL_GetGPUSwapchainTextureFormat(gpu,window); info.MSAASamples=SDL_GPU_SAMPLECOUNT_1;
 ImGui_ImplSDLGPU3_Init(&info);
 Connection connection; UI ui{connection}; CrtRenderer crt_renderer;
 crt_renderer.initialize(gpu,info.ColorTargetFormat);
 std::string renderer_error;
 ui.base_style=ImGui::GetStyle();
 const char *base=SDL_GetBasePath();
 char *pref=SDL_GetPrefPath("AngbandDeluxe","AngbandDeluxe");
 std::string user=pref?pref:"deluxe-user"; SDL_free(pref);
 std::string backend=(fs::path(base?base:".")/
#ifdef _WIN32
 "angband-backend.exe"
#else
 "angband-backend"
#endif
 ).string();
 std::string data=DELUXE_DATA_DIR;
 for(int i=1;i+1<argc;i+=2) {
  if(std::string(argv[i])=="--backend") backend=argv[i+1];
  else if(std::string(argv[i])=="--data-dir") data=argv[i+1];
  else if(std::string(argv[i])=="--user-dir") user=argv[i+1];
 }
 fs::create_directories(user); ui.settings_path=(fs::path(user)/"settings.json").string(); ui.load_settings();
 if(ui.fullscreen && !SDL_SetWindowFullscreen(window,true)) { ui.fullscreen=false; connection.notice(std::string("Fullscreen unavailable: ")+SDL_GetError()); }
 std::string ini=(fs::path(user)/"layout.ini").string(); io.IniFilename=ini.c_str();
 connection.start(backend,data,user);
 SDL_StartTextInput(window);
 HealthGlitch health_glitch;
 auto poll_backend=[&] {
  const auto phase=connection.state.value("phase","");
  const auto readiness=connection.state.value("readiness","");
  connection.poll();
  if(phase!=connection.state.value("phase","") || readiness!=connection.state.value("readiness","") || !connection.prompt.empty())
   ui.keys.clear(); // Buffered movement must not answer a newly opened menu/prompt.
 };
 while(!connection.closed) {
  // Pace first: sampling input and ImGui's clock before a blocking presentation
  // wait produces stale, uneven animation times even when GPU work is fast.
  if(!SDL_WaitForGPUSwapchain(gpu,window)) break;
  poll_backend();
  if(connection.restart_ready) {
   crt_renderer.reset_history();
   health_glitch=HealthGlitch{};
   // A saved return-to-menu or normal post-game completion has exited cleanly.
   connection.close_process();
   connection=Connection{};
   ui.grid_focus=ui.focus_requested=ui.return_from_prompt=false;
   ui.selected.clear(); ui.last_prompt.clear(); ui.keys.clear();
   ui.item_filter[0]=ui.command_filter[0]=ui.message_filter[0]=0;
   ui.message_search_open=false;
   ui.quit_dialog=false;
   connection.start(backend,data,user);
  }
  ui.prepare_frame(window); SDL_Event e;
  while(SDL_PollEvent(&e)) {
   if(e.type==SDL_EVENT_WINDOW_FOCUS_LOST) { ui.window_active=false; ui.keys.clear(); }
   if(e.type==SDL_EVENT_WINDOW_FOCUS_GAINED) ui.window_active=true;
   if(e.type==SDL_EVENT_MOUSE_BUTTON_DOWN) { ui.grid_focus=false; ui.keys.clear(); }
   if(e.type==SDL_EVENT_MOUSE_MOTION && ui.crt!=0 && crt_renderer.ready()) {
    auto vp=ImGui::GetMainViewport();
    CrtCurve curve(ui.crt==2?vp->Pos:ui.game_pos,ui.crt==2?vp->Size:ui.game_size,ui.crt_settings);
    if(e.motion.x>=curve.pos.x && e.motion.x<=curve.pos.x+curve.size.x && e.motion.y>=curve.pos.y && e.motion.y<=curve.pos.y+curve.size.y) {
     auto p=curve.map(ImVec2(e.motion.x,e.motion.y),true); e.motion.x=p.x; e.motion.y=p.y;
    }
   }
   if(ui.quickbar_event(e)) continue;
   ImGui_ImplSDL3_ProcessEvent(&e);
   if(e.type==SDL_EVENT_QUIT) {
    if(!connection.state.contains("terminal")) { connection.closed=true; if(connection.process) SDL_KillProcess(connection.process.get(),true); }
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
     default: if((e.key.mod&SDL_KMOD_CTRL) && e.key.key>='a' && e.key.key<='z') ui.keys.push_back(int(e.key.key-'a'+1));
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
  ImGui_ImplSDLGPU3_NewFrame(); ImGui_ImplSDL3_NewFrame();
  if(ui.crt!=0 && crt_renderer.ready() && SDL_GetMouseFocus()==window) {
   float x,y; SDL_GetMouseState(&x,&y);
   auto vp=ImGui::GetMainViewport();
   CrtCurve curve(ui.crt==2?vp->Pos:ui.game_pos,ui.crt==2?vp->Size:ui.game_size,ui.crt_settings);
   if(x>=curve.pos.x && x<=curve.pos.x+curve.size.x && y>=curve.pos.y && y<=curve.pos.y+curve.size.y) {
    auto p=curve.map(ImVec2(x,y),true); ImGui::GetIO().AddMousePosEvent(p.x,p.y);
   }
  }
  ImGui::NewFrame(); ui.draw(window);
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
   frame.scope=ui.crt; frame.settings=ui.crt_settings;
   frame.game=ui.game_draw_list; frame.game_pos=ui.game_pos; frame.game_size=ui.game_size;
   frame.seconds=double(SDL_GetTicksNS())/1e9; frame.ui_scale=ui.scale*ui.display_scale; frame.session=connection.state.value("phase","");
   frame.health_glitch=health_glitch.update(connection.state,frame.seconds,ui.low_animation,ui.death_animation);
   crt_renderer.render(cmd,surface,surface_width,surface_height,render_data,frame);
   if(renderer_error!=crt_renderer.error()) {
    renderer_error=crt_renderer.error(); if(!renderer_error.empty()) connection.notice(renderer_error);
   }
  }
  SDL_SubmitGPUCommandBuffer(cmd);
  if(SDL_GetWindowFlags(window)&SDL_WINDOW_MINIMIZED) SDL_Delay(20);
 }
 SDL_WaitForGPUIdle(gpu); crt_renderer.shutdown(); ImGui_ImplSDLGPU3_Shutdown(); ImGui_ImplSDL3_Shutdown(); ImGui::DestroyContext();
 connection.close_process();
 SDL_ReleaseWindowFromGPUDevice(gpu,window); SDL_DestroyGPUDevice(gpu); SDL_DestroyWindow(window); SDL_Quit(); return 0;
}
#endif
