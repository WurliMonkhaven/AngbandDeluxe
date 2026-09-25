// Cached, on-demand engine previews; no client gameplay calculations.
struct ItemComparison {
 static std::string value(int v,int scale) {
  char text[64];
  if(scale==0 && v>18) SDL_snprintf(text,sizeof(text),"18/%02d",v-18);
  else if(scale<=1) SDL_snprintf(text,sizeof(text),"%d",v);
  else SDL_snprintf(text,sizeof(text),"%.1f",float(v)/scale);
  return text;
 }
 static void draw(Connection &c,const json &item) {
  if(c.capabilities.value("item.compare",0)<1 || !item.value("comparison_available",false)) return;
  if(!ImGui::CollapsingHeader("Equipment comparison",ImGuiTreeNodeFlags_DefaultOpen)) return;
  const auto id=item.value("id","");
  if(!c.comparisons.contains(id)) {
   if(c.busy || !c.prompt.empty() || !(c.ready() || (c.state.contains("store") && c.state["store"].value("ready",false)))) {
    ImGui::TextDisabled("Available when the current action finishes."); return;
   }
   const auto request=c.send("item.compare",{{"item",id},{"revision",c.state.value("revision","")}});
   if(!request.empty()) { c.comparison_requests[request]=id; c.comparisons[id]=nullptr; }
  }
  if(!c.comparisons.contains(id) || c.comparisons[id].is_null()) { ImGui::TextDisabled("Calculating comparison..."); return; }
  const auto &comparison=c.comparisons[id];
  if(comparison.contains("error")) { ImGui::TextWrapped("%s",comparison.value("error","").c_str()); return; }
  const auto options=comparison.value("options",json::array());
  if(options.empty()) { ImGui::TextDisabled("No compatible equipment slot."); return; }
  auto *storage=ImGui::GetStateStorage(); const auto key=ImGui::GetID("Comparison slot");
  int choice=std::clamp(storage->GetInt(key,0),0,int(options.size())-1);
  if(options.size()>1 && ImGui::BeginCombo("Replace slot",display_label(options[choice].value("slot_label","")).c_str())) {
   for(size_t i=0;i<options.size();++i) {
    const auto label=display_label(options[i].value("slot_label",""))+": "+options[i].value("replaces","");
    ImGui::PushID(int(i));
    if(ImGui::Selectable(label.c_str(),choice==int(i))) { choice=int(i); storage->SetInt(key,choice); }
    ImGui::PopID();
   }
   ImGui::EndCombo();
  }
  const auto &option=options[choice];
  ImGui::TextWrapped("Replacing: %s",option.value("replaces","").c_str());
  if(option.value("blocked",false)) ImGui::TextWrapped("The current item cannot be removed. This is a hypothetical comparison.");
  if(!comparison.value("fully_known",false)) ImGui::TextWrapped("Known properties only; unidentified effects may differ.");
  const auto unchanged_key=ImGui::GetID("Show unchanged");
  bool unchanged=storage->GetBool(unchanged_key,false);
  if(ImGui::Checkbox("Show unchanged stats",&unchanged)) storage->SetBool(unchanged_key,unchanged);
  if(ImGui::BeginTable("Equipment changes",4,ImGuiTableFlags_RowBg|ImGuiTableFlags_SizingStretchProp)) {
   ImGui::TableSetupColumn("Stat",ImGuiTableColumnFlags_WidthStretch,2);
   for(const char *label:{"Current","Selected","Change"}) ImGui::TableSetupColumn(label,ImGuiTableColumnFlags_WidthStretch,1);
   ImGui::TableHeadersRow();
   for(const auto &row:option.value("metrics",json::array())) {
    const int scale=row.value("scale",1),delta=row.value("delta",0);
    const auto metric=row.value("id","");
    if(!unchanged && !delta && metric!="damage" && metric!="blows" && metric!="armour" && metric!="speed") continue;
    ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::TextWrapped("%s",row.value("label","").c_str());
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("%s",row.value("id","")=="damage"?"Average melee damage per round against an ordinary target. Brands and slays can change damage against specific targets.":"Whole-character preview. The replaced item stays in your pack; an item from outside your belongings adds its weight.");
    ImGui::TableNextColumn(); ImGui::TextUnformatted(value(row.value("before",0),scale).c_str());
    ImGui::TableNextColumn(); ImGui::TextUnformatted(value(row.value("after",0),scale).c_str());
    ImGui::TableNextColumn();
    if(!delta) ImGui::TextDisabled("-");
    else {
     const bool gain=row.value("id","")=="weight"?delta<0:delta>0;
     ImGui::PushStyleColor(ImGuiCol_Text,gain?ImVec4(.5f,.9f,.65f,1):ImVec4(1,.55f,.4f,1));
     const auto text=scale==0?(delta>0?std::string("Up"):std::string("Down")):(delta>0?"+":"")+value(delta,scale);
     ImGui::TextUnformatted(text.c_str()); ImGui::PopStyleColor();
    }
   }
   ImGui::EndTable();
  }
  if(option.contains("damage_note")) ImGui::TextWrapped("%s",option.value("damage_note","").c_str());
  const auto changes=option.value("changes",json::array());
  if(!changes.empty()) {
   ImGui::Spacing(); ImGui::TextDisabled("Resistances & abilities");
   for(const auto &change:changes)
    ImGui::TextWrapped("%s: %s -> %s",display_label(change.value("label","")).c_str(),change.value("before","").c_str(),change.value("after","").c_str());
  }
 }
};
