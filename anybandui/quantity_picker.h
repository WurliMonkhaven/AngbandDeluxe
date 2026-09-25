#pragma once
// Quantity bounds and optional per-stack quotes are owned by the engine.
struct QuantityPicker {
 int amount=1;
 static bool valid(int n,int maximum) { return n>=1 && n<=maximum; }
 static int half(int maximum) { return std::max(1,maximum/2); }
 bool draw(Connection &c,bool fresh) {
  const auto &p=c.prompt;
  const int maximum=std::max(0,p.value("maximum",0));
  if(fresh) amount=1;
  if(p.contains("item")) {
   const auto &item=p["item"];
   ImGui::PushStyleColor(ImGuiCol_Text,color(item.value("name_color",1)));
   ImGui::TextWrapped("%s",item.value("label","").c_str()); ImGui::PopStyleColor();
   ImGui::Spacing();
  }
  ImGui::TextDisabled("Available for this action: %d",maximum);
  if(fresh) ImGui::SetKeyboardFocusHere();
  ImGui::SetNextItemWidth(-1);
  ImGui::InputInt("##Amount",&amount,1,10);
  const bool enter=ImGui::IsItemFocused() && (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter));
  if(ImGui::Button("One")) amount=1;
  ImGui::SameLine(); if(ImGui::Button("Half")) amount=half(maximum);
  ImGui::SameLine(); if(ImGui::Button("All")) amount=maximum;
  if(maximum>1) { ImGui::SetNextItemWidth(-1); ImGui::SliderInt("##Quantity",&amount,1,maximum,"%d",ImGuiSliderFlags_AlwaysClamp); }
  bool allowed=valid(amount,maximum);
  if(p.contains("purchase_totals") && allowed && amount<=int(p["purchase_totals"].size())) {
   const int total=p["purchase_totals"][amount-1],gold=p.value("gold",0);
   ImGui::Separator();
   ImGui::TextColored(total>gold?ImVec4(1,.35f,.3f,1):AnybandUITheme::green(),"Total: %d gold",total);
   ImGui::TextDisabled("Your gold: %d",gold);
   allowed=total<=gold;
  }
  if(!valid(amount,maximum)) ImGui::TextDisabled("Choose an amount from 1 to %d.",maximum);
  ImGui::Spacing(); ImGui::BeginDisabled(!allowed);
  const bool submit=ImGui::Button("Confirm") || (allowed && enter);
  ImGui::EndDisabled();
  if(submit && allowed) { c.answer(amount); return true; }
  return false;
 }
};
