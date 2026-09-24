#include <set>

// Read-only server footprint. Never infer terrain or intercept input here.
struct BlastPreview {
 static bool matches(const json &preview,const json &state,int x,int y) {
  return state.value("blast_radius",0)>0 && preview.value("context","")==state.value("context","") &&
   preview.value("x",-1)==x && preview.value("y",-1)==y && preview.contains("tiles");
 }
 static void draw(ImDrawList *draw,const json &preview,ImVec2 origin,ImVec2 size,
                  float cw,float ch,int ox,int oy,float scale) {
  std::set<std::pair<int,int>> cells;
  for(const auto &p:preview.at("tiles")) cells.emplace(p[0].get<int>(),p[1].get<int>());
  draw->PushClipRect(origin,{origin.x+size.x,origin.y+size.y},true);
  const auto edge=IM_COL32(245,190,80,190);
  for(const auto &[x,y]:cells) {
   const float left=origin.x+(x-ox)*cw,top=origin.y+(y-oy)*ch;
   if(left+cw<origin.x || top+ch<origin.y || left>=origin.x+size.x || top>=origin.y+size.y) continue;
   draw->AddRectFilled({left,top},{left+cw,top+ch},IM_COL32(245,185,70,24));
   const float weight=std::max(1.f,scale);
   if(!cells.count({x-1,y})) draw->AddLine({left,top},{left,top+ch},edge,weight);
   if(!cells.count({x+1,y})) draw->AddLine({left+cw,top},{left+cw,top+ch},edge,weight);
   if(!cells.count({x,y-1})) draw->AddLine({left,top},{left+cw,top},edge,weight);
   if(!cells.count({x,y+1})) draw->AddLine({left,top+ch},{left+cw,top+ch},edge,weight);
  }
  draw->PopClipRect();
 }
};
