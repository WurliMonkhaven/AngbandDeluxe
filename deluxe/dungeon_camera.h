#pragma once
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <string>

struct DungeonCameraSettings {
 bool enabled=false, follow=true;
 int fixed_size=0, fixed_width=0; bool custom_size=false;
 void load(const nlohmann::json &j) {
  if(!j.is_object()) return;
  if(j.contains("fixed_size") && j["fixed_size"].is_number_integer()) { const int size=j["fixed_size"]; fixed_size=size==0?0:std::clamp(size,8,64); }
  if(j.contains("fixed_width") && j["fixed_width"].is_number_integer()) { const int width=j["fixed_width"]; fixed_width=width==0?0:std::clamp(width,8,64); }
  if(j.contains("custom_size") && j["custom_size"].is_boolean()) custom_size=j["custom_size"];
  if(j.contains("enabled") && j["enabled"].is_boolean()) enabled=j["enabled"];
  if(j.contains("follow") && j["follow"].is_boolean()) follow=j["follow"];
 }
 nlohmann::json serialize() const { return {{"enabled",enabled},{"follow",follow},{"fixed_size",fixed_size},{"fixed_width",fixed_width},{"custom_size",custom_size}}; }
};

// All positions are in world tiles. Camera movement never sends game input.
struct DungeonCamera {
 float x=0,y=0,zoom=1;
 int width=0,height=0,target_x=-1,target_y=-1;
 std::string level;
 bool initialized=false,paused=false,dragging=false;
 void reset() { *this=DungeonCamera{}; }
 void clamp() {
  x=std::clamp(x,0.f,float(width)); y=std::clamp(y,0.f,float(height));
 }
 void return_to_player(const nlohmann::json &player) {
  x=player.value("x",0)+.5f; y=player.value("y",0)+.5f; paused=false; clamp();
 }
 void update(const nlohmann::json &view,const nlohmann::json &player,bool follow) {
  const auto next=view.value("level_id",nlohmann::json()).dump();
  const int w=view.value("width",0),h=view.value("height",0);
  if(!initialized || level!=next || width!=w || height!=h) {
   level=next; width=w; height=h; initialized=true;
   target_x=target_y=-1; dragging=false; return_to_player(player);
  } else if(follow && !paused) return_to_player(player);
 }
 void pan(float dx,float dy,float cw,float ch) {
  x-=dx/cw; y-=dy/ch; paused=true; clamp();
 }
 void zoom_at(float wheel,float mx,float my,float vw,float vh,float base_w,float base_h,bool follow) {
  const float previous=zoom;
  zoom=std::clamp(zoom*std::pow(1.15f,wheel),.35f,4.f);
  if(!follow) {
   x+=(mx-vw*.5f)/base_w*(1/previous-1/zoom);
   y+=(my-vh*.5f)/base_h*(1/previous-1/zoom);
   clamp();
  }
 }
 // Keyboard targeting must remain visible without tying the camera to the
 // terminal's panel offsets. A stationary cursor does not undo manual panning.
 void reveal_target(const nlohmann::json &state,float columns,float rows) {
  if(!state.contains("targeting")) { target_x=target_y=-1; return; }
  const auto &t=state["targeting"];
  int tx=t.value("x",0),ty=t.value("y",0);
  if(tx==target_x && ty==target_y) return;
  target_x=tx; target_y=ty;
  const float hx=std::max(.5f,columns*.5f-2),hy=std::max(.5f,rows*.5f-2);
  x=std::clamp(x,tx+.5f-hx,tx+.5f+hx);
  y=std::clamp(y,ty+.5f-hy,ty+.5f+hy);
  clamp();
 }
};
