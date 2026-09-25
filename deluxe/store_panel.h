// Native store presentation. Prices, eligibility, comparisons and transaction
// prompts are supplied by the backend; this view contains no game rules.
struct StorePanel {
 int stock_selection=-1, inventory_selection=-1;
 std::string last_name;
 std::string browsed_spell;

 static const json *item(const Connection &c,const std::string &id) {
  for(const auto &record:c.state.at("items")) if(record.value("id","")==id) return &record;
  return nullptr;
 }
 void side(Connection &c,const json &store,bool stock,float height) {
  const bool home=store.value("home",false);
  const int gold=c.state.at("player").value("gold",0);
  const auto affordable=[&](const json &quote) { return !stock || home || quote.value("unit_price",0)<=gold; };
  const auto &entries=store.at(stock?"stock":"inventory");
  int &selection=stock?stock_selection:inventory_selection;
  if(selection>=int(entries.size())) selection=-1;
  if(!stock && selection>=0 && !entries[selection].value("eligible",false)) selection=-1;
  ImGui::PushID(stock?"stock":"inventory");
  ImGui::BeginChild("Side",ImVec2(0,height),ImGuiChildFlags_None,ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);
  std::string heading="Your inventory (Gold: "+std::to_string(gold)+")";
  if(stock) {
   heading=store.value("name",home?"Home":"Stock");
   if(store.contains("owner")) heading+=" — "+store.value("owner","");
  }
  DeluxeTheme::section(heading.c_str());
  const float rows_height=std::max(ImGui::GetTextLineHeightWithSpacing()*3,height*.43f);
  if(ImGui::BeginTable("List",2+(!stock)+(!home),ImGuiTableFlags_Resizable|ImGuiTableFlags_RowBg|ImGuiTableFlags_ScrollY,ImVec2(0,rows_height))) {
   ImGui::TableSetupColumn("Item",ImGuiTableColumnFlags_WidthStretch,3);
   ImGui::TableSetupColumn("Qty",ImGuiTableColumnFlags_WidthFixed,ImGui::CalcTextSize("999").x);
   if(!stock) ImGui::TableSetupColumn("Location",ImGuiTableColumnFlags_WidthStretch,1);
   if(!home) ImGui::TableSetupColumn("Gold each",ImGuiTableColumnFlags_WidthStretch,1);
   ImGui::TableHeadersRow();
   for(int i=0;i<int(entries.size());++i) {
    const auto &entry=entries[i]; const auto *o=item(c,entry.value("item_id",""));
    if(!o || (!stock && !entry.value("eligible",false))) continue;
    ImGui::PushID(i); ImGui::TableNextRow(); ImGui::TableNextColumn();
    const auto label=o->value("label","");
    ImGui::PushStyleColor(ImGuiCol_Text,ui_color(o->value("name_color",1)));
    if(DeluxeTheme::table_choice(label.c_str(),selection==i)) selection=i;
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("%s",label.c_str());
    ImGui::PopStyleColor();
    ImGui::TableNextColumn(); ImGui::Text("%d",o->value("quantity",0));
    if(!stock) { ImGui::TableNextColumn(); ImGui::TextUnformatted(display_label(o->value("location","")).c_str()); }
    if(!home) {
     ImGui::TableNextColumn();
     if(!affordable(entry)) ImGui::TextColored(ImVec4(1.f,.3f,.3f,1.f),"%d",entry.value("unit_price",0));
     else ImGui::Text("%d",entry.value("unit_price",0));
    }
    ImGui::PopID();
   }
   ImGui::EndTable();
  }
  const json *entry=selection>=0?&entries[selection]:nullptr;
  const json *selected=entry?item(c,entry->value("item_id","")):nullptr;
  const bool enabled=store.value("ready",false) && !c.busy && c.prompt.empty();
  const bool eligible=entry && (stock || entry->value("eligible",false));
  ImGui::BeginDisabled(!enabled || !eligible || (entry && !affordable(*entry)));
  const char *action=stock?(home?"Retrieve":"Buy"):(home?"Store":store.value("no_selling",false)?"Give":"Sell");
  if(ImGui::Button(action)) c.target(stock?"store.buy":"store.sell",{{"item",entry->value("item_id","")}});
  ImGui::EndDisabled();
  if(!stock && !home && store.value("no_selling",false) && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
   ImGui::SetTooltip("No-selling game: shops accept eligible gifts without paying gold.");
  if(stock) {
   ImGui::SameLine(); ImGui::BeginDisabled(!enabled);
   if((ImGui::Button(home?"Leave home":"Leave store") || (!ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopupId) && ImGui::IsKeyPressed(ImGuiKey_Escape))) && enabled) c.target("store.leave");
   ImGui::EndDisabled();
  }
  ImGui::BeginChild("Inspection");
  if(selected) {
   ImGui::PushStyleColor(ImGuiCol_Text,ui_color(selected->value("name_color",1)));
   ImGui::TextWrapped("%s",selected->value("label","").c_str());
   ImGui::PopStyleColor();
   ImGui::Separator();
   ItemComparison::draw(c,*selected);
   ImGui::PushID("Selected description"); ItemDescription::draw(*selected); ImGui::PopID();
   if(selected->contains("spells")) {
    if(ImGui::Button("Browse spells")) { browsed_spell.clear(); ImGui::OpenPopup("Book spells"); }
    ImGui::SetNextWindowSize(ImVec2(std::min(ImGui::GetMainViewport()->WorkSize.x-24,ImGui::GetFontSize()*48),0),ImGuiCond_Always);
    if(ImGui::BeginPopupModal("Book spells",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
     ImGui::TextWrapped("%s",selected->value("label","").c_str());
     const auto &spells=selected->at("spells");
     SpellPanel::list(spells,browsed_spell,ImGui::GetTextLineHeightWithSpacing()*9);
     ImGui::BeginChild("Description",ImVec2(0,ImGui::GetTextLineHeightWithSpacing()*7));
     if(const auto *spell=SpellPanel::find(spells,browsed_spell)) SpellPanel::description(*spell);
     ImGui::EndChild();
     if(ImGui::Button("Close") || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
     ImGui::EndPopup();
    }
   }
   for(const auto &id:entry->value("compare_with",json::array())) {
    const auto *equipped=item(c,id.get<std::string>());
    if(!equipped) continue;
    DeluxeTheme::section("Currently equipped");
    ImGui::PushStyleColor(ImGuiCol_Text,ui_color(equipped->value("name_color",1)));
    ImGui::TextWrapped("%s — %s",display_label(equipped->value("location","")).c_str(),equipped->value("label","").c_str());
    ImGui::PopStyleColor();
    ImGui::PushID(equipped->value("id","").c_str()); ItemDescription::draw(*equipped); ImGui::PopID();
   }
  } else ImGui::TextDisabled("Select an item to inspect it.");
  ImGui::EndChild(); ImGui::EndChild(); ImGui::PopID();
 }
 void draw(Connection &c) {
  const auto &store=c.state.at("store");
  const auto name=store.value("name","");
  if(name!=last_name) { stock_selection=inventory_selection=-1; last_name=name; }
  const float messages_height=ImGui::GetTextLineHeightWithSpacing()*3;
  const float height=std::max(1.f,ImGui::GetContentRegionAvail().y-messages_height-ImGui::GetStyle().ItemSpacing.y);
  if(ImGui::BeginTable("Store layout",2,ImGuiTableFlags_Resizable|ImGuiTableFlags_BordersInnerV,ImVec2(0,height))) {
   ImGui::TableSetupColumn("Stock",ImGuiTableColumnFlags_WidthStretch);
   ImGui::TableSetupColumn("Inventory",ImGuiTableColumnFlags_WidthStretch);
   ImGui::TableNextRow(); ImGui::TableNextColumn(); side(c,store,true,height);
   ImGui::TableNextColumn(); side(c,store,false,height); ImGui::EndTable();
  }
  ImGui::BeginChild("Store messages",ImVec2(0,0));
  if(c.state.value("message_pending",false)) {
   if(ImGui::Button("- more -") || ImGui::IsKeyPressed(ImGuiKey_Space) || ImGui::IsKeyPressed(ImGuiKey_Enter)) c.key("enter");
  }
  for(const auto &m:c.messages) ImGui::TextWrapped("%s",m.value("text","").c_str());
  ImGui::EndChild();
 }
};
