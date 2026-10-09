#!/usr/bin/env python3
"""Dumps an app's accessibility tree as screen readers see it (AT-SPI).

    scripts/a11y-probe.py --pid PID [--do NAME=ACTION] [--set NAME=VALUE]

Prints one JSON object: {"nodes": [...]}, each node with role, name,
description, states, value (min/max/now or null), actions and depth; or
{"error": "..."} when there is no AT-SPI bus or no such app. --do performs
the named action (e.g. "a11y.increment") on the first node with that name
before dumping; --set sets its value (AT-SPI's Value interface).
rn-gtk-host's GalleryAccessibility self-test runs it.
"""
import argparse
import json
import sys
import warnings

# Atspi.Action.get_action_name is deprecated, but get_name would resolve to
# the accessible's own name.
warnings.filterwarnings("ignore", category=DeprecationWarning)

try:
    import gi

    gi.require_version("Atspi", "2.0")
    from gi.repository import Atspi
except (ImportError, ValueError) as e:
    print(json.dumps({"error": f"no AT-SPI bindings: {e}"}))
    sys.exit(0)


def node(o, depth):
    states = []
    try:
        states = sorted(s.value_nick for s in o.get_state_set().get_states())
    except Exception:
        pass
    value = None
    try:
        v = o.get_value_iface()
        if v is not None:
            value = {
                "min": v.get_minimum_value(),
                "max": v.get_maximum_value(),
                "now": v.get_current_value(),
            }
    except Exception:
        pass
    actions = []
    try:
        a = o.get_action_iface()
        if a is not None:
            actions = [a.get_action_name(i) for i in range(a.get_n_actions())]
    except Exception:
        pass
    return {
        "role": o.get_role_name(),
        "name": o.get_name() or "",
        "description": o.get_description() or "",
        "states": states,
        "value": value,
        "actions": actions,
        "depth": depth,
    }


def walk(o, depth, out, limit=4000):
    if len(out) >= limit:
        return
    try:
        out.append((o, node(o, depth)))
        for i in range(o.get_child_count()):
            child = o.get_child_at_index(i)
            if child is not None:
                walk(child, depth + 1, out, limit)
    except Exception as e:  # an object that went away mid-walk
        out.append((None, {"error": str(e), "depth": depth}))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--pid", type=int, required=True)
    parser.add_argument("--do", action="append", default=[])
    parser.add_argument("--set", action="append", default=[])
    args = parser.parse_args()
    try:
        desktop = Atspi.get_desktop(0)
    except Exception as e:
        print(json.dumps({"error": f"no AT-SPI desktop: {e}"}))
        return
    app = None
    for i in range(desktop.get_child_count()):
        a = desktop.get_child_at_index(i)
        try:
            if a is not None and a.get_process_id() == args.pid:
                app = a
                break
        except Exception:
            continue
    if app is None:
        print(json.dumps({"error": f"no accessible app with pid {args.pid}"}))
        return
    for spec in args.do:
        name, _, action = spec.partition("=")
        nodes = []
        walk(app, 0, nodes)
        for o, n in nodes:
            if o is not None and n.get("name") == name and action in n.get("actions", []):
                a = o.get_action_iface()
                a.do_action(n["actions"].index(action))
                break
        else:
            print(json.dumps({"error": f"no action {action} on {name!r}"}))
            return
    for spec in args.set:
        name, _, value = spec.partition("=")
        nodes = []
        walk(app, 0, nodes)
        for o, n in nodes:
            if o is not None and n.get("name") == name and n.get("value") is not None:
                o.get_value_iface().set_current_value(float(value))
                break
        else:
            print(json.dumps({"error": f"no value on {name!r}"}))
            return
    nodes = []
    walk(app, 0, nodes)
    print(json.dumps({"nodes": [n for _, n in nodes]}))


main()
