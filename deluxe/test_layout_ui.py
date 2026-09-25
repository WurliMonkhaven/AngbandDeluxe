"""Offscreen mouse interaction checks. Does not open or control desktop windows."""
import argparse
import json
from pathlib import Path
import subprocess


def canonical(node):
    return {k: ([canonical(v) for v in value] if k == "children" else value)
            for k, value in node.items() if k != "id"}


def leaf(node, panel):
    if panel in node["tabs"]:
        return node
    for child in node.get("children", []):
        found = leaf(child, panel)
        if found:
            return found
    return None


def drag(start, end):
    return [{"frame": 2, "mouse": start},
            {"frame": 3, "down": True},
            {"frame": 4, "mouse": [start[0] + 20, start[1] - 10]},
            {"frame": 5, "mouse": end}, {"frame": 7, "down": False}]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--preview", type=Path, required=True)
    parser.add_argument("--fixture", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    base = json.loads(args.fixture.read_text(encoding="utf-8"))
    base.update(fonts={"interface": "Nouveau_IBM.ttf", "dungeon": ""},
                scale=1, crt_scope=0, layout_trace=True, layout_edit=True,
                layout_preset=0, layout_float=False, frames=12)
    for key in ("layout", "transition", "font_picker", "input"):
        base.pop(key, None)

    def run(name, **changes):
        fixture = dict(base, **changes)
        source = args.output / (name + ".json")
        target = args.output / (name + ".bmp")
        source.write_text(json.dumps(fixture), encoding="utf-8")
        subprocess.run([str(args.preview.resolve()), str(source), str(target), "1600", "1000"], check=True)
        return json.loads(Path(str(target) + ".json").read_text())

    original = run("original")
    docked = run("dock-right", input=drag([60, 790], [1260, 168]))
    assert canonical(docked["root"]) != canonical(original["root"])
    assert leaf(docked["root"], 2) and not docked["floating"]
    stacked = run("stack-tabs", input=drag([60, 790], [1140, 741]))
    assert 2 in leaf(stacked["root"], 3)["tabs"], "Message drop should stack with Inventory"
    moved = run("move-float", layout_float=True, input=drag([500, 251], [650, 325]))
    assert moved["floating"][0]["x"] > .20 and moved["floating"][0]["y"] > .20
    resized = run("resize-float", layout_float=True, input=drag([760, 558], [850, 630]))
    assert resized["floating"][0]["w"] > .39 and resized["floating"][0]["h"] > .43
    split = run("resize-split", input=drag([1098, 400], [980, 400]))
    assert split["root"]["ratio"] < original["root"]["ratio"]
    locked = run("locked", layout_edit=False, input=drag([1098, 400], [980, 400]))
    assert locked["root"]["ratio"] == original["root"]["ratio"], "Locked dividers must not move"
    cancelled = run("cancel", input=drag([60, 790], [1260, 168]) +
                    [{"frame": 8, "mouse": [242, 53]}, {"frame": 9, "down": True}, {"frame": 10, "down": False}])
    assert canonical(cancelled["root"]) == canonical(original["root"]), "Cancel must restore geometry"
    switched = run("switch-tab", layout_edit=False, input=[
        {"frame": 2, "mouse": [1260, 638]}, {"frame": 3, "down": True}, {"frame": 4, "down": False}])
    assert leaf(switched["root"], 3)["active"] == 5, "Locked tabs must remain selectable"
    unlocked = run("unlocked", layout_edit=False, layout_dividers_locked=False,
                   input=drag([1098, 400], [980, 400]))
    assert unlocked["root"]["ratio"] < original["root"]["ratio"], "Unlocked dividers must resize outside editing"
    movable = run("float-normal-play", layout_edit=False, layout_float=True,
                  input=drag([500, 204], [650, 280]))
    assert movable["floating"][0]["x"] > .20 and movable["floating"][0]["y"] > .20, "Floating title should drag during play"
    fixed = run("float-locked", layout_edit=False, layout_float=True, layout_floating_locked=True,
                input=drag([500, 204], [650, 280]))
    assert abs(fixed["floating"][0]["x"]-.15) < .001 and abs(fixed["floating"][0]["y"]-.15) < .001, "Locked floats must stay put"
    # Drag the boundary above a compact panel. Its height must stay fixed,
    # while the flexible areas on either side exchange space.
    def check_push(name, panel, above, below, source, delta):
        rect = source["panel_rects"][str(panel)]
        x, y = rect["x"] + rect["w"] / 2, rect["y"] - 3
        result = run(name, layout={"version": 3, "current": source}, input=drag([x, y], [x, y + delta]))
        before, after = source["panel_rects"], result["panel_rects"]
        shift = after[str(panel)]["y"] - before[str(panel)]["y"]
        assert shift * delta > 0, "Compact panel must follow the divider"
        assert abs(after[str(panel)]["h"] - before[str(panel)]["h"]) < 1, "Compact panel must retain its height"
        assert abs(after[str(above)]["h"] - before[str(above)]["h"] - shift) < 1, "Only the neighbouring flexible panel grows"
        assert abs(after[str(below)]["h"] - before[str(below)]["h"] + shift) < 1, "Opposite flexible panel yields the same space"
        return result

    pushed = check_push("push-tracker-down", 12, 11, 3, original, 60)
    assert abs(pushed["panel_rects"]["1"]["h"] - original["panel_rects"]["1"]["h"]) < 1, "Character height must not change"
    check_push("push-tracker-up", 12, 11, 3, pushed, -40)
    check_push("push-quickbar-down", 10, 0, 2, original, 40)
    limit = check_push("push-tracker-limit", 12, 11, 3, original, 10000)
    assert limit["panel_rects"]["3"]["h"] >= 8 * 18, "Inventory must retain its minimum height"
    print("Fifteen offscreen layout interaction checks passed")



if __name__ == "__main__":
    main()
