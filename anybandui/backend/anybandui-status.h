/* Presentation metadata for engine-owned timed effects. No gameplay changes. */
static const struct { int id; const char *name,*kind,*help; } anybandui_status_metadata[]={
  {TMD_FAST,"Haste","benefit","Increases your speed."},
  {TMD_SLOW,"Slowed","harm","Reduces your speed."},
  {TMD_BLIND,"Blind","harm","You cannot see your surroundings normally. Blindness also prevents reading scrolls."},
  {TMD_PARALYZED,"Paralyzed","harm","You cannot act until the paralysis ends."},
  {TMD_CONFUSED,"Confused","harm","Movement and aiming can go astray. Spellcasting and device use are impaired."},
  {TMD_AFRAID,"Afraid","harm","Fear prevents normal melee attacks."},
  {TMD_IMAGE,"Hallucinating","harm","Creatures and items may appear as something else. Device use is impaired."},
  {TMD_POISONED,"Poisoned","harm","Poison causes ongoing damage and interferes with recovery. Constitution affects how quickly it wears off."},
  {TMD_CUT,"Bleeding","harm","Wounds cause ongoing damage. More severe wounds bleed more heavily; mortal wounds do not heal naturally."},
  {TMD_STUN,"Stunned","harm","Stunning impairs combat and spellcasting. Severe stunning can knock you unconscious."},
  {TMD_FOOD,"Hungry","harm","Low nourishment impairs recovery. Severe hunger weakens you, can cause fainting, and eventually causes starvation damage."},
  {TMD_PROTEVIL,"Protection from evil","benefit","Can repel melee attacks from qualifying evil creatures; it is not protection from every evil attack."},
  {TMD_INVULN,"Invulnerability","benefit","Greatly increases armour class."},
  {TMD_HERO,"Heroism","benefit","Improves accuracy and device skill, and protects against fear."},
  {TMD_SHERO,"Berserk","mixed","Greatly improves melee skill and protects against fear, but reduces armour and device skill."},
  {TMD_SHIELD,"Shield","benefit","A magical shield increases your armour class."},
  {TMD_BLESSED,"Blessed","benefit","Improves armour, accuracy and device skill."},
  {TMD_SINVIS,"See invisible","benefit","Allows you to see invisible creatures when other visibility conditions permit."},
  {TMD_SINFRA,"Infravision","benefit","Extends your ability to see warm-blooded creatures in darkness."},
  {TMD_OPP_ACID,"Resist acid","benefit","Temporary acid resistance, in addition to equipment resistance."},
  {TMD_OPP_ELEC,"Resist lightning","benefit","Temporary lightning resistance, in addition to equipment resistance."},
  {TMD_OPP_FIRE,"Resist fire","benefit","Temporary fire resistance, in addition to equipment resistance."},
  {TMD_OPP_COLD,"Resist cold","benefit","Temporary cold resistance, in addition to equipment resistance."},
  {TMD_OPP_POIS,"Resist poison","benefit","Temporary poison resistance."},
  {TMD_OPP_CONF,"Resist confusion","benefit","Temporary protection against confusion."},
  {TMD_AMNESIA,"Amnesia","harm","Interferes with spellcasting and device use."},
  {TMD_TELEPATHY,"Telepathy","benefit","Senses the minds of creatures susceptible to telepathy."},
  {TMD_STONESKIN,"Stoneskin","mixed","Increases armour class but reduces your speed."},
  {TMD_TERROR,"Terror","harm","Fear prevents normal melee attacks, while panic increases your speed."},
  {TMD_SPRINT,"Sprint","mixed","Increases speed briefly, followed by slowness."},
  {TMD_BOLD,"Fearless","benefit","Protects against fear."},
  {TMD_SCRAMBLE,"Scrambled stats","harm","Your attributes have been temporarily rearranged."},
  {TMD_TRAPSAFE,"Trap safe","benefit","Protects you from triggering traps."},
  {TMD_FASTCAST,"Fast casting","benefit","Reduces the time spent casting spells."},
  {TMD_ATT_ACID,"Acid brand","benefit","Temporarily brands your attacks with acid."},
  {TMD_ATT_ELEC,"Lightning brand","benefit","Temporarily brands your attacks with lightning."},
  {TMD_ATT_FIRE,"Fire brand","benefit","Temporarily brands your attacks with fire."},
  {TMD_ATT_COLD,"Cold brand","benefit","Temporarily brands your attacks with cold."},
  {TMD_ATT_POIS,"Poison brand","benefit","Temporarily brands your attacks with poison."},
  {TMD_ATT_CONF,"Confusing attack","benefit","Your next successful melee hit can confuse its target."},
  {TMD_ATT_EVIL,"Slay evil","benefit","Temporarily strengthens attacks against evil creatures."},
  {TMD_ATT_DEMON,"Slay demons","benefit","Temporarily strengthens attacks against demons."},
  {TMD_ATT_VAMP,"Vampiric attacks","benefit","Attacks can drain life from suitable victims."},
  {TMD_HEAL,"Regenerating","benefit","Accelerated metabolism provides ongoing healing."},
  {TMD_COMMAND,"Commanding","neutral","You are controlling a monster."},
  {TMD_ATT_RUN,"Hit and run","benefit","An attack triggers your prepared escape."},
  {TMD_COVERTRACKS,"Covering tracks","benefit","Leaves no scent trail and reduces visibility to enemies."},
  {TMD_POWERSHOT,"Piercing shot","benefit","Your shots can pierce through targets."},
  {TMD_TAUNT,"Taunting","mixed","Aggravates nearby enemies."},
  {TMD_BLOODLUST,"Bloodlust","mixed","Increases melee power, but risks reckless attacks and physical strain."},
  {TMD_BLACKBREATH,"Black Breath","harm","A wasting affliction that can drain attributes and experience."},
  {TMD_STEALTH,"Stealth","benefit","Temporarily improves stealth."},
  {TMD_FREE_ACT,"Free action","benefit","Temporary protection against paralysis."}
 };
