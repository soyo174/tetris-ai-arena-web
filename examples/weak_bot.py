"""TETRA ARENA baseline source; build the two standalone files."""
from collections import deque, namedtuple
import json
import sys
from time import perf_counter

LEVEL = 'medium'  # Generated from bots/baseline_core.py; edit the source and rebuild.
VERSION = '1.1.0'
FULL = 1023
Placement = namedtuple('Placement', 'rows solids lines spin perfect_clear commands')


class SearchTimeout(Exception):
    pass


def one_ply(options):
    if not options:
        return -10**12, None, -10**12
    value, placement, *_ = options[0]
    return value, placement, value


class Model:
    def __init__(self, observation, deadline=None, *, rules=None, geometry=None):
        self.observation = observation
        self.deadline = deadline
        self.rules = observation.get('rules') or rules
        geometry = observation.get('geometry') or geometry
        self.cells = {p: tuple(tuple(tuple(c) for c in rotation) for rotation in values)
                      for p, values in geometry['cells'].items()}
        self.kicks90 = geometry['kicks_90']
        self.kicks180 = geometry['kicks_180']
        self.rows = tuple(sum(1 << x for x, c in enumerate(row) if c)
                          for row in observation['self']['board'])
        self.solids = tuple(3 in row for row in observation['self']['board'])
        self.values = {}

    def check_deadline(self):
        if self.deadline is not None and perf_counter() >= self.deadline:
            raise SearchTimeout

    def landings(self, piece, rows=None, solids=None, full=False):
        rows = self.rows if rows is None else rows
        solids = self.solids if solids is None else solids
        cells = self.cells[piece]
        fit_cache, fall_cache = {}, {}

        def fits(x, y, rotation):
            key = (x, y, rotation)
            if key not in fit_cache:
                fit_cache[key] = all(0 <= x + dx < 10 and 0 <= y + dy < 24
                                    and not rows[y + dy] & (1 << (x + dx))
                                    for dx, dy in cells[rotation])
            return fit_cache[key]

        def step(pose, command):
            x, y, r, spin = pose
            if command in ('LEFT', 'RIGHT', 'DOWN'):
                dx, dy = {'LEFT': (-1, 0), 'RIGHT': (1, 0), 'DOWN': (0, -1)}[command]
                return (x + dx, y + dy, r, 0) if fits(x + dx, y + dy, r) else pose
            delta = {'CW': 1, 'CCW': -1, 'ROT180': 2}[command]
            nr = (r + delta) % 4
            if delta == 2:
                offsets = self.kicks180[f'{r}:{nr}'] if isinstance(self.kicks180, dict) else self.kicks180
            else:
                offsets = self.kicks90[piece][f'{r}:{nr}']
            for index, (dx, dy) in enumerate(offsets, 1):
                if fits(x + dx, y + dy, nr):
                    flag = (2 if delta != 2 and index == 5 else 1) if piece == 'T' else 0
                    return x + dx, y + dy, nr, flag
            return pose

        def fall(pose):
            x, y, r, spin = pose
            key = (x, y, r)
            if key not in fall_cache:
                target, trail = y, []
                while fits(x, target - 1, r):
                    trail.append((x, target, r))
                    target -= 1
                    if (x, target, r) in fall_cache:
                        target = fall_cache[x, target, r]
                        break
                fall_cache[key] = target
                for visited in trail:
                    fall_cache[visited] = target
            target = fall_cache[key]
            return x, target, r, spin if target == y else 0

        start = (*self.rules['spawn_origins'][piece], 0, 0)
        if not fits(*start[:3]):
            return []
        finals, result = set(), []

        def add(pose, commands):
            x, y, r, flag = fall(pose)
            occupied = tuple(sorted((x + dx, y + dy) for dx, dy in cells[r]))
            spin = 'none'
            packed = list(rows)
            for col, row in occupied:
                packed[row] |= 1 << col
            cleared = [index for index, mask in enumerate(packed) if mask == FULL and not solids[index]]
            if piece == 'T' and flag:
                def corner(cx, cy):
                    return cx < 0 or cx >= 10 or cy < 0 or (cy < 24 and bool(packed[cy] & (1 << cx)))
                corners = (corner(x, y + 2), corner(x + 2, y + 2), corner(x + 2, y), corner(x, y))
                if sum(corners) >= 3:
                    spin = 'full' if ((corners[r] and corners[(r + 1) % 4]) or len(cleared) >= 2 or flag == 2) else 'mini'
            key = occupied, spin
            if key in finals:
                return
            finals.add(key)
            kept = tuple(mask for i, mask in enumerate(packed) if i not in cleared)
            kept_solids = tuple(s for i, s in enumerate(solids) if i not in cleared)
            result.append(Placement(kept + (0,) * len(cleared), kept_solids + (False,) * len(cleared),
                                    len(cleared), spin, not any(kept), [*commands, 'HARD_DROP']))

        if full:
            queue = deque([(start, ())])
            seen = {start}
            commands = ('LEFT', 'RIGHT', 'DOWN', 'CW', 'CCW', 'ROT180')
            if len(set(cells)) == 1:
                commands = ('LEFT', 'RIGHT', 'DOWN')
            while queue:
                self.check_deadline()
                pose, route = queue.popleft()
                add(pose, route)
                if len(route) >= self.rules['max_commands'] - 1:
                    continue
                for command in commands:
                    moved = step(pose, command)
                    if moved not in seen:
                        seen.add(moved)
                        queue.append((moved, (*route, command)))
        else:
            for rotation in ((), ('CW',), ('ROT180',), ('CCW',)):
                self.check_deadline()
                rotated = step(start, rotation[0]) if rotation else start
                for shift in range(-5, 6):
                    route = rotation + (('LEFT' if shift < 0 else 'RIGHT'),) * abs(shift)
                    pose = rotated
                    for command in route[len(rotation):]:
                        pose = step(pose, command)
                    add(pose, route)
        return result

    def board_value(self, rows, solids):
        key = rows, solids
        cached = self.values.get(key)
        if cached is not None:
            return cached
        base = 0
        while base < 24 and solids[base] and rows[base] == FULL:
            base += 1
        columns = [0] * 10
        for y, row in enumerate(rows):
            if not row:
                continue
            bit = 1 << y
            for x in range(10):
                if row & (1 << x):
                    columns[x] |= bit
        heights = [col.bit_length() for col in columns]
        holes = covered = 0
        floor_mask = (1 << base) - 1
        for col, height in zip(columns, heights):
            gaps = ((1 << height) - 1) & ~col & ~floor_mask
            holes += gaps.bit_count()
            while gaps:
                gap = gaps & -gaps
                covered += (col >> gap.bit_length()).bit_count()
                gaps -= gap
        relative = [max(0, h - base) for h in heights]
        roughness = sum(abs(a - b) for a, b in zip(relative, relative[1:]))
        peak = max(heights)
        if LEVEL == 'medium':
            value = (-52 * sum(relative) - 280 * holes - 22 * roughness
                     - 15 * max(relative) - 100 * max(0, peak - 19) ** 2)
        else:
            value = (-35 * sum(relative) - 400 * holes - 35 * covered - 16 * roughness
                     - 3 * max(relative) ** 2 - 250 * max(0, peak - 16) ** 2)
        self.values[key] = value
        return value

    def attack(self, placement, combo, b2b):
        lines, spin = placement.lines, placement.spin
        if not lines:
            return 0, 0, b2b
        combo += 1
        difficult = lines == 4 or spin == 'full' or (spin == 'mini' and lines >= 2)
        attack = self.rules['tspin_attack' if spin == 'full' or (spin == 'mini' and lines >= 2) else 'normal_attack'][lines]
        if difficult:
            attack += self.rules['b2b_bonus'] if b2b else 0
            b2b = True
        elif spin != 'mini':
            b2b = False
        attack += sum(combo > bound for bound in (2, 5, 7, 9, 12))
        if placement.perfect_clear:
            attack = max(attack, self.rules['perfect_clear_floor'])
        return attack, combo, b2b

    def imminent_cost(self, placement, attack):
        due = 0
        now_tick = self.observation.get('frame', self.observation['turn'])
        for warning in self.observation['self']['warnings']:
            remaining = max(0, warning['lines'] - attack)
            attack = max(0, attack - warning['lines'])
            if warning['due'] <= now_tick:
                due += remaining
        if 'overtime_first_frame' in self.rules:
            overtime = (now_tick >= self.rules['overtime_first_frame'] and
                        (now_tick - self.rules['overtime_first_frame']) % self.rules['overtime_interval_frames'] < 20)
        else:
            overtime = (now_tick >= self.rules['overtime_first'] and
                        (now_tick - self.rules['overtime_first']) % self.rules['overtime_interval'] == 0)
        peak = max((y + 1 for y, row in enumerate(placement.rows) if row), default=0)
        return 220 * max(0, peak + due + overtime - 18) ** 2 + 25 * due

    def ranked(self, piece, combo, b2b, full):
        options = []
        for placement in self.landings(piece, full=full):
            self.check_deadline()
            attack, next_combo, next_b2b = self.attack(placement, combo, b2b)
            reward = (120 * placement.lines + 25 * attack if LEVEL == 'medium'
                      else 45 * placement.lines + 160 * attack)
            cost = self.imminent_cost(placement, attack)
            value = self.board_value(placement.rows, placement.solids) + reward - cost
            options.append((value, placement, reward, cost, next_combo, next_b2b))
        # Stable enumeration breaks exact ties; no unordered containers decide a move.
        options.sort(key=lambda item: item[0], reverse=True)
        return options

    def search(self, options, next_piece, beam):
        if not options or next_piece is None:
            return one_ply(options)
        best = None
        for immediate, placement, reward, cost, next_combo, next_b2b in options[:beam]:
            future = -10**12
            for child in self.landings(next_piece, placement.rows, placement.solids):
                self.check_deadline()
                attack, _, _ = self.attack(child, next_combo, next_b2b)
                value = self.board_value(child.rows, child.solids) + 36 * child.lines + 128 * attack
                if value > future:
                    future = value
            total = future + reward - cost
            if best is None or total > best[0]:
                best = total, placement, immediate
        return best

    def plan(self):
        own = self.observation['self']
        combo, b2b = own['combo'], own['b2b']
        alternative = (own['hold'] or own['next']) if own['can_hold'] else None
        after_hold = own['next'] if own['hold'] is not None else None

        def select(current, held):
            value, placement, immediate = current
            hold_value, hold_placement, _ = held
            # An empty Hold hides the following piece, so use one-ply scores.
            reference = immediate if after_hold is None else value
            margin = 80 if LEVEL == 'medium' else (120 if after_hold is None else 45)
            if hold_placement is not None and hold_value > reference + margin:
                return 'HOLD', hold_placement.commands
            return 'PLAY', placement.commands if placement is not None else ['HARD_DROP']

        def ranked(full):
            current = self.ranked(own['current'], combo, b2b, full)
            held = self.ranked(alternative, combo, b2b, full) if alternative is not None else []
            return current, held

        best = 'PLAY', ['HARD_DROP']
        try:
            if self.deadline is not None:
                current, held = ranked(False)
                best = select(one_ply(current), one_ply(held))
            current, held = ranked(True)
            best = select(one_ply(current), one_ply(held))
            if LEVEL == 'strong':
                beam = 8 if self.deadline is None or self.deadline - perf_counter() > 0.1 else 4
                best = select(self.search(current, own['next'], beam), self.search(held, after_hold, beam))
        except SearchTimeout:
            pass
        return best


