#pragma once
#include "imgui.h"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <array>
#include <string>
#include <algorithm>

struct FontChoice { const char *file,*name; };
inline constexpr std::array<FontChoice,15> font_choices={{
 {"Cousine-Regular.ttf","Cousine Regular"}, {"ConsolaMono-Book.ttf","Consola Mono"},
 {"Erika Type.ttf","Erica Type"}, {"F25_Bank_Printer.ttf","F25 Bank Printer"},
 {"Flexi_IBM_VGA_True.ttf","Flexi IBM VGA True"}, {"Hack-Regular.ttf","Hack"},
 {"LiberationMono-Regular.ttf","Liberation Mono"}, {"MonospaceTypewriter.ttf","Monospace Typewriter"},
 {"Nouveau_IBM.ttf","Nouveau IBM"}, {"RetraConsole.ttf","RetraConsole"},
 {"Sono-Regular.ttf","Sono"}, {"SVBasicManual.ttf","SV Basic Manual"},
 {"Terminal F4.ttf","Terminal F4"}, {"Xanmono-Regular.ttf","Xanmono"}, {"Zector.ttf","Zector"}
}};
struct FontSettings {
 std::string interface_font="Nouveau_IBM.ttf", dungeon_font;
 static int index(const std::string &id) {
  for(int i=0;i<int(font_choices.size());++i) if(id==font_choices[i].file) return i;
  return 0;
 }
 void load(const nlohmann::json &j) {
  if(!j.is_object()) return;
  if(j.contains("interface") && j["interface"].is_string()) interface_font=font_choices[index(j["interface"].get<std::string>())].file;
  if(j.contains("dungeon") && j["dungeon"].is_string()) {
   const auto id=j["dungeon"].get<std::string>(); dungeon_font=id.empty()?"":font_choices[index(id)].file;
  }
 }
 std::string dungeon() const { return dungeon_font.empty()?interface_font:dungeon_font; }
 nlohmann::json serialize() const { return {{"interface",interface_font},{"dungeon",dungeon_font}}; }
};

// Fonts live for the lifetime of the atlas. Switching never clears GPU textures
// or rebuilds the atlas while draw commands still reference it.
struct FontLibrary {
 std::array<ImFont*,15> fonts{};
 std::array<float,15> widths{};
 void load(const std::filesystem::path &folder) {
  auto *atlas=ImGui::GetIO().Fonts;
  const auto fallback=folder/font_choices[0].file;
  for(size_t i=0;i<fonts.size();++i) {
   const auto file=folder/font_choices[i].file;
   if(!std::filesystem::is_regular_file(file)) continue;
   fonts[i]=atlas->AddFontFromFileTTF(file.string().c_str(),18);
   if(fonts[i] && i && std::filesystem::is_regular_file(fallback)) {
    ImFontConfig merge; merge.MergeMode=true;
    atlas->AddFontFromFileTTF(fallback.string().c_str(),18,&merge);
   }
  }
 }
 ImFont *get(const std::string &id) const {
  auto *font=fonts[FontSettings::index(id)];
  return font?font:fonts[0]?fonts[0]:ImGui::GetFont();
 }
 float cell_ratio(const std::string &id) {
  const int i=FontSettings::index(id);
  if(widths[i]==0) {
   // A fixed grid even for decorative faces whose punctuation has a different
   // advance. Measure once at a stable reference size, not on every tile.
   float width=0;
   for(char c=33;c<127;++c) { const char text[]={c,0}; width=std::max(width,get(id)->CalcTextSizeA(18,1000,0,text).x); }
   widths[i]=std::max(.1f,width/18);
  }
  return widths[i];
 }
 void picker(const char *label,std::string &id,bool inherit=false) const {
  ImGui::TextUnformatted(label); ImGui::SetNextItemWidth(-1);
  if(ImGui::BeginCombo((std::string("##")+label).c_str(),inherit && id.empty()?"Same as interface":font_choices[FontSettings::index(id)].name)) {
   if(inherit && ImGui::Selectable("Same as interface",id.empty())) id.clear();
   for(size_t i=0;i<fonts.size();++i) if(fonts[i]) {
    ImGui::PushID(int(i)); ImGui::PushFont(fonts[i],0);
    if(ImGui::Selectable(font_choices[i].name,id==font_choices[i].file)) id=font_choices[i].file;
    ImGui::PopFont(); ImGui::PopID();
   }
   ImGui::EndCombo();
  }
 }
 void preview(const FontSettings &settings) {
  ImGui::PushFont(get(settings.interface_font),0);
  ImGui::TextUnformatted("Adventurer - Human Warrior");
  ImGui::TextColored(ImVec4(.5f,.9f,.65f,1),"a Potion of Cure Light Wounds");
  ImGui::ProgressBar(.72f,ImVec2(-1,0),"HP 18 / 25");
  ImGui::TextWrapped("You hear footsteps in the darkness. 0O 1lI  [] {} !?");
  ImGui::PopFont();
  ImGui::Separator();
  auto *font=get(settings.dungeon());
  const float ratio=cell_ratio(settings.dungeon());
  const float pixels=std::min(ImGui::GetFontSize(),ImGui::GetContentRegionAvail().x/(24*ratio));
  const float cw=pixels*ratio,ch=pixels*1.12f;
  const char *rows[]={"########################","#....!....#.....o......>#","#..@......'............#","#.........#....$.......#","########################"};
  auto origin=ImGui::GetCursorScreenPos(); auto *draw=ImGui::GetWindowDrawList();
  for(int y=0;y<5;++y) for(int x=0;rows[y][x];++x) {
   const char glyph[]={rows[y][x],0}; const float w=font->CalcTextSizeA(pixels,1000,0,glyph).x;
   const auto ink=glyph[0]=='@'?IM_COL32(255,240,170,255):glyph[0]=='o'?IM_COL32(240,100,100,255):IM_COL32(130,205,175,255);
   draw->AddText(font,pixels,{origin.x+x*cw+(cw-w)*.5f,origin.y+y*ch},ink,glyph);
  }
  ImGui::Dummy({24*cw,5*ch});
 }
};
