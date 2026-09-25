#pragma once
#include <SDL3/SDL.h>
#include "imgui.h"
#include <filesystem>
#include <string>
#include <array>

// IDs match Angband's bundled lib/tiles/list.txt. PNGs and mappings remain
// unmodified; the backend alone decides which artwork the player may see.
struct TilesetLibrary {
 struct Set { const char *name,*file; int cell,overdraw_first=0,overdraw_last=0; };
 inline static const std::array<Set,7> sets={{
  {"ASCII","",0}, {"Original tiles","old/8x8.png",8},
  {"Adam Bolt","adam-bolt/16x16.png",16}, {"David Gervais","gervais/32x32.png",32},
  {"Nomad","nomad/8x16.png",16}, {"Shockbolt - Dark","shockbolt/64x64.png",64,27,31},
  {"Shockbolt - Light","shockbolt/64x64.png",64,27,31}
 }};
 struct Atlas { SDL_GPUTexture *texture=nullptr; int width=0,height=0; bool tried=false; std::string error; };
 SDL_GPUDevice *device=nullptr;
 std::filesystem::path directory;
 std::array<Atlas,7> atlases{};
 bool load(int id);
 void shutdown();
 void preview(int id);
 bool tile(ImDrawList *draw,int id,int glyph,int attr,ImVec2 p,ImVec2 size,ImU32 tint=IM_COL32_WHITE) const {
  if(id<1 || id>6 || !(attr&128) || !(glyph&128)) return false;
  const auto &a=atlases[id]; const auto &s=sets[id];
  const int col=glyph&127,row=attr&127;
  if(!a.texture || (col+1)*s.cell>a.width || (row+1)*s.cell>a.height) return false;
  const bool tall=row>=s.overdraw_first && row<=s.overdraw_last && s.overdraw_first>0;
  const ImVec2 uv0((col*s.cell+.5f)/a.width,((row-int(tall))*s.cell+.5f)/a.height);
  const ImVec2 uv1(((col+1)*s.cell-.5f)/a.width,((row+1)*s.cell-.5f)/a.height);
  draw->AddImage((ImTextureID)(intptr_t)a.texture,{p.x,p.y-(tall?size.y:0)},
   {p.x+size.x,p.y+size.y},uv0,uv1,tint);
  return true;
 }
};
