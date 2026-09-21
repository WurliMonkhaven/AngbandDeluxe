// Angband Deluxe desktop client. GPLv2. No engine headers or linked engine state.
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlgpu3.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <cctype>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>
using json = nlohmann::json;
namespace fs = std::filesystem;

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
 std::string received, outgoing, diagnostic, error;
 std::map<std::string,std::string> requests;
 unsigned long next = 0;
 bool connected = false, negotiated = false, busy = false, close_requested = false, closed = false;
 json state = json::object(), prompt = json::object(), pending_prompt = json::object(), commands = json::array(), saves = json::array(), catalog = json::object();
 ~Connection() { if (process) SDL_DestroyProcess(process); }
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
  if (!process) { error = SDL_GetError(); return false; }
  connected = true;
  send("hello",{{"protocols",json::array({{{"major",0},{"minor",1}}})},{"max_frame_bytes",1048576}});
  return true;
 }
 void receive(const json &j) {
  if (j.value("kind","") == "event") {
   auto name = j.value("event","");
   if (name == "state.changed") { state = j.at("data"); busy = false; }
   if (name == "prompt.requested") { prompt = j.at("data"); busy = false; }
   return;
  }
  auto id = j.value("id",""); auto it = requests.find(id);
  if (it == requests.end()) return;
  auto method = it->second; requests.erase(it);
  if (j.contains("error")) {
   error = j["error"].value("message","Request failed"); busy = false;
   if(method == "prompt.reply") { prompt = pending_prompt; pending_prompt = json::object(); }
   return;
  }
  const auto &result = j.at("result");
  if (method == "hello") {
   negotiated = true; send("saves.list"); send("commands.list");
  } else if (method == "saves.list") saves = result;
  else if (method == "commands.list") commands = result;
  else if (method == "catalog.get") catalog = result;
  else if (method == "session.new" || method == "session.load") send("catalog.get");
  else if (method == "session.save") { busy = false; error = "Game saved."; }
  else if (method == "session.close") { closed = true; busy = false; }
  else if (method == "prompt.reply") pending_prompt = json::object();
 }
 void poll() {
  if (!connected) return;
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
    if (end > 1048576) { error = "Backend frame too large"; connected = false; return; }
    try { receive(json::parse(received.substr(0,end))); }
    catch (const std::exception &e) { error = std::string("Invalid backend message: ") + e.what(); connected = false; return; }
    received.erase(0,end+1);
   }
   if (received.size() > 1048576) { error = "Backend frame too large"; connected = false; return; }
  }
  if (!outgoing.empty()) {
   n = SDL_WriteIO(SDL_GetProcessInput(process),outgoing.data(),outgoing.size());
   outgoing.erase(0,n);
  }
  int exit_code;
  if (SDL_WaitProcess(process,false,&exit_code)) {
   connected = false; busy = false;
   if (!closed) error = "Backend stopped (" + std::to_string(exit_code) + "). " + diagnostic;
  }
 }
 bool ready() const { return connected && !busy && prompt.empty() && state.value("readiness","") == "ready"; }
 void key(const json &k) {
  if (!connected || busy || !prompt.empty() || state.empty()) return;
  send("terminal.input",{{"context",state.value("context","")},{"key",k}}); busy = true;
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
   const std::string v = it.value().is_boolean() ? (it.value().get<bool>()?"Yes":"No") :
    (it.value().is_string() ? it.value().get<std::string>() : it.value().dump());
   ImGui::TextWrapped("%s: %s",display_label(it.key()).c_str(),v.c_str());
  }
 }
}
struct UI {
 Connection &c;
 float scale = 1.0f, game_fraction = .72f;
 float split_drag_y = 0.f, split_drag_fraction = .72f;
 bool quit_dialog = false;
 bool grid_focus = false, focus_requested = false, window_active = true;
 bool return_from_prompt = false;
 float display_scale = 1.f;
 ImGuiStyle base_style;
 char item_filter[128]{}, message_filter[128]{}, command_filter[128]{}, save_name[65] = "Adventurer";
 char prompt_text[4096]{};
 std::string last_prompt, selected, settings_path;
 std::vector<json> keys;
 void load_settings() {
  try { std::ifstream in(settings_path); if (!in) return; json j; in >> j;
   scale=std::clamp(j.value("scale",1.f),0.75f,1.5f);
   game_fraction=std::clamp(j.value("game_fraction",.72f),.2f,.9f);
  } catch (...) { c.error = "Settings could not be read; using defaults."; }
 }
 void save_settings() {
  std::ofstream out(settings_path); out << json{{"scale",scale},{"game_fraction",game_fraction}}.dump(2);
 }
 void focus_game() { focus_requested=true; keys.clear(); }
 void execute(const std::string &id,const std::string &item="") { c.command(id,item); focus_game(); }
 bool owns_keyboard() const {
  return grid_focus && window_active && c.prompt.empty() && c.pending_prompt.empty()
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
  ImGui::TextUnformatted("ANGBAND DELUXE");
  ImGui::TextWrapped("Create a character or continue an existing game. Character creation uses Angband's original controls.");
  ImGui::InputText("New save name",save_name,sizeof(save_name));
  ImGui::BeginDisabled(!c.negotiated || c.busy);
  if (ImGui::Button("New character")) { c.send("session.new",{{"save",save_name}}); c.busy=true; focus_game(); }
  ImGui::SeparatorText("Saved characters");
  for (const auto &s:c.saves) {
   std::string id=s.value("id","");
   if (ImGui::Selectable((id+" — "+s.value("description","")).c_str())) { c.send("session.load",{{"save",id}}); c.busy=true; focus_game(); }
  }
  ImGui::EndDisabled();
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
  draw->AddRectFilled(start,ImVec2(start.x+viewport.x,start.y+viewport.y),IM_COL32(12,15,20,255));
  for (size_t y=0;y<rows.size();++y) for(size_t x=0;x<rows[y].size();++x) {
   unsigned glyph=rows[y][x][0].get<unsigned>(); int col=rows[y][x][1].get<int>();
   if (glyph && glyph!=' ') draw->AddText(ImGui::GetFont(),pixels,
    ImVec2(origin.x+float(x)*cw,origin.y+float(y)*ch),color(col),utf8(glyph).c_str());
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
   char b[80]; int v=p.value(cur,0),m=p.value(max,0); SDL_snprintf(b,sizeof(b),"%s %d / %d",name,v,m);
   ImGui::PushStyleColor(ImGuiCol_PlotHistogram,fill);
   ImGui::PushStyleColor(ImGuiCol_FrameBg,ImVec4(fill.x*.3f,fill.y*.3f,fill.z*.3f,1));
   ImGui::ProgressBar(m?float(v)/m:0,ImVec2(-1,0),b);
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
  auto values=c.state.value("items",json::array());
  std::stable_sort(values.begin(),values.end(),[](const json &a,const json &b){return a.value("location","")<b.value("location","");});
  if(ImGui::BeginTable("items",3,ImGuiTableFlags_Resizable|ImGuiTableFlags_RowBg|ImGuiTableFlags_ScrollY,ImVec2(0,ImGui::GetTextLineHeightWithSpacing()*10))) {
   ImGui::TableSetupColumn("Item",ImGuiTableColumnFlags_WidthStretch); ImGui::TableSetupColumn("Location"); ImGui::TableSetupColumn("Qty"); ImGui::TableHeadersRow();
   for(const auto &o:values) {
    if(o.value("location","")=="Floor") {
     auto p=c.state.value("player",json::object());
     if(o.value("x",-1)!=p.value("x",-2)||o.value("y",-1)!=p.value("y",-2)) continue;
    }
    std::string label=o.value("label","");
    if(!matches(label,item_filter)) continue;
    auto id=o.value("id",""); ImGui::PushID(id.c_str());
    ImGui::TableNextRow(); ImGui::TableNextColumn();
    if(ImGui::Selectable(label.c_str(),selected==id,ImGuiSelectableFlags_SpanAllColumns)) selected=id;
    if(ImGui::IsItemHovered()) { ImGui::BeginTooltip(); ImGui::TextUnformatted(label.c_str()); ImGui::Text("Inscription: %s",o.value("inscription","").c_str()); ImGui::EndTooltip(); }
    ImGui::TableNextColumn(); ImGui::TextUnformatted(display_label(o.value("location","")).c_str());
    ImGui::TableNextColumn(); ImGui::Text("%d",o.value("quantity",0)); ImGui::PopID();
   }
   ImGui::EndTable();
  }
  for(const auto &o:values) if(o.value("id","")==selected) {
   ImGui::SeparatorText("Inspection");
   properties(o["player_known"]);
   ImGui::BeginDisabled(!c.ready());
   if(ImGui::Button("Wield")) execute("core.wield",selected); ImGui::SameLine();
   if(ImGui::Button("Use")) execute("core.use",selected); ImGui::SameLine();
   if(ImGui::Button("Drop")) execute("core.drop",selected); ImGui::SameLine();
   if(ImGui::Button("Inscribe")) execute("core.inscribe",selected);
   ImGui::EndDisabled();
  }
 }
 void creatures() {
  for(const auto &m:c.state.value("monsters",json::array())) {
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
     if(type=="quantity") { try { c.answer(std::stoi(prompt_text)); answered=true; } catch(...) { c.error="Enter a number."; } }
     else { c.answer(std::string(prompt_text)); answered=true; }
    }
   }
   if(type!="choice") ImGui::SameLine();
   if(!answered && (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape))) { c.answer(nullptr); answered=true; }
   if(answered) ImGui::CloseCurrentPopup(); ImGui::EndPopup();
  }
 }
 void draw() {
  auto vp=ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(vp->WorkPos); ImGui::SetNextWindowSize(vp->WorkSize);
  ImGui::Begin("Angband Deluxe",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoSavedSettings);
  if(ImGui::Button("Save")) { if(c.ready()) { c.send("session.save"); c.busy=true; } }
  ImGui::SameLine(); if(ImGui::Button("Save & exit")) quit_dialog=true;
  ImGui::SameLine();
  const float settings_width=ImGui::CalcTextSize("Settings").x+2*ImGui::GetStyle().FramePadding.x;
  ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(),ImGui::GetWindowSize().x-ImGui::GetStyle().WindowPadding.x-settings_width));
  if(ImGui::Button("Settings")) ImGui::OpenPopup("Settings popup");
  const auto settings_corner=ImGui::GetItemRectMax();
  ImGui::SetNextWindowPos(ImVec2(settings_corner.x,settings_corner.y+ImGui::GetStyle().ItemSpacing.y),ImGuiCond_Always,ImVec2(1,0));
  if(ImGui::BeginPopup("Settings popup")) {
   ImGui::TextUnformatted("Settings"); ImGui::Separator();
   ImGui::SetNextItemWidth(ImGui::GetFontSize()*6);
   char zoom[16]; SDL_snprintf(zoom,sizeof(zoom),"%.0f%%",scale*100);
   if(ImGui::BeginCombo("UI scale",zoom)) {
    for(float value:{.75f,1.f,1.25f,1.5f}) {
     char label[16]; SDL_snprintf(label,sizeof(label),"%.0f%%",value*100);
     if(ImGui::Selectable(label,scale==value)) { scale=value; save_settings(); }
    }
    ImGui::EndCombo();
   }
   ImGui::EndPopup();
  }
  if(!c.error.empty()) { ImGui::TextWrapped("%s",c.error.c_str()); ImGui::SameLine(); if(ImGui::SmallButton("Dismiss")) c.error.clear(); }
  if(ImGui::BeginTable("layout",2,ImGuiTableFlags_Resizable|ImGuiTableFlags_BordersInnerV)) {
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
   ImGui::SeparatorText("Messages");
   ImGui::InputTextWithHint("##messages","Search messages",message_filter,sizeof(message_filter));
   for(const auto &m:c.state.value("messages",json::array())) {
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
   if(ImGui::Button("Save and close")) { c.send("session.close"); c.busy=true; ImGui::CloseCurrentPopup(); }
   ImGui::EndDisabled(); ImGui::SameLine();
   if(ImGui::Button("Continue playing")) { ImGui::CloseCurrentPopup(); focus_game(); }
   if(!c.state.contains("player") || !c.connected) if(ImGui::Button("Close")) { c.closed=true; if(c.process) SDL_KillProcess(c.process,true); }
   ImGui::EndPopup();
  }
  prompts(); ImGui::End();
  if(owns_keyboard() && !ImGui::GetIO().WantTextInput && !keys.empty()) c.key(keys.front());
  keys.clear();
 }
};

