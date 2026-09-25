// Bounded, presentation-only animations. No input capture or engine requests.
struct CombatFeedback {
 struct Mark { int x,y,amount; bool player; std::string kind; double born; };
 std::vector<Mark> marks;
 std::string level;
 static constexpr double lifetime=.72;
 void update(std::deque<json> &events,const json &state,bool enabled,double now) {
  const auto current=state.contains("dungeon")?state["dungeon"].value("level_id",""):"";
  if(!enabled || current.empty() || state.value("phase","")!="playing") { marks.clear(); events.clear(); level=current; return; }
  if(level!=current) { marks.clear(); level=current; }
  marks.erase(std::remove_if(marks.begin(),marks.end(),[&](const Mark &m){return now-m.born>=lifetime;}),marks.end());
  for(const auto &e:events) {
   const double born=e.value("received",now);
   const auto kind=e.value("kind","");
   if(e.value("level_id","")!=level || now-born>=lifetime || (kind!="damage" && kind!="heal" && kind!="miss")) continue;
   Mark mark{e.value("x",0),e.value("y",0),e.value("amount",0),e.value("player",false),kind,born};
   bool merged=false;
   for(auto &m:marks) if(m.x==mark.x && m.y==mark.y && m.kind==kind && m.player==mark.player && born-m.born<.09) {
    m.amount+=mark.amount; merged=true; break;
   }
   if(!merged) { if(marks.size()>=48) marks.erase(marks.begin()); marks.push_back(mark); }
  }
  events.clear();
 }
 void draw(ImDrawList *draw,ImVec2 origin,ImVec2 size,float cw,float ch,int ox,int oy,double now) const {
  draw->PushClipRect(origin,ImVec2(origin.x+size.x,origin.y+size.y),true);
  for(size_t i=0;i<marks.size();++i) {
   const auto &m=marks[i];
   const float age=float(now-m.born),progress=std::clamp(age/float(lifetime),0.f,1.f);
   const float x=origin.x+(m.x-ox+.5f)*cw,y=origin.y+(m.y-oy+.5f)*ch;
   if(x<origin.x || y<origin.y || x>=origin.x+size.x || y>=origin.y+size.y) continue;
   const ImVec4 tint=m.kind=="heal"?ImVec4(.42f,1,.69f,1):m.kind=="miss"?ImVec4(.7f,.78f,.87f,1):m.player?ImVec4(1,.38f,.32f,1):ImVec4(1,.81f,.39f,1);
   if(m.kind!="miss" && age<.28f) {
    const float pulse=1-std::clamp((age-.06f)/.22f,0.f,1.f);
    auto flash=tint; flash.w=.48f*pulse;
    draw->AddRectFilled(ImVec2(x-cw*.5f,y-ch*.5f),ImVec2(x+cw*.5f,y+ch*.5f),ImGui::GetColorU32(flash),2);
    flash.w=.9f*pulse;
    draw->AddRect(ImVec2(x-cw*.5f,y-ch*.5f),ImVec2(x+cw*.5f,y+ch*.5f),ImGui::GetColorU32(flash),2,0,std::max(1.f,ImGui::GetFontSize()/14.f));
   }
   std::string text=m.kind=="miss"?"Miss":(m.kind=="heal"?"+":"-")+std::to_string(m.amount);
   const float font=std::max(15.f,ImGui::GetFontSize()*1.2f);
   const auto measure=ImGui::GetFont()->CalcTextSizeA(font,FLT_MAX,0,text.c_str());
   int lane=0; for(size_t j=0;j<i;++j) if(marks[j].x==m.x && marks[j].y==m.y) ++lane;
   ImVec2 at(std::clamp(x-measure.x*.5f,origin.x,std::max(origin.x,origin.x+size.x-measure.x)),
    std::max(origin.y,y-ch*.7f-font-progress*ch*.65f-(lane%3)*font));
   const float alpha=std::min(1.f,(1-progress)*3.f);
   draw->AddText(ImGui::GetFont(),font,ImVec2(at.x+1,at.y+1),IM_COL32(0,0,0,int(230*alpha)),text.c_str());
   auto ink=tint; ink.w=alpha;
   draw->AddText(ImGui::GetFont(),font,at,ImGui::GetColorU32(ink),text.c_str());
  }
  draw->PopClipRect();
 }
};
