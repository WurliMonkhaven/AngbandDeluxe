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

def run(name, x, y, empty=False):
    f = json.loads(json.dumps(base))
    if empty:
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
print('Four native inventory interaction checks passed')
