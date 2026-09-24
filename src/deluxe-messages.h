/* Presentation groups derived from engine message types, never guessed from
 * localized prose. Generic/unclassified messages remain visible in Other. */
static const char *deluxe_message_group(int type)
{
 if((type>=MSG_MON_HIT && type<=MSG_MON_MOAN) ||
    (type>=MSG_BR_ELEMENTS && type<=MSG_SUM_UNIQUE) ||
    (type>=MSG_HIT_GOOD && type<=MSG_HIT_HI_SUPERB)) return "combat";
 switch(type) {
 case MSG_HIT: case MSG_MISS: case MSG_FLEE: case MSG_KILL:
 case MSG_DEATH: case MSG_SHOOT: case MSG_SHOOT_HIT: case MSG_HITPOINT_WARN:
 case MSG_SPELL: case MSG_PRAYER: case MSG_KILL_UNIQUE: case MSG_KILL_KING:
 case MSG_CREATE_TRAP: case MSG_SHRIEK: case MSG_CAST_FEAR:
 case MSG_DRAIN_STAT: case MSG_MULTIPLY: case MSG_SCRAMBLE: return "combat";
 case MSG_DROP: case MSG_DESTROY: case MSG_MONEY1: case MSG_MONEY2:
 case MSG_MONEY3: case MSG_IDENT_BAD: case MSG_IDENT_EGO: case MSG_IDENT_ART:
 case MSG_WIELD: case MSG_QUIVER: case MSG_CURSED: case MSG_RUNE:
 case MSG_STORE1: case MSG_STORE2: case MSG_STORE3: case MSG_STORE4:
 case MSG_STORE5: return "loot";
 default: return "other";
 }
}
