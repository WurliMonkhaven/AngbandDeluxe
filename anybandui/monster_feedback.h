// Ambient, local-only monster-status markers. No events, requests or input capture.
struct MonsterFeedback {
 static bool eligible(const json &monster,const json &view,const char *status="asleep") {
  if(!monster.value("visible",false) || !monster.value(status,false)) return false;
  const int x=monster.value("x",-1)-view.value("x",0),y=monster.value("y",-1)-view.value("y",0);
  if(x<0 || y<0 || x>=view.value("width",0) || y>=view.value("height",0)) return false;
  const auto &cell=view["cells"][y][x];
  return cell[6].get<int>()!=0 && !cell[11].get<int>() && !cell[12].get<int>();
 }
 static void draw(ImDrawList *draw,const json &state,ImVec2 origin,ImVec2 size,float cw,float ch,double now,bool sleeping=true,bool frightened=true) {
  if(!state.contains("dungeon") || !state.contains("monsters")) return;
  const auto &view=state["dungeon"];
  draw->PushClipRect(origin,ImVec2(origin.x+size.x,origin.y+size.y),true);
  for(const auto &m:state["monsters"]) {
   const bool fear=frightened && eligible(m,view,"afraid");
   if(!fear && !(sleeping && eligible(m,view))) continue;
   const int x=m.value("x",0),y=m.value("y",0);
   const ImVec2 center(origin.x+(x-view.value("x",0)+.5f)*cw,origin.y+(y-view.value("y",0)+.5f)*ch);
   // A level-local monster index stays stable while frightened actors move.
   // Older backends retain the original tile-derived sleep timing.
   const double phase=m.contains("index")?m.value("index",0)*.173:x*.137+y*.211;
   for(int i=0;i<2;++i) {
    const float t=float(std::fmod(now/(fear?1.3:1.8)+phase+i*.5,1.));
    const float alpha=.85f*std::min(1.f,t*7.f)*std::min(1.f,(1-t)*4.f);
    const float font=std::max(10.f,ImGui::GetFontSize()*(.65f+.2f*t));
    const char *glyph=fear?"!":"z";
    const float glyph_width=ImGui::GetFont()->CalcTextSizeA(font,FLT_MAX,0,glyph).x;
    const ImVec2 at(fear?center.x-glyph_width*.5f:center.x+cw*(.25f+.35f*t),center.y-ch*(.35f+.9f*t)-font*.5f);
    draw->AddText(ImGui::GetFont(),font,ImVec2(at.x+1,at.y+1),IM_COL32(0,0,0,int(210*alpha)),glyph);
    draw->AddText(ImGui::GetFont(),font,at,(fear?IM_COL32(245,186,105,int(255*alpha)):IM_COL32(160,205,245,int(255*alpha))),glyph);
   }
  }
  draw->PopClipRect();
 }
};
