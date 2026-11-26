"""TETRA ARENA strong baseline: settlement-aware Hold planning.

Only public observations are used. The online search returns a completed plan
before its deadline; untimed analysis uses fixed bounds and tie breaking.
"""
from collections import deque, namedtuple
import json
import sys
from time import perf_counter

VERSION = '2.1.0'
FULL = 1023
Placement = namedtuple('Placement', 'rows solids lines spin perfect_clear commands')
Forecast = namedtuple('Forecast', 'rows solids warnings alive spawnable overflow')
Choice = namedtuple('Choice', 'piece held placement forecast combo b2b hold reward value')
RootChoice = namedtuple('RootChoice', 'placement held next_piece hold forecast combo b2b reward value')


class SearchTimeout(Exception):
    pass


class Model:
    def __init__(self, observation, deadline=None):
        self.observation = observation
        self.deadline = deadline
        self.rules = observation['rules']
        geometry = observation['geometry']
        self.cells = {p: tuple(tuple(tuple(c) for c in rotation) for rotation in values)
                      for p, values in geometry['cells'].items()}
        self.kicks90 = geometry['kicks_90']
        self.kicks180 = geometry['kicks_180']
        self.rows = tuple(sum(1 << x for x, c in enumerate(row) if c)
                          for row in observation['self']['board'])
        self.solids = tuple(3 in row for row in observation['self']['board'])
        self.values = {}
        self.landing_cache = {}
        self.spawn_cache = {}
        self.spawn_masks = []
        self.spawn_bits = {}
        for index, piece in enumerate('IOTSZJL'):
            x, y = self.rules['spawn_origins'][piece]
            masks, valid = {}, True
            for dx, dy in self.cells[piece][0]:
                col, row = x + dx, y + dy
                if not (0 <= col < 10 and 0 <= row < 24):
                    valid = False
                    break
                masks[row] = masks.get(row, 0) | (1 << col)
            self.spawn_masks.append(tuple(masks.items()) if valid else None)
            self.spawn_bits[piece] = 1 << index

    def check_deadline(self):
        if self.deadline is not None and perf_counter() >= self.deadline:
            raise SearchTimeout

    def landings(self, piece, rows=None, solids=None, full=False):
        rows = self.rows if rows is None else rows
        solids = self.solids if solids is None else solids
        key = piece, rows, solids, full
        if key not in self.landing_cache:
            self.landing_cache[key] = self._landings(piece, rows, solids, full)
        return self.landing_cache[key]

    def _landings(self, piece, rows=None, solids=None, full=False):
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
            offsets = self.kicks180 if delta == 2 else self.kicks90[piece][f'{r}:{nr}']
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
                    if cx < 0 or cx >= 10 or cy < 0:
                        return True
                    # Unknown garbage may have its hole at this corner. Do not
                    # turn a guessed occupied cell into a spin/B2B reward.
                    return cy < 24 and solids[cy] != 2 and bool(packed[cy] & (1 << cx))
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
        while base < 24 and solids[base] is True and rows[base] == FULL:
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

    def spawnable_mask(self, rows):
        if rows not in self.spawn_cache:
            mask = 0
            for index, cells in enumerate(self.spawn_masks):
                if cells is not None and all(not rows[y] & bits for y, bits in cells):
                    mask |= 1 << index
            self.spawn_cache[rows] = mask
        return self.spawn_cache[rows]

    def forecast(self, placement, attack, next_piece, turn=None, warnings=None):
        """Resolve known settlement before forecasting the next move.

        Hole columns are private. Unknown raised rows use opaque occupied masks
        and marker 2 (not a real solid floor), preventing guessed hole clears.
        Mandatory spawn feasibility above those rows is exact; when an unknown
        current is involved we count all seven public spawn shapes conservatively.
        """
        turn = self.observation['turn'] if turn is None else turn
        warnings = self.observation['self']['warnings'] if warnings is None else warnings
        rows, solids = placement.rows, placement.solids
        future, due = [], []
        for warning in warnings:
            lines, deadline = ((warning['lines'], warning['due']) if isinstance(warning, dict) else warning)
            used = min(attack, lines)
            attack -= used
            left = lines - used
            if left:
                if deadline <= turn:
                    due.append(left)
                else:
                    future.append((left, deadline))
        overflow = False
        for count in due:
            floor = 0
            while floor < 24 and solids[floor] is True and rows[floor] == FULL:
                floor += 1
            capacity = 24 - floor
            retained = max(0, capacity - count)
            overflow |= count > capacity or any(rows[floor + retained:])
            inserted = min(count, capacity)
            rows = rows[:floor] + (FULL,) * inserted + rows[floor:floor + retained]
            solids = solids[:floor] + (2,) * inserted + solids[floor:floor + retained]
        if turn >= self.rules['overtime_first'] and (turn - self.rules['overtime_first']) % self.rules['overtime_interval'] == 0:
            overflow |= bool(rows[-1])
            rows = (FULL,) + rows[:23]
            solids = (True,) + solids[:23]
        spawn_mask = self.spawnable_mask(rows)
        spawnable = spawn_mask.bit_count()
        alive = not overflow and (bool(spawn_mask & self.spawn_bits[next_piece]) if next_piece is not None else spawnable > 0)
        return Forecast(rows, solids, tuple(future), alive, spawnable, bool(overflow))

    def state_value(self, forecast, combo, b2b):
        if not forecast.alive:
            return -10**12
        return (self.board_value(forecast.rows, forecast.solids) + 25 * min(combo, 12)
                + 80 * bool(b2b) - 1200 * (7 - forecast.spawnable))

    def second_choices(self, state, current, held, combo, b2b, turn, full=False):
        if not state.alive or (current is None and state.spawnable < 7):
            return []
        options = []
        sources = [(current, False, held)] if current is not None else []
        if held is not None and held != current:
            sources.append((held, True, current))
        for piece, use_hold, after_hold in sources:
            for placement in self.landings(piece, state.rows, state.solids, full):
                self.check_deadline()
                attack, next_combo, next_b2b = self.attack(placement, combo, b2b)
                following = self.forecast(placement, attack, None, turn, state.warnings)
                reward = 45 * placement.lines + 160 * attack
                value = self.state_value(following, next_combo, next_b2b) + 3 * reward // 4
                options.append(Choice(piece, use_hold, placement, following, next_combo,
                                      next_b2b, after_hold, reward, value))
        options.sort(key=lambda candidate: candidate.value, reverse=True)
        return options

    def tail_value(self, child, turn):
        # A third Hold is only a conditional opportunity after every possible
        # hidden current can spawn. Never pretend stored Hold bypasses top-out.
        if child.hold is None or not child.forecast.alive or child.forecast.spawnable < 7:
            return self.state_value(child.forecast, child.combo, child.b2b)
        best = -10**12
        for placement in self.landings(child.hold, child.forecast.rows, child.forecast.solids):
            self.check_deadline()
            attack, combo, b2b = self.attack(placement, child.combo, child.b2b)
            future = self.forecast(placement, attack, None, turn, child.forecast.warnings)
            value = self.state_value(future, combo, b2b) + (45 * placement.lines + 160 * attack) // 2
            if value > best:
                best = value
        return best

    def root_choices(self, branches, full):
        own = self.observation['self']
        roots = []
        for piece, use_hold, next_piece, stored in branches:
            candidates = []
            for placement in self.landings(piece, full=full):
                self.check_deadline()
                attack, combo, b2b = self.attack(placement, own['combo'], own['b2b'])
                state = self.forecast(placement, attack, next_piece)
                reward = 45 * placement.lines + 160 * attack
                value = self.state_value(state, combo, b2b) + reward
                candidates.append(RootChoice(placement, use_hold, next_piece, stored, state, combo, b2b, reward, value))
            candidates.sort(key=lambda candidate: candidate.value, reverse=True)
            roots.extend(candidates[:6 if len(branches) == 2 else 12])
        return roots

    def plan(self):
        own = self.observation['self']
        turn = self.observation['turn']
        branches = [(own['current'], False, own['next'], own['hold'])]
        if own['can_hold']:
            alternative = own['hold'] if own['hold'] is not None else own['next']
            if alternative != own['current']:
                branches.append((alternative, True, own['next'] if own['hold'] is not None else None, own['current']))
        best = None
        try:
            if self.deadline is not None:
                quick = self.root_choices(branches, full=False)
                best = max(quick, key=lambda candidate: (candidate.value, not candidate.held), default=None)
            roots = self.root_choices(branches, full=True)
            if roots:
                if self.deadline is not None:
                    roots.sort(key=lambda candidate: (candidate.value, not candidate.held), reverse=True)
                best = max(roots, key=lambda candidate: (candidate.value, not candidate.held))
            preliminary = []
            best_value = None
            for candidate in roots:
                children = self.second_choices(candidate.forecast, candidate.next_piece, candidate.hold,
                                               candidate.combo, candidate.b2b, turn + 1)
                value = children[0].value + candidate.reward if children else candidate.value - 10**10
                preliminary.append((value, candidate))
                if best_value is None or value > best_value:
                    best, best_value = candidate, value
            preliminary.sort(key=lambda candidate: candidate[0], reverse=True)
            best_score = None
            for _, candidate in preliminary[:3]:
                children = self.second_choices(candidate.forecast, candidate.next_piece, candidate.hold,
                                               candidate.combo, candidate.b2b, turn + 1, full=True)
                value = candidate.value - 10**10
                for child in children[:4]:
                    score = self.tail_value(child, turn + 2) + candidate.reward + 3 * child.reward // 4
                    value = max(value, score)
                # On equal values prefer keeping Hold; route order stays stable.
                selection = value, not candidate.held
                if best_score is None or selection > best_score:
                    best, best_score = candidate, selection
        except SearchTimeout:
            pass
        if best is None:
            return 'PLAY', ['HARD_DROP']
        return ('HOLD' if best.held else 'PLAY'), best.placement.commands

    def choose(self):
        kind, commands = self.plan()
        return kind, commands if kind == 'PLAY' else None


def decide(observation):
    return Model(observation).choose()


def serve():
    after_hold = None
    for line in sys.stdin:
        started = perf_counter()
        request = json.loads(line)
        observation = request['observation']
        own = observation['self']
        turn = observation['turn']
        if after_hold and after_hold[:2] == (turn, own['current']) and not own['can_hold']:
            kind, commands = 'PLAY', after_hold[2]
        else:
            remaining = request.get('remaining_ms')
            # Reserve time for the protocol, scheduling and the PLAY after HOLD.
            deadline = started + max(0, remaining - 12) / 1000 if type(remaining) is int else None
            kind, commands = Model(observation, deadline).plan()
        after_hold = (turn, own['hold'] or own['next'], commands) if kind == 'HOLD' else None
        response = {'type': kind, 'request_id': request['request_id']}
        if kind == 'PLAY':
            response['commands'] = commands
        print(json.dumps(response, separators=(',', ':')), flush=True)


if __name__ == '__main__':
    serve()
