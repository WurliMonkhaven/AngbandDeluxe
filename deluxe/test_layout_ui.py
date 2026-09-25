"""Offscreen mouse interaction checks. Does not open or control desktop windows."""
import argparse
import copy
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

    def run(name, size=(1600, 1000), **changes):
        fixture = dict(base, **changes)
        source = args.output / (name + ".json")
        target = args.output / (name + ".bmp")
        source.write_text(json.dumps(fixture), encoding="utf-8")
        subprocess.run([str(args.preview.resolve()), str(source), str(target), str(size[0]), str(size[1])], check=True)
        return json.loads(Path(str(target) + ".json").read_text())

    original = run("original")
    probe = run("drag-guides", frames=7, input=drag([60, 790], [500, 400]))
    stack_point = [probe["panel_rects"]["3"]["x"] + 38, probe["panel_rects"]["3"]["y"] + 72]
    normal = run("normal", layout_edit=False)
    spells_point = [normal["panel_rects"]["3"]["x"] + 158, normal["panel_rects"]["3"]["y"] + 23]
    docked = run("dock-right", input=drag([60, 790], [1260, 168]))
    assert canonical(docked["root"]) != canonical(original["root"])
    assert leaf(docked["root"], 2) and not docked["floating"]
    stacked = run("stack-tabs", input=drag([60, 790], stack_point))
    assert 2 in leaf(stacked["root"], 3)["tabs"], "Message drop should stack with Inventory"
    moved = run("move-float", layout_float=True, input=drag([500, 251], [650, 325]))
    assert moved["floating"][0]["x"] > .20 and moved["floating"][0]["y"] > .20
    resized = run("resize-float", layout_float=True, input=drag([760, 558], [850, 630]))
    assert resized["floating"][0]["w"] > .39 and resized["floating"][0]["h"] > .43
    divider_x = original["panel_rects"]["1"]["x"] - 3
    normal_divider_x = normal["panel_rects"]["1"]["x"] - 3
    split = run("resize-split", input=drag([divider_x, 400], [980, 400]))
    assert split["root"]["ratio"] < original["root"]["ratio"]
    locked = run("locked", layout_edit=False, input=drag([normal_divider_x, 400], [980, 400]))
    assert locked["root"]["ratio"] == original["root"]["ratio"], "Locked dividers must not move"
    cancelled = run("cancel", input=drag([60, 790], [1260, 168]) +
                    [{"frame": 8, "mouse": [242, 53]}, {"frame": 9, "down": True}, {"frame": 10, "down": False}])
    assert canonical(cancelled["root"]) == canonical(original["root"]), "Cancel must restore geometry"
    switched = run("switch-tab", layout_edit=False, input=[
        {"frame": 2, "mouse": spells_point}, {"frame": 3, "down": True}, {"frame": 4, "down": False}])
    assert leaf(switched["root"], 3)["active"] == 5, "Locked tabs must remain selectable"
    unlocked = run("unlocked", layout_edit=False, layout_dividers_locked=False,
                   input=drag([normal_divider_x, 400], [980, 400]))
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

    tracker_layout = copy.deepcopy(original)
    leaf(tracker_layout["root"], 8)["tabs"].remove(8)
    leaf(tracker_layout["root"], 11)["tabs"] = [8]
    leaf(tracker_layout["root"], 8)["active"] = 8
    tracker_original = run("tracker-flexible-neighbour", layout={"version": 4, "current": tracker_layout})
    pushed = check_push("push-tracker-down", 12, 8, 3, tracker_original, 60)
    assert abs(pushed["panel_rects"]["1"]["h"] - tracker_original["panel_rects"]["1"]["h"]) < 1, "Character height must not change"
    check_push("push-tracker-up", 12, 8, 3, pushed, -40)
    check_push("push-quickbar-down", 10, 0, 2, original, 40)
    limit = check_push("push-tracker-limit", 12, 8, 3, tracker_original, 10000)
    assert limit["panel_rects"]["3"]["h"] >= 8 * 18, "Inventory must retain its minimum height"
    assert split["undo_count"] == 1, "A complete divider drag must be one undo step"
    undo = run("undo-docking", frames=14, input=drag([60, 790], [1260, 168]) + [
        {"frame": 9, "mouse": [312, 53]}, {"frame": 10, "down": True}, {"frame": 11, "down": False}])
    assert canonical(undo["root"]) == canonical(original["root"]), "Undo button restores the previous docking"
    redo = run("redo-docking", frames=20, input=drag([60, 790], [1260, 168]) + [
        {"frame": 9, "mouse": [312, 53]}, {"frame": 10, "down": True}, {"frame": 11, "down": False},
        {"frame": 14, "mouse": [365, 53]}, {"frame": 15, "down": True}, {"frame": 16, "down": False}])
    assert canonical(redo["root"]) == canonical(docked["root"]), "Redo button reapplies the docking"
    for name, destination, delivered, label in [
            ("preview-right", [1260, 168], docked, "Place right of Character"),
            ("preview-stack", stack_point, stacked, "Stack with Inventory")]:
        preview = run(name, frames=7, input=drag([60, 790], destination))
        hint = preview["dock_preview"]
        assert hint["active"] and hint["label"] == label, "Docking hint must describe the proposed action"
        landed = delivered["panel_rects"]["2"]
        assert all(abs(hint[k] - landed[k]) < 2 for k in ("x", "y", "w", "h")), "Preview must match the delivered panel bounds"
        assert canonical(preview["root"]) == canonical(original["root"]), "Hovering a preview must not change the layout"
    # Compact tabs fit the selected content, including their tab strip.
    def pane(tabs, active=None):
        return {"axis": 0, "tabs": tabs, "active": tabs[0] if active is None else active}
    def split_node(axis, ratio, a, b):
        return {"axis": axis, "ratio": ratio, "tabs": [], "children": [a, b]}
    def custom(node, floating=None):
        return {"version": 4, "current": {"version": 4, "root": node, "floating": floating or []}}
    compact_tree = split_node(1, .65, pane([0]), split_node(2, .85, pane([1, 11]), pane([3])))
    compact = run("compact-tabs", layout_edit=False, layout=custom(compact_tree))
    char_h = compact["panel_rects"]["1"]["h"]
    assert 200 < char_h < 340, "Character tabs must fit content rather than the stored split ratio"
    small_tab = copy.deepcopy(compact_tree)
    small_tab["children"][1]["children"][0]["active"] = 11
    smaller = run("compact-dungeon-tab", layout_edit=False, layout=custom(small_tab))
    assert smaller["panel_rects"]["11"]["h"] < char_h - 50, "Dungeon details release the Character tab's unused space"
    mixed_tree = split_node(1, .65, pane([0]), split_node(2, .01, pane([1, 3]), pane([2])))
    mixed = run("mixed-minimum", layout_edit=False, layout=custom(mixed_tree))
    assert mixed["panel_rects"]["1"]["h"] >= char_h - 1, "A flexible tab group must protect Character contents"
    tall_mixed = copy.deepcopy(mixed_tree)
    tall_mixed["children"][1]["ratio"] = .8
    before_drag = run("mixed-before-drag", layout_edit=False, layout=custom(tall_mixed))
    r = before_drag["panel_rects"]["1"]
    shrunk = run("mixed-divider-limit", layout_edit=False, layout_dividers_locked=False,
        layout=custom(tall_mixed), input=drag([r["x"]+50, r["y"]+r["h"]+3], [r["x"]+50, r["y"]+40]))
    assert char_h-1 <= shrunk["panel_rects"]["1"]["h"] < r["h"], "Dragging a mixed group's divider must stop before clipping Character"
    compact_float = run("compact-float", layout_edit=False,
        layout=custom(pane([0]), [{"node":pane([1,11]),"x":.2,"y":.1,"w":.4,"h":.9}]))
    assert compact_float["panel_rects"]["1"]["h"] < 340, "Floating compact tabs must discard oversized saved heights"
    cramped = run("cramped-workspace", size=(1000, 600), layout_edit=False, layout=custom(mixed_tree))
    assert cramped["panel_rects"]["1"]["h"] >= char_h - 1, "A small workspace must retain compact content height"
    overfull = run("overflow-workspace", size=(1000, 400), layout_edit=False, layout=custom(mixed_tree))
    assert overfull["panel_rects"]["1"]["h"] >= char_h - 1, "Insufficient total height must not proportionally squash Character"
    assert overfull["panel_rects"]["2"]["y"] + overfull["panel_rects"]["2"]["h"] > 400, "Overflow uses a scrollable workspace"
    mixed_float = run("mixed-small-float", layout_edit=False,
        layout=custom(pane([0]), [{"node":pane([1,3]),"x":.2,"y":.1,"w":.4,"h":.05}]))
    assert mixed_float["panel_rects"]["1"]["h"] >= char_h - 1, "Floating mixed tabs respect compact content minima"
    assert abs(probe["panel_rects"]["1"]["h"] - original["panel_rects"]["1"]["h"]) < 1, "Drag guides must not resize compact panels"
    print("Layout checks passed: resizing, docking, undo/redo and measured drop previews")



if __name__ == "__main__":
    main()