_STATIC_CACHE = {}


def decide(observation):
    key = observation.get('rules_sha256')
    if 'rules' in observation and 'geometry' in observation:
        _STATIC_CACHE[key] = (observation['rules'], observation['geometry'])
    rules, geometry = _STATIC_CACHE.get(key, (None, None))
    kind, commands = Model(observation, rules=rules, geometry=geometry).plan()
    return kind, commands if kind == 'PLAY' else None


def serve():
    after_hold = None
    cached_rules = None
    cached_geometry = None
    for line in sys.stdin:
        started = perf_counter()
        request = json.loads(line)
        observation = request['observation']
        if 'rules' in observation:
            cached_rules = observation['rules']
        if 'geometry' in observation:
            cached_geometry = observation['geometry']
        own = observation['self']
        turn = observation['turn']
        if after_hold and after_hold[:2] == (turn, own['current']) and not own['can_hold']:
            kind, commands = 'PLAY', after_hold[2]
        else:
            remaining = request.get('remaining_ms')
            bank = request.get('bank_ms')
            budget = remaining if bank is None else min(remaining, max(0, remaining - bank) + min(bank, 100))
            # Reserve time for the protocol, scheduling and the PLAY after HOLD.
            deadline = started + max(0, budget - 12) / 1000 if type(budget) is int else None
            kind, commands = Model(observation, deadline, rules=cached_rules, geometry=cached_geometry).plan()
        after_hold = (turn, own['hold'] or own['next'], commands) if kind == 'HOLD' else None
        response = {'type': kind, 'request_id': request['request_id']}
        if kind == 'PLAY':
            response['commands'] = commands
        print(json.dumps(response, separators=(',', ':')), flush=True)


if __name__ == '__main__':
    serve()
