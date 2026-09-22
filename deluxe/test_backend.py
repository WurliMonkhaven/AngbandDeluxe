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
        self.count = 0
        self.state = {}
        self.prompt = None
        self.responses = {}
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
            raise AssertionError("Backend exited: " + "".join(self.errors))
        if j.get("kind") == "event":
            if j["event"] == "state.changed":
                self.state = j["data"]
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

    def birth(self, name="ProtocolTest"):
        result = self.call("session.new", {"save": name})
        assert "result" in result, result
        self.next_state(None)
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
        self.assertIn("core.use", potion["actions"])
        self.assertIn("core.drop", potion["actions"])
        self.assertIn("An inheritance from your family", potion["description"])
        self.assertIn("When quaffed", potion["description"])
        weapon = next(i for i in initial["items"] if i["location"] == "weapon")
        self.assertIn("Combat info", weapon["description"])
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

    def test_stairs_and_dungeon_inspection(self):
        e = self.engine
        e.hello()
        e.birth()
        catalog = e.call("catalog.get")["result"]["features"]
        floors = {f["id"] for f in catalog if f["name"] in
                  ("open floor", "open door", "broken door", "up staircase", "down staircase")}
        stairs = next(f["id"] for f in catalog if f["name"] == "down staircase")
        # Navigate an ordinary new character through town using real movement.
        for _ in range(150):
            s = e.call("state.get")["result"]
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
        e.key(ord(">"))
        while e.state["readiness"] != "ready":
            e.key("enter")
        dungeon = e.call("state.get")["result"]
        self.assertEqual(dungeon["player"]["depth"], 1)
        self.assertTrue(any(i["location"] == "Floor" for i in dungeon["items"]))
        for item in dungeon["items"]:
            self.assertEqual(e.call("inspect.get", {"handle": item["id"]})["result"], item)
        self.assertEqual(e.call("state.get")["result"], dungeon)
        # The return stairs also exercise the '<' codepoint and another level change.
        e.key(ord("<"))
        while e.state["readiness"] != "ready":
            e.key("enter")
        self.assertEqual(e.state["player"]["depth"], 0)

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
