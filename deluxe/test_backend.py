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
            raise AssertionError(f"Backend exited ({self.process.wait(timeout=2)}): " + "".join(self.errors))
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
        remaining=sum(o['quantity'] for o in e.state['items'] if o['location']=='Pack' and o['actual']['kind']==kind)
        self.assertEqual(remaining,item['quantity']-1)
        self.assertNotIn('targeting',e.state)
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
        self.assertIn("core.quaff", potion["actions"])
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
        for quote in comparisons:
            for handle in quote['compare_with']:
                self.assertEqual(records[handle]['location'],'weapon')
                self.assertTrue(records[handle]['description'])

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
                for _ in range(3): self.spell_command('core.browse'); self.store_reply(None)
            self.spell_command('core.cast',self.spell_book()['spells'][0]['id'])
            if branch.prompt: self.store_reply(True)
            branch.key(ord('6'))
            while branch.state['readiness']!='ready': branch.key('enter')
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
