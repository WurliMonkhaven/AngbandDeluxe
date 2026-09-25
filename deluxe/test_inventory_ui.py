"""Exercise native inventory actions and dismissal without opening a desktop window."""
import argparse
import json
from pathlib import Path
import subprocess

p = argparse.ArgumentParser()
p.add_argument('--preview', type=Path, required=True)
p.add_argument('--fixture', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=True)
base = json.loads(a.fixture.read_text())
base.update(inventory_window=True, inventory_trace=True, frames=8, scale=1,
            fonts={'interface':'Nouveau_IBM.ttf','dungeon':''})

def run(name, x, y, empty=False, equipment=False):
    f = json.loads(json.dumps(base))
    if equipment:
        f.update(inventory_window=False, equipment_window=True, quickbar_enabled=True)
        equipped = next(v for v in f['state']['items'] if v.get('location') not in ('Pack','Quiver','Floor','Store','Home'))
        equipped['actions'] = ['core.takeoff','core.use','core.drop','core.inscribe']
        equipped['label'] = 'The Adamantite Plate Mail of a Very Long Artifact Name [40,+25]'
        f['state']['items'] = [equipped] + [v for v in f['state']['items'] if v.get('location') == 'Pack']
    if empty and equipment:
        f['state']['items'] = [v for v in f['state']['items'] if v.get('location') == 'Pack']
    elif empty:
        f['state']['items'] = [v for v in f['state']['items'] if v.get('location') != 'Pack']
    f['input'] = [{'frame':2,'mouse':[x,y]}, {'frame':3,'down':True}, {'frame':4,'down':False}]
    source = a.output / (name+'.json')
    target = a.output / (name+'.bmp')
    source.write_text(json.dumps(f))
    subprocess.run([str(a.preview.resolve()),str(source),str(target),'1600','1000'],check=True)
    return json.loads(Path(str(target)+'.json').read_text())

used = run('quaff',370,542)
assert not used['open'], 'An item action must close the inventory window'
requests = [json.loads(line) for line in used['outgoing'].splitlines()]
commands = [r for r in requests if r['method']=='command.execute']
assert len(commands)==1 and commands[0]['params']['command']=='core.quaff'
assert commands[0]['params']['item']==used['selected']
for name,x,y in [('close',370,783),('title-close',1251,206)]:
    closed = run(name,x,y)
    assert not closed['open'] and not closed['outgoing'], 'Closing inventory must not issue a game action'
empty = run('empty',20,20,True)
assert empty['open'] and not empty['selected'], 'An empty pack must not select equipment or floor items'
taken = run('equipment-takeoff',370,542,equipment=True)
commands = [json.loads(line) for line in taken['outgoing'].splitlines() if json.loads(line)['method']=='command.execute']
assert not taken['open'] and len(commands)==1
assert commands[0]['params']['command']=='core.takeoff' and commands[0]['params']['item']==taken['selected']
empty_equipment = run('empty-equipment',20,20,empty=True,equipment=True)
assert empty_equipment['open'] and not empty_equipment['selected'], 'Equipment must not select pack items'
closed = run('equipment-close',370,783,equipment=True)
assert not closed['open'] and not closed['outgoing']
print('Seven native inventory/equipment interaction checks passed')
