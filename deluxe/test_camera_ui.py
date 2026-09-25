"""Camera input checks using the offscreen renderer, never desktop automation.

Pass a preview fixture containing a real full_level dungeon snapshot.
"""
import argparse
import json
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--preview', type=Path, required=True)
    parser.add_argument('--fixture', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    base = json.loads(args.fixture.read_text(encoding='utf-8'))
    assert base['state']['dungeon']['full_level']
    base.update(camera={'enabled': True, 'follow': True}, camera_trace=True,
                layout_trace=False, frames=12, crt_scope=0)
    # The fixture is already configured; no live backend acknowledges requests.
    base.get('capabilities', {}).pop('presentation.camera', None)

    def run(name, **changes):
        source, target = args.output / (name+'.json'), args.output / (name+'.bmp')
        source.write_text(json.dumps(dict(base, **changes)), encoding='utf-8')
        subprocess.run([str(args.preview.resolve()), str(source), str(target), '1600', '1000'], check=True)
        return json.loads(Path(str(target)+'.json').read_text())

    initial = run('initial')
    sx, sy, w, h = initial['surface']
    cx, cy = sx+w/2, sy+h/2
    cw, ch = initial['cell']
    pan = [{'frame': 2, 'mouse': [cx, cy]}, {'frame': 3, 'middle': True},
           {'frame': 4, 'mouse': [cx-80, cy-60]}, {'frame': 5, 'middle': False}]
    moved = run('pan', input=pan)
    assert moved['paused'], 'Middle drag must pause following'
    assert abs(moved['x']-initial['x']-80/cw) < .01
    assert abs(moved['y']-initial['y']-60/ch) < .01
    assert not any(json.loads(line)['method'] in ('dungeon.click', 'terminal.input', 'targeting.select')
                   for line in moved['outgoing'].splitlines()), 'Panning must not spend a turn'
    zoomed = run('zoom', input=pan+[{'frame': 7, 'mouse': [cx, cy], 'wheel': 2}])
    assert zoomed['zoom'] > 1 and zoomed['paused']
    clicked = run('click-after-pan', input=pan+[
        {'frame': 7, 'mouse': [cx, cy]}, {'frame': 8, 'down': True}, {'frame': 9, 'down': False}])
    clicks = [json.loads(line)['params'] for line in clicked['outgoing'].splitlines()
              if json.loads(line)['method']=='dungeon.click']
    assert len(clicks)==1, clicked
    assert (clicks[0]['x'], clicks[0]['y']) == (int(moved['x']), int(moved['y'])), 'Clicks must use transformed world coordinates'
    # This toolbar uses the default fixture font. Return occupies x=256..426.
    returned = run('return-to-player', input=pan+[
        {'frame': 7, 'mouse': [330, sy-14]}, {'frame': 8, 'down': True}, {'frame': 9, 'down': False}])
    assert not returned['paused'] and returned['x']==initial['x'] and returned['y']==initial['y']
    print('Camera pan, zoom, world-coordinate click and return checks passed.')


if __name__ == '__main__':
    main()
