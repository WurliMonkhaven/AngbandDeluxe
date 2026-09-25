// Engine-authored sections; never infer mechanics from prose.
struct ItemDescription {
 static void combat(const json &entries) {
  for(const auto &entry:entries) {
   const auto kind=entry.value("kind","");
   if(kind=="warning" || kind=="note") {
    if(kind=="warning") ImGui::PushStyleColor(ImGuiCol_Text,ImVec4(1,.55f,.35f,1));
    ImGui::TextWrapped("%s",entry.value("label","").c_str());
    if(kind=="warning") ImGui::PopStyleColor();
   }
  }
  const int columns=ImGui::GetContentRegionAvail().x>ImGui::GetFontSize()*27?3:2;
  ImGui::TextDisabled("Basic stats");
  ImGui::PushStyleVar(ImGuiStyleVar_CellPadding,ImVec2(8,6));
  if(ImGui::BeginTable("Combat metrics",columns,ImGuiTableFlags_SizingStretchSame|ImGuiTableFlags_BordersInnerV|ImGuiTableFlags_PadOuterX)) {
   // The enclosing description is indented; cells use their own padding.
   for(int i=0;i<columns;++i) ImGui::TableSetupColumn("",ImGuiTableColumnFlags_IndentDisable);
   for(const auto &entry:entries) {
    const auto kind=entry.value("kind",""); const int value=entry.value("value",0);
    const char *label=nullptr,*tip="";
    if(kind=="blows") { label="Blows / rd"; tip="Melee attacks per round with this weapon, at your current strength and dexterity."; }
    if(kind=="damage") { label="Dmg / rd"; tip="Average damage per round against an ordinary target, including attacks per round. Special target damage is listed below."; }
    if(kind=="throw_damage") { label="Throw dmg"; tip="Average damage from one throw against an ordinary target."; }
    if(kind=="range") { label="Range"; tip="Maximum firing range with your current launcher, in feet."; }
    if(kind=="break") { label="Break"; tip="Chance of ammunition breaking upon contact."; }
    if(!label) continue;
    ImGui::TableNextColumn();
    ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg,ImGui::GetColorU32(ImVec4(.17f,.21f,.26f,.55f)));
    ImGui::BeginGroup(); ImGui::TextDisabled("%s",label);
    if(kind=="range") ImGui::Text("%d ft",value);
    else if(kind=="break") ImGui::Text("%d%%",value);
    else ImGui::Text("%.1f",kind=="blows"?(value/10)/10.f:value/10.f);
    ImGui::EndGroup();
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("%s",tip);
   }
   ImGui::EndTable();
  }
  ImGui::PopStyleVar();
  bool upgrades=false,variants=false;
  for(const auto &entry:entries) {
   const auto kind=entry.value("kind","");
   upgrades|=kind=="upgrade"; variants|=kind=="damage_variant" || kind=="throw_variant";
  }
  if(upgrades) {
   ImGui::Spacing(); ImGui::TextDisabled("Attack speed upgrades");
   if(ImGui::IsItemHovered()) ImGui::SetTooltip("Alternative STR / DEX increases that improve attacks with this weapon. Each row is a separate option.");
   if(ImGui::BeginTable("Blow upgrades",3,ImGuiTableFlags_SizingStretchSame|ImGuiTableFlags_RowBg)) {
    for(const char *label:{"STR","DEX","Blows / rd"}) ImGui::TableSetupColumn(label);
    ImGui::TableHeadersRow();
    for(const auto &entry:entries) if(entry.value("kind","")=="upgrade") {
     ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::Text("+%d",entry.value("str",0));
     ImGui::TableNextColumn(); ImGui::Text("+%d",entry.value("dex",0));
     ImGui::TableNextColumn(); const int value=entry.value("value",0);
     if(value%10) ImGui::TextUnformatted("Faster");
     else ImGui::Text("%.1f",value/100.f);
     if(ImGui::IsItemHovered()) ImGui::SetTooltip("%s",value%10?"Slightly faster attacks, without changing the displayed blows per round.":"Resulting attacks per round with the STR and DEX increases in this row.");
    }
    ImGui::EndTable();
   }
  }
  if(variants) {
   ImGui::Spacing(); ImGui::TextDisabled("Damage by target");
   if(ImGui::BeginTable("Target damage",2,ImGuiTableFlags_SizingStretchProp|ImGuiTableFlags_RowBg)) {
    ImGui::TableSetupColumn("Target",ImGuiTableColumnFlags_WidthStretch,3);
    ImGui::TableSetupColumn("Damage",ImGuiTableColumnFlags_WidthStretch,1);
    for(const auto &entry:entries) {
     const auto kind=entry.value("kind","");
     if(kind!="damage_variant" && kind!="throw_variant") continue;
     ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::TextWrapped("%s",display_label(entry.value("label","")).c_str());
     ImGui::TableNextColumn(); ImGui::Text("%.1f / %s",entry.value("value",0)/10.f,kind=="throw_variant"?"throw":"rd");
    }
    ImGui::EndTable();
   }
  }
 }
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
    const auto entries=item.value("combat_details",json::array());
    if(id=="combat" && !entries.empty()) combat(entries);
    else {
     auto text=section.value("text","");
     if(id=="combat" && text.rfind("Combat info:\n",0)==0) text.erase(0,13);
     ImGui::TextWrapped("%s",text.c_str());
    }
    ImGui::Spacing();
    ImGui::Unindent(ImGui::GetFontSize()*.6f);
   }
   ImGui::PopID();
  }
 }
};
