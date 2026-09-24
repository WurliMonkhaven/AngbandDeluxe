// Ambient, local-only sleep markers. No events, requests or input capture.
struct SleepFeedback {
 static bool eligible(const json &monster,const json &view) {
  if(!monster.value("visible",false) || !monster.value("asleep",false)) return false;
  const int x=monster.value("x",-1)-view.value("x",0),y=monster.value("y",-1)-view.value("y",0);
  if(x<0 || y<0 || x>=view.value("width",0) || y>=view.value("height",0)) return false;
  const auto &cell=view["cells"][y][x];
  return cell[6].get<int>()!=0 && !cell[11].get<int>() && !cell[12].get<int>();
 }
 static void draw(ImDrawList *draw,const json &state,ImVec2 origin,ImVec2 size,float cw,float ch,double now) {
  if(!state.contains("dungeon") || !state.contains("monsters")) return;
  const auto &view=state["dungeon"];
  draw->PushClipRect(origin,ImVec2(origin.x+size.x,origin.y+size.y),true);
  for(const auto &m:state["monsters"]) {
   if(!eligible(m,view)) continue;
   const int x=m.value("x",0),y=m.value("y",0);
   const ImVec2 center(origin.x+(x-view.value("x",0)+.5f)*cw,origin.y+(y-view.value("y",0)+.5f)*ch);
   // Tile-derived phase keeps sleepers from breathing in lockstep and stays
   // stable when state snapshots replace the engine's revision-based IDs.
   for(int i=0;i<2;++i) {
    const float t=float(std::fmod(now/1.8+x*.137+y*.211+i*.5,1.));
    const float alpha=.85f*std::min(1.f,t*7.f)*std::min(1.f,(1-t)*4.f);
    const float font=std::max(10.f,ImGui::GetFontSize()*(.65f+.2f*t));
    const ImVec2 at(center.x+cw*(.25f+.35f*t),center.y-ch*(.35f+.9f*t)-font*.5f);
    draw->AddText(ImGui::GetFont(),font,ImVec2(at.x+1,at.y+1),IM_COL32(0,0,0,int(210*alpha)),"z");
    draw->AddText(ImGui::GetFont(),font,at,IM_COL32(160,205,245,int(255*alpha)),"z");
   }
  }
  draw->PopClipRect();
 }
};
