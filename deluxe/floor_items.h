#pragma once
// The backend identifies observable, non-ignored items at the player's feet.
struct FloorItems {
 static std::vector<const json*> collect(const json &state) {
  std::vector<const json*> result;
  if(state.value("phase","")!="playing" || !state.contains("items")) return result;
  for(const auto &item:state["items"])
   if(item.value("location","")=="Floor" && item.value("on_player_tile",false)) result.push_back(&item);
  return result;
 }
};
