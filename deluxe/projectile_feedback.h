// Real, visible engine tiles only. Animation never owns input or delays a turn.
struct ProjectileFeedback {
 struct Tile { int x,y; float delay; char glyph; ImVec4 colour; double born; };
 std::vector<Tile> tiles;
 std::string level;
 static constexpr double lifetime=.38;
 static ImVec4 colour(const std::string &kind) {
  if(kind=="FIRE" || kind=="PLASMA") return {1,.45f,.16f,1};
  if(kind=="COLD" || kind=="ICE") return {.45f,.87f,1,1};
  if(kind=="ELEC") return {1,.95f,.43f,1};
  if(kind=="ACID") return {.64f,1,.24f,1};
  if(kind=="POIS") return {.36f,.88f,.5f,1};
  if(kind=="DARK" || kind=="NETHER" || kind=="CHAOS") return {.8f,.52f,1,1};
  if(kind=="MISSILE") return {1,.82f,.48f,1};
  return {.68f,.82f,1,1};
 }
 void update(std::deque<json> &events,const json &state,bool enabled,double now) {
  const auto current=state.contains("dungeon")?state["dungeon"].value("level_id",""):"";
  if(!enabled || current.empty() || state.value("phase","")!="playing") {
   tiles.clear(); events.clear(); level=current; return;
  }
  if(level!=current) { tiles.clear(); level=current; }
  tiles.erase(std::remove_if(tiles.begin(),tiles.end(),[&](const Tile &t){return now-t.born>=lifetime;}),tiles.end());
  for(const auto &event:events) {
   const double born=event.value("received",now);
   if(event.value("level_id","")!=level || now-born>=lifetime) continue;
   for(const auto &effect:event.at("effects")) {
    const auto &points=effect.at("tiles");
    const bool blast=effect.value("blast",false);
    const auto kind=effect.value("element","");
    const auto ink=colour(kind);
    int radius=1;
    if(blast) for(const auto &p:points) radius=std::max(radius,p[2].get<int>());
    for(size_t i=0;i<points.size() && tiles.size()<2048;++i) {
     const auto &p=points[i];
     char glyph='*';
     if(!blast) {
      int dx=1,dy=0;
      if(i) { dx=p[0].get<int>()-points[i-1][0].get<int>(); dy=p[1].get<int>()-points[i-1][1].get<int>(); }
      glyph=dy==0?'-':dx==0?'|':dx*dy>0?'\\':'/';
      if(kind=="ELEC") glyph=i%2?'~':'*';
     } else if(kind=="COLD" || kind=="ICE") glyph='+';
     const float delay=blast?.07f+.1f*p[2].get<float>()/radius:.12f*float(i)/std::max(size_t(1),points.size()-1);
     tiles.push_back({p[0].get<int>(),p[1].get<int>(),delay,glyph,ink,born});
    }
   }
  }
  events.clear();
 }
 void draw(ImDrawList *draw,ImVec2 origin,ImVec2 size,float cw,float ch,int ox,int oy,double now) const {
  draw->PushClipRect(origin,ImVec2(origin.x+size.x,origin.y+size.y),true);
  for(const auto &t:tiles) {
   const float age=float(now-t.born)-t.delay;
   if(age<0 || age>.20f) continue;
   const float x=origin.x+(t.x-ox+.5f)*cw,y=origin.y+(t.y-oy+.5f)*ch;
   if(x<origin.x || y<origin.y || x>=origin.x+size.x || y>=origin.y+size.y) continue;
   auto ink=t.colour; ink.w=(1-age/.20f)*.9f;
   auto halo=ink; halo.w*=.14f;
   draw->AddRectFilled({x-cw*.45f,y-ch*.4f},{x+cw*.45f,y+ch*.4f},ImGui::GetColorU32(halo),3);
   const char text[2]={t.glyph,0};
   const float font=std::min(ch,ImGui::GetFontSize()*1.15f);
   const auto measure=ImGui::GetFont()->CalcTextSizeA(font,FLT_MAX,0,text);
   draw->AddText(ImGui::GetFont(),font,{x-measure.x*.5f,y-measure.y*.5f},ImGui::GetColorU32(ink),text);
  }
  draw->PopClipRect();
 }
};
