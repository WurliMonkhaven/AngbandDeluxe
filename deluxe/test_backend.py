"""End-to-end tests against the real engine; Python is test tooling only."""
import argparse
import json
from pathlib import Path
import queue
import shutil
import subprocess
import tempfile
import threading
import unittest
from collections import deque

ARGS = None


class Engine:
    def __init__(self, folder):
        self.process = subprocess.Popen(
            [str(ARGS.backend), "--data-dir", str(ARGS.data), "--user-dir", str(folder)],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            text=True, encoding="utf-8", bufsize=1,
            creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
        )
        self.incoming = queue.Queue()
        self.errors = []
        self.sound_events = []
        self.count = 0
        self.state = {}
        self.prompt = None
        self.responses = {}
        self.travel = {}
        self.knowledge_events = []
        self.activity_events = []
        self.combat_events = []
        self.projectile_events = []
        self.motion_events = []
        threading.Thread(target=self._read, daemon=True).start()
        threading.Thread(target=self._errors, daemon=True).start()

    def _read(self):
        try:
            for raw in self.process.stdout.buffer:
                try:
                    self.incoming.put(json.loads(raw.decode("utf-8")))
                except UnicodeDecodeError as exc:
                    self.errors.append(repr(raw[max(0, exc.start-150):exc.end+150]))
                    raise
        finally:
            self.incoming.put(None)

    def _errors(self):
        for line in self.process.stderr:
            self.errors.append(line)

    def receive(self):
        try:
            j = self.incoming.get(timeout=25)
        except queue.Empty:
            raise AssertionError("Backend timed out: " + "".join(self.errors))
        if j is None:
            raise AssertionError(f"Backend exited ({self.process.wait(timeout=2)}): " + "".join(self.errors))
        if j.get("kind") == "event":
            if j["event"] == "state.changed":
                self.state = j["data"]
            elif j["event"] == "sound.play":
                self.sound_events.append(j["data"]["name"])
            elif j["event"] == "knowledge.changed":
                self.knowledge_events.append(j["data"])
            elif j["event"] == "travel.changed":
                self.travel = j["data"]
            elif j["event"] == "activity.changed":
                self.activity_events.append(j["data"])
            elif j["event"] == "motion.feedback":
                self.motion_events.append(j["data"])
            elif j["event"] == "projectile.feedback":
                self.projectile_events.append(j["data"])
            elif j["event"] == "combat.feedback":
                self.combat_events.append(j["data"])
            elif j["event"] == "prompt.requested":
                self.prompt = j["data"]
        else:
            self.responses[j["id"]] = j
        return j

    def send(self, method, params=None, request_id=None):
        self.count += 1
        rid = request_id or f"test-{self.count}"
        self.responses.pop(rid, None)
        payload = {"kind": "request", "id": rid, "method": method,
                   "params": {"session_id": "session-1", **(params or {})}}
        self.process.stdin.write(json.dumps(payload) + "\n")
        self.process.stdin.flush()
        return rid

    def call(self, method, params=None, request_id=None):
        rid = self.send(method, params, request_id)
        while rid not in self.responses:
            self.receive()
        return self.responses.pop(rid)

    def next_state(self, previous):
        while self.state.get("revision") == previous or not self.state:
            self.receive()
        return self.state

    def key(self, key):
        self.state = self.call("state.get")["result"]
        old = self.state["revision"]
        result = self.call("terminal.input", {"key": key, "context": self.state["context"]})
        assert "result" in result, (result, self.prompt)
        return self.next_state(old)

    def screen(self):
        return "\n".join("".join(chr(c[0] or 32) for c in row)
                         for row in self.state.get("terminal", []))

    def hello(self):
        result = self.call("hello", {"protocols": [{"major": 0, "minor": 1}]})
        assert "result" in result, result

    def birth(self, name="ProtocolTest", class_index=0):
        result = self.call("session.new", {"save": name})
        assert "result" in result, result
        self.next_state(None)
        if class_index:
            self.key('enter')  # Human, then the class menu.
            for _ in range(class_index): self.key('down')
            self.key('enter')
        for _ in range(50):
            if self.state.get("readiness") == "ready":
                return
            screen = self.screen()
            if ARGS.verbose:
                print(screen[:500], flush=True)
            # Native string/check hooks publish a state immediately before their prompt.
            self.call("state.get")
            if self.prompt:
                p = self.prompt
                value = True if p["type"] == "confirmation" else name
                if p["type"] == "quantity": value = 1
                old = self.state["revision"]
                self.prompt = None
                self.call("prompt.reply", {"prompt_id": p["prompt_id"], "value": value})
                self.next_state(old)
            else:
                self.key("enter")
        raise AssertionError("Birth did not complete:\n" + self.screen())

    def stop(self):
        self.process.stdin.close()
        if self.process.poll() is None:
            try:
                self.process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait(timeout=5)
        self.process.stdout.close()
        self.process.stderr.close()

    def wait_prompt(self):
        for _ in range(10):
            self.state = self.call("state.get")["result"]
            if self.prompt:
                return self.prompt
            if self.state.get("readiness") != "ready":
                self.key("enter")
            else:
                self.receive()
        raise AssertionError("Expected a native prompt: " + self.screen())


