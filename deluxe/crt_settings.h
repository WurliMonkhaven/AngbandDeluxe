#pragma once
// CRT preferences and small CPU-side helpers shared by UI, rendering and tests.
#include "imgui.h"
#include <nlohmann/json.hpp>
#include <array>
#include <algorithm>
#include <cmath>
using json = nlohmann::json;

enum CrtPart { Scanlines, Glow, Bloom, Fringe, Edges, Barrel, Hum, Ghost, Dots, Interference, Beam, Focus, Glass, CrtPartCount };
static const char *crt_labels[]={"Scanlines","Phosphor Glow","Bloom","RGB Convergence","Vignetting","Barrel Distortion","Hum Bar","Ghosting","Phosphor Dots","Signal Interference","Beam Width","Edge Defocus","Glass Diffusion"};
static const char *crt_keys[]={"scanlines","glow","bloom","chromatic_aberration","edge_shading","barrel_distortion","hum_bar","ghosting","phosphor_dots","interference","beam_width","edge_defocus","glass_diffusion"};
struct CrtControl {
 bool enabled=true; float value=0;
 bool operator==(const CrtControl &other) const { return enabled==other.enabled && value==other.value; }
};
struct CrtSettings {
 std::array<CrtControl,CrtPartCount> parts;
 int raster_lines=480, mask=1; // 0: delta dots, 1: aperture grille, 2: slot mask
 int tube_preset=0; // -1 for a customized tube
 CrtSettings(int strength=1) {
  // Slider percentages, in CrtPart order. Keep persisted component keys stable.
  static constexpr float presets[4][CrtPartCount]={
   {32.f,22.5f,17.5f,12.5f,50.f,22.222222f,24.f,5.f,5.f,0.f,20.f,8.f,8.f},
   {41.333333f,35.f,27.5f,31.25f,50.f,38.888889f,15.f,25.f,10.f,12.f,35.f,12.f,12.f},
   {57.333333f,52.5f,42.5f,53.125f,65.625f,55.555556f,15.f,45.f,25.f,13.f,50.f,20.f,20.f},
   {73.333333f,80.f,75.f,78.125f,78.125f,72.222222f,15.f,65.f,60.f,15.f,85.f,60.f,60.f}
  };
  const auto &values=presets[std::clamp(strength,0,3)];
  for(int i=0;i<CrtPartCount;++i) parts[i]={true,values[i]};
  if(strength<1) { parts[Hum].enabled=false; parts[Interference].enabled=false; }
 }
 bool operator==(const CrtSettings &other) const { return parts==other.parts && raster_lines==other.raster_lines && mask==other.mask && tube_preset==other.tube_preset; }
 float level(int part) const { return parts[part].enabled?parts[part].value/100.f:0.f; }
 void tube(int preset) {
  *this=CrtSettings(1); tube_preset=preset;
  parts[Hum].enabled=false; parts[Interference].enabled=false;
  if(preset==1) { raster_lines=360; mask=0; parts[Dots].value=35; parts[Barrel].value=45; }
  if(preset==2) { raster_lines=240; mask=2; parts[Beam].value=65; parts[Glow].value=45; parts[Glass].value=30; parts[Focus].value=30; parts[Dots].value=30; }
 }
 json serialize() const {
  json j={{"tube",{{"lines",raster_lines},{"mask",mask},{"preset",tube_preset}}}};
  for(int i=0;i<CrtPartCount;++i) j[crt_keys[i]]={{"enabled",parts[i].enabled},{"value",parts[i].value}};
  return j;
 }
 void load(const json &j) {
  if(j.contains("tube")) {
   const auto &t=j.at("tube"); raster_lines=std::clamp(t.value("lines",480),0,1200);
   mask=std::clamp(t.value("mask",1),0,2); tube_preset=std::clamp(t.value("preset",-1),-1,2);
  } else { raster_lines=0; mask=0; tube_preset=-1; } // Preserve existing dot layout.

  for(int i=0;i<CrtPartCount;++i) if(j.contains(crt_keys[i])) {
   const auto &v=j.at(crt_keys[i]); parts[i].enabled=v.value("enabled",parts[i].enabled);
   const float value=v.value("value",parts[i].value);
   if(std::isfinite(value)) parts[i].value=std::clamp(value,0.f,100.f);
  }
 }
};
// A downward-moving front lights the surface abruptly, fading smoothly behind it.
inline float crt_hum_trail(float distance) {
 if(distance<0 || distance>=.24f) return 0;
 const float t=distance/.24f; return (1-t)*(1-t)*(1-t);
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
// Frame-rate-independent phosphor persistence. Repeated compositing of an
// unchanged image leaves it unchanged; only changed pixels retain an afterimage.
inline float crt_persistence_alpha(float strength,double elapsed) {
 if(strength<=0 || elapsed<0 || elapsed>.3) return 0;
 const double decay=.008+.022*std::clamp(strength,0.f,1.f);
 return float(std::exp(-elapsed/decay));
}