int main(int argc,char **argv) {
 if(!SDL_Init(SDL_INIT_VIDEO|SDL_INIT_GAMEPAD)) return 1;
 const float dpi=SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
 SDL_Rect bounds{}; SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(), &bounds);
 const int width=std::min(int(1280*dpi),std::max(640,bounds.w-80));
 const int height=std::min(int(800*dpi),std::max(480,bounds.h-80));
 SDL_Window *window=SDL_CreateWindow("Angband Deluxe",width,height,SDL_WINDOW_RESIZABLE|SDL_WINDOW_HIGH_PIXEL_DENSITY);
 if(window) SDL_SetWindowPosition(window,SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED);
 SDL_GPUDevice *gpu=SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV|SDL_GPU_SHADERFORMAT_DXIL|SDL_GPU_SHADERFORMAT_METALLIB,false,nullptr);
 if(!window || !gpu || !SDL_ClaimWindowForGPUDevice(gpu,window)) {
  SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,"Angband Deluxe",SDL_GetError(),window); return 1;
 }
 SDL_SetGPUSwapchainParameters(gpu,window,SDL_GPU_SWAPCHAINCOMPOSITION_SDR,SDL_GPU_PRESENTMODE_VSYNC);
 IMGUI_CHECKVERSION(); ImGui::CreateContext();
 auto &io=ImGui::GetIO(); io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard|ImGuiConfigFlags_NavEnableGamepad;
 io.Fonts->AddFontFromFileTTF(DELUXE_FONT_FILE,18.f);
 ImGui::StyleColorsDark(); ImGui::GetStyle().WindowRounding=5;
 ImGui::GetStyle().FontSizeBase=18.f;
 ImGui_ImplSDL3_InitForSDLGPU(window);
 ImGui_ImplSDLGPU3_InitInfo info{}; info.Device=gpu; info.ColorTargetFormat=SDL_GetGPUSwapchainTextureFormat(gpu,window); info.MSAASamples=SDL_GPU_SAMPLECOUNT_1;
 ImGui_ImplSDLGPU3_Init(&info);
 Connection connection; UI ui{connection};
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
 std::string ini=(fs::path(user)/"layout.ini").string(); io.IniFilename=ini.c_str();
 connection.start(backend,data,user);
 SDL_StartTextInput(window);
 while(!connection.closed) {
  connection.poll(); ui.prepare_frame(window); SDL_Event e;
  while(SDL_PollEvent(&e)) {
   if(e.type==SDL_EVENT_WINDOW_FOCUS_LOST) { ui.window_active=false; ui.keys.clear(); }
   if(e.type==SDL_EVENT_WINDOW_FOCUS_GAINED) ui.window_active=true;
   if(e.type==SDL_EVENT_MOUSE_BUTTON_DOWN) { ui.grid_focus=false; ui.keys.clear(); }
   ImGui_ImplSDL3_ProcessEvent(&e);
   if(e.type==SDL_EVENT_QUIT) ui.quit_dialog=true;
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
  ImGui_ImplSDLGPU3_NewFrame(); ImGui_ImplSDL3_NewFrame(); ImGui::NewFrame(); ui.draw(); ImGui::Render();
  auto cmd=SDL_AcquireGPUCommandBuffer(gpu); SDL_GPUTexture *surface=nullptr;
  if(!cmd) break;
  if(!SDL_WaitAndAcquireGPUSwapchainTexture(cmd,window,&surface,nullptr,nullptr)) { SDL_CancelGPUCommandBuffer(cmd); break; }
  if(surface) {
   ImGui_ImplSDLGPU3_PrepareDrawData(ImGui::GetDrawData(),cmd);
   SDL_GPUColorTargetInfo target{}; target.texture=surface; target.load_op=SDL_GPU_LOADOP_CLEAR; target.store_op=SDL_GPU_STOREOP_STORE;
   target.clear_color=SDL_FColor{.04f,.05f,.07f,1.f};
   auto pass=SDL_BeginGPURenderPass(cmd,&target,1,nullptr);
   ImGui_ImplSDLGPU3_RenderDrawData(ImGui::GetDrawData(),cmd,pass); SDL_EndGPURenderPass(pass);
  }
  SDL_SubmitGPUCommandBuffer(cmd);
  if(SDL_GetWindowFlags(window)&SDL_WINDOW_MINIMIZED) SDL_Delay(20);
 }
 SDL_WaitForGPUIdle(gpu); ImGui_ImplSDLGPU3_Shutdown(); ImGui_ImplSDL3_Shutdown(); ImGui::DestroyContext();
 SDL_ReleaseWindowFromGPUDevice(gpu,window); SDL_DestroyGPUDevice(gpu); SDL_DestroyWindow(window); SDL_Quit(); return 0;
}
