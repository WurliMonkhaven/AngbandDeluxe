// Developer-only editor of engine timed-effect counters.
struct DevStatusDialog {
 bool requested=false;
 std::string selected="POISONED";
 int amount=20;
 void open(Connection &c) {
  requested=true; amount=20; c.debug_status_catalog=nullptr;
  c.send("debug.status.list");
 }
 bool draw(Connection &c) {
  if(requested) { ImGui::OpenPopup("Inflict status effect"); requested=false; }
  ImGui::SetNextWindowSize(ImVec2(std::min(ImGui::GetIO().DisplaySize.x*.9f,ImGui::GetFontSize()*34),0),ImGuiCond_Appearing);
  bool closed=false;
  if(ImGui::BeginPopupModal("Inflict status effect",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
   const auto &catalog=c.debug_status_catalog;
   if(catalog.is_null()) ImGui::TextUnformatted("Loading status effects...");
   else if(catalog.is_object()) ImGui::TextWrapped("%s",catalog.value("error","Effects unavailable.").c_str());
   else {
    const json *current=nullptr;
    for(const auto &effect:catalog) if(effect.value("id","")==selected) current=&effect;
    if(!current && !catalog.empty()) { current=&catalog[0]; selected=current->value("id",""); }
    ImGui::TextUnformatted("Status effect"); ImGui::SetNextItemWidth(-1);
    if(ImGui::BeginCombo("##Effect",current?current->value("name","").c_str():"Select an effect")) {
     for(const auto &effect:catalog) if(ImGui::Selectable(effect.value("name","").c_str(),effect.value("id","")==selected)) { selected=effect.value("id",""); current=&effect; }
     ImGui::EndCombo();
    }
    if(current) {
     ImGui::TextWrapped("%s",current->value("description","").c_str()); ImGui::Spacing();
     const auto kind=current->value("counter_kind","duration");
     ImGui::TextUnformatted(kind=="severity"?"Severity":kind=="nourishment"?"Nourishment":"Effect counter");
     ImGui::SetNextItemWidth(-1); ImGui::InputInt("##Status amount",&amount,1,10);
     ImGui::TextWrapped("Sets the value to this amount (does not add to it). Maximum: %d.",current->value("maximum",30000));
     ImGui::TextWrapped(kind=="nourishment"?"Zero means starving.":"Zero clears the effect.");
     if(kind=="duration") ImGui::TextWrapped("The counter is not a count of player actions; speed and recovery affect duration.");
     if((kind=="severity" || kind=="nourishment") && ImGui::CollapsingHeader("Severity thresholds")) {
      int minimum=1;
      for(const auto &grade:current->value("grades",json::array())) {
       ImGui::Text("%d - %d: %s",minimum,grade.value("maximum",0),grade.value("name","").c_str());
       minimum=grade.value("maximum",0)+1;
      }
     }
     ImGui::Separator();
     ImGui::BeginDisabled(!c.ready() || amount<0 || amount>current->value("maximum",30000));
     if(ImGui::Button("Apply")) {
      c.send("debug.status",{{"effect",selected},{"amount",amount}}); c.busy=true;
      ImGui::CloseCurrentPopup(); closed=true;
     }
     ImGui::EndDisabled(); ImGui::SameLine();
    }
   }
   if(ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape)) { ImGui::CloseCurrentPopup(); closed=true; }
   ImGui::EndPopup();
  }
  return closed;
 }
};