class BackendTests(unittest.TestCase):
    def assert_semantic_view(self, state):
        view = state["dungeon"]
        self.assertEqual(len(view["cells"]), view["height"])
        self.assertLess(view["width"], len(state["terminal"][0]))
        for y, row in enumerate(view["cells"]):
            self.assertEqual(len(row), view["width"])
            for x, cell in enumerate(row):
                self.assertEqual(len(cell), 13)
                top = cell[:2]
                for layer in (2, 4, 6):
                    if cell[layer]:
                        top = cell[layer:layer+2]
                # Compare against the classic UI only in this parity test.
                # Production code never interprets or crops terminal cells.
                self.assertEqual(top, state["terminal"][y+1][x+13], (x, y, cell))
        player = state["player"]
        cell = view["cells"][player["y"]-view["y"]][player["x"]-view["x"]]
        self.assertTrue(cell[12])

    def test_native_keybindings(self):
        e=self.engine
        e.hello()
        self.assertIn('error',e.call('keybindings.get'))
        (Path(self.temp.name)/'user'/'user.prf').write_text('keymap-act:R\nkeymap-input:0:[F7]\n',encoding='utf-8')
        e.birth()
        data=e.call('keybindings.get')['result']
        self.assertGreater(len(data['commands']),35)
        self.assertEqual(data['bindings'],[])
        before=e.state['turn']
        bind={'mode':0,'key':137,'command':ord(',')}
        for rows in ([{**bind,'key':ord('1')}],[{**bind,'key':ord('\\')}],[{**bind,'mode':2}],[{**bind,'command':999}], [bind,bind]):
            result=e.call('keybindings.set',{'revision':data['revision'],'bindings':rows})
            self.assertEqual(result['error']['code'],'invalid_argument')
            self.assertEqual(e.call('keybindings.get')['result'],data)
        self.assertEqual(e.call('keybindings.set',{'revision':-1,'bindings':[bind]})['error']['code'],'stale_revision')
        reply=e.call('keybindings.set',{'revision':data['revision'],'bindings':[bind]})
        self.assertIn('result',reply); result=reply['result']
        self.assertEqual(result['bindings'],[bind]); self.assertEqual(e.call('state.get')['result']['turn'],before)
        e.key(137)
        while e.state['readiness']!='ready': e.key('enter')
        self.assertGreater(e.state['turn'],before,'Function-key binding must execute the native wait command')
        # Overriding then restoring an imported macro must recover its sequence.
        data=e.call('keybindings.get')['result']
        native=next(k for k in data['keys'] if k['mode']==0 and k['key']==138)
        self.assertEqual(native['sequence'],'R')
        data=e.call('keybindings.set',{'revision':data['revision'],'bindings':[bind,{**bind,'key':138}]})['result']
        e.call('keybindings.set',{'revision':data['revision'],'bindings':[bind]})
        e.key(138); e.wait_prompt(); self.assertEqual(e.prompt.get('selection_kind'),'rest')
        prompt=e.prompt; e.prompt=None; old=e.state['revision']
        e.call('prompt.reply',{'prompt_id':prompt['prompt_id'],'value':None}); e.next_state(old)
        while e.state['readiness']!='ready': e.key('enter')
        # Persist beyond a process and character boundary, without relying on a game save.
        e.stop(); self.engine=Engine(Path(self.temp.name)); e=self.engine
        e.hello(); e.birth('BindingsAgain')
        data=e.call('keybindings.get')['result']; self.assertEqual(data['bindings'],[bind])
        # A different command in the other keyset remains independent.
        rogue={'mode':1,'key':137,'command':ord('R')}
        data=e.call('keybindings.set',{'revision':data['revision'],'bindings':[bind,rogue]})['result']
        options=e.call('options.get')['result']; old=e.state['revision']
        e.call('options.set',{'context':options['context'],'values':{'rogue_like_commands':True}}); e.next_state(old)
        e.key(137); e.wait_prompt()
        self.assertEqual(e.prompt.get('selection_kind'),'rest')
        prompt=e.prompt; e.prompt=None; old=e.state['revision']
        e.call('prompt.reply',{'prompt_id':prompt['prompt_id'],'value':None}); e.next_state(old)
        while e.state['readiness']!='ready': e.key('enter')
        data=e.call('keybindings.get')['result']
        self.assertEqual(data['mode'],1)
        data=e.call('keybindings.set',{'revision':data['revision'],'bindings':[bind,{**rogue,'command':ord('l')}]})['result']
        e.key(137)
        self.assertEqual(e.state['targeting']['mode'],'look','Roguelike binding must expand to x, not the original movement key l')
        e.key('escape')
        while e.state['readiness']!='ready': e.key('enter')
        data=e.call('keybindings.get')['result']
        blocked=Path(self.temp.name)/'user'/'deluxe-keybindings.json.tmp'
        blocked.mkdir()
        self.assertEqual(e.call('keybindings.set',{'revision':data['revision'],'bindings':[]})['error']['code'],'io_error')
        self.assertEqual(e.call('keybindings.get')['result']['bindings'],data['bindings'])
        blocked.rmdir()
        data=e.call('keybindings.set',{'revision':data['revision'],'bindings':[]})['result']
        self.assertEqual(data['bindings'],[])
        e.stop(); self.engine=Engine(Path(self.temp.name)); e=self.engine
        e.hello(); e.birth('BindingsRestored')
        self.assertEqual(e.call('keybindings.get')['result']['bindings'],[])

    def test_native_options(self):
        e = self.engine
        e.hello()
        self.assertIn("error", e.call("options.get"))
        e.birth()
        before = e.call("state.get")["result"]
        options = e.call("options.get")["result"]
        entries = {o["id"]: o for o in options["entries"]}
        self.assertIn("mouse_movement", entries)
        self.assertNotIn("birth_no_selling", entries)
        self.assertNotIn("cheat_live", entries)
        changed = not entries["show_damage"]["value"]
        for values in ({"show_damage": changed, "cheat_live": True},
                       {"show_damage": changed, "hitpoint_warn": 10},
                       {"delay_factor": -1}, {"delay_factor": 256},
                       {"lazymove_delay": 1.5}, {"show_damage": 1},
                       {"unknown": True}, {"hitpoint_warn": "3"}):
            result = e.call("options.set", {"context": options["context"], "values": values})
            self.assertEqual(result["error"]["code"], "invalid_argument")
            self.assertEqual(e.call("options.get")["result"], options)
        self.assertEqual(e.call("options.set", {"context": "stale", "values": {"show_damage": changed}})["error"]["code"], "stale_revision")
        values = {"show_damage": changed, "mouse_movement": False, "hitpoint_warn": 7, "delay_factor": 0, "lazymove_delay": 0}
        self.assertIn("result", e.call("options.set", {"context": options["context"], "values": values}))
        e.next_state(before["revision"])
        self.assertEqual(e.state["turn"], before["turn"], "Options must not spend a turn")
        self.assertEqual(e.state["readiness"], "ready")
        def verify():
            current = e.call("options.get")["result"]
            actual = {o["id"]: o["value"] for o in current["entries"]}
            actual.update({k: current[k] for k in ("hitpoint_warn", "delay_factor", "lazymove_delay")})
            for key, value in values.items(): self.assertEqual(actual[key], value)
        verify()
        self.assertIn("result", e.call("session.close"))
        e.process.wait(timeout=10); e.stop()
        self.engine = e = Engine(self.temp.name)
        e.hello(); e.call("session.load", {"save": "ProtocolTest"}); e.next_state(None)
        while e.state["readiness"] != "ready": e.key("enter")
        verify()

    def test_debug_status_effects(self):
        e = self.engine
        e.hello()
        self.assertIn("error", e.call("debug.status", {"effect": "POISONED", "amount": 20}))
        self.assertIn("error", e.call("debug.status.list"))
        e.birth()
        catalog = e.call("debug.status.list")["result"]
        effects = {row["id"]: row for row in catalog}
        self.assertIn("POISONED", effects)
        self.assertIn("CUT", effects)
        self.assertNotIn("COMMAND", effects)
        self.assertTrue(effects["CUT"]["grades"])
        before = e.call("state.get")["result"]
        for params in ({"effect": "invalid", "amount": 20}, {"effect": "COMMAND", "amount": 20},
                       {"effect": "CUT", "amount": -1}, {"effect": "CUT", "amount": 1.5},
                       {"effect": "CUT", "amount": "20"}, {"effect": "CUT", "amount": 30001}):
            self.assertEqual(e.call("debug.status", params)["error"]["code"], "invalid_argument")
        self.assertEqual(e.call("state.get")["result"], before)
        def apply(effect, amount):
            old = e.state["revision"]
            self.assertIn("result", e.call("debug.status", {"effect": effect, "amount": amount}))
            e.next_state(old)
            while e.state["readiness"] != "ready": e.key("enter")
        apply("CUT", 500)
        cut = next(s for s in e.state["player"]["statuses"] if s["id"] == "CUT")
        self.assertEqual(cut["name"], "Deep Gash")
        self.assertEqual(cut["duration"], 500)
        apply("CUT", 20)
        self.assertEqual(next(s for s in e.state["player"]["statuses"] if s["id"] == "CUT")["name"], "Light Cut")
        apply("CUT", 0)
        self.assertFalse(any(s["id"] == "CUT" for s in e.state["player"]["statuses"]))
        apply("FAST", 25)
        self.assertEqual(next(s for s in e.state["player"]["statuses"] if s["id"] == "FAST")["name"], "Haste")
        apply("FAST", 0)
        self.assertEqual(e.state["turn"], before["turn"], "Dev status changes must not spend a gameplay turn")

    def test_audio_events(self):
        e = self.engine
        e.hello(); e.birth()
        # The legacy sound option defaults off; semantic cues still reach Deluxe.
        prefs = e.call("options.get")["result"]
        self.assertFalse(next(o["value"] for o in prefs["entries"] if o["id"] == "use_sound"))
        e.sound_events.clear()
        potion = next(i for i in e.state["items"] if "core.quaff" in i["actions"])
        old = e.state["revision"]
        self.assertIn("result", e.call("command.execute", {"revision":old,"command":"core.quaff","item":potion["id"]}))
        e.next_state(old)
        while e.state["readiness"] != "ready": e.key("enter")
        self.assertEqual(e.sound_events.count("quaff"),1)
        before = list(e.sound_events)
        for _ in range(3): e.call("state.get")
        self.assertEqual(e.sound_events,before,"Queries must not replay sound history")
        e.sound_events.clear()
        self.targeting("targeting.set",x=e.state["player"]["x"]+1,y=e.state["player"]["y"])
        while e.state["readiness"] != "ready": e.key("enter")
        self.assertEqual(e.sound_events.count("target_confirmed"),1)
        e.sound_events.clear()
        self.targeting("targeting.begin",mode="target")
        e.key("escape")
        while e.state["readiness"] != "ready": e.key("enter")
        self.assertNotIn("target_confirmed",e.sound_events,"Cancelling a target must stay silent")

    def test_native_status_effects(self):
        e = self.engine
        e.hello(); e.birth()
        food = next(s for s in e.state["player"]["statuses"] if s["id"] == "FOOD")
        self.assertFalse(food["visible"])
        self.assertEqual(food["counter_kind"], "nourishment")
        potion = next(item for item in e.state["items"] if "Berserk" in item["label"])
        old = e.state["revision"]
        self.assertIn("result", e.call("command.execute", {"revision": old, "command": "core.quaff", "item": potion["id"]}))
        e.next_state(old)
        while e.state["readiness"] != "ready": e.key("enter")
        berserk = next(s for s in e.state["player"]["statuses"] if s["id"] == "SHERO")
        self.assertEqual(berserk["name"], "Berserk")
        self.assertEqual(berserk["kind"], "mixed")
        self.assertTrue(berserk["visible"])
        self.assertGreater(berserk["duration"], 0)
        self.assertIn("armour", berserk["description"])
        self.assertGreater(berserk["grade"], 0)
        def set_wound(amount):
            # Existing wizard commands, confined to this disposable character.
            e.key(1); e.key(ord('E'))
            for _ in range(30):
                e.state = e.call("state.get")["result"]
                if e.prompt:
                    prompt = e.prompt; e.prompt = None; old = e.state["revision"]
                    text = prompt.get("text", "").lower()
                    if prompt["type"] == "confirmation": value = True
                    elif prompt["type"] == "quantity": value = 0
                    elif "which effect" in text: value = "TIMED_SET"
                    elif "dice" in text: value = str(amount)
                    elif "subtype" in text: value = "CUT"
                    else: self.fail(f"Unexpected effect prompt: {prompt}")
                    e.call("prompt.reply", {"prompt_id": prompt["prompt_id"], "value": value})
                    e.next_state(old)
                elif e.state["readiness"] != "ready": e.key("enter")
                else: return
            self.fail("Wizard effect did not finish")
        set_wound(500)
        cut = next(s for s in e.state["player"]["statuses"] if s["id"] == "CUT")
        self.assertEqual(cut["name"], "Deep Gash")
        self.assertEqual(cut["kind"], "harm")
        self.assertEqual(cut["counter_kind"], "severity")
        set_wound(0)
        self.assertFalse(any(s["id"] == "CUT" for s in e.state["player"]["statuses"]))
        before = e.call("state.get")["result"]
        self.assertEqual(e.call("state.get")["result"], before)

    def test_map_overview_metadata(self):
        e = self.engine
        e.hello(); e.birth()
        before = e.call("state.get")["result"]
        self.assertEqual(before["map"]["level_id"], before["dungeon"]["level_id"])
        features = e.call("catalog.get")["result"]["features"]
        kinds = {f["map_kind"] for f in features}
        self.assertTrue({"up", "down", "shop", "wall", "floor", "door"}.issubset(kinds))
        for row in before["map"]["known"]:
            for feature in row:
                self.assertEqual(features[feature]["id"], feature)
        self.assertEqual(e.call("state.get")["result"], before)

    def test_creature_lore_watch(self):
        e = self.engine
        e.hello(); e.birth()
        def change_lore(key):
            # Exercise real lore changes through existing wizard commands.
            e.key(1); e.key(ord(key))
            for _ in range(20):
                e.state = e.call("state.get")["result"]
                if e.prompt:
                    prompt = e.prompt; e.prompt = None; old = e.state["revision"]
                    self.assertEqual(prompt["type"], "confirmation")
                    e.call("prompt.reply", {"prompt_id": prompt["prompt_id"], "value": True})
                    e.next_state(old)
                elif e.state["readiness"] != "ready": e.key(ord('a'))
                else: return
            self.fail("Lore command did not complete")
        change_lore('r')
        entries = e.call("knowledge.list", {"category": "creatures"})["result"]["entries"]
        race = entries[-1]["id"]
        e.call("knowledge.get", {"category": "creatures", "id": race, "watch": True})
        e.knowledge_events.clear()
        # Unchanged input boundaries must not regenerate or send recall.
        for _ in range(3): e.key("escape")
        e.call("state.get")
        self.assertEqual(e.knowledge_events, [])
        change_lore('W')
        e.call("state.get")
        self.assertTrue(e.knowledge_events)
        self.assertEqual(e.knowledge_events[-1]["id"], race)
        self.assertEqual(e.knowledge_events[-1]["category"], "creatures")
        self.assertIn("description_sections", e.knowledge_events[-1])
        e.call("knowledge.unwatch")
        e.knowledge_events.clear()
        change_lore('r')
        e.call("state.get")
        self.assertEqual(e.knowledge_events, [])

    def test_creature_inspection_ids(self):
        e = self.engine
        e.hello(); e.birth()
        before = e.call("state.get")["result"]
        known = {row["id"]: row for row in e.call("knowledge.list", {"category": "creatures"})["result"]["entries"]}
        for monster in before["monsters"]:
            self.assertIsInstance(monster["race_id"], int)
            self.assertGreater(monster["race_id"], 0)
            if monster["race_id"] in known:
                detail = e.call("knowledge.get", {"category": "creatures", "id": monster["race_id"]})["result"]
                self.assertEqual(detail["name"].lower(), monster["name"].lower())
                self.assertIsInstance(detail["description_sections"], list)
        self.assertEqual(e.call("state.get")["result"], before)

    def test_native_knowledge(self):
        e = self.engine
        e.hello()
        self.assertEqual(e.call("knowledge.list", {"category": "items"})["error"]["code"], "wrong_phase")
        e.birth()
        before = e.call("state.get")["result"]
        counts = {}
        for category in ("creatures", "items", "artifacts", "terrain"):
            listing = e.call("knowledge.list", {"category": category})["result"]
            self.assertEqual(listing["category"], category)
            entries = listing["entries"]
            counts[category] = len(entries)
            self.assertEqual(len({row["id"] for row in entries}), len(entries))
            for entry in entries:
                self.assertTrue(entry["name"])
                self.assertIsInstance(entry["known"], bool)
                if category == "items":
                    self.assertNotEqual(entry["color"], 0, "Item names must remain readable")
                    if entry["group"] in ("potion", "ring", "wand", "rod", "scroll", "amulet"):
                        self.assertIn(entry["group"], entry["name"].lower())
                detail = e.call("knowledge.get", {"category": category, "id": entry["id"]})["result"]
                self.assertEqual(detail["name"], entry["name"])
                self.assertIsInstance(detail["stats"], list)
                self.assertIsInstance(detail["description_sections"], list)
                for section in detail["description_sections"]:
                    self.assertTrue(section["title"])
                    self.assertTrue(section["text"].strip())
            for bad in (-1, 999999, 1.5, "1"):
                self.assertEqual(e.call("knowledge.get", {"category": category, "id": bad})["error"]["code"], "invalid_argument")
        self.assertGreater(counts["items"], 10)
        self.assertGreater(counts["terrain"], 10)
        self.assertEqual(e.call("knowledge.list", {"category": "invalid"})["error"]["code"], "invalid_argument")
        self.assertEqual(e.call("state.get")["result"], before, "Browsing must not change turns, tracking, gear or gameplay state")

    def test_knowledge_artifact_recall(self):
        e = self.engine
        e.hello(); e.birth()
        # Wizard mode is confined to this disposable character, exposing all
        # artifact templates so their descriptions can be exercised.
        e.key(23)
        for _ in range(20):
            e.state = e.call("state.get")["result"]
            if e.prompt:
                prompt = e.prompt; e.prompt = None; old = e.state["revision"]
                e.call("prompt.reply", {"prompt_id": prompt["prompt_id"], "value": True})
                e.next_state(old)
            elif e.state["readiness"] != "ready":
                e.key("enter")
            else:
                break
        entries = e.call("knowledge.list", {"category": "artifacts"})["result"]["entries"]
        self.assertGreater(len(entries), 20)
        for entry in entries:
            detail = e.call("knowledge.get", {"category": "artifacts", "id": entry["id"]})["result"]
            self.assertTrue(detail["description_sections"], entry)
        self.assertEqual(e.call("knowledge.list", {"category": "artifacts"})["result"]["entries"], entries)

    def test_semantic_view_and_fallback(self):
        e = self.engine
        e.hello(); e.birth()
        self.assert_semantic_view(e.state)
        before = e.call("state.get")["result"]
        for _ in range(5):
            self.assertEqual(e.call("state.get")["result"], before)
        for key in (ord('C'),):
            e.key(key)
            self.assertNotIn("dungeon", e.state)
            e.key("escape")
            while e.state["readiness"] != "ready":
                e.key("escape")
            self.assert_semantic_view(e.state)

    def targeting(self, method, **params):
        e = self.engine
        old = e.state['revision']
        result = e.call(method, {'context': e.state['context'], **params})
        self.assertIn('result', result)
        return e.next_state(old)

    def test_native_look_target_and_validation(self):
        e = self.engine
        e.hello(); e.birth()
        turn = e.state['turn']
        x, y = e.state['player']['x'], e.state['player']['y']
        self.targeting('targeting.begin', mode='look', x=x, y=y)
        self.assertIn('dungeon', e.state)
        self.assertEqual(e.state['targeting']['mode'], 'look')
        self.assertEqual((e.state['targeting']['x'], e.state['targeting']['y']), (x, y))
        original = e.call('state.get')['result']
        for _ in range(3):
            self.assertEqual(e.call('state.get')['result'], original)
        self.assertEqual(e.call('targeting.select', {'context':'old','x':x,'y':y})['error']['code'], 'stale_revision')
        for bad in (-1, 0, 9999, 1.5):
            self.assertEqual(e.call('targeting.select', {'context':e.state['context'],'x':bad,'y':y})['error']['code'], 'invalid_argument')
        self.targeting('targeting.control', operation='free')
        e.key('right')
        self.assertEqual(e.state['targeting']['x'], x+1)
        self.assertIn('dungeon', e.state)
        self.targeting('targeting.select', x=x, y=y)
        self.assertTrue(e.state['targeting']['can_confirm'])
        self.targeting('targeting.control', operation='next')
        self.assertIn([e.state['targeting']['x'],e.state['targeting']['y']], e.state['targeting']['candidates'])
        self.targeting('targeting.control', operation='previous')
        selected = (e.state['targeting']['x'], e.state['targeting']['y'])
        e.key(ord('t'))
        while e.state['readiness'] != 'ready': e.key('enter')
        self.assertIn('selected_target', e.state)
        self.assertEqual((e.state['selected_target']['x'], e.state['selected_target']['y']), selected)
        self.targeting('targeting.begin', mode='look', x=x, y=y)
        self.targeting('targeting.control', operation='next')
        self.targeting('targeting.control', operation='previous')
        self.targeting('targeting.control', operation='confirm')
        while e.state['readiness'] != 'ready': e.key('enter')
        self.assertEqual(e.state['turn'], turn)
        e.key(ord('*'))
        self.assertIn('dungeon', e.state)
        self.assertEqual(e.state['targeting']['mode'], 'target')
        self.assertIsInstance(e.state['targeting']['path'], list)
        self.targeting('targeting.control', operation='cancel')
        while e.state['readiness'] != 'ready': e.key('enter')
        self.assertNotIn('targeting', e.state)
        self.assertEqual(e.state['turn'], turn)
        self.assert_semantic_view(e.state)

    def test_individual_floor_pickup(self):
        e=self.engine
        e.hello(); e.birth()
        def settle(confirmation=True):
            for _ in range(20):
                if e.state['readiness']=='ready': return
                e.call('state.get')
                if e.prompt:
                    prompt=e.prompt; e.prompt=None; old=e.state['revision']
                    self.assertNotEqual(prompt['type'],'choice', 'Explicit pickup must not open the rest-of-pile selector')
                    value=confirmation if prompt['type']=='confirmation' else '!g' if prompt['type']=='text' else 1
                    e.call('prompt.reply',{'prompt_id':prompt['prompt_id'],'value':value})
                    e.next_state(old)
                else: e.key('enter')
            self.fail('Pickup did not settle')
        def command(name,item,confirmation=True):
            old=e.state['revision']
            self.assertIn('result',e.call('command.execute',{'revision':old,'command':name,'item':item['id']}))
            e.next_state(old); settle(confirmation)
        # New characters stand on stairs, which scatter dropped objects. Use
        # ordinary open floor so both drops belong to the same current pile.
        features=e.call('catalog.get')['result']['features']
        open_floor=next(f['id'] for f in features if f['name']=='open floor')
        x,y=e.state['player']['x'],e.state['player']['y']
        dx,dy,key=next((dx,dy,key) for dx,dy,key in ((1,0,'6'),(-1,0,'4'),(0,1,'2'),(0,-1,'8'))
                       if e.state['map']['actual'][y+dy][x+dx]==open_floor)
        e.key(ord(key)); settle()
        potion=next(o for o in e.state['items'] if o['location']=='Pack' and 'Potion' in o['label'])
        kind=potion['actual']['kind']
        command('core.inscribe',potion)
        potion=next(o for o in e.state['items'] if o['location']=='Pack' and o['actual']['kind']==kind)
        command('core.drop',potion)
        scroll=next(o for o in e.state['items'] if o['location']=='Pack' and 'Scroll' in o['label'])
        other=scroll['actual']['kind']
        command('core.drop',scroll)
        floor=lambda k: next(o for o in e.state['items'] if o['location']=='Floor' and o['actual']['kind']==k)
        self.assertTrue(floor(kind)['on_player_tile'], (floor(kind),e.state['player']))
        self.assertTrue(floor(other)['on_player_tile'])
        command('core.pickup',floor(kind),False)
        self.assertTrue(floor(kind)['on_player_tile'],'Declining an inscription confirmation must leave the item')
        old_handle=floor(kind)['id']
        command('core.pickup',floor(kind))
        self.assertFalse(any(o['location']=='Floor' and o['actual']['kind']==kind for o in e.state['items']))
        self.assertTrue(floor(other)['on_player_tile'],'Picking one item must leave the other pile entries untouched')
        self.assertIn('error',e.call('command.execute',{'revision':e.state['revision'],'command':'core.pickup','item':old_handle}))
        owned=next(o for o in e.state['items'] if o['location']=='Pack' and o['actual']['kind']==kind)
        self.assertIn('error',e.call('command.execute',{'revision':e.state['revision'],'command':'core.pickup','item':owned['id']}))
        command('core.pickup',floor(other))
        self.assertFalse(any(o.get('on_player_tile') for o in e.state['items']))

    def test_walk_and_pickup(self):
        self.walk_and_pickup(False)

    def test_walk_and_pickup_interrupted(self):
        self.walk_and_pickup(True)

    def test_remembered_pickup(self):
        self.walk_and_pickup(True, remembered=True)

    def walk_and_pickup(self, interrupt, remembered=False):
        e = self.engine
        e.hello(); e.birth()
        # Isolate route completion from random town residents, using Angband's
        # existing wizard command only in this disposable test character.
        e.key(1); e.key(ord('z'))
        for _ in range(20):
            if e.state['readiness']=='ready': break
            e.call('state.get')
            if e.prompt:
                prompt=e.prompt; e.prompt=None; old=e.state['revision']
                value=True if prompt['type']=='confirmation' else 20
                e.call('prompt.reply',{'prompt_id':prompt['prompt_id'],'value':value})
                e.next_state(old)
            else: e.key('enter')
        item = next(o for o in e.state['items'] if o['location']=='Pack' and 'Potion' in o['label'])
        kind = item['actual']['kind']
        x,y = e.state['player']['x'],e.state['player']['y']
        old=e.state['revision']
        self.assertIn('result',e.call('command.execute',{'revision':old,'command':'core.drop','item':item['id']}))
        e.next_state(old)
        while e.state['readiness']!='ready':
            e.call('state.get')
            if e.prompt:
                prompt=e.prompt; e.prompt=None; old=e.state['revision']
                e.call('prompt.reply',{'prompt_id':prompt['prompt_id'],'value':1})
                e.next_state(old)
            else: e.key('enter')
        floor=next((o for o in e.state['items'] if o['location']=='Floor' and o['actual']['kind']==kind),None)
        self.assertIsNotNone(floor, (e.screen(),e.state['items']))
        x,y=floor['x'],floor['y']
        self.assertTrue(floor['can_pickup'])
        observed = [o for o in e.state['dungeon']['items'] if (o['x'],o['y'])==(x,y)]
        self.assertTrue(observed, "Dropped items must have semantic hover labels")
        self.assertTrue(any('Potion' in o['label'] for o in observed))

        catalog=e.call('catalog.get')['result']['features']
        floors={f['id'] for f in catalog if f['name'] in ('open floor','up staircase','down staircase')}
        terrain=e.state['map']['actual']
        dx,dy,key=next((dx,dy,key) for dx,dy,key in ((1,0,'6'),(-1,0,'4'),(0,1,'2'),(0,-1,'8'))
                       if all(terrain[y+dy*i][x+dx*i] in floors for i in range(1,4)))
        for _ in range(3):
            e.key(ord(key))
            while e.state['readiness']!='ready': e.key('enter')
        if remembered:
            start=(e.state['player']['x'],e.state['player']['y'])
            queue=deque([(start,[])]); visited={start}; route=None
            while queue:
                (cx,cy),path=queue.popleft()
                if max(abs(cx-x),abs(cy-y))>21:
                    route=path; break
                for dx,dy,key in ((1,0,'6'),(-1,0,'4'),(0,1,'2'),(0,-1,'8')):
                    point=(cx+dx,cy+dy)
                    if point not in visited and 0<point[1]<len(terrain)-1 and 0<point[0]<len(terrain[0])-1 and terrain[point[1]][point[0]] in floors:
                        visited.add(point); queue.append((point,path+[key]))
            self.assertIsNotNone(route)
            for key in route:
                if e.state['map']['visible'][y][x]=='0': break
                e.key(ord(key))
                while e.state['readiness']!='ready': e.key('enter')
            self.assertEqual(e.state['map']['visible'][y][x],'0')
            view=e.state['dungeon']
            if view['x']<=x<view['x']+view['width'] and view['y']<=y<view['y']+view['height']:
                self.assertTrue(any(o['x']==x and o['y']==y and 'Potion' in o['label'] for o in view['items']),
                                "Unseen remembered piles must retain their labels")

        snapshot = e.call('state.get')['result']
        for _ in range(3):
            preview = e.call('dungeon.route', {'context': snapshot['context'], 'x': x, 'y': y})['result']
            self.assertTrue(preview['reachable'])
            self.assertEqual(preview['path'][-1], [x, y])
            self.assertEqual(e.call('state.get')['result'], snapshot)
        self.assertEqual(e.call('dungeon.route', {'context': 'old', 'x': x, 'y': y})['error']['code'], 'stale_revision')
        self.assertEqual(e.call('dungeon.pickup',{'context':'old','x':x,'y':y})['error']['code'],'stale_revision')
        self.assertEqual(e.call('dungeon.pickup',{'context':e.state['context'],'x':-1,'y':y})['error']['code'],'invalid_argument')
        if interrupt:
            old=e.state['revision']
            context=e.state['context']
            requests=[{'kind':'request','id':'pickup-batch','method':'dungeon.pickup','params':{'context':context,'x':x,'y':y}},
                      {'kind':'request','id':'cancel-batch','method':'terminal.input','params':{'context':context,'key':'escape'}}]
            for request in requests: request['params']['session_id']='session-1'
            e.process.stdin.write(''.join(json.dumps(r)+'\n' for r in requests)); e.process.stdin.flush()
            while 'cancel-batch' not in e.responses: e.receive()
            self.assertIn('result',e.responses['pickup-batch'])
            self.assertIn('result',e.responses['cancel-batch'])
            self.assertFalse(e.travel['active'])
            self.assertTrue(e.travel['interrupted'])
            self.assertIn('cancelled',e.travel['label'])
            e.next_state(old)
            while e.state['readiness']!='ready': e.key('enter')
            self.assertTrue(any(o['location']=='Floor' and o['actual']['kind']==kind for o in e.state['items']))
            stopped=(e.state['player']['x'],e.state['player']['y'])
            e.key(ord('5'))
            while e.state['readiness']!='ready': e.key('enter')
            self.assertEqual((e.state['player']['x'],e.state['player']['y']),stopped)
            self.assertTrue(any(o['location']=='Floor' and o['actual']['kind']==kind for o in e.state['items']))
            return
        self.targeting('dungeon.pickup',x=x,y=y)
        self.assertEqual(e.state['readiness'],'ready',e.screen())
        self.assertEqual((e.state['player']['x'],e.state['player']['y']),(x,y),e.errors)
        self.assertTrue(any(o['location']=='Pack' and o['actual']['kind']==kind for o in e.state['items']),(e.screen(),e.errors))
        self.assertFalse(any(o['location']=='Floor' and o['x']==x and o['y']==y and o['actual']['kind']==kind for o in e.state['items']))

    def test_tunnel_direction_click(self):
        e=self.engine
        e.hello(); e.birth()
        x,y=e.state['player']['x'],e.state['player']['y']
        for semantic in (False,True):
            if semantic:
                old=e.state['revision']
                self.assertIn('result',e.call('command.execute',{'revision':old,'command':'core.tunnel'}))
                e.next_state(old)
            else: e.key(ord('T'))
            self.assertTrue(e.state.get('direction_prompt'),e.screen())
            self.assertIn('dungeon',e.state)
            self.assertFalse(e.state.get('aiming',False))
            self.assertEqual(e.call('targeting.control',{'context':e.state['context'],'operation':'target'})['error']['code'],'invalid_argument')
            self.targeting('targeting.select',x=x+1,y=y)
            while e.state['readiness']!='ready': e.key('enter')
            self.assertFalse(e.state.get('direction_prompt',False))
            self.assertNotIn('targeting',e.state)
        e.key(ord('T'))
        self.targeting('targeting.control',operation='cancel')
        self.assertEqual(e.state['readiness'],'ready')

    def test_immediate_target(self):
        e = self.engine
        e.hello(); e.birth()
        x, y = e.state['player']['x'], e.state['player']['y']
        turn = e.state['turn']
        self.assertEqual(e.call('targeting.set', {'context':'old','x':x,'y':y})['error']['code'], 'stale_revision')
        for params in ({}, {'x':-1,'y':y}, {'x':x+0.5,'y':y}):
            self.assertEqual(e.call('targeting.set', {'context':e.state['context'],**params})['error']['code'], 'invalid_argument')
        self.targeting('targeting.set', x=x+1, y=y)
        self.assertNotIn('targeting', e.state)
        while e.state['readiness'] != 'ready': e.key('enter')
        self.assertEqual((e.state['selected_target']['x'],e.state['selected_target']['y']), (x+1,y))
        self.assertEqual(e.state['turn'],turn)
        self.assert_semantic_view(e.state)
        self.targeting('targeting.begin', mode='look')
        self.assertEqual(e.call('targeting.set', {'context':e.state['context'],'x':x,'y':y})['error']['code'], 'busy')

    def test_native_mouse_walk_and_validation(self):
        e = self.engine
        e.hello(); e.birth()
        x, y = e.state['player']['x'], e.state['player']['y']
        self.assertEqual(e.call('dungeon.click', {'context':'old','x':x,'y':y})['error']['code'], 'stale_revision')
        for bad in (-1, 0, 9999, 1.5):
            self.assertEqual(e.call('dungeon.click', {'context':e.state['context'],'x':bad,'y':y})['error']['code'], 'invalid_argument')
        catalog = e.call('catalog.get')['result']['features']
        floors = {f['id'] for f in catalog if f['name'] in ('open floor', 'open door', 'broken door', 'up staircase', 'down staircase')}
        cells = e.state['map']['actual']
        # Birth starts on the town staircase, surrounded by walkable squares.
        destination = next((x+dx,y+dy) for dx,dy in ((1,0),(-1,0),(0,1),(0,-1))
                           if cells[y+dy][x+dx] in floors)
        turn = e.state['turn']
        self.targeting('dungeon.click', x=destination[0], y=destination[1])
        while e.state['readiness'] != 'ready': e.key('enter')
        self.assertEqual((e.state['player']['x'],e.state['player']['y']), destination)
        self.assertGreater(e.state['turn'],turn)
        self.assert_semantic_view(e.state)
        self.targeting('targeting.begin', mode='look')
        self.assertEqual(e.call('dungeon.click', {'context':e.state['context'],'x':x,'y':y})['error']['code'], 'busy')
        turn = e.state['turn']
        self.targeting('dungeon.click', x=x, y=y, exit_look=True)
        while e.state['readiness'] != 'ready': e.key('enter')
        self.assertNotIn('targeting', e.state)
        self.assertEqual((e.state['player']['x'],e.state['player']['y']), (x,y))
        self.assertGreater(e.state['turn'],turn)
        self.assert_semantic_view(e.state)
        self.targeting('targeting.begin', mode='target')
        self.assertEqual(e.call('dungeon.click', {'context':e.state['context'],'x':x,'y':y,'exit_look':True})['error']['code'], 'busy')

    def test_combat_feedback(self):
        e=self.engine
        e.hello(); e.birth()
        before=e.state; old=before['revision']
        e.call('debug.damage',{'amount':3}); e.next_state(old)
        self.assertEqual(len(e.combat_events),1)
        damage=e.combat_events[0]
        self.assertEqual((damage['kind'],damage['amount'],damage['player']),('damage',3,True))
        self.assertEqual((damage['x'],damage['y']),(before['player']['x'],before['player']['y']))
        self.assertEqual(damage['level_id'],before['dungeon']['level_id'])
        for _ in range(3): e.call('state.get')
        self.assertEqual(len(e.combat_events),1,'Queries must not replay combat feedback')
        # Restore more than the missing HP; report only the actual healing.
        e.key(1); e.key(ord('E'))
        for _ in range(30):
            e.call('state.get')
            if e.prompt:
                p=e.prompt; e.prompt=None; old=e.state['revision']
                text=p.get('text','').lower()
                if p['type']=='confirmation': value=True
                elif p['type']=='quantity': value=0
                elif 'which effect' in text: value='HEAL_HP'
                elif 'dice' in text: value='100'
                elif 'subtype' in text: value='0'
                else: self.fail(f'Unexpected effect prompt: {p}')
                e.call('prompt.reply',{'prompt_id':p['prompt_id'],'value':value}); e.next_state(old)
            elif e.state['readiness']!='ready': e.key('enter')
            else: break
        heals=[v for v in e.combat_events if v['kind']=='heal']
        self.assertEqual(len(heals),1)
        self.assertEqual(heals[0]['amount'],3)

    def test_native_rest(self):
        e = self.engine
        e.hello(); e.birth()
        # Remove nearby town creatures so waking one cannot legitimately end
        # the long rest before its first interruption poll.
        e.key(1); e.key(ord('z'))
        for _ in range(20):
            if e.state['readiness']=='ready': break
            e.call('state.get')
            if e.prompt:
                p=e.prompt; e.prompt=None; old=e.state['revision']
                e.call('prompt.reply',{'prompt_id':p['prompt_id'],'value':True if p['type']=='confirmation' else 20})
                e.next_state(old)
            else: e.key('enter')
        def begin(keyboard=False):
            old=e.state['revision']
            if keyboard: e.key(ord('R'))
            else:
                self.assertIn('result',e.call('command.execute',{'revision':old,'command':'core.rest'}))
                e.next_state(old)
            e.call('state.get')
            self.assertEqual(e.prompt.get('selection_kind'),'rest')
            return e.prompt['prompt_id']
        before=e.state['turn']
        pid=begin(True); old=e.state['revision']; e.prompt=None
        e.call('prompt.reply',{'prompt_id':pid,'value':None}); e.next_state(old)
        self.assertEqual(e.state['turn'],before,'Cancelling the dialog must not spend a turn')
        for choice in ('&','*','!','5'):
            pid=begin(); old=e.state['revision']; e.prompt=None
            e.call('prompt.reply',{'prompt_id':pid,'value':choice}); e.next_state(old)
            while e.state['readiness']!='ready': e.key('enter')
            self.assertEqual(e.state['player']['resting'],0)
            self.assert_semantic_view(e.state)
        pid=begin(); old=e.state['revision']; e.prompt=None
        e.call('prompt.reply',{'prompt_id':pid,'value':'9999'})
        while not any(a.get('resting') for a in e.activity_events) and e.state['revision']==old:
            e.receive()
        self.assertIn('result',e.call('rest.cancel',{'context':e.state['context']}))
        e.next_state(old)
        while e.state['readiness']!='ready': e.key('enter')
        self.assertEqual(e.state['player']['resting'],0)
        self.assertTrue(any(a.get('resting') for a in e.activity_events), (e.activity_events, e.state['turn'], e.state['messages'][:4]))
        self.assertTrue(any('Cancelled' in m['text'] for m in e.state['messages']))

    def test_confused_mouse_walk(self):
        e = self.engine
        e.hello(); e.birth()
        old = e.state['revision']
        self.assertIn('result', e.call('debug.status', {'effect': 'CONFUSED', 'amount': 100}))
        e.next_state(old)
        while e.state['readiness'] != 'ready': e.key('enter')
        # Both direct clicks and clicks exiting look must provide a direction;
        # the engine then randomizes that direction using its normal rules.
        for exit_look in (False, True):
            if exit_look:
                self.targeting('targeting.begin', mode='look')
            p = e.state['player']
            turn = e.state['turn']
            self.targeting('dungeon.click', x=p['x']+1, y=p['y'], exit_look=exit_look)
            self.assertFalse(e.state.get('direction_prompt'), e.screen())
            self.assertNotIn('targeting', e.state)
            self.assertFalse(e.state.get('aiming'), e.screen())
            while e.state['readiness'] != 'ready': e.key('enter')
            self.assertGreater(e.state['turn'], turn)
            self.assert_semantic_view(e.state)

    def test_mouse_disarms_gas_trap(self):
        self.check_gas_trap_action('dungeon.click')

    def test_context_disarms_gas_trap(self):
        self.check_gas_trap_action('dungeon.terrain')

    def check_gas_trap_action(self, method):
        e = self.engine
        e.hello(); e.birth()

        def settle(value=None):
            for _ in range(30):
                if e.state['readiness'] == 'ready': return
                e.call('state.get')
                if e.prompt:
                    p = e.prompt; e.prompt = None
                    old = e.state['revision']
                    e.call('prompt.reply', {'prompt_id': p['prompt_id'],
                           'value': True if p['type'] == 'confirmation' else value})
                    e.next_state(old)
                else:
                    e.key('enter')
            self.fail('Trap fixture did not settle')

        def wizard(key, value=None):
            e.key(1); e.key(ord(key)); settle(value)

        e.key(ord('>')); settle()
        self.assertEqual(e.state['player']['depth'], 1)
        wizard('z')
        floors = {f['id'] for f in e.call('catalog.get')['result']['features']
                  if f['name'] == 'open floor'}
        for attempt in range(5):
            p = e.state['player']; origin = (p['x'], p['y'])
            cells = e.state['map']['actual']
            occupied = {(o['x'], o['y']) for o in e.state['items'] if o['location'] == 'Floor'}
            candidates = [(p['x']+dx, p['y']+dy)
                          for dx,dy in ((1,0),(-1,0),(0,1),(0,-1),(1,1),(-1,-1),(1,-1),(-1,1))
                          if cells[p['y']+dy][p['x']+dx] in floors
                          and (p['x']+dx, p['y']+dy) not in occupied]
            if candidates: break
            # Some random stairs have no empty adjacent floor. Regenerate only
            # this disposable fixture; never weaken the disarming assertions.
            wizard('j', 1); wizard('z')
        self.assertTrue(candidates, 'No empty floor for the gas-trap fixture')
        trap_pos = candidates[0]
        self.targeting('dungeon.click', x=trap_pos[0], y=trap_pos[1]); settle()
        wizard('T', 'gas trap')
        wizard('d')
        self.targeting('dungeon.click', x=origin[0], y=origin[1]); settle()
        args = {'x': trap_pos[0], 'y': trap_pos[1]}
        if method == 'dungeon.terrain':
            action = next(a for a in e.state['terrain_actions'] if (a['x'], a['y']) == trap_pos)
            self.assertEqual(action['action'], 'disarm')
            self.assertIn('Click to attempt disarming', action['hint'])
            args['action'] = 'disarm'
            self.assertEqual(e.call(method, {'context': 'old', **args})['error']['code'], 'stale_revision')
            self.assertEqual(e.call(method, {'context': e.state['context'], **args, 'action': 'open'})['error']['code'], 'invalid_argument')
        self.targeting(method, **args)
        # A disarm attempt stays beside the trap, whether it succeeds, safely
        # fails, or sets it off. Walking into it would move onto its square.
        self.assertEqual((e.state['player']['x'], e.state['player']['y']), origin)
        text = '\n'.join(m['text'] for m in e.state['messages']).lower()
        self.assertTrue('disarm' in text or 'set off the gas trap' in text, text)

    def test_context_door_actions(self):
        e = self.engine
        e.hello(); e.birth()
        # Create ordinary adjacent doors with an existing wizard projection,
        # confined to this disposable character.
        e.key(1); e.key(ord('E'))
        for _ in range(30):
            e.call('state.get')
            if e.prompt:
                p = e.prompt; e.prompt = None; old = e.state['revision']
                text = p.get('text', '').lower()
                if p['type'] == 'confirmation': value = True
                elif p['type'] == 'quantity': value = 0
                elif 'which effect' in text: value = 'TOUCH'
                elif 'dice' in text: value = '0'
                elif 'subtype' in text: value = 'MAKE_DOOR'
                else: self.fail(f'Unexpected effect prompt: {p}')
                e.call('prompt.reply', {'prompt_id': p['prompt_id'], 'value': value})
                e.next_state(old)
            elif e.state['readiness'] != 'ready': e.key('enter')
            else: break
        origin = (e.state['player']['x'], e.state['player']['y'])
        door = next(a for a in e.state['terrain_actions'] if a['action'] == 'open'
                    and max(abs(a['x']-origin[0]), abs(a['y']-origin[1])) == 1)
        x,y = door['x'],door['y']
        self.assertIn('Click to attempt opening', door['hint'])
        for action, following in (('open', 'close'), ('close', 'open')):
            turn = e.state['turn']
            self.targeting('dungeon.terrain', x=x, y=y, action=action)
            while e.state['readiness'] != 'ready': e.key('enter')
            self.assertEqual((e.state['player']['x'], e.state['player']['y']), origin)
            self.assertGreater(e.state['turn'], turn)
            self.assertTrue(any(a['x']==x and a['y']==y and a['action']==following for a in e.state['terrain_actions']))
            self.assertEqual(e.call('dungeon.terrain', {'context':e.state['context'], 'x':x, 'y':y, 'action':action})['error']['code'], 'invalid_argument')
            self.assert_semantic_view(e.state)

    def test_quick_targeting_throw(self):
        e=self.engine
        e.hello(); e.birth()
        before=e.state
        item=next(o for o in before['items'] if o['location']=='Pack')
        kind=item['actual']['kind']
        amount=sum(o['quantity'] for o in before['items'] if o['location']=='Pack' and o['actual']['kind']==kind)
        e.call('command.execute',{'revision':before['revision'],'command':'core.throw','item':item['id']})
        e.next_state(before['revision'])
        self.assertTrue(e.state.get('aiming'))
        p=e.state['player']
        self.targeting('targeting.select',x=p['x']+1,y=p['y'],confirm=True)
        while e.state['readiness']!='ready': e.key('enter')
        self.assertNotIn('targeting',e.state)
        self.assertGreater(e.state['turn'],before['turn'])
        self.assertEqual(sum(o['quantity'] for o in e.state['items'] if o['location']=='Pack' and o['actual']['kind']==kind),amount-1)

    def test_quick_targeting_spell_and_look(self):
        e=self.engine
        e.hello(); e.birth(class_index=1)
        missile=next(s for s in self.spell_book()['spells'] if s['label']=='Magic Missile')
        self.spell_command('core.study',missile['id'])
        while e.state['readiness']!='ready': e.key('enter')
        before=e.state
        self.spell_command('core.cast',missile['id'])
        self.targeting('targeting.control',operation='target')
        self.assertIn('targeting',e.state)
        p=e.state['player']
        self.targeting('targeting.select',x=p['x']+1,y=p['y'],confirm=True)
        while e.state['readiness']!='ready': e.key('enter')
        self.assertNotIn('targeting',e.state)
        self.assertGreater(e.state['turn'],before['turn'])
        self.assertEqual(e.state['player']['sp'],before['player']['sp']-1)
        self.targeting('targeting.begin',mode='look')
        turn=e.state['turn']
        self.targeting('targeting.select',x=p['x']+1,y=p['y'],confirm=True)
        self.assertEqual(e.state['targeting']['mode'],'look')
        self.assertEqual(e.state['turn'],turn)

    def test_native_aim_cancel_preserves_item_and_turn(self):
        e = self.engine
        e.hello(); e.birth()
        before=e.state
        item=next(o for o in before['items'] if o['location']=='Pack')
        e.call('command.execute', {'revision':before['revision'], 'command':'core.throw', 'item':item['id']})
        e.next_state(before['revision'])
        self.assertTrue(e.state.get('aiming'), e.screen())
        self.assertIn('dungeon', e.state)
        x,y=e.state['player']['x'],e.state['player']['y']
        self.targeting('targeting.select', x=x+1,y=y)
        self.assertIn('targeting', e.state)
        self.assertIn('dungeon', e.state)
        self.targeting('targeting.control', operation='cancel')
        self.assertTrue(e.state.get('aiming'))
        self.targeting('targeting.control', operation='cancel')
        self.assertEqual(e.state['turn'], before['turn'])
        self.assertEqual([(o['label'],o['quantity']) for o in e.state['items']], [(o['label'],o['quantity']) for o in before['items']])

    def test_native_ranged_confirmation_uses_engine_action(self):
        e = self.engine
        e.hello(); e.birth()
        before=e.state
        item=next(o for o in before['items'] if o['location']=='Pack')
        kind=item['actual']['kind']
        e.call('command.execute', {'revision':before['revision'], 'command':'core.throw', 'item':item['id']})
        e.next_state(before['revision'])
        self.assertTrue(e.state.get('aiming'))
        self.targeting('targeting.control', operation='target')
        self.targeting('targeting.control', operation='player')
        e.key('right')
        self.assertTrue(e.state['targeting']['can_confirm'])
        self.targeting('targeting.control', operation='confirm')
        for _ in range(20):
            if e.state['readiness']=='ready': break
            e.key('enter')
        self.assertEqual(e.state['readiness'],'ready')
        self.assertGreater(e.state['turn'],before['turn'])
        self.assertTrue(e.projectile_events, 'Throwing must emit visible path feedback')
        batch=e.projectile_events[-1]
        self.assertEqual(batch['level_id'],e.state['dungeon']['level_id'])
        paths=[fx for fx in batch['effects'] if not fx['blast']]
        self.assertTrue(paths)
        self.assertTrue(all(len(tile)==3 for fx in paths for tile in fx['tiles']))
        count=len(e.projectile_events)
        e.call('state.get')
        self.assertEqual(len(e.projectile_events),count,'Queries must not replay paths')
        remaining=sum(o['quantity'] for o in e.state['items'] if o['location']=='Pack' and o['actual']['kind']==kind)
        self.assertEqual(remaining,item['quantity']-1)
        self.assertNotIn('targeting',e.state)
        self.assert_semantic_view(e.state)

    def test_loaded_spell_reminder_keeps_dungeon(self):
        e = self.engine
        e.hello(); e.birth(class_index=8)  # Blackguard, with an unlearned ritual.
        self.assertGreater(e.state["player"]["new_spells"], 0)
        self.assertIn("result", e.call("session.close"))
        e.process.wait(timeout=10); e.stop()
        self.engine = e = Engine(self.temp.name)
        e.hello(); e.call("session.load", {"save": "ProtocolTest"}); e.next_state(None)
        self.assertEqual(e.state["phase"], "playing")
        self.assertIn("dungeon", e.state)
        self.assertNotIn("birth", e.state)
        self.assertIn("ritual", e.screen())
        # A short reminder can remain on the message line; additional startup
        # messages may flush it into a continuation prompt. Both use game UI.
        if e.state.get("message_pending"):
            self.assertEqual(e.state["readiness"], "awaiting_prompt")
            e.key("enter")
        else:
            self.assertEqual(e.state["readiness"], "ready")
        self.assert_semantic_view(e.state)

    def test_message_acknowledgement_keeps_dungeon(self):
        e = self.engine
        e.hello(); e.birth()
        before = e.state
        e.call("debug.damage", {"amount": before["player"]["hp"] - 1})
        e.next_state(before["revision"])
        self.assertTrue(e.state.get("message_pending"), e.screen())
        self.assertIn("dungeon", e.state)
        self.assertEqual(e.state["readiness"], "awaiting_prompt")
        for _ in range(10):
            if not e.state.get("message_pending"):
                break
            e.key(32)
        self.assertFalse(e.state.get("message_pending"))
        self.assert_semantic_view(e.state)

    def setUp(self):
        self.test_root = ARGS.backend.parent / "test-runs"
        self.test_root.mkdir(exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(prefix="deluxe-test-", dir=self.test_root)
        self.engine = Engine(self.temp.name)

    def tearDown(self):
        self.engine.stop()
        Path(self.temp.name).resolve().relative_to(self.test_root.resolve())
        self.temp.cleanup()

    def test_negotiation_and_validation(self):
        e = self.engine
        self.assertEqual(e.call("state.get")["error"]["code"], "unsupported_protocol")
        self.assertEqual(e.call("hello", {"protocols": [{"major": 99, "minor": 0}]})["error"]["code"], "unsupported_protocol")
        e.hello()
        result = e.call("commands.list", request_id="same")
        self.assertGreater(len(result["result"]), 10)
        self.assertEqual(e.call("commands.list", request_id="same")["error"]["code"], "duplicate_id")
        self.assertEqual(e.call("session.new", {"save": "../escape"})["error"]["code"], "invalid_argument")
        self.assertEqual(e.call("session.load", {"save": "missing"})["error"]["code"], "invalid_argument")
        self.assertEqual(e.call("anything")["error"]["code"], "unsupported_capability")

    def test_save_rename_delete(self):
        e = self.engine
        e.hello()
        e.birth()
        e.call("session.close")
        e.process.wait(timeout=10)
        e.stop()
        self.engine = e = Engine(self.temp.name)
        e.hello()
        save_dir = Path(self.temp.name) / "save"
        original = save_dir / "ProtocolTest"
        data = original.read_bytes()
        saved = next(s for s in e.call("saves.list")["result"] if s["id"] == "ProtocolTest")
        self.assertFalse(saved["dead"])
        self.assertEqual(saved["level"], 1)
        self.assertEqual(saved["depth"], 0)
        self.assertIn("Human", saved["identity"])
        self.assertGreater(saved["modified"], 0)
        for name in ("ProtocolTest", "Missing", "../ProtocolTest"):
            self.assertEqual(e.call("session.replay", {"save":name})["error"]["code"], "invalid_argument")
        occupied = save_dir / "Occupied"
        occupied.write_bytes(data)
        for method, params in [
            ("saves.rename", {"save": "../ProtocolTest", "name": "Renamed"}),
            ("saves.rename", {"save": "ProtocolTest", "name": "../escape"}),
            ("saves.rename", {"save": "ProtocolTest", "name": "Occupied"}),
            ("saves.rename", {"save": "Missing", "name": "Renamed"}),
            ("saves.delete", {"save": "../ProtocolTest"}),
            ("saves.delete", {"save": "Missing"}),
        ]:
            self.assertEqual(e.call(method, params)["error"]["code"], "invalid_argument")
        self.assertEqual(original.read_bytes(), data)
        self.assertEqual(occupied.read_bytes(), data)
        self.assertIn("result", e.call("saves.rename", {"save": "ProtocolTest", "name": "Renamed"}))
        self.assertFalse(original.exists())
        self.assertEqual((save_dir / "Renamed").read_bytes(), data)
        saves = e.call("saves.list")["result"]
        self.assertIn("Renamed", [s["id"] for s in saves])
        self.assertNotIn("ProtocolTest", [s["id"] for s in saves])
        e.call("session.load", {"save": "Renamed"})
        e.next_state(None)
        while e.state["readiness"] != "ready":
            e.key("enter")
        self.assertEqual(e.call("saves.delete", {"save": "Renamed"})["error"]["code"], "wrong_phase")
        self.assertEqual(e.call("saves.rename", {"save": "Renamed", "name": "Other"})["error"]["code"], "wrong_phase")
        e.call("session.close")
        e.process.wait(timeout=10)
        e.stop()
        self.engine = e = Engine(self.temp.name)
        e.hello()
        self.assertIn("result", e.call("saves.delete", {"save": "Renamed"}))
        self.assertFalse((save_dir / "Renamed").exists())
        self.assertNotIn("Renamed", [s["id"] for s in e.call("saves.list")["result"]])
        self.assertEqual(occupied.read_bytes(), data)

    def native_birth_action(self, action, **args):
        e = self.engine
        old = e.state["revision"]
        self.assertIn("result", e.call("birth.action", {"revision": old, "action": action, **args}))
        return e.next_state(old)

    def test_native_birth(self):
        e = self.engine
        e.hello()
        self.assertIn("result", e.call("session.new", {"save": "NativeBirth", "native_birth": True}))
        e.next_state(None)
        self.assertEqual(e.state["phase"], "birth")
        initial = e.call("state.get")["result"]
        self.assertEqual(len(initial["birth"]["stats"]), 5)
        self.assertTrue(initial["birth"]["races"])
        self.assertTrue(initial["birth"]["classes"])
        for _ in range(3): self.assertEqual(initial, e.call("state.get")["result"])
        invalid = e.call("birth.action", {"revision": e.state["revision"], "action": "race", "choice": 99999})
        self.assertEqual(invalid["error"]["code"], "invalid_argument")
        mage = next(c for c in initial["birth"]["classes"] if c["name"] == "Mage")
        elf = next(r for r in initial["birth"]["races"] if r["name"] == "Elf")
        self.native_birth_action("race", choice=elf["id"])
        self.native_birth_action("class", choice=mage["id"])
        self.native_birth_action("reset")
        self.assertEqual(e.state["birth"]["points_left"], 20)
        self.assertTrue(all(s["base"] == 10 for s in e.state["birth"]["stats"]))
        before = e.state["revision"]
        self.native_birth_action("buy", choice=1)
        self.assertEqual(e.state["birth"]["stats"][1]["base"], 11)
        self.assertEqual(e.state["birth"]["points_left"], 19)
        self.assertEqual(e.call("birth.action", {"revision": before, "action": "buy", "choice": 1})["error"]["code"], "stale_revision")
        self.native_birth_action("sell", choice=1)
        self.assertEqual(e.state["birth"]["points_left"], 20)
        self.native_birth_action("roll")
        first = e.state["birth"]["stats"]
        self.assertFalse(e.state["birth"]["previous_roll"])
        self.native_birth_action("roll")
        self.assertTrue(e.state["birth"]["previous_roll"])
        self.native_birth_action("previous")
        self.assertEqual(e.state["birth"]["stats"], first)
        self.native_birth_action("suggest")
        opt = next(o for o in e.state["birth"]["options"] if o["id"] == "birth_no_selling")
        self.native_birth_action("option", option=opt["id"], value=not opt["value"])
        self.assertEqual(next(o for o in e.state["birth"]["options"] if o["id"] == opt["id"])["value"], not opt["value"])
        self.native_birth_action("accept", name="Native Hero", history="A test adventurer.")
        for _ in range(20):
            if e.state.get("readiness") == "ready": break
            e.key("enter")
        self.assertEqual(e.state["phase"], "playing")
        self.assertNotIn("birth", e.state)
        self.assertEqual(e.state["player"]["name"], "Native Hero")
        self.assertEqual(e.state["player"]["race"], "Elf")
        self.assertEqual(e.state["player"]["class"], "Mage")
        self.assertEqual(e.state["player"]["character_sheet"]["history"], "A test adventurer.")
        self.assertIn("result", e.call("session.save"))

    def test_native_birth_cancel(self):
        e = self.engine
        e.hello()
        e.call("session.new", {"save": "CancelledBirth", "native_birth": True})
        e.next_state(None)
        self.assertIn("result", e.call("birth.cancel", {"revision": e.state["revision"]}))
        self.assertEqual(e.process.wait(timeout=5), 0)
        self.assertFalse(list(Path(self.temp.name).rglob("CancelledBirth")))

    def test_birth_stat_cursor(self):
        e = self.engine
        e.hello()
        e.call("session.new", {"save": "CursorTest"})
        e.next_state(None)
        for _ in range(20):
            if "Total Cost:" in e.screen():
                break
            e.key("enter")
        else:
            self.fail("Did not reach point-based stat allocation")
        cursor = e.state["cursor"]
        self.assertTrue(cursor["visible"])
        self.assertEqual(cursor["y"], 2)
        self.assertEqual(cursor["x"], 78)
        self.assertIn("STR", e.screen().splitlines()[cursor["y"]])
        e.key("down")
        self.assertTrue(e.state["cursor"]["visible"])
        self.assertEqual(e.state["cursor"]["x"], cursor["x"])
        self.assertEqual(e.state["cursor"]["y"], cursor["y"] + 1)
        self.assertIn("INT", e.screen().splitlines()[e.state["cursor"]["y"]])
        e.key("up")
        self.assertEqual(e.state["cursor"], cursor)

    def test_quit_without_saving(self):
        e=self.engine
        e.hello(); e.birth()
        saved_hp=e.state['player']['hp']
        self.assertIn('result',e.call('session.close'))
        e.process.wait(timeout=10); e.stop()
        save=Path(self.temp.name)/'save'/'ProtocolTest'
        original=save.read_bytes()
        self.engine=e=Engine(self.temp.name)
        e.hello()
        self.assertIn('result',e.call('session.load',{'save':'ProtocolTest'}))
        e.next_state(None)
        while e.state['readiness']!='ready': e.key('enter')
        self.assertEqual(save.read_bytes(),original,'Loading must preserve the save')
        old=e.state['revision']
        e.call('debug.damage',{'amount':1}); e.next_state(old)
        self.assertEqual(e.state['player']['hp'],saved_hp-1)
        self.assertIn('result',e.call('debug.quit'))
        self.assertEqual(e.process.wait(timeout=10),0); e.stop()
        self.assertEqual(save.read_bytes(),original,'Quit without saving must preserve the last save exactly')
        self.engine=e=Engine(self.temp.name)
        e.hello(); e.call('session.load',{'save':'ProtocolTest'}); e.next_state(None)
        while e.state['readiness']!='ready': e.key('enter')
        self.assertEqual(e.state['player']['hp'],saved_hp)

    def test_run_summary_and_replay(self):
        e = self.engine
        e.hello(); e.birth()
        self.assertIn("error", e.call("run.finish"))
        old = e.state["revision"]
        e.call("debug.damage", {"amount": 30000}); e.next_state(old)
        self.assertNotIn("run", e.state, "Fatal message must precede the post-mortem")
        for _ in range(12):
            if e.state["phase"] == "dead": break
            e.key("enter")
        self.assertEqual(e.state["phase"], "dead")
        report = e.state["run"]
        self.assertEqual(report["player"]["name"], e.state["player"]["name"])
        self.assertEqual(report["cause"], "Deluxe developer tools")
        self.assertIn("character_sheet", report["player"])
        self.assertGreaterEqual(report["score"], 0)
        self.assertTrue(report["items"])
        self.assertTrue(report["messages"])
        self.assertTrue(all(i["location"] != "Floor" and "actions" not in i and "id" not in i for i in report["items"]))
        self.assertTrue(all(i["player_known"]["identified"] for i in report["items"]))
        self.assertEqual(report['journal']['entries'][-1]['kind'],'ending')
        self.assertTrue(any(v['kind']=='beginning' for v in report['journal']['entries']))
        self.assertEqual(e.call("state.get")["result"]["run"], report)
        self.assertIn("result", e.call("run.finish"))
        for _ in range(20):
            if e.state["phase"] == "finished": break
            e.receive()
        self.assertEqual(e.state["phase"], "finished")
        self.assertEqual(e.state["run"], report)
        self.assertEqual(e.process.wait(timeout=10), 0); e.stop()
        self.assertTrue((Path(self.temp.name)/"save"/"ProtocolTest").exists(), "Engine must save the dead character normally")
        self.engine = e = Engine(self.temp.name)
        e.hello()
        dead_save = Path(self.temp.name)/"save"/"ProtocolTest"
        original = dead_save.read_bytes()
        listing = e.call("saves.list")["result"]
        saved = next(s for s in listing if s["id"] == "ProtocolTest")
        self.assertTrue(saved["dead"])
        self.assertGreater(saved["modified"], 0)
        self.assertTrue(saved["last_saved"])
        self.assertEqual(dead_save.read_bytes(), original)
        self.assertIn("result", e.call("session.replay", {"save":"ProtocolTest", "native_birth":True}))
        e.next_state(None)
        self.assertIn("birth", e.state, "Completed saves should open native quickstart")
        birth = e.state["birth"]
        self.assertTrue(birth["quickstart"])
        self.assertTrue(birth["rolled"], "Preserved starting stats must not be editable as a fresh point allocation")
        original_stats = [v["base"] for v in birth["stats"]]
        self.native_birth_action("quickstart")
        self.assertEqual([v["base"] for v in e.state["birth"]["stats"]], original_stats)
        self.assertEqual(dead_save.read_bytes(), original, "Opening quickstart must not replace the dead save")
        self.assertIn("result", e.call("birth.cancel", {"revision":e.state["revision"]}))
        self.assertEqual(e.process.wait(timeout=10), 0); e.stop()
        self.assertEqual(dead_save.read_bytes(), original, "Cancelling must preserve the dead save")
        self.engine = e = Engine(self.temp.name); e.hello()
        e.call("session.replay", {"save":"ProtocolTest", "native_birth":True}); e.next_state(None)
        birth=e.state["birth"]
        self.native_birth_action("accept", name=birth["name"] or "ProtocolTest", history=birth["history"])
        for _ in range(20):
            if e.state["readiness"] == "ready": break
            e.key("enter")
        self.assertEqual(e.state["readiness"], "ready")
        self.assertEqual(e.state["player"]["race"], report["player"]["race"])
        self.assertEqual(e.state["player"]["class"], report["player"]["class"])
        self.assertEqual(e.state["player"]["level"], 1)
        self.assertGreater(e.state["player"]["hp"], 0)
        e.call("session.close")
        self.assertEqual(e.process.wait(timeout=10), 0)
        self.assertTrue(dead_save.exists())

    def test_breath_feedback_and_free_test_spell(self):
        e=self.engine; e.hello()
        self.assertIn('error',e.call('debug.breath',{'element':'FIRE'}))
        e.birth()
        for value in ('BOGUS','',4,None):
            self.assertEqual(e.call('debug.breath',{'element':value})['error']['code'],'invalid_argument')
        for element in ('FIRE','COLD','ELEC','ACID','POIS'):
            before=e.state; old=before['revision']
            self.assertIn('result',e.call('debug.breath',{'element':element})); e.next_state(old)
            self.assertTrue(e.state['aiming'])
            self.assertEqual(e.state.get('blast_radius',0),0,'Breath must not advertise a ball preview')
            e.projectile_events.clear(); e.key(ord('6'))
            for _ in range(20):
                e.state=e.call('state.get')['result']
                if e.state['readiness']=='ready': break
                e.key('enter')
            self.assertEqual(e.state['turn'],before['turn'])
            self.assertEqual(e.state['player']['sp'],before['player']['sp'])
            cones=[fx for batch in e.projectile_events for fx in batch['effects'] if fx.get('arc')]
            self.assertTrue(cones,'Native breath must emit cone metadata')
            x,y=before['player']['x'],before['player']['y']
            for fx in cones:
                self.assertEqual(fx['element'],element)
                self.assertTrue(fx['blast'])
                self.assertTrue(fx['tiles'])
                for tx,ty,distance in fx['tiles']:
                    self.assertGreaterEqual(tx,x,'Eastward breath cannot expand behind the caster')
                    self.assertLessEqual(abs(ty-y),(tx-x)*.7+1)
                    self.assertLessEqual(distance,12)
            count=len(e.projectile_events); e.call('state.get')
            self.assertEqual(len(e.projectile_events),count)
        old=e.state['revision']; e.call('debug.breath',{'element':'FIRE'}); e.next_state(old)
        e.projectile_events.clear(); e.key('escape')
        for _ in range(20):
            if e.state['readiness']=='ready': break
            e.key('escape')
        self.assertFalse(e.projectile_events,'Cancelling breath must not project')

    def test_blast_preview_and_free_test_spell(self):
        e=self.engine
        e.hello(); e.birth()
        initial=e.state
        for radius in (0,21,2.5,"2"):
            self.assertEqual(e.call('debug.blast',{'radius':radius})['error']['code'],'invalid_argument')
        for radius in (1,2,3):
            old=e.state['revision']; mana=e.state['player']['sp']; turn=e.state['turn']
            self.assertIn('result',e.call('debug.blast',{'radius':radius}))
            e.next_state(old)
            self.assertTrue(e.state['aiming'])
            self.assertEqual(e.state['blast_radius'],radius)
            p=e.state['player']; x,y=p['x'],p['y']
            params={'context':e.state['context'],'x':x,'y':y}
            preview=e.call('targeting.blast',params)['result']
            self.assertTrue(preview['tiles'])
            self.assertEqual(e.call('targeting.blast',params)['result'],preview)
            self.assertEqual(e.call('targeting.blast',{**params,'context':'old'})['error']['code'],'stale_revision')
            self.assertEqual(e.call('targeting.blast',{**params,'x':-1})['error']['code'],'invalid_argument')
            self.assertEqual(e.state['turn'],turn)
            self.assertEqual(e.state['player']['sp'],mana)
            e.projectile_events.clear()
            self.targeting('targeting.select',x=x,y=y,confirm=True)
            while e.state['readiness']!='ready': e.key('enter')
            self.assertEqual(e.state['player']['sp'],mana)
            self.assertEqual(e.state['turn'],turn,'Dev cast is free and advances no game turns')
            self.assertNotIn('blast_radius',e.state)
            blasts=[fx for batch in e.projectile_events for fx in batch['effects'] if fx['blast']]
            self.assertTrue(blasts)
            self.assertFalse(any(fx.get('arc') for fx in blasts),'Balls stay explosions')
            actual={(p[0],p[1]) for fx in blasts for p in fx['tiles']}
            expected={tuple(p) for p in preview['tiles']}
            self.assertEqual(actual,expected,'Preview must match the visible native explosion')
        old=e.state['revision']; e.call('debug.blast',{'radius':5}); e.next_state(old)
        e.projectile_events.clear(); e.key('escape')
        while e.state['readiness']!='ready': e.key('escape')
        self.assertFalse(e.projectile_events,'Cancelling must never cast a spell')
        self.assertNotIn('blast_radius',e.state)
        self.assertEqual(e.state['player']['sp'],initial['player']['sp'])

    def test_blast_preview_walls(self):
        e=self.engine
        e.hello(); e.birth()
        d=e.state['dungeon']; player=e.state['player']
        visible={(d['x']+x,d['y']+y) for y,row in enumerate(d['cells']) for x,cell in enumerate(row) if cell[10]}
        walls=[(d['x']+x,d['y']+y) for y,row in enumerate(d['cells']) for x,cell in enumerate(row) if cell[0]==ord('#') and cell[10] and 0<d['x']+x<len(e.state['map']['actual'][0])-1 and 0<d['y']+y<len(e.state['map']['actual'])-1]
        self.assertTrue(walls)
        x,y=min(walls,key=lambda p:abs(p[0]-player['x'])+abs(p[1]-player['y']))
        old=e.state['revision']; e.call('debug.blast',{'radius':3}); e.next_state(old)
        params={'context':e.state['context'],'x':x,'y':y}
        result=e.call('targeting.blast',params)
        self.assertIn('result',result,repr((x,y,e.state.get('blast_radius'),result)))
        preview=result['result']
        self.assertNotIn([x,y],preview['tiles'],'A wall stops the ball before impact')
        e.projectile_events.clear()
        self.targeting('targeting.select',x=x,y=y,confirm=True)
        for _ in range(20):
            if e.state['readiness']=='ready': break
            e.key('enter')
        self.assertEqual(e.state['readiness'],'ready')
        actual={(p[0],p[1]) for batch in e.projectile_events for fx in batch['effects'] if fx['blast'] for p in fx['tiles']}
        self.assertEqual(actual,{tuple(p) for p in preview['tiles']} & visible)

    def test_native_ball_spell_preview(self):
        e=self.engine
        e.hello(); e.birth(class_index=1)
        old=e.state['revision']; e.call('debug.experience',{'amount':1000}); e.next_state(old)
        for _ in range(20):
            if e.state['readiness']=='ready': break
            e.key('enter')
        self.assertEqual(e.state['readiness'],'ready',e.screen())
        spell=next(s for s in self.spell_book()['spells'] if s['label']=='Fire Ball')
        self.spell_command('core.study',spell['id'])
        for _ in range(20):
            if e.state['readiness']=='ready': break
            e.key('enter')
        self.assertEqual(e.state['readiness'],'ready',e.screen())
        self.spell_command('core.cast',spell['id'])
        if e.prompt: self.store_reply(True)
        self.assertEqual(e.state['blast_radius'],2)
        p=e.state['player']
        self.assertTrue(e.call('targeting.blast',{'context':e.state['context'],'x':p['x'],'y':p['y']})['result']['tiles'])
        e.key('escape')
        for _ in range(20):
            if e.state['readiness']=='ready': break
            e.key('escape')
        self.assertEqual(e.state['readiness'],'ready',e.screen())
        self.assertNotIn('blast_radius',e.state)

    def test_run_journal_persistence(self):
        e=self.engine
        e.hello()
        self.assertEqual(e.call('journal.get')['error']['code'],'wrong_phase')
        e.birth()
        before=e.call('journal.get')['result']
        self.assertTrue(any(v['kind']=='beginning' for v in before['entries']))
        old=e.state['revision']; e.call('debug.experience',{'amount':1000}); e.next_state(old)
        for _ in range(30):
            if e.state['readiness']=='ready': break
            e.key('enter')
        journal=e.call('journal.get')['result']
        levels=[v['level'] for v in journal['entries'] if v['kind']=='level']
        self.assertEqual(levels,list(range(2,e.state['player']['level']+1)))
        state=e.call('state.get')['result']
        for _ in range(5): self.assertEqual(e.call('journal.get')['result'],journal)
        self.assertEqual(e.call('state.get')['result'],state,'Reading the journal is observational')
        e.call('session.close'); e.process.wait(timeout=10); e.stop()
        self.engine=e=Engine(self.temp.name); e.hello()
        e.call('session.load',{'save':'ProtocolTest'}); e.next_state(None)
        for _ in range(30):
            if e.state['readiness']=='ready': break
            e.key('enter')
        self.assertEqual(e.call('journal.get')['result'],journal,'Native saves preserve the journal without duplicate load milestones')

    def test_blink_feedback(self):
        e=self.engine; e.hello(); e.birth()
        start=e.state['player'].copy(); turn=e.state['turn']
        view=e.state['dungeon']
        visible={(x+view['x'],y+view['y']) for y,row in enumerate(view['cells']) for x,cell in enumerate(row) if cell[10]}
        old=e.state['revision']; self.assertIn('result',e.call('debug.blink')); e.next_state(old)
        for _ in range(30):
            e.state=e.call('state.get')['result']
            if e.prompt:
                p=e.prompt; e.prompt=None; old=e.state['revision']
                self.assertEqual(p['type'],'confirmation')
                e.call('prompt.reply',{'prompt_id':p['prompt_id'],'value':True}); e.next_state(old)
            elif e.state['readiness']!='ready': e.key('enter')
            else: break
        blinks=[fx for batch in e.motion_events for fx in batch['effects'] if fx['blink']]
        self.assertEqual(len(blinks),2)
        self.assertEqual(e.state['player']['sp'],start['sp'])
        self.assertEqual(e.state['turn'],turn,'Test Blink costs no turn')
        blink=blinks[0]
        self.assertEqual((blink['x'],blink['y']),(start['x'],start['y']))
        self.assertEqual((blink['tx'],blink['ty']),(start['x'],start['y']),'Departure ripple contains only its own centre')
        self.assertTrue(blink['tiles'])
        self.assertTrue(all(tuple(tile) in visible for tile in blink['tiles']))
        self.assertTrue(all((x-start['x'])**2+(y-start['y'])**2<=9 for x,y in blink['tiles']))
        arrival=blinks[1]; end=e.state['player']; view=e.state['dungeon']
        visible_after={(x+view['x'],y+view['y']) for y,row in enumerate(view['cells']) for x,cell in enumerate(row) if cell[10]}
        self.assertEqual((arrival['x'],arrival['y']),(end['x'],end['y']))
        self.assertEqual((arrival['tx'],arrival['ty']),(end['x'],end['y']))
        self.assertTrue(arrival['tiles'])
        self.assertTrue(all(tuple(tile) in visible_after for tile in arrival['tiles']))
        self.assertTrue(all((x-end['x'])**2+(y-end['y'])**2<=9 for x,y in arrival['tiles']))
        count=len(e.motion_events); e.call('state.get'); e.call('state.get')
        self.assertEqual(len(e.motion_events),count,'Queries do not replay animations')

    def test_debug_experience(self):
        e=self.engine
        e.hello()
        self.assertIn('error',e.call('debug.experience',{'amount':100}))
        e.birth()
        for amount in (0,-1,1.5,100000000,'100',True):
            self.assertEqual(e.call('debug.experience',{'amount':amount})['error']['code'],'invalid_argument')
        before=e.state
        amount=before['player']['next_level_experience']
        self.assertIn('result',e.call('debug.experience',{'amount':amount}))
        e.next_state(before['revision'])
        for _ in range(20):
            if e.state['readiness']=='ready': break
            e.key('enter')
        self.assertEqual(e.state['player']['experience'],before['player']['experience']+amount)
        self.assertGreater(e.state['player']['level'],before['player']['level'])
        self.assertEqual(e.state['turn'],before['turn'])

    def test_debug_damage(self):
        e = self.engine
        e.hello()
        self.assertIn("error", e.call("debug.damage", {"amount": 1}))
        e.birth()
        for amount in (0, -1, 1.5, 30001, "10"):
            self.assertEqual(e.call("debug.damage", {"amount": amount})["error"]["code"], "invalid_argument")
        before = e.state
        self.assertIn("result", e.call("debug.damage", {"amount": 1}))
        after = e.next_state(before["revision"])
        self.assertEqual(after["player"]["hp"], before["player"]["hp"] - 1)
        self.assertEqual(after["turn"], before["turn"])
        self.assertIn("result", e.call("debug.damage", {"amount": 30000}))
        e.next_state(after["revision"])
        self.assertTrue(e.state["player"]["death_pending"])
        for _ in range(10):
            if e.state["phase"] == "dead":
                break
            e.key("enter")
        self.assertEqual(e.state["phase"], "dead")

    def test_play_queries_save_reload(self):
        e = self.engine
        e.hello()
        e.birth()
        initial = e.call("state.get")["result"]
        self.assertIn("player", initial)
        self.assertEqual(initial["player"]["level_start_experience"], 0)
        sheet = initial["player"]["character_sheet"]
        self.assertEqual(len(sheet["attributes"]), 5)
        self.assertEqual(len(sheet["resistances"]), 13)
        self.assertEqual({r["group"] for r in sheet["rows"]},
                         {"Identity", "Background", "Progression", "Combat", "Skills"})
        self.assertTrue(sheet["history"])
        self.assertTrue(any(r["label"] == "Blows" for r in sheet["rows"]))

        self.assertEqual(initial["player"]["depth_feet"], initial["player"]["depth"] * 50)
        self.assertEqual(initial["player"]["feeling_description"], "Looks like a typical town.")
        self.assertGreater(initial["player"]["next_level_experience"], 0)
        self.assertEqual(initial["player"]["hp_warning"], initial["player"]["max_hp"] * 3 // 10)
        self.assertFalse(initial["player"]["death_pending"])
        self.assertGreater(initial["player"]["food_max"], 0)
        self.assertGreater(initial["player"]["food"], 0)
        self.assertLessEqual(initial["player"]["food"], initial["player"]["food_max"])
        food_status = next(s for s in initial["player"]["statuses"] if s["label"] == "FOOD")
        self.assertEqual(initial["player"]["food"], food_status["duration"])
        self.assertGreater(len(initial["items"]), 0)
        self.assertIn("actual", initial["items"][0])
        potion = next(i for i in initial["items"] if "Potion" in i["label"])
        self.assertNotIn("core.wield", potion["actions"])
        self.assertIn("core.quaff", potion["actions"])
        self.assertTrue(potion["binding_key"])
        self.assertEqual(potion["category"], "potion")
        self.assertIn("core.drop", potion["actions"])
        self.assertIn("An inheritance from your family", potion["description"])
        self.assertIn("When quaffed", potion["description"])
        weapon = next(i for i in initial["items"] if i["location"] == "weapon")
        self.assertIn("Combat info", weapon["description"])
        combat = weapon["combat_details"]
        self.assertTrue(any(r["kind"] == "damage" for r in combat))
        blows = next(r for r in combat if r["kind"] == "blows")
        shown_blows = f'{blows["value"] // 100}.{(blows["value"] // 10) % 10}'
        self.assertIn(shown_blows + " blow", weapon["description"])
        for row in combat:
            if row["kind"] == "upgrade" and row["value"] % 10 == 0:
                self.assertIn(f'With +{row["str"]} STR and +{row["dex"]} DEX', weapon["description"])
        sections = {s["id"]: s["text"] for s in weapon["description_sections"]}
        self.assertIn("Combat info", sections["combat"])
        self.assertIn("An inheritance from your family", sections["lore"])
        for section in weapon["description_sections"]:
            self.assertIn(section["text"], weapon["description"])
        self.assertTrue(any("When quaffed" in s["text"] and s["id"] == "use"
                            for s in potion["description_sections"]))

        for _ in range(10):
            self.assertEqual(e.call("state.get")["result"], initial)
        stale = e.call("command.execute", {"revision": "0", "command": "core.hold"})
        self.assertEqual(stale["error"]["code"], "stale_revision")
        self.assertEqual(e.call("state.get")["result"], initial)
        old = initial["revision"]
        result = e.call("command.execute", {"revision": old, "command": "core.hold"})
        self.assertIn("result", result)
        e.next_state(old)
        # A message acknowledgement is an ordinary terminal prompt.
        for _ in range(10):
            if e.state["readiness"] == "ready": break
            e.key("enter")
        self.assertGreater(e.state["turn"], initial["turn"])
        final = e.state["player"]
        self.assertIn("result", e.call("session.save"))
        self.assertIn("result", e.call("session.close"))
        e.process.wait(timeout=10)
        e.stop()
        self.engine = e = Engine(self.temp.name)
        e.hello()
        saves = e.call("saves.list")["result"]
        self.assertTrue(any(s["id"] == "ProtocolTest" for s in saves))
        self.assertIn("result", e.call("session.load", {"save": "ProtocolTest"}))
        e.next_state(None)
        for _ in range(10):
            if e.state["readiness"] == "ready": break
            e.key("enter")
        self.assertEqual(e.state["player"]["name"], final["name"])
        self.assertEqual(e.state["player"]["hp"], final["hp"])
        reloaded = next(i for i in e.state["items"] if "Potion" in i["label"])
        self.assertEqual(reloaded["binding_key"], potion["binding_key"])

    def test_stairs_and_dungeon_inspection(self):
        e = self.engine
        e.hello()
        e.birth()
        self.assert_semantic_view(e.state)
        town_level = e.state["dungeon"]["level_id"]
        catalog = e.call("catalog.get")["result"]["features"]
        floors = {f["id"] for f in catalog if f["name"] in
                  ("open floor", "open door", "broken door", "up staircase", "down staircase")}
        stairs = next(f["id"] for f in catalog if f["name"] == "down staircase")
        # Navigate an ordinary new character through town using real movement.
        for _ in range(150):
            s = e.call("state.get")["result"]
            self.assert_semantic_view(s)
            terrain = s["map"]["actual"]
            start = (s["player"]["x"], s["player"]["y"])
            if terrain[start[1]][start[0]] == stairs:
                break
            frontier = deque([(start, [])])
            seen = {start}
            path = None
            while frontier:
                (x, y), keys = frontier.popleft()
                if terrain[y][x] == stairs:
                    path = keys
                    break
                for dx, dy, key in [(1,0,"6"),(-1,0,"4"),(0,1,"2"),(0,-1,"8"),
                                    (1,1,"3"),(-1,1,"1"),(1,-1,"9"),(-1,-1,"7")]:
                    pos = (x + dx, y + dy)
                    if (pos not in seen and 0 <= pos[1] < len(terrain)
                            and 0 <= pos[0] < len(terrain[0]) and terrain[pos[1]][pos[0]] in floors):
                        seen.add(pos)
                        frontier.append((pos, keys + [key]))
            self.assertTrue(path, "No path to town stairs")
            e.key(ord(path[0]))
            while e.state["readiness"] != "ready":
                e.key("enter")
        else:
            self.fail("Did not reach town stairs")
        down=next(a for a in e.state['terrain_actions'] if a['x']==e.state['player']['x'] and a['y']==e.state['player']['y'])
        self.assertEqual(down['action'],'down')
        self.assertEqual(e.call('dungeon.terrain',{'context':'old',**down})['error']['code'],'stale_revision')
        self.assertEqual(e.call('dungeon.terrain',{'context':e.state['context'],**down,'action':'tunnel'})['error']['code'],'invalid_argument')
        self.targeting('dungeon.terrain',**down)
        while e.state["readiness"] != "ready":
            e.key("enter")
        depth_entries=[v for v in e.call('journal.get')['result']['entries'] if v['kind']=='depth']
        self.assertEqual(len([v for v in depth_entries if v['depth']==1]),1)
        dungeon = e.call("state.get")["result"]
        self.assertEqual(dungeon["player"]["depth"], 1)
        self.assert_semantic_view(dungeon)
        self.assertNotEqual(dungeon["dungeon"]["level_id"], town_level)
        self.assertTrue(any(i["location"] == "Floor" for i in dungeon["items"]))
        for item in dungeon["items"]:
            self.assertEqual(e.call("inspect.get", {"handle": item["id"]})["result"], item)
        self.assertEqual(e.call("state.get")["result"], dungeon)
        # Look beyond the original viewport: camera movement and cancellation
        # must stay semantic and must not move the player or advance the turn.
        self.targeting('targeting.begin',mode='look')
        far_x=len(dungeon['map']['actual'][0])-2
        far_y=len(dungeon['map']['actual'])-2
        self.targeting('targeting.select',x=far_x,y=far_y)
        view=e.state['dungeon']
        self.assertLessEqual(view['x'],far_x)
        self.assertLess(far_x,view['x']+view['width'])
        self.assertLessEqual(view['y'],far_y)
        self.assertLess(far_y,view['y']+view['height'])
        self.assertEqual((e.state['targeting']['x'],e.state['targeting']['y']),(far_x,far_y))
        self.targeting('targeting.control',operation='cancel')
        self.assertEqual(e.state['turn'],dungeon['turn'])
        self.assert_semantic_view(e.state)
        up=next(a for a in e.state['terrain_actions'] if a['action']=='up' and a['x']==e.state['player']['x'] and a['y']==e.state['player']['y'])
        self.targeting('dungeon.terrain',**up)
        while e.state["readiness"] != "ready":
            e.key("enter")
        self.assertEqual(e.state["player"]["depth"], 0)
        self.assert_semantic_view(e.state)
        self.assertNotEqual(e.state["dungeon"]["level_id"], dungeon["dungeon"]["level_id"])
        # Exercise tunnelling through the same context-action API on a fresh
        # dungeon. Select the closest observed diggable wall, never permanent rock.
        self.targeting('dungeon.terrain',x=e.state['player']['x'],y=e.state['player']['y'],action='down')
        while e.state['readiness']!='ready': e.key('enter')
        # Give this disposable character a real chance to dig granite and clear
        # nearby monsters, so this checks completion rather than futile digging
        # or a legitimate danger interruption.
        for wizard_key in ('A','z'):
            e.key(1); e.key(ord(wizard_key))
            for _ in range(20):
                if e.state['readiness']=='ready': break
                e.call('state.get')
                if e.prompt:
                    prompt=e.prompt; e.prompt=None; old=e.state['revision']
                    value=True if prompt['type']=='confirmation' else 20
                    e.call('prompt.reply',{'prompt_id':prompt['prompt_id'],'value':value})
                    e.next_state(old)
                else: e.key('enter')
        px,py=e.state['player']['x'],e.state['player']['y']
        walls=[a for a in e.state['terrain_actions'] if a['action']=='tunnel']
        self.assertTrue(walls)
        wall=min(walls,key=lambda a:max(abs(a['x']-px),abs(a['y']-py)))
        original_feature=e.state['map']['actual'][wall['y']][wall['x']]
        turn=e.state['turn']
        self.targeting('dungeon.terrain',**wall)
        while e.state['readiness']!='ready': e.key('enter')
        self.assertGreater(e.state['turn'],turn)
        self.assertLessEqual(max(abs(e.state['player']['x']-wall['x']),abs(e.state['player']['y']-wall['y'])),1)
        self.assertNotEqual(e.state['map']['actual'][wall['y']][wall['x']],original_feature,e.screen())

    def enter_store(self, name):
        e = self.engine
        catalog = e.call('catalog.get')['result']['features']
        target = next(f['id'] for f in catalog if f['name'] == name)
        floors = {f['id'] for f in catalog if f['name'] in
                  ('open floor', 'open door', 'broken door', 'up staircase', 'down staircase')}
        floors.add(target)
        for _ in range(220):
            e.state = e.call('state.get')['result']
            if 'store' in e.state:
                self.assertEqual(e.state['store']['name'], name)
                return
            if e.state['readiness'] != 'ready':
                e.key('enter')
                continue
            terrain = e.state['map']['actual']
            start = (e.state['player']['x'], e.state['player']['y'])
            frontier = deque([(start, [])]); seen = {start}; route = None
            while frontier:
                (x,y), path = frontier.popleft()
                if terrain[y][x] == target and path:
                    route = path; break
                for dx,dy,key in [(1,0,'6'),(-1,0,'4'),(0,1,'2'),(0,-1,'8'),
                                  (1,1,'3'),(-1,1,'1'),(1,-1,'9'),(-1,-1,'7')]:
                    pos = x+dx,y+dy
                    if pos not in seen and 0 <= pos[1] < len(terrain) and 0 <= pos[0] < len(terrain[0]) and terrain[pos[1]][pos[0]] in floors:
                        seen.add(pos); frontier.append((pos,path+[key]))
            self.assertTrue(route, 'No route to '+name)
            e.key(ord(route[0]))
        self.fail('Did not enter '+name)

    def store_action(self, method, item=None):
        e = self.engine
        args = {'context':e.state['context']}
        if item: args['item'] = item
        old = e.state['revision']
        self.assertIn('result', e.call(method,args))
        e.next_state(old)
        e.state = e.call('state.get')['result']

    def store_reply(self, value):
        e = self.engine
        self.assertIsNotNone(e.prompt)
        old = e.state['revision']; prompt = e.prompt; e.prompt = None
        self.assertIn('result',e.call('prompt.reply',{'prompt_id':prompt['prompt_id'],'value':value}))
        e.next_state(old)
        e.state = e.call('state.get')['result']

    def finish_store_prompts(self, confirm=True):
        e = self.engine
        for _ in range(20):
            if e.prompt:
                self.store_reply(confirm if e.prompt['type']=='confirmation' else 1)
            elif e.state.get('message_pending'):
                e.key('enter'); e.state=e.call('state.get')['result']
            elif e.state.get('store',{}).get('ready'):
                return
            else:
                e.receive()
        self.fail('Store did not finish')

    def test_native_store_transactions(self):
        e=self.engine
        e.hello(); e.birth(); self.enter_store('General Store')
        shop=e.state['store']; before=e.state['player']['gold']
        records={o['id']:o for o in e.state['items']}
        entry=next(q for q in shop['stock'] if 'Ration' in records[q['item_id']]['label'])
        kind=records[entry['item_id']]['actual']['kind']; price=entry['unit_price']
        amount=lambda:sum(o['quantity'] for o in e.state['items'] if o['location']=='Pack' and o['actual']['kind']==kind)
        original=amount()
        self.assertTrue(records[entry['item_id']]['description'])
        self.assertEqual(e.call('store.buy',{'context':'stale','item':entry['item_id']})['error']['code'],'stale_revision')
        inventory=shop['inventory'][0]['item_id']
        self.assertEqual(e.call('store.buy',{'context':e.state['context'],'item':inventory})['error']['code'],'invalid_argument')
        self.store_action('store.buy',entry['item_id'])
        self.assertEqual(e.prompt['type'],'quantity')
        self.assertIn('Ration',e.prompt['item']['label'])
        self.assertEqual(e.prompt['purchase_totals'],[price*n for n in range(1,e.prompt['maximum']+1)])
        self.assertEqual(e.prompt['gold'],before)
        self.assertFalse(e.state['store']['ready'])
        self.assertEqual(e.call('store.leave',{'context':e.state['context']})['error']['code'],'busy')
        self.store_reply(None)
        self.assertTrue(e.state['store']['ready']); self.assertEqual(amount(),original)
        for confirm in (False,True):
            records={o['id']:o for o in e.state['items']}
            entry=next(q for q in e.state['store']['stock'] if records[q['item_id']]['actual']['kind']==kind)
            self.store_action('store.buy',entry['item_id']); self.store_reply(1)
            self.assertEqual(e.prompt['type'],'confirmation')
            self.assertIn('Buy',e.prompt['text'])
            self.assertIn('Ration',e.prompt['text'])
            self.assertIn(f'Price: {price} gold',e.prompt['text'])
            self.finish_store_prompts(confirm)
            self.assertEqual(e.state['player']['gold'],before-(price if confirm else 0))
            self.assertEqual(amount(),original+(1 if confirm else 0))
        # Sell/give that item back through the engine's actual no-selling policy.
        records={o['id']:o for o in e.state['items']}
        entry=next(q for q in e.state['store']['inventory'] if records[q['item_id']]['actual']['kind']==kind and q['eligible'])
        sale=entry['unit_price']
        self.store_action('store.sell',entry['item_id']); self.finish_store_prompts()
        self.assertEqual(amount(),original)
        self.assertEqual(e.state['player']['gold'],before-price+sale)
        self.store_action('store.leave')
        while e.state['readiness']!='ready': e.key('enter')
        self.assertNotIn('store',e.state); self.assertIn('dungeon',e.state)

    def test_native_home_and_equipment_comparison(self):
        e=self.engine
        e.hello(); e.birth(); self.enter_store('Home')
        before=e.state['player']['gold']
        records={o['id']:o for o in e.state['items']}
        entry=next(q for q in e.state['store']['inventory'] if records[q['item_id']]['location']=='Pack' and 'Ration' in records[q['item_id']]['label'])
        kind=records[entry['item_id']]['actual']['kind']
        self.store_action('store.sell',entry['item_id']); self.finish_store_prompts()
        self.assertTrue(e.state['store']['home'])
        self.assertEqual(len(e.state['store']['stock']),1)
        item=next(o for o in e.state['items'] if o['location']=='Home')
        self.assertEqual(item['actual']['kind'],kind); self.assertEqual(item['quantity'],1)
        self.store_action('store.buy',item['id']); self.finish_store_prompts()
        self.assertFalse(e.state['store']['stock']); self.assertEqual(e.state['player']['gold'],before)
        self.store_action('store.leave')
        self.enter_store('Weapon Smiths')
        records={o['id']:o for o in e.state['items']}
        comparisons=[q for q in e.state['store']['stock'] if q['compare_with']]
        self.assertTrue(comparisons)
        current = e.call("state.get")["result"]
        quote = comparisons[0]
        preview = e.call("item.compare", {"revision": current["revision"], "item": quote["item_id"]})["result"]
        self.assertTrue(preview["options"])
        self.assertTrue(any(r["id"] == "weight" and r["delta"] > 0
                            for r in preview["options"][0]["metrics"]))
        self.assertEqual(e.call("state.get")["result"], current)

        for quote in comparisons:
            for handle in quote['compare_with']:
                self.assertEqual(records[handle]['location'],'weapon')
                self.assertTrue(records[handle]['description'])

    def test_native_item_preferences(self):
        e = self.engine
        e.hello(); e.birth()
        potion = next(o for o in e.state["items"] if "Potion" in o["label"])
        kind = potion["actual"]["kind"]
        def current():
            return next(o for o in e.state["items"] if o["actual"]["kind"] == kind)
        def apply(operation, **values):
            old = e.state["revision"]
            result = e.call("item.preferences", {"revision": old, "item": current()["id"], "operation": operation, **values})
            self.assertIn("result", result)
            e.next_state(old)
            while e.state["readiness"] != "ready": e.key("enter")
        apply("autoinscribe", text="!d")
        self.assertEqual(current()["inscription"], "!d")
        self.assertEqual(current()["preferences"]["autoinscription"], "!d")
        # Updating the auto rule preserves an existing manual/nonempty inscription.
        apply("autoinscribe", text="!q")
        self.assertEqual(current()["inscription"], "!d")
        self.assertEqual(current()["preferences"]["autoinscription"], "!q")
        apply("ignore_item", enabled=True)
        self.assertTrue(current()["preferences"]["item_ignored"])
        apply("ignore_item", enabled=False)
        self.assertFalse(current()["preferences"]["item_ignored"])
        self.assertTrue(current()["preferences"]["kind_allowed"])
        apply("ignore_kind", enabled=True)
        self.assertTrue(current()["preferences"]["kind_ignored"])
        rules = e.call("item.rules.list")["result"]
        ignored = next(r for r in rules["rules"] if r["type"] == "kind")
        old = e.state["revision"]
        self.assertIn("result", e.call("item.rules.clear", {"revision": old, "rule": ignored["id"]}))
        e.next_state(old)
        while e.state["readiness"] != "ready": e.key("enter")
        self.assertFalse(current()["preferences"]["kind_ignored"])
        self.assertTrue(any(r["type"] == "auto" for r in e.call("item.rules.list")["result"]["rules"]))
        apply("autoinscribe", text="")
        self.assertFalse(any(r["type"] == "auto" for r in e.call("item.rules.list")["result"]["rules"]))
        self.assertEqual(current()["inscription"], "!d")
        self.assertEqual(e.call("item.preferences", {"revision": "0", "item": current()["id"], "operation": "ignore_item", "enabled": True})["error"]["code"], "stale_revision")

    def test_native_item_comparison(self):
        e = self.engine
        e.hello(); e.birth()
        initial = e.call("state.get")["result"]
        weapon = next(o for o in initial["items"] if o["location"] == "weapon")
        params = {"revision": initial["revision"], "item": weapon["id"]}
        same = e.call("item.compare", params)["result"]
        self.assertTrue(same["options"])
        self.assertTrue(all(r["delta"] == 0 for r in same["options"][0]["metrics"]))
        self.assertFalse(same["options"][0]["changes"])
        for _ in range(3):
            self.assertEqual(e.call("item.compare", params)["result"], same)
            self.assertEqual(e.call("state.get")["result"], initial)
        self.assertEqual(e.call("item.compare", {**params, "revision": "0"})["error"]["code"], "stale_revision")
        self.assertEqual(e.call("item.compare", {**params, "item": "bad"})["error"]["code"], "stale_handle")
        armour = next(o for o in initial["items"] if o["location"] == "body")
        kind = armour["actual"]["kind"]
        old = e.state["revision"]
        self.assertIn("result", e.call("command.execute", {"revision": old, "command": "core.takeoff", "item": armour["id"]}))
        e.next_state(old)
        while e.state["readiness"] != "ready": e.key("enter")
        armour = next(o for o in e.state["items"] if o["actual"]["kind"] == kind)
        baseline = e.call("state.get")["result"]
        prediction = e.call("item.compare", {"revision": baseline["revision"], "item": armour["id"]})["result"]
        metrics = {r["id"]: r for r in prediction["options"][0]["metrics"]}
        self.assertGreater(metrics["armour"]["delta"], 0)
        self.assertEqual(metrics["weight"]["delta"], 0)
        self.assertEqual(e.call("state.get")["result"], baseline)
        old = e.state["revision"]
        self.assertIn("result", e.call("command.execute", {"revision": old, "command": "core.wield", "item": armour["id"]}))
        e.next_state(old)
        while e.state["readiness"] != "ready": e.key("enter")
        self.assertEqual(e.state["player"]["armour"], metrics["armour"]["after"])
        self.assertEqual(e.state["player"]["speed"], metrics["speed"]["after"])
        for index, name in enumerate(("STR", "INT", "WIS", "DEX", "CON")):
            self.assertEqual(e.state["player"]["stats"][index], metrics[name]["after"])

    def spell_command(self, command, spell=None):
        e=self.engine
        book=next(o for o in e.state['items'] if o.get('book_available'))
        params={'revision':e.state['revision'],'command':command,'item':book['id']}
        if spell: params['spell']=spell
        old=e.state['revision']
        self.assertIn('result',e.call('command.execute',params))
        e.next_state(old); e.state=e.call('state.get')['result']

    def spell_book(self):
        return next(o for o in self.engine.state['items'] if o.get('book_available'))

    def test_native_spells(self):
        e=self.engine
        e.hello(); e.birth(class_index=1)
        self.assertEqual(e.state['player']['class'],'Mage')
        book=self.spell_book(); spells=book['spells']
        self.assertTrue(book['choose_spells'])
        missile=next(s for s in spells if s['label']=='Magic Missile')
        self.assertTrue(missile['can_study']); self.assertFalse(missile['can_cast'])
        self.assertTrue(missile['description']); self.assertEqual(missile['mana'],1)
        turn=e.state['turn']
        # Browsing exposes future spells without taking a turn or consuming mana.
        self.spell_command('core.browse')
        self.assertEqual(e.prompt['selection_kind'],'spell'); self.assertTrue(e.prompt['browse'])
        self.assertTrue(e.state['spell_selection']); self.assertIn('dungeon',e.state)
        self.store_reply(None); self.assertEqual(e.state['turn'],turn)
        # Ordinary keyboard/menu Study uses eligible native book and spell choices.
        self.spell_command('core.study')
        self.assertEqual(e.prompt['selection_kind'],'spell')
        self.assertTrue(all(s['can_study'] for s in e.prompt['choices']))
        self.assertEqual(e.call('prompt.reply',{'prompt_id':e.prompt['prompt_id'],'value':'999999'})['error']['code'],'invalid_argument')
        self.store_reply(None); self.assertEqual(e.state['turn'],turn)
        self.spell_command('core.study',missile['id'])
        while e.state['readiness']!='ready': e.key('enter')
        learned=next(s for s in self.spell_book()['spells'] if s['id']==missile['id'])
        self.assertTrue(learned['can_cast']); self.assertFalse(learned['can_study'])
        self.assertGreater(e.state['turn'],turn)
        # Unknown and out-of-book spell handles must not reach engine arrays.
        for handle in ('spell-999999','spell-6'):
            self.assertEqual(e.call('command.execute',{'revision':e.state['revision'],'command':'core.cast','item':self.spell_book()['id'],'spell':handle})['error']['code'],'invalid_argument')
        before=e.state
        self.spell_command('core.cast',missile['id'])
        self.assertTrue(e.state['aiming']); self.assertIn('dungeon',e.state)
        e.key('escape')
        self.assertEqual(e.state['turn'],before['turn']); self.assertEqual(e.state['player']['sp'],before['player']['sp'])
        self.spell_command('core.cast',missile['id'])
        e.key(ord('6'))
        while e.state['readiness']!='ready': e.key('enter')
        self.assertGreater(e.state['turn'],before['turn'])
        self.assertEqual(e.state['player']['sp'],before['player']['sp']-1)
        # Repeated casting reaches the engine's low-mana confirmation, which
        # must remain available rather than being disabled by the client.
        for _ in range(20):
            self.spell_command('core.cast',missile['id'])
            if e.prompt:
                self.assertEqual(e.prompt['type'],'confirmation')
                self.assertIn('Attempt it anyway',e.prompt['text'])
                self.assertTrue(e.state['native_prompt'])
                self.assert_semantic_view(e.state)
                mana=e.state['player']['sp']; turn=e.state['turn']
                self.store_reply(False)
                self.assertEqual(e.state['turn'],turn); self.assertEqual(e.state['player']['sp'],mana)
                break
            self.assertTrue(e.state['aiming']); e.key(ord('6'))
            while e.state['readiness']!='ready': e.key('enter')
        else: self.fail('No low-mana confirmation')

    def test_random_book_study(self):
        e=self.engine
        e.hello(); e.birth(class_index=3)
        self.assertEqual(e.state['player']['class'],'Priest')
        book=self.spell_book()
        self.assertFalse(book['choose_spells'])
        eligible=[s for s in book['spells'] if s['can_study']]
        self.assertTrue(eligible)
        self.assertEqual(e.call('command.execute',{'revision':e.state['revision'],'command':'core.study','item':book['id'],'spell':eligible[0]['id']})['error']['code'],'invalid_argument')
        self.spell_command('core.study')
        while e.state['readiness']!='ready': e.key('enter')
        learned=[s for s in self.spell_book()['spells'] if s['can_cast']]
        self.assertEqual(len(learned),1)
        self.assertIn(learned[0]['id'],[s['id'] for s in eligible])

    def test_spell_inscriptions_and_keyboard_selection(self):
        e=self.engine
        e.hello(); e.birth(class_index=1)
        missile=next(s for s in self.spell_book()['spells'] if s['label']=='Magic Missile')
        self.spell_command('core.study',missile['id'])
        while e.state['readiness']!='ready': e.key('enter')
        self.spell_command('core.inscribe')
        e.wait_prompt()
        self.store_reply('!m')
        before=e.state
        self.spell_command('core.cast',missile['id'])
        self.assertEqual(e.prompt['type'],'confirmation')
        self.store_reply(False)
        self.assertEqual(e.state['turn'],before['turn'])
        self.assertEqual(e.state['player']['sp'],before['player']['sp'])
        self.spell_command('core.inscribe'); e.wait_prompt(); self.store_reply('')
        # Keyboard m still goes through the native book selector, then the
        # native spell selector; a cancelled direct cast leaves no stale choice.
        e.key(ord('m')); e.wait_prompt()
        self.assertEqual(e.prompt['selection_kind'],'item')
        self.store_reply(e.prompt['choices'][0]['id'])
        self.assertEqual(e.prompt['selection_kind'],'spell')
        self.assertEqual([s['label'] for s in e.prompt['choices']],['Magic Missile'])
        self.store_reply(e.prompt['choices'][0]['id'])
        self.assertTrue(e.state['aiming']); e.key('escape')
        self.assertEqual(e.state['turn'],before['turn'])

    def test_spell_query_randomness(self):
        e=self.engine
        e.hello(); e.birth(class_index=1)
        self.spell_command('core.study',self.spell_book()['spells'][0]['id'])
        while e.state['readiness']!='ready': e.key('enter')
        # Work the spell so its dynamic get_spell_info summary is exercised.
        for _ in range(8):
            self.spell_command('core.cast',self.spell_book()['spells'][0]['id'])
            if e.prompt: self.store_reply(True)
            e.key(ord('6'))
            while e.state['readiness']!='ready': e.key('enter')
            if self.spell_book()['spells'][0]['status']=='Learned': break
        self.assertEqual(self.spell_book()['spells'][0]['status'],'Learned')
        self.assertTrue(self.spell_book()['spells'][0]['info'])
        e.call('session.close'); e.process.wait(timeout=10); e.stop()
        baseline=Path(self.temp.name)/'spell-baseline'; shutil.copytree(Path(self.temp.name)/'save',baseline)
        def cast(browse):
            folder=Path(self.temp.name)/('browse' if browse else 'direct')
            shutil.copytree(baseline,folder/'save')
            branch=Engine(folder); self.engine=branch
            branch.hello(); branch.call('session.load',{'save':'ProtocolTest'}); branch.next_state(None)
            while branch.state['readiness']!='ready': branch.key('enter')
            if browse:
                for category in ('creatures','items','artifacts','terrain'):
                    for entry in branch.call('knowledge.list',{'category':category})['result']['entries']:
                        self.assertIn('result',branch.call('knowledge.get',{'category':category,'id':entry['id']}))
                for _ in range(3): self.spell_command('core.browse'); self.store_reply(None)
                for item in branch.state['items']:
                    if item.get('comparison_available'):
                        self.assertIn('result', branch.call('item.compare', {'revision': branch.state['revision'], 'item': item['id']}))
            self.spell_command('core.cast',self.spell_book()['spells'][0]['id'])
            # Low-mana confirmation, message pauses and aiming are separate
            # input boundaries. A direction sent to -more- is consumed there.
            for _ in range(60):
                branch.state=branch.call('state.get')['result']
                if branch.state['readiness']=='ready': break
                if branch.prompt:
                    self.assertEqual(branch.prompt['type'],'confirmation')
                    self.store_reply(True)
                elif branch.state.get('message_pending'):
                    branch.key('enter')
                elif branch.state.get('aiming'):
                    branch.key(ord('6'))
                else:
                    branch.key('enter')
            self.assertEqual(branch.state['readiness'],'ready',f'Cast did not finish: state={branch.state.get("phase")} aiming={branch.state.get("aiming")} prompt={branch.prompt}')
            result={key:branch.state[key] for key in ('turn','player','map','monsters','items')}
            for collection in ('items','monsters'):
                for entity in result[collection]: entity.pop('id',None)
            branch.call('session.close'); branch.process.wait(timeout=10); branch.stop()
            return result
        direct=cast(False); browsed=cast(True)
        self.engine=Engine(Path(self.temp.name)/'cleanup')
        self.assertEqual(direct,browsed)

    def test_native_item_selection(self):
        e=self.engine
        e.hello(); e.birth()
        before=e.state
        self.assertIn('result',e.call('command.execute',{'revision':before['revision'],'command':'core.quaff'}))
        e.wait_prompt()
        self.assertEqual(e.prompt['selection_kind'],'item')
        self.assertTrue(e.state['item_selection'])
        self.assertIn('dungeon',e.state)
        items={o['id']:o for o in e.state['items']}
        self.assertTrue(e.prompt['choices'])
        for option in e.prompt['choices']:
            self.assertIn(option['item_id'],items)
            self.assertIn('core.quaff',items[option['item_id']]['actions'])
        self.assertGreater(len(items),len(e.prompt['choices']))
        self.assertEqual(e.call('prompt.reply',{'prompt_id':e.prompt['prompt_id'],'value':'not-an-option'})['error']['code'],'invalid_argument')
        choice=e.prompt['choices'][0]
        kind=items[choice['item_id']]['actual']['kind']
        amount=sum(o['quantity'] for o in e.state['items'] if o['location']=='Pack' and o['actual']['kind']==kind)
        old=e.state['revision']; prompt=e.prompt; e.prompt=None
        self.assertIn('result',e.call('prompt.reply',{'prompt_id':prompt['prompt_id'],'value':choice['id']}))
        e.next_state(old)
        while e.state['readiness']!='ready': e.key('enter')
        self.assertEqual(sum(o['quantity'] for o in e.state['items'] if o['location']=='Pack' and o['actual']['kind']==kind),amount-1)
        self.assertGreater(e.state['turn'],before['turn'])
        turn=e.state['turn']
        self.assertIn('result',e.call('command.execute',{'revision':e.state['revision'],'command':'core.read'}))
        e.wait_prompt()
        self.assertEqual(e.prompt['selection_kind'],'item')
        old=e.state['revision']; prompt=e.prompt; e.prompt=None
        e.call('prompt.reply',{'prompt_id':prompt['prompt_id'],'value':None})
        e.next_state(old)
        self.assertEqual(e.state['turn'],turn)

    def test_item_prompt_cancel_and_inscription(self):
        e = self.engine
        e.hello()
        e.birth()
        initial = e.call("state.get")["result"]
        self.assertIn("result", e.call("command.execute", {
            "revision": initial["revision"], "command": "core.inscribe"}))
        e.wait_prompt()
        self.assertEqual(e.prompt["type"], "choice")
        pid = e.prompt["prompt_id"]
        self.assertEqual(e.call("prompt.reply", {"prompt_id": pid, "value": "invalid"})["error"]["code"], "invalid_argument")
        self.assertEqual(e.call("command.execute", {"revision": e.state["revision"], "command": "core.hold"})["error"]["code"], "busy")
        old = e.state["revision"]
        e.call("prompt.reply", {"prompt_id": pid, "value": None})
        e.prompt = None
        e.next_state(old)
        self.assertEqual(e.state["turn"], initial["turn"])

        current = e.call("state.get")["result"]
        item = next(i for i in current["items"] if i["location"] == "Pack")
        e.call("command.execute", {"revision": current["revision"], "command": "core.inscribe", "item": item["id"]})
        e.wait_prompt()
        self.assertEqual(e.prompt["type"], "text")
        old = e.state["revision"]
        e.call("prompt.reply", {"prompt_id": e.prompt["prompt_id"], "value": "Deluxe test"})
        e.prompt = None
        e.next_state(old)
        self.assertTrue(any(i["inscription"] == "Deluxe test" for i in e.state["items"]))
        self.assertEqual(e.state["turn"], initial["turn"])

    def test_semantic_and_keyboard_action_parity(self):
        e = self.engine
        e.hello()
        e.birth()
        e.call("session.close")
        e.process.wait(timeout=10)
        e.stop()
        baseline = Path(self.temp.name) / "baseline"
        shutil.copytree(Path(self.temp.name) / "save", baseline)

        def play(semantic):
            save_dir = Path(self.temp.name) / "save"
            for save in baseline.iterdir():
                if save.is_file():
                    shutil.copy2(save, save_dir / save.name)
            self.engine = engine = Engine(self.temp.name)
            engine.hello()
            engine.call("session.load", {"save": "ProtocolTest"})
            engine.next_state(None)
            while engine.state["readiness"] != "ready":
                engine.key("enter")
            # Exercise the native targeting controls and the equivalent keys
            # from identical saves before comparing gameplay/RNG outcomes.
            for operation, key in [('begin',ord('l')),('free',ord('o')),('player',ord('p'))]:
                if semantic:
                    old=engine.state['revision']
                    method='targeting.begin' if operation=='begin' else 'targeting.control'
                    engine.call(method, {'context':engine.state['context'],'mode':'look','operation':operation})
                    engine.next_state(old)
                else:
                    engine.key(key)
                self.assertIn('dungeon', engine.state)
            engine.key('right')
            if semantic:
                old=engine.state['revision']
                engine.call('targeting.control', {'context':engine.state['context'],'operation':'confirm'})
                engine.next_state(old)
            else:
                engine.key(ord('t'))
            while engine.state['readiness'] != 'ready': engine.key('enter')
            for _ in range(5):
                if semantic:
                    state = engine.call("state.get")["result"]
                    for item in state["items"]:
                        self.assertEqual(engine.call("inspect.get", {"handle": item["id"]})["result"], item)
                    engine.call("command.execute", {"revision": state["revision"], "command": "core.hold"})
                    engine.next_state(state["revision"])
                else:
                    engine.key(ord(','))
                while engine.state["readiness"] != "ready":
                    engine.key("enter")
            state = engine.call("state.get")["result"]
            for collection in ("items", "monsters"):
                for entity in state[collection]:
                    entity.pop("id", None)
            result = {key: state[key] for key in ("turn", "player", "items", "monsters", "map")}
            engine.call("session.close")
            engine.process.wait(timeout=10)
            engine.stop()
            return result

        self.assertEqual(play(False), play(True))


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--backend", type=Path, required=True)
    parser.add_argument("--data", type=Path, default=Path(__file__).resolve().parents[1] / "lib")
    parser.add_argument("--verbose", action="store_true")
    ARGS, remaining = parser.parse_known_args()
    ARGS.backend = ARGS.backend.resolve()
    unittest.main(argv=[__file__, *remaining])
