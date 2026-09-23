// Engine-authored sections; never infer mechanics from prose.
struct ItemDescription {
 static void draw(const json &item) {
  const auto sections=item.value("description_sections",json::array());
  if(sections.empty()) {
   ImGui::TextWrapped("%s",item.value("description","").c_str());
   return;
  }
  for(const auto &section:sections) {
   const auto id=section.value("id","");
   const auto title=section.value("title","");
   const bool secondary=id=="lore" || id=="durability" || id=="digging";
   ImGui::PushID(id.c_str());
   ImGui::Spacing();
   if(ImGui::CollapsingHeader(title.c_str(),secondary?ImGuiTreeNodeFlags_None:ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::Indent(ImGui::GetFontSize()*.6f);
    ImGui::Spacing();
    ImGui::TextWrapped("%s",section.value("text","").c_str());
    ImGui::Spacing();
    ImGui::Unindent(ImGui::GetFontSize()*.6f);
   }
   ImGui::PopID();
  }
 }
};
