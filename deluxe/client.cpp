// Angband Deluxe desktop client. GPLv2. No engine headers or linked engine state.
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlgpu3.h"
#include "crt_renderer.h"
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
 SDL_Process *process = nullptr;
 std::string received, outgoing, diagnostic, menu_error;
 std::deque<json> messages;
 json previous_messages = json::array();
 std::map<std::string,std::string> requests;
 unsigned long next = 0;
 bool connected = false, negotiated = false, busy = false, close_requested = false, closed = false;
 bool return_to_menu = false, restart_ready = false, close_confirmed = false;
 json state = json::object(), prompt = json::object(), pending_prompt = json::object(), commands = json::array(), saves = json::array(), catalog = json::object();
 ~Connection() { if (process) SDL_DestroyProcess(process); }
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
 std::string send(const std::string &method, json params = json::object()) {
  if (!connected) return "";
  auto id = "r" + std::to_string(++next);
  params["session_id"] = "session-1";
  outgoing += json{{"kind","request"},{"id",id},{"method",method},{"params",params}}.dump() + "\n";
  requests[id] = method;
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
  process = SDL_CreateProcessWithProperties(properties); SDL_DestroyProperties(properties);
  if (!process) { menu_error=SDL_GetError(); notice(menu_error); return false; }
  connected = true;
  send("hello",{{"protocols",json::array({{{"major",0},{"minor",1}}})},{"max_frame_bytes",1048576}});
  return true;
 }
 void receive(json j) {
  if (j.value("kind","") == "event") {
   auto name = j.value("event","");
   if (name == "state.changed") { state = std::move(j.at("data")); update_messages(state.at("messages")); busy = false; }
   if (name == "prompt.requested") { prompt = j.at("data"); busy = false; }
   return;
  }
  auto id = j.value("id",""); auto it = requests.find(id);
  if (it == requests.end()) return;
  auto method = it->second; requests.erase(it);
  if (j.contains("error")) {
   const auto error=j["error"].value("message","Request failed"); notice(error); busy = false;
   if(!state.contains("terminal")) menu_error=error;
   if(method=="session.close") { close_requested=false; return_to_menu=false; }
   if(method == "prompt.reply") { prompt = pending_prompt; pending_prompt = json::object(); }
   return;
  }
  const auto &result = j.at("result");
  if (method == "hello") {
   negotiated = true; send("saves.list"); send("commands.list");
  } else if (method == "saves.list") { saves = result; busy=false; }
  else if (method == "saves.rename" || method == "saves.delete") { menu_error.clear(); send("saves.list"); }
  else if (method == "commands.list") commands = result;
  else if (method == "catalog.get") catalog = result;
  else if (method == "session.new" || method == "session.load") { menu_error.clear(); send("catalog.get"); }
  else if (method == "session.save") { busy = false; notice("Game saved."); }
  else if (method == "session.close") { close_confirmed=true; closed = !return_to_menu; busy = false; }
  else if (method == "prompt.reply") pending_prompt = json::object();
 }
 void flush_input() {
  if(connected && !outgoing.empty()) {
   const auto n=SDL_WriteIO(SDL_GetProcessInput(process),outgoing.data(),outgoing.size());
   outgoing.erase(0,n);
  }
 }
 void process_stopped(int exit_code) {
  connected=false; busy=false;
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
  // Observe termination BEFORE draining stdout, so the last state/close reply
  // cannot arrive between the drain and our exit decision.
  int exit_code=0;
  const bool exited=SDL_WaitProcess(process,false,&exit_code);
  char buffer[16384]; size_t n;
  auto err = static_cast<SDL_IOStream*>(SDL_GetPointerProperty(SDL_GetProcessProperties(process),SDL_PROP_PROCESS_STDERR_POINTER,nullptr));
  if (err) while ((n = SDL_ReadIO(err,buffer,sizeof(buffer))) > 0) {
   diagnostic.append(buffer,n); if (diagnostic.size() > 65536) diagnostic.erase(0,diagnostic.size()-65536);
  }
  auto output = SDL_GetProcessOutput(process);
  size_t read = 0;
  while (read < 4*1024*1024 && (n = SDL_ReadIO(output,buffer,sizeof(buffer))) > 0) {
   read += n; received.append(buffer,n);
   size_t end;
   while ((end = received.find('\n')) != std::string::npos) {
    if (end > 1048576) { notice("Backend frame too large"); connected = false; return; }
    try { receive(json::parse(received.substr(0,end))); }
    catch (const std::exception &e) { notice(std::string("Invalid backend message: ") + e.what()); connected = false; return; }
    received.erase(0,end+1);
   }
   if (received.size() > 1048576) { notice("Backend frame too large"); connected = false; return; }
  }
  if(!exited) flush_input();
  else if(read<4*1024*1024) process_stopped(exit_code); // Drain capped output next frame first.
 }
 bool ready() const { return connected && !busy && prompt.empty() && state.value("readiness","") == "ready"; }
 bool key(const json &k) {
  if (!connected || busy || !prompt.empty() || state.empty()) return false;
  send("terminal.input",{{"context",state.value("context","")},{"key",k}}); busy = true;
  return true;
 }
 void command(const std::string &id, const std::string &item = "") {
  if (!ready()) return;
  send("command.execute",{{"revision",state.value("revision","")},{"command",id},{"item",item}}); busy = true;
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
struct UI {
 Connection &c;
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
 float display_scale = 1.f;
 ImGuiStyle base_style;
 char item_filter[128]{}, message_filter[128]{}, command_filter[128]{}, save_name[65] = "Adventurer";
 char prompt_text[4096]{};
 std::string last_prompt, selected, settings_path;
 std::string managed_save;
 char renamed_save[65]{};
 std::vector<json> keys;
 void load_settings() {
  try { std::ifstream in(settings_path); if (!in) return; json j; in >> j;
   scale=std::clamp(j.value("scale",1.f),0.75f,1.5f);
   game_fraction=std::clamp(j.value("game_fraction",.72f),.2f,.9f);
   fullscreen=j.value("fullscreen",false);
   low_animation=j.value("low_health_animation",true); death_animation=j.value("death_animation",true);
   crt=std::clamp(j.value("crt",0),0,2);
   crt_strength=std::clamp(j.value("crt_strength",1),-1,3);
   crt_settings=CrtSettings(crt_strength);
   crt_settings.parts[Hum].enabled=j.value("hum_bar",false);
   crt_settings.parts[Ghost].enabled=false; // New effects remain opt-in for legacy preferences.
   if(j.contains("crt_components")) crt_settings.load(j.at("crt_components"));
  } catch (...) { c.notice("Settings could not be read; using defaults."); }
 }
 bool write_settings(float zoom,bool full,int effect,int strength,const CrtSettings &settings,bool low,bool death) {
  const std::string temporary=settings_path+".tmp";
  std::ofstream out(temporary);
  out << json{{"scale",zoom},{"game_fraction",game_fraction},{"fullscreen",full},{"crt",effect},{"crt_strength",strength},{"crt_components",settings.serialize()},{"low_health_animation",low},{"death_animation",death}}.dump(2);
  out.close();
  return bool(out) && SDL_RenamePath(temporary.c_str(),settings_path.c_str());
 }
 void save_settings() {
  if(!write_settings(scale,fullscreen,crt,crt_strength,crt_settings,low_animation,death_animation)) c.notice("Settings could not be saved.");
 }
 void begin_settings() {
  draft_low_animation=low_animation; draft_death_animation=death_animation;
  draft_scale=scale; draft_fullscreen=fullscreen; draft_crt=crt; settings_error.clear();
  draft_crt_strength=crt_strength; draft_crt_settings=crt_settings;
 }
 bool apply_settings(SDL_Window *window) {
  if(draft_fullscreen!=fullscreen && !SDL_SetWindowFullscreen(window,draft_fullscreen)) {
   settings_error=SDL_GetError(); return false;
  }
  if(!write_settings(draft_scale,draft_fullscreen,draft_crt,draft_crt_strength,draft_crt_settings,draft_low_animation,draft_death_animation)) {
   if(draft_fullscreen!=fullscreen) SDL_SetWindowFullscreen(window,fullscreen);
   settings_error="Settings could not be saved. Please try again."; return false;
  }
  low_animation=draft_low_animation; death_animation=draft_death_animation;
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
 void focus_game() { focus_requested=true; keys.clear(); }
 void execute(const std::string &id,const std::string &item="") { c.command(id,item); focus_game(); }
 bool owns_keyboard() const {
  return c.state.contains("terminal") && grid_focus && window_active && c.prompt.empty() && c.pending_prompt.empty()
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
  const auto &rows=c.state["terminal"];
  size_t columns=1;
  for(const auto &row:rows) columns=std::max(columns,row.size());
  const auto start=ImGui::GetCursorScreenPos();
  const auto available=ImGui::GetContentRegionAvail();
  const ImVec2 viewport(std::max(1.f,available.x),std::max(1.f,available.y));
  // Use the largest glyphs that keep every terminal row and column visible.
  const float pixels=std::min(
   std::max(.01f,viewport.x-2)/(float(columns)*.60f),
   std::max(.01f,viewport.y-2)/(float(std::max(size_t(1),rows.size()))*1.12f));
  const float cw=pixels*.60f, ch=pixels*1.12f;
  const ImVec2 size(cw*float(columns),ch*float(rows.size()));
  const ImVec2 origin(start.x+(viewport.x-size.x)*.5f,start.y+(viewport.y-size.y)*.5f);
  ImGui::InvisibleButton("Dungeon keyboard surface",viewport,ImGuiButtonFlags_EnableNav);
  if (ImGui::IsItemClicked()) grid_focus = true;
  if(ImGui::IsItemFocused()) grid_focus=true;
  auto draw=ImGui::GetWindowDrawList();
  game_draw_list=draw; game_pos=ImGui::GetWindowPos(); game_size=ImGui::GetWindowSize();
  draw->AddRectFilled(start,ImVec2(start.x+viewport.x,start.y+viewport.y),IM_COL32(12,15,20,255));
  for (size_t y=0;y<rows.size();++y) for(size_t x=0;x<rows[y].size();++x) {
   unsigned glyph=rows[y][x][0].get<unsigned>(); int col=rows[y][x][1].get<int>();
   if (glyph && glyph!=' ') draw->AddText(ImGui::GetFont(),pixels,
    ImVec2(origin.x+float(x)*cw,origin.y+float(y)*ch),color(col),utf8(glyph).c_str());
  }
  if(c.state.contains("cursor")) {
   const auto &cursor=c.state["cursor"];
   const int x=cursor.value("x",-1), y=cursor.value("y",-1);
   if(cursor.value("visible",false) && x>=0 && y>=0 && size_t(y)<rows.size() && size_t(x)<columns) {
    const ImVec2 p(origin.x+x*cw,origin.y+y*ch);
    draw->AddRect(p,ImVec2(p.x+cw,p.y+ch),IM_COL32(255,225,125,255),0,0,std::max(1.f,display_scale));
   }
  }

  if(!ImGui::IsWindowFocused()) grid_focus=false;
  ImGui::EndChild(); ImGui::PopStyleVar(); ImGui::PopStyleColor();
 }
 void character() {
  if (!c.state.contains("player")) return;
  const auto &p=c.state["player"];
  ImGui::Text("%s",p.value("name","").c_str());
  ImGui::TextWrapped("%s %s · Level %d",p.value("race","").c_str(),p.value("class","").c_str(),p.value("level",0));
  auto bar=[&](const char *name,const char *cur,const char *max,ImVec4 fill) {
   char b[80]; int v=p.value(cur,0),m=p.value(max,0); SDL_snprintf(b,sizeof(b),"%s %d / %d",name,std::max(0,v),std::max(0,m));
   ImGui::PushStyleColor(ImGuiCol_PlotHistogram,fill);
   ImGui::PushStyleColor(ImGuiCol_FrameBg,ImVec4(fill.x*.3f,fill.y*.3f,fill.z*.3f,1));
   ImGui::ProgressBar(resource_fraction(v,m),ImVec2(-1,0),b);
   ImGui::PopStyleColor(2);
  };
  bar("HP","hp","max_hp",ImVec4(.68f,.16f,.20f,1));
  bar("SP","sp","max_sp",ImVec4(.16f,.36f,.72f,1));
  const int food=p.value("food",0), food_max=p.value("food_max",0);
  const float food_fraction=food_max>0?float(food)/float(food_max):0.f;
  char food_label[80];
  SDL_snprintf(food_label,sizeof(food_label),"Food %.1f%% (%d)",100.f*food_fraction,food);
  ImGui::PushStyleColor(ImGuiCol_PlotHistogram,ImVec4(.16f,.48f,.27f,1));
  ImGui::PushStyleColor(ImGuiCol_FrameBg,ImVec4(.05f,.14f,.08f,1));
  ImGui::ProgressBar(std::clamp(food_fraction,0.f,1.f),ImVec2(-1,0),food_label);
  ImGui::PopStyleColor(2);
  ImGui::TextWrapped("Depth %d · Gold %d",p.value("depth",0),p.value("gold",0));
  ImGui::TextWrapped("Armour %d · Speed %+d",p.value("armour",0),p.value("speed",0));
  static const char *stats[]={"STR","INT","WIS","DEX","CON"};
  if(p.contains("stats")) for(size_t i=0;i<p["stats"].size();++i) {
   int v=p["stats"][i];
   char label[40];
   if(v>18) SDL_snprintf(label,sizeof(label),"%s 18/%02d",i<std::size(stats)?stats[i]:"Stat",v-18);
   else SDL_snprintf(label,sizeof(label),"%s %d",i<std::size(stats)?stats[i]:"Stat",v);
   const float right=ImGui::GetCursorScreenPos().x+ImGui::GetContentRegionAvail().x;
   if(i && ImGui::GetItemRectMax().x+ImGui::CalcTextSize(label).x+ImGui::GetStyle().ItemSpacing.x<right) ImGui::SameLine();
   ImGui::TextUnformatted(label);
  }
  for(const auto &s:p.value("statuses",json::array())) {
   if(s.value("label","")=="FOOD") continue;
   ImGui::Text("%s (%d)",s.value("label","").c_str(),s.value("duration",0));
  }
 }
 void items() {
  ImGui::InputTextWithHint("##items","Search items",item_filter,sizeof(item_filter));
  if(!c.state.contains("items")) return;
  std::vector<const json*> values;
  for(const auto &o:c.state["items"]) {
   if(o.value("location","")=="Floor") {
    if(!c.state.contains("player")) continue;
    const auto &p=c.state["player"];
    if(o.value("x",-1)!=p.value("x",-2)||o.value("y",-1)!=p.value("y",-2)) continue;
   }
   values.push_back(&o);
  }
  std::stable_sort(values.begin(),values.end(),[](const json *a,const json *b){return a->value("location","")<b->value("location","");});
  if(ImGui::BeginTable("items",3,ImGuiTableFlags_Resizable|ImGuiTableFlags_RowBg|ImGuiTableFlags_ScrollY,ImVec2(0,ImGui::GetTextLineHeightWithSpacing()*10))) {
   ImGui::TableSetupColumn("Item",ImGuiTableColumnFlags_WidthStretch); ImGui::TableSetupColumn("Location"); ImGui::TableSetupColumn("Qty"); ImGui::TableHeadersRow();
   for(const auto *item:values) {
    const auto &o=*item;
    std::string label=o.value("label","");
    if(!matches(label,item_filter)) continue;
    auto id=o.value("id",""); ImGui::PushID(id.c_str());
    ImGui::TableNextRow(); ImGui::TableNextColumn();
    if(ImGui::Selectable(label.c_str(),selected==id,ImGuiSelectableFlags_SpanAllColumns)) selected=id;
    if(ImGui::IsItemHovered()) {
     ImGui::BeginTooltip(); ImGui::TextUnformatted(label.c_str());
     const auto inscription=o.value("inscription","");
     if(!inscription.empty()) ImGui::Text("Inscription: %s",inscription.c_str());
     ImGui::EndTooltip();
    }
    ImGui::TableNextColumn(); ImGui::TextUnformatted(display_label(o.value("location","")).c_str());
    ImGui::TableNextColumn(); ImGui::Text("%d",o.value("quantity",0)); ImGui::PopID();
   }
   ImGui::EndTable();
  }
  for(const auto *item:values) if(item->value("id","")==selected) {
   const auto &o=*item;
   ImGui::SeparatorText("Inspection");
   properties(o["player_known"]);
   ImGui::BeginDisabled(!c.ready());
   bool first=true;
   for(const auto &action:o.value("actions",json::array())) {
    const std::string id=action.get<std::string>();
    const char *label=id=="core.wield"?"Wield":id=="core.use"?"Use":id=="core.drop"?"Drop":"Inscribe";
    if(!first) ImGui::SameLine(); first=false;
    if(ImGui::Button(label)) execute(id,selected);
   }
   ImGui::EndDisabled();
   const auto description=o.value("description","");
   if(!description.empty()) { ImGui::Spacing(); ImGui::TextWrapped("%s",description.c_str()); }
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
 void prompts() {
  if(c.prompt.empty()) return;
  auto id=c.prompt.value("prompt_id","");
  const bool fresh=id!=last_prompt;
  if(fresh) { last_prompt=id; SDL_strlcpy(prompt_text,c.prompt.value("initial","").c_str(),sizeof(prompt_text)); }
  if(!ImGui::IsPopupOpen("Angband asks")) ImGui::OpenPopup("Angband asks");
  if(ImGui::BeginPopupModal("Angband asks",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
   ImGui::TextWrapped("%s",c.prompt.value("text","").c_str());
   auto type=c.prompt.value("type",""); bool answered=false;
   if(type=="confirmation") {
    if(ImGui::Button("Yes")) { c.answer(true); answered=true; } ImGui::SameLine();
    if(ImGui::Button("No")) { c.answer(false); answered=true; }
   } else if(type=="choice") {
    for (const auto &option:c.prompt.value("choices",json::array())) {
     if(ImGui::Selectable(option.value("label","").c_str())) { c.answer(option.at("id")); answered=true; break; }
    }
   } else {
    if(fresh) ImGui::SetKeyboardFocusHere();
    const bool submitted=ImGui::InputText("##answer",prompt_text,sizeof(prompt_text),ImGuiInputTextFlags_EnterReturnsTrue);
    if(ImGui::Button("OK") || submitted) {
     if(type=="quantity") { try { c.answer(std::stoi(prompt_text)); answered=true; } catch(...) { c.notice("Enter a number."); } }
     else { c.answer(std::string(prompt_text)); answered=true; }
    }
   }
   if(type!="choice") ImGui::SameLine();
   if(!answered && (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape))) { c.answer(nullptr); answered=true; }
   if(answered) ImGui::CloseCurrentPopup(); ImGui::EndPopup();
  }
 }
 void draw(SDL_Window *window) {
  game_draw_list=nullptr;
  auto vp=ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(vp->WorkPos); ImGui::SetNextWindowSize(vp->WorkSize);
  ImGui::Begin("Angband Deluxe",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoSavedSettings);
  const bool in_game=c.state.contains("terminal");
  if(in_game) {
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
  const bool creating_character=in_game && (phase=="birth" || phase=="launcher");
  if(creating_character) grid(std::max(1.f,ImGui::GetContentRegionAvail().y));
  if(in_game && !creating_character && ImGui::BeginTable("layout",2,ImGuiTableFlags_Resizable|ImGuiTableFlags_BordersInnerV)) {
   ImGui::TableSetupColumn("Game",ImGuiTableColumnFlags_WidthStretch,0.69f);
   ImGui::TableSetupColumn("Panels",ImGuiTableColumnFlags_WidthStretch,0.31f);
   ImGui::TableNextRow(); ImGui::TableNextColumn();
   ImGui::BeginChild("Game",ImVec2(0,0),ImGuiChildFlags_None,ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);
   ImGui::SetScrollX(0); ImGui::SetScrollY(0);
   const float divider_height=8.f*display_scale*scale;
   const float usable_height=std::max(1.f,ImGui::GetContentRegionAvail().y-divider_height-2*ImGui::GetStyle().ItemSpacing.y);
   const float min_fraction=std::max(.2f,std::min(.4f,4*ImGui::GetTextLineHeight()/usable_height));
   const float max_fraction=std::min(.9f,1-std::min(.4f,3*ImGui::GetTextLineHeight()/usable_height));
   const float fraction=std::clamp(game_fraction,min_fraction,max_fraction);
   grid(std::max(1.f,usable_height*fraction));
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
   if(ImGui::BeginTabBar("panels")) {
    if(ImGui::BeginTabItem("Items")) { ImGui::BeginChild("Item content"); items(); ImGui::EndChild(); ImGui::EndTabItem(); }
    if(ImGui::BeginTabItem("Creatures")) { ImGui::BeginChild("Creature content"); creatures(); ImGui::EndChild(); ImGui::EndTabItem(); }
    if(ImGui::BeginTabItem("Map")) { ImGui::BeginChild("Map content"); minimap(); ImGui::EndChild(); ImGui::EndTabItem(); }
    if(ImGui::BeginTabItem("Commands")) {
     ImGui::BeginChild("Command content");
     ImGui::InputTextWithHint("##commands","Search commands",command_filter,sizeof(command_filter));
     ImGui::BeginDisabled(!c.ready());
     for(const auto &cmd:c.commands) {
      auto label=cmd.value("label",""); if(matches(label,command_filter) && ImGui::Selectable(label.c_str())) execute(cmd.value("id",""));
     }
     ImGui::EndDisabled(); ImGui::EndChild(); ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
   }
   ImGui::EndChild(); ImGui::EndTable();
  }
  if(quit_dialog) { ImGui::OpenPopup("Close game"); quit_dialog=false; }
  if(ImGui::BeginPopupModal("Close game",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
   ImGui::TextWrapped(c.ready()?"Save this character and close Deluxe?":"Return to normal play to save. You can finish the current menu first.");
   ImGui::BeginDisabled(!c.ready());
   if(ImGui::Button("Save and quit")) { c.save(true); ImGui::CloseCurrentPopup(); }
   ImGui::EndDisabled(); ImGui::SameLine();
   if(ImGui::Button("Continue playing")) { ImGui::CloseCurrentPopup(); focus_game(); }
   if(!c.state.contains("player") || !c.connected) if(ImGui::Button("Close")) { c.closed=true; if(c.process) SDL_KillProcess(c.process,true); }
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
   SDL_DestroyProcess(connection.process); connection.process=nullptr;
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
   ImGui_ImplSDL3_ProcessEvent(&e);
   if(e.type==SDL_EVENT_QUIT) {
    if(!connection.state.contains("terminal")) { connection.closed=true; if(connection.process) SDL_KillProcess(connection.process,true); }
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
 SDL_ReleaseWindowFromGPUDevice(gpu,window); SDL_DestroyGPUDevice(gpu); SDL_DestroyWindow(window); SDL_Quit(); return 0;
}
#endif
