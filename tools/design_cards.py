#!/usr/bin/env python3
"""Rewrites the BATTLE fields of data/cards.json (tag, cost, atk, hp, rush, trigger, abilities).
The quiz content (scenario, choices, correct answer, explanation) is never touched.
Run from the project root:  python3 tools/design_cards.py
Numbers written as [a, b, c] are the Level 1 / 2 / 3 values of that effect."""
import json, pathlib, collections
ROOT = pathlib.Path(__file__).resolve().parent.parent
def o(op, n=None, m=None, k=None, fb=None, heads=None, tails=None, options=None):
    d = {"op": op}
    for key, val in (("n", n), ("m", m), ("k", k), ("fb", fb), ("heads", heads), ("tails", tails), ("options", options)):
        if val is not None: d[key] = val
    return d
def ab(on, ops, cost=None):
    d = {"on": on, "do": ops}
    if cost is not None: d["cost"] = cost
    return d
D = {}
def mon(i, tag, cost, atk, hp, abilities, rush=False): D[i] = dict(tag=tag, card_type="MONSTER", cost=cost, atk=atk, hp=hp, rush=rush, abilities=abilities)
def spl(i, tag, cost, abilities): D[i] = dict(tag=tag, card_type="SPELL", cost=cost, atk=0, hp=0, rush=False, abilities=abilities)
def trp(i, tag, cost, trig, ops): D[i] = dict(tag=tag, card_type="TRAP", cost=cost, atk=0, hp=0, rush=False, trigger=trig, abilities=[ab("TRAP", ops)])
# ---------------------------------------------------------------- Absurdism: coin flips, chaos, rule-breaking
mon("ABS-01", "Gambler's Grin", 4, 3, 3, [ab("TURN_START", [o("coin", heads=[o("dmg_face", n=[2,3,4])], tails=[])])])
mon("ABS-02", "Heads or Draw", 1, 1, 1, [ab("SUMMON", [o("coin", heads=[o("draw", n=[1,1,2])], tails=[])])])
mon("ABS-03", "Paper Lifeboat", 3, 2, 3, [ab("SUMMON", [o("shield", n=[0,1,2])]), ab("DEATH", [o("coin", heads=[o("return_self")], tails=[])])])
mon("ABS-04", "Wild Pick", 3, 3, 2, [ab("SUMMON", [o("one_of", options=[o("dmg_face", n=[3,4,5]), o("shield", n=[3,4,5]), o("draw", n=[1,1,2])])])])
mon("ABS-05", "Lucky Flip", 3, 2, 2, [ab("SUMMON", [o("coin", heads=[o("insight", n=[3,3,4])], tails=[o("insight", n=[1,1,2])])])])
mon("ABS-06", "Random Snag", 2, 2, 2, [ab("SUMMON", [o("dmg_random", n=[1,2,3])])])
mon("ABS-07", "Backstage Pass", 4, 3, 3, [ab("SUMMON", [o("reveal_traps"), o("destroy_traps", n=[1,1,2])])])
spl("ABS-08", "Windfall Gambit", 2, [ab("SPELL", [o("coin", heads=[o("dmg_face", n=[4,5,6])], tails=[o("dmg_self", n=[1,1,2])])])])
spl("ABS-09", "Laugh Track", 1, [ab("SPELL", [o("discard_enemy", n=[1,1,2])]), ab("GRAVE", [o("discard_enemy", n=1)], cost=2)])
trp("ABS-10", "Absurd Reversal", 2, "ON_SUMMON", [o("coin", heads=[o("t_destroy")], tails=[o("t_dmg_summoner", n=[2,3,3])])])
# ---------------------------------------------------------------- Existentialism: choice, search, self-made paths
mon("EXI-01", "True North", 2, 2, 2, [ab("SUMMON", [o("search_monster", n=1, m=[2,3,4])])])
mon("EXI-02", "Open Door", 1, 1, 2, [ab("SUMMON", [o("draw", n=[1,1,2])])])
mon("EXI-03", "Blank Slate", 2, 1, 3, [ab("SUMMON", [o("search_spell", n=1), o("search_trap", n=[0,0,1])])])
mon("EXI-04", "Own Path", 4, 3, 3, [ab("ATTACK", [o("buff_self_atk", n=[1,1,2])])])
mon("EXI-05", "Reflection", 5, 2, 4, [ab("SUMMON", [o("steal_gy", fb=[2,3,4])])])
mon("EXI-06", "Signed Deal", 3, 2, 3, [ab("ATTACK", [o("draw", n=1)])])
mon("EXI-07", "Detour", 4, 3, 3, [ab("SUMMON", [o("bounce_strongest", fb=[2,3,4])])])
spl("EXI-08", "First Strokes", 3, [ab("SPELL", [o("search_monster", n=1), o("search_spell", n=1), o("draw", n=[0,0,1])])])
spl("EXI-09", "Skeleton Key", 2, [ab("SPELL", [o("reveal_traps"), o("destroy_traps", n=[1,1,2])]), ab("GRAVE", [o("destroy_traps", n=1)], cost=1)])
trp("EXI-10", "Ledger Reset", 3, "ON_ATTACK", [o("negate_attack"), o("t_bounce"), o("draw", n=[0,1,1])])
# ---------------------------------------------------------------- Hedonism: pleasure now, pain later, drain
mon("HED-01", "Forbidden Fruit", 4, 2, 3, [ab("SUMMON", [o("indulge", n=[3,4,5])])], rush=True)
mon("HED-02", "Quick Ray", 1, 1, 2, [ab("SUMMON", [o("indulge", n=[1,1,2])])], rush=True)
mon("HED-03", "Sweet Sting", 2, 2, 1, [ab("SUMMON", [o("dmg_face", n=[1,2,2]), o("heal", n=[1,1,2])])])
mon("HED-04", "Slow Burn", 3, 2, 4, [ab("TURN_START", [o("dmg_face", n=[1,1,2])])])
mon("HED-05", "Pay for Pleasure", 3, 3, 2, [ab("SUMMON", [o("pay_lp", n=2), o("draw", n=[2,2,3])])])
mon("HED-06", "Blossoming", 3, 1, 4, [ab("TURN_END", [o("buff_self_atk", n=[1,1,2])])])
mon("HED-07", "Comfort Zone", 5, 3, 5, [ab("SUMMON", [o("heal", n=[3,4,5])]), ab("DEATH", [o("heal", n=[3,4,5])])])
spl("HED-08", "Sweet Drain", 3, [ab("SPELL", [o("dmg_face", n=[3,4,5]), o("heal", n=[3,4,5])])])
spl("HED-09", "Fair Trade", 1, [ab("SPELL", [o("pay_lp", n=2), o("insight", n=[2,3,3])])])
trp("HED-10", "Stolen Moment", 3, "ON_ATTACK", [o("negate_attack"), o("leech_attacker")])
# ---------------------------------------------------------------- Nihilism: void, destruction, the graveyard
mon("NIH-01", "Time's Up", 6, 5, 5, [ab("SUMMON", [o("destroy_strongest", fb=[3,4,5])])])
mon("NIH-02", "Crumble", 3, 3, 1, [ab("DEATH", [o("dmg_all", n=[2,3,4])])])
mon("NIH-03", "Last Gasp", 2, 2, 1, [ab("DEATH", [o("dmg_face", n=[2,3,4])]), ab("MILL", [o("dmg_face", n=[1,1,2])])])
mon("NIH-04", "Echo Draw", 3, 2, 3, [ab("SUMMON", [o("mill_self", n=[2,2,3])]), ab("GRAVE", [o("draw", n=[1,1,2])], cost=1)])
mon("NIH-05", "Erase Evidence", 4, 3, 2, [ab("SUMMON", [o("banish_gy", n=[3,4,5]), o("dmg_per_banished", k=1)])])
mon("NIH-06", "Autumn Draw", 2, 1, 3, [ab("SUMMON", [o("mill_self", n=[1,2,2])]), ab("MILL", [o("draw", n=1)])])
mon("NIH-07", "Return Ripple", 5, 4, 4, [ab("SUMMON", [o("revive_strongest", fb=[3,4,5])])])
spl("NIH-08", "Burn Down", 1, [ab("SPELL", [o("mill_self", n=[2,3,4])])])
spl("NIH-09", "Vanish", 4, [ab("SPELL", [o("banish_strongest", fb=[3,4,5])])])
trp("NIH-10", "Null Seed", 3, "ON_SPELL", [o("negate_spell", k=1)])
# ---------------------------------------------------------------- Stoicism: shields, calm, endurance
mon("STO-01", "Raise Shield", 1, 1, 2, [ab("SUMMON", [o("shield", n=[2,3,4])])])
mon("STO-02", "Steady Upkeep", 2, 1, 3, [ab("TURN_START", [o("shield", n=[1,1,2])])])
mon("STO-03", "Last Stand", 3, 2, 3, [ab("DEATH", [o("shield", n=[3,4,5])])])
mon("STO-04", "Calm Freeze", 3, 2, 3, [ab("SUMMON", [o("freeze_strongest"), o("shield", n=[1,2,2])])])
mon("STO-05", "Steady Healing", 4, 3, 4, [ab("SUMMON", [o("heal", n=[2,3,4])]), ab("TURN_END", [o("heal", n=[1,1,2])])])
mon("STO-06", "Unshaken", 5, 4, 5, [ab("SUMMON", [o("silence_strongest"), o("shield", n=[3,4,5])])])
mon("STO-07", "Fortress", 6, 5, 6, [ab("SUMMON", [o("shield_per_monster", n=[2,3,4])])])
spl("STO-08", "Recover", 2, [ab("SPELL", [o("heal", n=[4,5,6])]), ab("GRAVE", [o("heal", n=[2,3,4])], cost=2)])
spl("STO-09", "Clear Path", 2, [ab("SPELL", [o("reveal_traps"), o("block_traps"), o("shield", n=[2,3,4])])])
trp("STO-10", "Still Water", 2, "ON_ATTACK", [o("negate_attack"), o("shield_from_attacker")])
# ---------------------------------------------------------------- Utilitarianism: the team, the greater good
mon("UTI-01", "Tip the Scales", 3, 2, 3, [ab("SUMMON", [o("dmg_per_monster", n=[1,1,2])])])
mon("UTI-02", "Round Table", 3, 2, 3, [ab("SUMMON", [o("buff_allies_hp", n=[1,2,3])])])
mon("UTI-03", "Share the Loaf", 1, 2, 2, [ab("SUMMON", [o("share_draw", n=[1,1,2], m=1)])])
mon("UTI-04", "Level the Field", 2, 2, 2, [ab("SUMMON", [o("draw_if_behind", n=[1,1,2])])])
mon("UTI-05", "Greater Good", 4, 3, 4, [ab("SUMMON", [o("sac_for_team", n=[2,2,3], m=[3,4,5])])])
mon("UTI-06", "Majority Vote", 3, 2, 3, [ab("TURN_END", [o("shield_per_monster", n=[1,1,2])])])
mon("UTI-07", "Helping Hand", 4, 3, 3, [ab("SUMMON", [o("revive_cost_le", n=1, m=[2,3,4])])])
spl("UTI-08", "Rally the Fallen", 4, [ab("SPELL", [o("revive_cost_le", n=[1,1,2], m=[3,3,4])])])
spl("UTI-09", "Bridge Together", 3, [ab("SPELL", [o("buff_allies_atk", n=[1,1,2]), o("buff_allies_hp", n=[1,2,2])])])
trp("UTI-10", "Equal Terms", 2, "ON_SUMMON", [o("t_negate_effect"), o("t_weaken", n=[1,2,3])])
RENAMES = {"HED-10": "The Final Course"}      # was a second "The Evening Star"
path = ROOT / "data" / "cards.json"
cards = json.loads(path.read_text(encoding="utf8"))
assert len(cards) == 60 and set(D) == {c["id"] for c in cards}
for c in cards:
    d = D[c["id"]]
    assert c["card_type"] == d["card_type"], c["id"]            # type split stays 42 / 12 / 6
    for k in ("effect_type", "trigger"): c.pop(k, None)
    c.update({k: v for k, v in d.items()})
    c["rush"] = bool(d.get("rush", False))
    if c["id"] in RENAMES: c["collectible"] = RENAMES[c["id"]]
path.write_text(json.dumps(cards, indent=2, ensure_ascii=False) + "\n", encoding="utf8")
tags = collections.Counter(c["tag"] for c in cards); assert max(tags.values()) == 1
names = collections.Counter(c["collectible"] for c in cards); assert max(names.values()) == 1, names
sig = collections.Counter(json.dumps([c["cost"], c["atk"], c["hp"], c["abilities"]], sort_keys=True) for c in cards); assert max(sig.values()) == 1
print("OK  cost histogram:", sorted(collections.Counter(c["cost"] for c in cards).items()))
print("monster costs:", sorted(collections.Counter(c["cost"] for c in cards if c["card_type"] == "MONSTER").items()))
