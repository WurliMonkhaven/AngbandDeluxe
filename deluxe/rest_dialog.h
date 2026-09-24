// Presentation of the original rest prompt. Replies remain ordinary Angband
// rest choices, so regeneration, interruptions and repeat behavior stay native.
struct RestDialog {
 int mode=0, turns=100;
 static std::string reply(int mode,int turns) {
  if(mode==0) return "&";
  if(mode==1) return "*";
  if(mode==2) return "!";
  return mode==3 && turns>=1 && turns<=9999?std::to_string(turns):"";
 }
 template<class C> bool draw(C &c,bool fresh) {
  if(fresh) mode=0;
  ImGui::PushTextWrapPos(ImGui::GetCursorPosX()+ImGui::GetFontSize()*32);
  ImGui::SeparatorText("Recover");
  ImGui::RadioButton("Fully recovered",&mode,0);
  if(mode==0) ImGui::TextWrapped("Recover HP and mana, wait out harmful conditions, and finish pending recall or descent. Uses Angband's normal recovery rules.");
  ImGui::RadioButton("HP and mana",&mode,1);
  if(mode==1) ImGui::TextWrapped("Stop when both are full.");
  ImGui::RadioButton("HP or mana",&mode,2);
  if(mode==2) ImGui::TextWrapped("Stop as soon as either is full, even if the other is still low.");
  ImGui::SeparatorText("Timed rest");
  ImGui::RadioButton("Number of turns",&mode,3);
  ImGui::BeginDisabled(mode!=3);
  ImGui::SetNextItemWidth(ImGui::GetFontSize()*10);
  ImGui::InputInt("Turns",&turns,1,100);
  ImGui::EndDisabled();
  if(mode==3 && (turns<1 || turns>9999)) ImGui::TextWrapped("Enter between 1 and 9999 turns.");
  ImGui::Spacing();
  ImGui::TextWrapped("Danger interrupts rest normally. Press Escape or Stop resting to cancel.");
  ImGui::PopTextWrapPos();
  const auto value=reply(mode,turns);
  ImGui::BeginDisabled(value.empty());
  bool start=ImGui::Button("Rest") || (!value.empty() && ImGui::IsKeyPressed(ImGuiKey_Enter));
  ImGui::EndDisabled();
  if(start) c.answer(value);
  return start;
 }
};