static cJSON *anybandui_statuses(void)
{
 cJSON *statuses=cJSON_CreateArray(); int i;
 for(i=0;i<TMD_MAX;++i) if(player->timed[i]>0 || i==TMD_FOOD) {
  const struct timed_effect_data *effect=&timed_effects[i];
  const struct timed_grade *grade=effect->grade;
  const char *name=effect->desc?effect->desc:effect->name,*kind="neutral",*help=name;
  cJSON *row=cJSON_CreateObject(); size_t j; int priority=2;
  while(grade && grade->next && player->timed[i]>grade->max) grade=grade->next;
  for(j=0;j<N_ELEMENTS(anybandui_status_metadata);++j) if(anybandui_status_metadata[j].id==i) {
   name=anybandui_status_metadata[j].name; kind=anybandui_status_metadata[j].kind; help=anybandui_status_metadata[j].help; break;
  }
  if((i==TMD_CUT || i==TMD_STUN || i==TMD_FOOD) && grade && grade->name) name=grade->name;
  if(i==TMD_FOOD && player->timed[i]<=0) name="Starving";
  if(streq(kind,"harm")) priority=0; else if(streq(kind,"mixed")) priority=1;
  string(row,"id",effect->name); string(row,"label",effect->name);
  string(row,"name",name); string(row,"description",help); string(row,"kind",kind);
  number(row,"priority",priority); number(row,"duration",player->timed[i]);
  number(row,"color",grade?grade->color:COLOUR_WHITE);
  number(row,"grade",grade?grade->grade:0);
  json_bool(row,"visible",i!=TMD_FOOD || player->timed[i]<=PY_FOOD_HUNGRY);
  string(row,"counter_kind",i==TMD_FOOD?"nourishment":i==TMD_CUT || i==TMD_STUN?"severity":"duration");
  cJSON_AddItemToArray(statuses,row);
 }
 return statuses;
}

