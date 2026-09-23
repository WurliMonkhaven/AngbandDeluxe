// Draft-only editor: engine options are never written until Save and Close.
struct EngineOptions {
 json entries=json::array(), original=json::object(), values=json::object();
 std::string context,error;
 bool loaded=false;
 char search[128]{};
 void reset() { *this=EngineOptions(); }
 void load(const json &data) {
  loaded=true;
  if(data.contains("error")) { error=data.value("error",""); return; }
  entries=data.value("entries",json::array()); context=data.value("context","");
  for(const auto &row:entries) values[row.at("id").get<std::string>()]=row.at("value");
  for(const char *id:{"hitpoint_warn","delay_factor","lazymove_delay"}) values[id]=data.at(id);
  original=values;
 }
 json changes() const {
  json result=json::object();
  for(auto it=values.begin();it!=values.end();++it)
   if(!original.contains(it.key()) || original[it.key()]!=it.value()) result[it.key()]=it.value();
  return result;
 }
 struct Help { const char *id,*group,*text; };
 static Help help(const std::string &id) {
  static const Help notes[]={
   {"rogue_like_commands","Input and targeting","Use letter keys for movement and the roguelike command bindings. Deluxe buttons still invoke the same actions."},
   {"autoexplore_commands","Input and targeting","Stair commands can travel to a known staircase; p explores toward the nearest unexplored location."},
   {"use_old_target","Input and targeting","Aimed actions reuse the current target instead of asking. This includes a previously targeted empty location."},
   {"mouse_movement","Input and targeting","Allow left-click movement in the game view."},
   {"disturb_near","Input and targeting","Stop running, resting or repeated actions when a visible monster moves, appears or disappears."},
   {"pickup_always","Items","Automatically pick up items you walk over when safe to do so."},
   {"pickup_inven","Items","Automatically pick up copies of items already carried."},
   {"show_flavors","Items","Keep colors and varieties in identified item names. Store names are unaffected."},
   {"show_damage","Messages","Include damage dealt by your attacks in the message log."},
   {"auto_more","Messages","Continue past message pauses automatically. This also removes Deluxe's waiting-for-input ribbon for those pauses."},
   {"notify_recharge","Messages","Report when rods and activatable equipment finish recharging."},
   {"use_sound","Messages","Enable the engine's sound preference. Deluxe does not currently play engine sound events."},
   {"show_target","Display","Ask the engine to highlight the current target with its cursor."},
   {"highlight_player","Display","Ask the engine to highlight the player between turns."},
   {"solid_walls","Display","Draw solid walls instead of # and % where the display supports them."},
   {"hybrid_walls","Display","Draw wall symbols on shaded backgrounds. Takes precedence over solid walls."},
   {"view_yellow_light","Display","Tint terrain illuminated by torchlight yellow."},
   {"animate_flicker","Display","Enable the engine preference for shimmering multicolored creatures and objects. Animation depends on display support."},
   {"center_player","Display","Keep the dungeon view centered on the player rather than scrolling when you approach its edge."},
   {"purple_uniques","Display","Use light purple for unique monsters."},
   {"hp_changes_color","Display","Change the player glyph's color as health falls."},
   {"effective_speed","Display","Show a movement-rate multiplier in the engine's speed display instead of a speed modifier."}
  };
  for(const auto &note:notes) if(id==note.id) return note;
  return {"","Other",""};
 }
 void draw() {
  if(!loaded) { ImGui::TextWrapped("Loading Angband options..."); return; }
  if(!error.empty()) { ImGui::TextWrapped("%s",error.c_str()); return; }
  ImGui::TextWrapped("For this character. Changes take effect on Save and Close and persist when you save the game.");
  ImGui::Spacing(); ImGui::SetNextItemWidth(-1);
  ImGui::InputTextWithHint("##Angband search","Search options",search,sizeof(search));
  bool any=false;
  for(const char *group:{"Input and targeting","Items","Messages","Display","Other"}) {
   bool heading=false;
   for(auto &row:entries) {
    const auto id=row.at("id").get<std::string>(),label=row.at("label").get<std::string>();
    const auto note=help(id);
    if(std::string(note.group)!=group || !matches(label+" "+id+" "+note.text+" "+group,search)) continue;
    if(!heading) { ImGui::Spacing(); ImGui::SeparatorText(group); heading=true; }
    any=true; ImGui::PushID(id.c_str());
    bool value=values[id].get<bool>();
    if(ImGui::Checkbox("##value",&value)) values[id]=value;
    bool hovered=ImGui::IsItemHovered(); ImGui::SameLine();
    ImGui::TextWrapped("%s",label.c_str());
    if(hovered || ImGui::IsItemHovered()) {
     ImGui::BeginTooltip(); ImGui::PushTextWrapPos(ImGui::GetFontSize()*25);
     ImGui::TextUnformatted(*note.text?note.text:label.c_str());
     ImGui::PopTextWrapPos(); ImGui::EndTooltip();
    }
    ImGui::PopID();
   }
  }
  struct Numeric { const char *id,*label,*help; int maximum,multiplier; const char *format; };
  const Numeric numbers[]={
   {"hitpoint_warn","Low hitpoint warning","Warn below this percentage of maximum HP, in 10% steps. Also sets the threshold for Deluxe's low-health animation.",9,10,"%d%%"},
   {"delay_factor","Animation delay","Stored engine animation delay in milliseconds. Deluxe currently presents completed engine frames, so this does not control Deluxe or CRT animation timing.",255,1,"%d ms"},
   {"lazymove_delay","Movement key delay","Time in milliseconds, in 10 ms steps, to combine two direction keys into a diagonal. Zero avoids this intentional input delay.",255,10,"%d ms"}
  };
  bool heading=false;
  for(const auto &n:numbers) {
   if(!matches(std::string(n.label)+" "+n.id+" "+n.help,search)) continue;
   if(!heading) { ImGui::Spacing(); ImGui::SeparatorText("Warnings and timing"); heading=true; }
   any=true; ImGui::TextUnformatted(n.label);
   if(ImGui::IsItemHovered()) ImGui::SetTooltip("%s",n.help);
   int value=values[n.id].get<int>()*n.multiplier;
   ImGui::PushID(n.id); ImGui::SetNextItemWidth(-1);
   if(ImGui::SliderInt("##value",&value,0,n.maximum*n.multiplier,n.format,ImGuiSliderFlags_AlwaysClamp)) values[n.id]=value/n.multiplier;
   if(ImGui::IsItemHovered()) ImGui::SetTooltip("%s",n.help);
   ImGui::PopID();
  }
  if(!any) ImGui::TextDisabled("No matching options.");
 }
};
