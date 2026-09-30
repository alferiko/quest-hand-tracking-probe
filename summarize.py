"""Summarize source evidence; never infer physical controller holding from API alone."""
import argparse
import collections
import csv
import itertools
import json
import math
from pathlib import Path


def bend(rows, a, b, c):
    points = []
    for j in (a, b, c):
        r = rows[j]
        if not int(r['flags']) & 2:
            return None
        p = [float(r[k]) for k in ('x', 'y', 'z')]
        if not all(math.isfinite(v) for v in p):
            return None
        points.append(p)
    u = [x-y for x, y in zip(points[0], points[1])]
    v = [x-y for x, y in zip(points[2], points[1])]
    length = math.sqrt(sum(x*x for x in u) * sum(x*x for x in v))
    if length < 1e-10:
        return None
    return 180-math.degrees(math.acos(max(-1, min(1, sum(x*y for x, y in zip(u, v))/length))))


def summarize(path):
    counts = collections.defaultdict(collections.Counter)
    overlap = collections.Counter()
    pressed = collections.Counter()
    simultaneous_sources = collections.Counter()
    auto_states = {}
    angles = collections.defaultdict(list)
    span = [None, None]
    incomplete = 0
    with path.open(newline='', encoding='utf-8') as f:
        records = csv.DictReader(f)
        for key, group in itertools.groupby(records, lambda r: (r['sample'], r['hand'], r['mode'])):
            rows = {int(r['joint']): r for r in group}
            if set(rows) != set(range(26)):
                incomplete += 1
                continue
            r = rows[0]
            name = ('LEFT' if key[1] == '0' else 'RIGHT') + (' AUTO' if key[2] == '0' else ' CAMERA_ONLY')
            t = int(r['xr_time'])
            span[0] = t if span[0] is None else min(span[0], t)
            span[1] = t if span[1] is None else max(span[1], t)
            if int(r['locate_result']) < 0:
                state = 'error_or_unavailable'
            elif r['hand_active'] != '1':
                state = 'inactive'
            elif r['source_active'] != '1':
                state = 'unknown_source'
            else:
                state = {'1': 'camera', '2': 'controller'}.get(r['source'], 'unknown_source')
            counts[name][state] += 1
            if key[2] == '0':
                auto_states[key[0], key[1]] = state
            valid = all(int(j['flags']) & 2 for j in rows.values())
            # Controller action activity is supporting evidence, not an in-hand sensor.
            candidate = (state == 'camera' and valid and r['focused'] == '1'
                         and r['multi_result'] == '0' and r['sync_result'] == '0'
                         and r['grip_active'] == '1' and int(r['grip_flags']) & 3 == 3)
            if candidate:
                overlap[name] += 1
                if key[2] == '1' and auto_states.get((key[0], key[1])) == 'controller':
                    simultaneous_sources[name] += 1
                if ((r['trigger_active'] == '1' and float(r['trigger']) > .1)
                        or (r['squeeze_active'] == '1' and float(r['squeeze']) > .1)):
                    pressed[name] += 1
                for finger, joints in [('index', (7, 8, 9)), ('middle', (12, 13, 14)),
                                       ('ring', (17, 18, 19)), ('little', (22, 23, 24))]:
                    angle = bend(rows, *joints)
                    if angle is not None:
                        angles[name, finger].append(angle)
    ranges = {}
    for (name, finger), values in angles.items():
        ranges.setdefault(name, {})[finger] = {
            'min_degrees': round(min(values), 1), 'max_degrees': round(max(values), 1),
            'range_degrees': round(max(values)-min(values), 1), 'samples': len(values)}
    return {
        'file': str(path),
        'duration_seconds': round((span[1]-span[0])/1e9, 2) if span[0] is not None else 0,
        'source_samples': dict(counts),
        'camera_with_active_valid_controller_pose': dict(overlap),
        'camera_with_active_controller_and_button_pressed': dict(pressed),
        'camera_only_while_same_hand_auto_uses_controller': dict(simultaneous_sources),
        'finger_bend_during_overlap': ranges,
        'incomplete_groups_skipped': incomplete,
        'interpretation': ('Overlap is a candidate observation, not proof of independent optical finger accuracy. '
                           'Correlate with a person actually holding the same-side controller and moving individual '
                           'fingers. No overlap does not prove hardware impossibility; check focus, settings and visibility.')}


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('csv', type=Path)
    parser.add_argument('--out', type=Path)
    args = parser.parse_args()
    report = json.dumps(summarize(args.csv), indent=2)
    if args.out:
        args.out.write_text(report+'\n', encoding='utf-8')
    print(report)
