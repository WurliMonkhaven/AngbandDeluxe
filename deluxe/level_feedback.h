#pragma once
// Session-local observation: loading and recovering a drained level are quiet.
struct LevelFeedback {
 int highest=0, level=0;
 double started=-100;
 static constexpr double duration=2.2;
 void reset() { highest=level=0; started=-100; }
 void update(const json &state,double now) {
  const auto phase=state.value("phase","");
  if(phase!="playing" && phase!="store") { reset(); return; }
  if(!state.contains("player")) return;
  const auto &p=state["player"];
  const int current=p.value("level",0),best=std::max(current,p.value("max_level",current));
  if(highest && current>highest && !p.value("dead",false)) { level=current; started=now; }
  highest=std::max(highest,best);
 }
 float progress(double now) const { return float(std::clamp((now-started)/duration,0.,1.)); }
 float intensity(double now) const {
  const float t=progress(now);
  return std::min(1.f,t*12.f)*std::max(0.f,1.f-t)*1.15f;
 }
};
