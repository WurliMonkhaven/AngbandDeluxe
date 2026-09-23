#pragma once
#include <nlohmann/json.hpp>
#include <vector>
#include <algorithm>

// A separate semantic presentation stream, not a crop or interpretation of
// terminal text. Unavailable/unsupported views deliberately use terminal fallback.
inline const nlohmann::json *dungeon_view(const nlohmann::json &state) {
 if(state.value("phase","")!="playing" || (state.value("readiness","")!="ready" && !state.value("message_pending",false) && !state.contains("targeting") && !state.value("aiming",false) && !state.value("direction_prompt",false) && !state.value("item_selection",false) && !state.value("spell_selection",false))) return nullptr;
 auto it=state.find("dungeon");
 if(it==state.end() || !it->is_object() || !it->contains("cells")) return nullptr;
 const auto &cells=it->at("cells");
 const int width=it->value("width",0),height=it->value("height",0);
 if(width<1 || height<1 || width>512 || height>512 || !cells.is_array() || int(cells.size())!=height) return nullptr;
 for(const auto &row:cells) {
  if(!row.is_array() || int(row.size())!=width) return nullptr;
  for(const auto &cell:row) if(!cell.is_array() || cell.size()!=13) return nullptr;
 }
 return &*it;
}

// Decode once on receipt, not on every animation frame. The wire representation
// keeps all layers; opaque text rendering only needs the topmost present glyph.
struct RenderGrid {
 struct Cell { unsigned glyph=0; int color=0; };
 bool semantic=false;
 size_t width=0, height=0;
 std::vector<Cell> cells;
 void update(const nlohmann::json &state) {
  const auto *view=dungeon_view(state);
  semantic=view!=nullptr; width=height=0; cells.clear();
  if(!semantic && !state.contains("terminal")) return;
  const auto &rows=semantic?view->at("cells"):state.at("terminal");
  height=rows.size();
  for(const auto &row:rows) width=std::max(width,row.size());
  cells.resize(width*height);
  for(size_t y=0;y<height;++y) for(size_t x=0;x<rows[y].size();++x) {
   const auto &source=rows[y][x];
   auto &cell=cells[y*width+x];
   for(int layer=0;layer<(semantic?4:1);++layer) {
    const auto glyph=source.at(layer*2).get<unsigned>();
    if(glyph) cell={glyph,source.at(layer*2+1).get<int>()};
   }
  }
 }
};

inline bool grid_cell_at(float x,float y,float cw,float ch,size_t width,size_t height,int &column,int &row) {
 if(cw<=0 || ch<=0 || x<0 || y<0 || x>=cw*width || y>=ch*height) return false;
 column=int(x/cw); row=int(y/ch);
 return true;
}
