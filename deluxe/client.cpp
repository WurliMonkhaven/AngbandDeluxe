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
struct CrtPreset {
 int scan_alpha,edge_alpha,hum_alpha;
 float fringe,fringe_alpha,glow_radius,glow_alpha,bloom_radius,bloom_alpha;
};
static const CrtPreset &crt_preset(int strength) {
 static const CrtPreset presets[]={
  {48,80,12, .20f,.16f,.45f,.09f,1.0f,.035f}, // Subtle
  {62,80,18, .50f,.30f,.65f,.14f,1.5f,.055f}, // Classic
  {86,105,26, .85f,.40f,.85f,.21f,2.0f,.085f}, // Deluxe
  {110,125,38, 1.25f,.52f,1.0f,.32f,2.5f,.15f} // Zero Cool
 };
 return presets[std::clamp(strength,0,3)];
}
enum CrtPart { Scanlines, Glow, Bloom, Fringe, Edges, Barrel, Hum, CrtPartCount };
static const char *crt_labels[]={"Scanlines","Phosphor Glow","Bloom","Chromatic Aberration","Edge Shading","Barrel Distortion","Hum Bar"};
static const char *crt_keys[]={"scanlines","glow","bloom","chromatic_aberration","edge_shading","barrel_distortion","hum_bar"};
struct CrtControl { bool enabled=true; float value=0; };
struct CrtSettings {
 std::array<CrtControl,CrtPartCount> parts;
 CrtSettings(int strength=1) {
  const auto &p=crt_preset(strength);
  const float values[]={p.scan_alpha/1.5f,p.glow_alpha/.004f,p.bloom_alpha/.002f,
   p.fringe/ .016f,p.edge_alpha/1.6f,(.004f+.003f*std::clamp(strength,0,3))/.00018f,p.hum_alpha/.5f};
  for(int i=0;i<CrtPartCount;++i) parts[i]={true,values[i]};
 }
 float level(int part) const { return parts[part].enabled?parts[part].value/100.f:0.f; }
 json serialize() const {
  json j=json::object();
  for(int i=0;i<CrtPartCount;++i) j[crt_keys[i]]={{"enabled",parts[i].enabled},{"value",parts[i].value}};
  return j;
 }
 void load(const json &j) {
  for(int i=0;i<CrtPartCount;++i) if(j.contains(crt_keys[i])) {
   const auto &v=j.at(crt_keys[i]); parts[i].enabled=v.value("enabled",parts[i].enabled);
   const float value=v.value("value",parts[i].value);
   if(std::isfinite(value)) parts[i].value=std::clamp(value,0.f,100.f);
  }
 }
 CrtPreset resolved() const {
  return {int(150*level(Scanlines)),int(160*level(Edges)),int(50*level(Hum)),
   1.6f*level(Fringe),.65f*level(Fringe),1.2f*level(Glow),.4f*level(Glow),
   3.f*level(Bloom),.2f*level(Bloom)};
 }
};
// A downward-moving front lights the surface abruptly, fading smoothly behind it.
static float crt_hum_trail(float distance) {
 if(distance<0 || distance>=.24f) return 0;
 const float t=distance/.24f; return (1-t)*(1-t)*(1-t);
}
// Scanlines, edge shading and the asymmetric rolling wave above the glow.
static void crt_effect(ImDrawList *draw, ImVec2 pos, ImVec2 size,const CrtSettings &settings=CrtSettings{},double seconds=-1) {
 if(size.x<=0 || size.y<=0) return;
 const auto preset=settings.resolved();
 const ImVec2 end(pos.x+size.x,pos.y+size.y);
 draw->PushClipRect(pos,end,true);
 const float pixel=1.f/std::max(1.f,ImGui::GetIO().DisplayFramebufferScale.y);
 for(float y=pos.y;preset.scan_alpha>0 && y<end.y;y+=3.f*pixel)
  draw->AddRectFilled(ImVec2(pos.x,y),ImVec2(end.x,std::min(end.y,y+pixel)),IM_COL32(0,0,0,preset.scan_alpha));
 if(preset.edge_alpha>0) {
 const float edge=std::min(size.x,size.y)*.09f;
 const ImU32 dark=IM_COL32(0,8,5,preset.edge_alpha),clear=IM_COL32(0,8,5,0);
 draw->AddRectFilledMultiColor(pos,ImVec2(pos.x+edge,end.y),dark,clear,clear,dark);
 draw->AddRectFilledMultiColor(ImVec2(end.x-edge,pos.y),end,clear,dark,dark,clear);
 draw->AddRectFilledMultiColor(pos,ImVec2(end.x,pos.y+edge),dark,dark,clear,clear);
 draw->AddRectFilledMultiColor(ImVec2(pos.x,end.y-edge),end,clear,clear,dark,dark);
 }
 if(preset.hum_alpha>0) {
  const float front=float(std::fmod(seconds<0?ImGui::GetTime():seconds,12.0)/12.0);
  // Split at the actual front, not fixed screen rows: its leading edge stays
  // hard at every animation phase. The previous sweep's tail wraps at the top.
  for(float head:{front,front+1.f}) for(int band=0;band<48;++band) {
   const float near_distance=band*.24f/48,far_distance=(band+1)*.24f/48;
   const float top=std::max(0.f,head-far_distance),bottom=std::min(1.f,head-near_distance);
   if(bottom<=top) continue;
   auto light=[&](float y) { return IM_COL32(255,255,255,int(preset.hum_alpha*crt_hum_trail(head-y))); };
   draw->AddRectFilledMultiColor(ImVec2(pos.x,pos.y+size.y*top),ImVec2(end.x,pos.y+size.y*bottom),
    light(top),light(top),light(bottom),light(bottom));
  }
 }
 draw->PopClipRect();
}
// Reuse the font's antialiased glyph coverage for a small phosphor halo and
// red/cyan channel fringes. Keep the original sharp glyph on top. Solid UI
// backgrounds are passed through, and original draw order/clip rectangles
// preserve popup occlusion. No full-screen blur buffers or flicker are needed.
static std::unique_ptr<ImDrawList> crt_phosphor(const ImDrawList &source,const CrtSettings &settings=CrtSettings{}) {
 const auto preset=settings.resolved();
 auto out=std::make_unique<ImDrawList>(ImGui::GetDrawListSharedData());
 out->_ResetForNewFrame(); out->Flags=source.Flags;
 const float pixel=1.f/std::max(1.f,ImGui::GetIO().DisplayFramebufferScale.y);
 for(const auto &cmd:source.CmdBuffer) {
  out->PushClipRect(ImVec2(cmd.ClipRect.x,cmd.ClipRect.y),ImVec2(cmd.ClipRect.z,cmd.ClipRect.w));
  out->PushTexture(cmd.TexRef);
  if(cmd.UserCallback) out->AddCallback(cmd.UserCallback,cmd.UserCallbackData);
  else for(unsigned i=0;i+2<cmd.ElemCount;) {
   // Keep both triangles of a glyph together so all glow is beneath its sharp
   // original. Per-triangle layering allowed the second halo to blur the first.
   unsigned count=3;
   const auto *indices=source.IdxBuffer.Data+cmd.IdxOffset+i;
   if(i+5<cmd.ElemCount && indices[0]==indices[3] && indices[2]==indices[4]) count=6;
   ImDrawVert v[6];
   for(unsigned k=0;k<count;++k) v[k]=source.VtxBuffer[cmd.VtxOffset+indices[k]];
   bool font=false;
   for(const auto *texture:ImGui::GetIO().Fonts->TexList) if(cmd.TexRef._TexData==texture) font=true;
   const bool textured=v[0].uv.x!=v[1].uv.x || v[0].uv.y!=v[1].uv.y || v[0].uv.x!=v[2].uv.x || v[0].uv.y!=v[2].uv.y;
   auto emit=[&](float dx,float dy,float opacity,ImU32 channels) {
    out->PrimReserve(count,count);
    for(unsigned k=0;k<count;++k) {
     const auto &vertex=v[k];
     const unsigned alpha=(vertex.col>>IM_COL32_A_SHIFT)&255;
     const ImU32 tint=(vertex.col&channels&~IM_COL32_A_MASK) | (ImU32(alpha*opacity)<<IM_COL32_A_SHIFT);
     out->PrimVtx(ImVec2(vertex.pos.x+dx*pixel,vertex.pos.y+dy*pixel),vertex.uv,tint);
    }
   };
   if(font && textured) {
    // Closely spaced horizontal taps simulate phosphor bleed without creating
    // vertically displaced letter copies. Cap spread relative to small glyphs.
    float left=v[0].pos.x,right=left;
    for(unsigned k=1;k<count;++k) { left=std::min(left,v[k].pos.x); right=std::max(right,v[k].pos.x); }
    const float limit=std::max(.25f,(right-left)/pixel*.22f);
    const float radius=std::min(preset.glow_radius,limit),bloom=std::min(preset.bloom_radius,limit);
    if(preset.glow_alpha>0) { emit(-radius,0,preset.glow_alpha,IM_COL32_WHITE); emit(radius,0,preset.glow_alpha,IM_COL32_WHITE); }
    for(int tap=1;preset.bloom_alpha>0 && tap<=3;++tap) {
     const float distance=bloom*tap/3.f;
     const float opacity=preset.bloom_alpha*std::exp(-float(tap*tap)/4.f);
     emit(-distance,0,opacity,IM_COL32_WHITE); emit(distance,0,opacity,IM_COL32_WHITE);
    }
    const float fringe=std::min(preset.fringe,limit);
    if(preset.fringe_alpha>0) { emit(-fringe,0,preset.fringe_alpha,IM_COL32(255,0,0,255));
     emit(fringe,0,preset.fringe_alpha,IM_COL32(0,255,255,255)); }
   }
   emit(0,0,1.f,IM_COL32_WHITE);
   i+=count;
  }
  out->PopTexture(); out->PopClipRect();
 }
 return out;
}
struct CrtCurve {
 ImVec2 pos,size;
 float amount;
 CrtCurve(ImVec2 p,ImVec2 s,const CrtSettings &settings):pos(p),size(s),amount(.018f*settings.level(Barrel)) {}
 ImVec2 map(ImVec2 p,bool inverse=false) const {
  if(amount==0 || size.x<=0 || size.y<=0) return p;
  const float x=2*(p.x-pos.x)/size.x-1,y=2*(p.y-pos.y)/size.y-1;
  float u=x,v=y;
  if(inverse) {
   for(int i=0;i<6;++i) { u=x/(1-amount*v*v); v=y/(1-amount*u*u); }
  } else { u=x*(1-amount*y*y); v=y*(1-amount*x*x); }
  return ImVec2(pos.x+(u+1)*size.x*.5f,pos.y+(v+1)*size.y*.5f);
 }
};
static ImDrawVert crt_lerp(const ImDrawVert &a,const ImDrawVert &b,float t) {
 ImDrawVert v;
 v.pos=ImVec2(a.pos.x+(b.pos.x-a.pos.x)*t,a.pos.y+(b.pos.y-a.pos.y)*t);
 v.uv=ImVec2(a.uv.x+(b.uv.x-a.uv.x)*t,a.uv.y+(b.uv.y-a.uv.y)*t);
 v.col=0;
 for(int shift=0;shift<32;shift+=8) {
  const float c=float((a.col>>shift)&255),d=float((b.col>>shift)&255);
  v.col|=ImU32(std::clamp(c+(d-c)*t,0.f,255.f))<<shift;
 }
 return v;
}
// Clip before curving: rectangular GPU scissors alone cannot describe a bowed
// scroll-pane edge. Subdivide long triangles to keep scanlines smoothly curved.
static std::unique_ptr<ImDrawList> crt_curve(const ImDrawList &source,const CrtCurve &curve) {
 auto out=std::make_unique<ImDrawList>(ImGui::GetDrawListSharedData());
 out->_ResetForNewFrame(); out->Flags=source.Flags;
 auto triangle=[&](auto &&self,const ImDrawVert &a,const ImDrawVert &b,const ImDrawVert &c,int depth)->void {
  const ImDrawVert v[]={a,b,c};
  const ImVec2 p[]={curve.map(a.pos),curve.map(b.pos),curve.map(c.pos)};
  int split=-1; float worst=.2f*.2f;
  const float width=std::max({a.pos.x,b.pos.x,c.pos.x})-std::min({a.pos.x,b.pos.x,c.pos.x});
  const float height=std::max({a.pos.y,b.pos.y,c.pos.y})-std::min({a.pos.y,b.pos.y,c.pos.y});
  const float extent=std::max(width/curve.size.x,height/curve.size.y);
  // Small glyph triangles are already far below the subdivision tolerance.
  const bool small=12*curve.amount*std::max(curve.size.x,curve.size.y)*extent*extent<.2f;
  for(int i=0;!small && i<3;++i) {
   const int j=(i+1)%3;
   const auto mid=curve.map(ImVec2((v[i].pos.x+v[j].pos.x)*.5f,(v[i].pos.y+v[j].pos.y)*.5f));
   const float dx=mid.x-(p[i].x+p[j].x)*.5f,dy=mid.y-(p[i].y+p[j].y)*.5f;
   if(dx*dx+dy*dy>worst) { worst=dx*dx+dy*dy; split=i; }
  }
  if(split>=0 && depth<9) {
   const int j=(split+1)%3,k=(split+2)%3;
   const auto mid=crt_lerp(v[split],v[j],.5f);
   self(self,v[split],mid,v[k],depth+1); self(self,mid,v[j],v[k],depth+1);
  } else {
   out->PrimReserve(3,3);
   for(int i=0;i<3;++i) out->PrimVtx(p[i],v[i].uv,v[i].col);
  }
 };
 for(const auto &cmd:source.CmdBuffer) {
  out->PushClipRect(curve.pos,ImVec2(curve.pos.x+curve.size.x,curve.pos.y+curve.size.y));
  out->PushTexture(cmd.TexRef);
  if(cmd.UserCallback) out->AddCallback(cmd.UserCallback,cmd.UserCallbackData);
  else {
   const float bounds[]={std::max(cmd.ClipRect.x,curve.pos.x),std::min(cmd.ClipRect.z,curve.pos.x+curve.size.x),
    std::max(cmd.ClipRect.y,curve.pos.y),std::min(cmd.ClipRect.w,curve.pos.y+curve.size.y)};
   if(bounds[0]<bounds[1] && bounds[2]<bounds[3]) for(unsigned i=0;i+2<cmd.ElemCount;i+=3) {
    ImDrawVert polygon[12],clipped[12]; int count=3;
    for(int k=0;k<3;++k) polygon[k]=source.VtxBuffer[cmd.VtxOffset+source.IdxBuffer[cmd.IdxOffset+i+k]];
    bool inside=true;
    for(int k=0;k<3;++k) inside=inside && polygon[k].pos.x>=bounds[0] && polygon[k].pos.x<=bounds[1]
     && polygon[k].pos.y>=bounds[2] && polygon[k].pos.y<=bounds[3];
    if(inside) { triangle(triangle,polygon[0],polygon[1],polygon[2],0); continue; }
    for(int edge=0;edge<4 && count; ++edge) {
     auto distance=[&](const ImDrawVert &v) {
      const float coordinate=edge<2?v.pos.x:v.pos.y;
      return edge%2?bounds[edge]-coordinate:coordinate-bounds[edge];
     };
     int n=0;
     for(int k=0;k<count;++k) {
      const auto &a=polygon[k],&b=polygon[(k+1)%count];
      const float da=distance(a),db=distance(b);
      if(da>=0) clipped[n++]=a;
      if((da>=0)!=(db>=0)) clipped[n++]=crt_lerp(a,b,da/(da-db));
     }
     count=n; std::copy(clipped,clipped+n,polygon);
    }
    for(int k=1;k+1<count;++k) triangle(triangle,polygon[0],polygon[k],polygon[k+1],0);
   }
  }
  out->PopTexture(); out->PopClipRect();
 }
 return out;
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
    if (end > 1048576) { notice("Backend frame too large"); connected = false; return; }
    try { receive(json::parse(received.substr(0,end))); }
    catch (const std::exception &e) { notice(std::string("Invalid backend message: ") + e.what()); connected = false; return; }
    received.erase(0,end+1);
   }
   if (received.size() > 1048576) { notice("Backend frame too large"); connected = false; return; }
  }
  flush_input();
  int exit_code;
  if (SDL_WaitProcess(process,false,&exit_code)) {
   connected = false; busy = false;
   if(close_confirmed && return_to_menu && exit_code==0) restart_ready=true;
   else if (!closed) { notice("Backend stopped (" + std::to_string(exit_code) + "). " + diagnostic); if(!state.contains("terminal")) menu_error="Backend stopped. Restart Deluxe to try again."; }
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
   crt=std::clamp(j.value("crt",0),0,2);
   crt_strength=std::clamp(j.value("crt_strength",1),-1,3);
   crt_settings=CrtSettings(crt_strength);
   crt_settings.parts[Hum].enabled=j.value("hum_bar",false);
   if(j.contains("crt_components")) crt_settings.load(j.at("crt_components"));
  } catch (...) { c.notice("Settings could not be read; using defaults."); }
 }
 bool write_settings(float zoom,bool full,int effect,int strength,const CrtSettings &settings) {
  const std::string temporary=settings_path+".tmp";
  std::ofstream out(temporary);
  out << json{{"scale",zoom},{"game_fraction",game_fraction},{"fullscreen",full},{"crt",effect},{"crt_strength",strength},{"crt_components",settings.serialize()}}.dump(2);
  out.close();
  return bool(out) && SDL_RenamePath(temporary.c_str(),settings_path.c_str());
 }
 void save_settings() {
  if(!write_settings(scale,fullscreen,crt,crt_strength,crt_settings)) c.notice("Settings could not be saved.");
 }
 void begin_settings() {
  draft_scale=scale; draft_fullscreen=fullscreen; draft_crt=crt; settings_error.clear();
  draft_crt_strength=crt_strength; draft_crt_settings=crt_settings;
 }
 bool apply_settings(SDL_Window *window) {
  if(draft_fullscreen!=fullscreen && !SDL_SetWindowFullscreen(window,draft_fullscreen)) {
   settings_error=SDL_GetError(); return false;
  }
  if(!write_settings(draft_scale,draft_fullscreen,draft_crt,draft_crt_strength,draft_crt_settings)) {
   if(draft_fullscreen!=fullscreen) SDL_SetWindowFullscreen(window,fullscreen);
   settings_error="Settings could not be saved. Please try again."; return false;
  }
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
    if(ImGui::BeginTabItem("CRT effects")) {
     ImGui::Spacing(); ImGui::TextUnformatted("Effects Enabled"); ImGui::SetNextItemWidth(-1);
     const char *effects[]={"Off","Game Window Only","Full"};
     ImGui::Combo("##CRT Effects",&draft_crt,effects,3);
     ImGui::Spacing(); ImGui::TextUnformatted("Effect Strength"); ImGui::SetNextItemWidth(-1);
     const char *strengths[]={"Subtle","Classic","Deluxe","Zero Cool"};
     if(ImGui::BeginCombo("##CRT Effects Strength",draft_crt_strength<0?"Custom":strengths[draft_crt_strength])) {
      for(int i=0;i<4;++i) if(ImGui::Selectable(strengths[i],draft_crt_strength==i)) {
       draft_crt_strength=i; draft_crt_settings=CrtSettings(i);
      }
      ImGui::EndCombo();
     }
     ImGui::TextWrapped("Presets reset all effect sliders and switches.");
     ImGui::Spacing(); ImGui::Separator();
     for(int i=0;i<CrtPartCount;++i) {
      auto &control=draft_crt_settings.parts[i];
      ImGui::PushID(i); ImGui::Spacing();
      if(ImGui::Checkbox(crt_labels[i],&control.enabled)) draft_crt_strength=-1;
      ImGui::BeginDisabled(!control.enabled); ImGui::SetNextItemWidth(-1);
      if(ImGui::SliderFloat("##Amount",&control.value,0.f,100.f,"%.0f%%",ImGuiSliderFlags_AlwaysClamp)) draft_crt_strength=-1;
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
  if(crt==1) crt_effect(draw,start,viewport,crt_settings);
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
  ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(),ImGui::GetWindowSize().x-ImGui::GetStyle().WindowPadding.x-settings_width));
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
  if(owns_keyboard() && !ImGui::GetIO().WantTextInput && !keys.empty()) c.key(keys.front());
  keys.clear();
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
 if(ui.fullscreen && !SDL_SetWindowFullscreen(window,true)) { ui.fullscreen=false; connection.notice(std::string("Fullscreen unavailable: ")+SDL_GetError()); }
 std::string ini=(fs::path(user)/"layout.ini").string(); io.IniFilename=ini.c_str();
 connection.start(backend,data,user);
 SDL_StartTextInput(window);
 while(!connection.closed) {
  connection.poll();
  if(connection.restart_ready) {
   // session.close has acknowledged a successful save and the child has exited.
   SDL_DestroyProcess(connection.process); connection.process=nullptr;
   connection=Connection{};
   ui.grid_focus=ui.focus_requested=ui.return_from_prompt=false;
   ui.selected.clear(); ui.last_prompt.clear(); ui.keys.clear();
   ui.item_filter[0]=ui.command_filter[0]=ui.message_filter[0]=0;
   ui.message_search_open=false;
   connection.start(backend,data,user);
  }
  ui.prepare_frame(window); SDL_Event e;
  while(SDL_PollEvent(&e)) {
   if(e.type==SDL_EVENT_WINDOW_FOCUS_LOST) { ui.window_active=false; ui.keys.clear(); }
   if(e.type==SDL_EVENT_WINDOW_FOCUS_GAINED) ui.window_active=true;
   if(e.type==SDL_EVENT_MOUSE_BUTTON_DOWN) { ui.grid_focus=false; ui.keys.clear(); }
   if(e.type==SDL_EVENT_MOUSE_MOTION && ui.crt!=0) {
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
  ImGui_ImplSDLGPU3_NewFrame(); ImGui_ImplSDL3_NewFrame();
  if(ui.crt!=0 && SDL_GetMouseFocus()==window) {
   float x,y; SDL_GetMouseState(&x,&y);
   auto vp=ImGui::GetMainViewport();
   CrtCurve curve(ui.crt==2?vp->Pos:ui.game_pos,ui.crt==2?vp->Size:ui.game_size,ui.crt_settings);
   if(x>=curve.pos.x && x<=curve.pos.x+curve.size.x && y>=curve.pos.y && y<=curve.pos.y+curve.size.y) {
    auto p=curve.map(ImVec2(x,y),true); ImGui::GetIO().AddMousePosEvent(p.x,p.y);
   }
  }
  ImGui::NewFrame(); ui.draw(window);
  if(ui.crt==2) {
   auto vp=ImGui::GetMainViewport(); crt_effect(ImGui::GetForegroundDrawList(),vp->Pos,vp->Size,ui.crt_settings);
  }
  ImGui::Render();
  auto *render_data=ImGui::GetDrawData();
  ImDrawData crt_data;
  std::vector<std::unique_ptr<ImDrawList>> phosphor_lists;
  if(ui.crt!=0) {
   crt_data=*render_data; crt_data.CmdLists.resize(0);
   crt_data.CmdListsCount=crt_data.TotalIdxCount=crt_data.TotalVtxCount=0;
   for(auto *list:render_data->CmdLists) {
    if(ui.crt==2 || list==ui.game_draw_list) {
     auto glow=crt_phosphor(*list,ui.crt_settings);
     auto vp=ImGui::GetMainViewport();
     CrtCurve curve(ui.crt==2?vp->Pos:ui.game_pos,ui.crt==2?vp->Size:ui.game_size,ui.crt_settings);
     phosphor_lists.push_back(curve.amount>0?crt_curve(*glow,curve):std::move(glow)); list=phosphor_lists.back().get();
    }
    crt_data.AddDrawList(list);
   }
   render_data=&crt_data;
  }
  connection.flush_input(); // Dispatch this frame's input before waiting for presentation.
  // ImGui may stop text input when one of its textboxes loses focus. The
  // game also needs SDL's layout-aware text events (including shifted keys).
  if(ui.owns_keyboard() && !SDL_TextInputActive(window)) SDL_StartTextInput(window);
  auto cmd=SDL_AcquireGPUCommandBuffer(gpu); SDL_GPUTexture *surface=nullptr;
  if(!cmd) break;
  if(!SDL_WaitAndAcquireGPUSwapchainTexture(cmd,window,&surface,nullptr,nullptr)) { SDL_CancelGPUCommandBuffer(cmd); break; }
  if(surface) {
   ImGui_ImplSDLGPU3_PrepareDrawData(render_data,cmd);
   SDL_GPUColorTargetInfo target{}; target.texture=surface; target.load_op=SDL_GPU_LOADOP_CLEAR; target.store_op=SDL_GPU_STOREOP_STORE;
   target.clear_color=ui.crt==2?SDL_FColor{0,0,0,1.f}:SDL_FColor{.04f,.05f,.07f,1.f};
   auto pass=SDL_BeginGPURenderPass(cmd,&target,1,nullptr);
   ImGui_ImplSDLGPU3_RenderDrawData(render_data,cmd,pass); SDL_EndGPURenderPass(pass);
  }
  SDL_SubmitGPUCommandBuffer(cmd);
  if(SDL_GetWindowFlags(window)&SDL_WINDOW_MINIMIZED) SDL_Delay(20);
 }
 SDL_WaitForGPUIdle(gpu); ImGui_ImplSDLGPU3_Shutdown(); ImGui_ImplSDL3_Shutdown(); ImGui::DestroyContext();
 SDL_ReleaseWindowFromGPUDevice(gpu,window); SDL_DestroyGPUDevice(gpu); SDL_DestroyWindow(window); SDL_Quit(); return 0;
}
#endif
