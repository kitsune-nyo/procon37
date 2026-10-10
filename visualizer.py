"""Local daily replay viewer. Run: python3 visualizer.py [--port 8765]."""
import argparse
import copy
import json
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlparse, parse_qs

ROOT = Path(__file__).resolve().parent
DIRS = [[(-1, 0), (-1, 1), (0, 1), (1, 1), (1, 0), (0, -1)],
        [(-1, -1), (-1, 0), (0, 1), (1, 0), (1, -1), (0, -1)]]
NAMES = ['北西', '北東', '東', '南東', '南西', '西']


def normalize(raw):
    if isinstance(raw, list):
        setting, days = None, {}
        for record in raw:
            kind = record.get('type')
            if kind == 'setting':
                setting = record['setting']
            elif kind == 'day_start':
                info = record['info']
                days.setdefault(info['day'], {}).update(info=info)
            elif kind in ('plan', 'submission'):
                day = days.setdefault(record['day'], {})
                if kind == 'plan':
                    day.update(plans=record['plans'])
                else:
                    day.update(submission=record)
        if setting is None:
            raise ValueError('setting レコードがありません')
        raw = dict(setting=setting, days=[dict(day=d, **v) for d, v in sorted(days.items())])
    if 'problem' in raw:
        p = raw['problem']
        raw = dict(setting=dict(p, map={k: p[k] for k in ('width', 'height', 'cells')},
                                agents=p['agentStarts']), days=[])
    if 'setting' not in raw:
        raise ValueError('試合ログまたは problem を含む設定 JSON を指定してください')
    setting = raw['setting']
    m = setting['map']
    if m['width'] <= 0 or m['height'] <= 0 or len(m['cells']) != m['height'] or any(
            len(row) != m['width'] or any(c not in (0, 1, 2, 3) for c in row) for row in m['cells']):
        raise ValueError('盤面のサイズまたは地形が不正です')
    if m['width'] * m['height'] > 100000 or sum(setting['daySteps']) > 100000:
        raise ValueError('可視化できるサイズの上限を超えています')
    days = []
    for record in raw.get('days', []):
        if 'info' not in record:
            continue
        if 'plans' not in record:
            days.append(dict(day=record['info']['day'], pending=True, info=record['info']))
            continue
        day = replay_day(setting, record['info'], record['plans'])
        day['submission'] = record.get('submission', {})
        days.append(day)
    for day in days:
        if day.get('pending'):
            continue
        following = next((r for r in raw.get('days', []) if r.get('info', {}).get('day') == day['day'] + 1), None)
        day['verification'] = None
        if following and day['submission'].get('accepted'):
            actual = following['info']['agents']
            predicted = [{k: a[k] for k in ('kind', 'pos', 'fuel')} for a in day['frames'][-1]['agents']]
            day['verification'] = dict(matches=actual == predicted, actual=actual, predicted=predicted)
    return dict(setting=setting, days=days)


def replay_day(setting, info, plans):
    """Replay submitted actions with the same ordering/costs as state.cpp.

    Frames are boundary states; agents remain at the origin until arrival.
    Supplies/collection are predictions; next-day observations verify pos/fuel.
    """
    width, height = setting['map']['width'], setting['map']['height']
    cells = setting['map']['cells']
    agents = copy.deepcopy(info['agents'])
    steps = setting['daySteps'][info['day']]
    if len(plans) != len(agents):
        raise ValueError('車両数と行動列数が一致しません')
    for a in agents:
        if a['kind'] not in (0, 1) or not 0 <= a['pos'] < width * height or a['fuel'] < 0:
            raise ValueError('車両の開始状態が不正です')
    traffic = {t['pos']: t['status'] for t in info.get('traffics', [])}
    stock = {s['pos']: s['stocks'] for s in setting['spots']}
    brands = {s['pos']: s['brand'] for s in setting['spots']}
    visited = [set() for _ in agents]
    indices = [0] * len(agents)
    busy = [0] * len(agents)
    moving, contacts = {}, set()
    events, frames = [], []
    stats = [dict(moves=0, wait=0, moving=0, supplies=0, received=0, udon=0) for _ in agents]

    def event(t, i, kind, text, **extra):
        events.append(dict(step=t, agent=i, type=kind, text=text, **extra))

    for tick in range(steps + 1):
        if tick:
            for i, a in enumerate(agents):
                if i in moving and busy[i] == tick:
                    dest, fuel, start = moving.pop(i)
                    old = a['pos']
                    a['pos'] = dest
                    if a['kind'] == 0:
                        a['fuel'] -= fuel
                    event(tick, i, 'arrive', f'{old} → {dest} に到着', fuel=a['fuel'])
            for i, a in enumerate(agents):
                pos = a['pos']
                if a['kind'] == 0 and pos in stock and pos not in visited[i] and stock[pos] > 0:
                    stock[pos] -= 1
                    visited[i].add(pos)
                    stats[i]['udon'] += 1
                    event(tick, i, 'collect', f'地点 {pos} / ブランド {brands[pos]} のうどんを取得')
            current_contacts = set()
            for i, s in enumerate(agents):
                if s['kind'] != 1:
                    continue
                for j, p in enumerate(agents):
                    if p['kind'] != 0 or s['pos'] != p['pos']:
                        continue
                    current_contacts.add((i, j))
                    before = p['fuel']
                    if before < setting['fuelLimits']:
                        p['fuel'] = setting['fuelLimits']
                        stats[i]['supplies'] += 1
                        stats[j]['received'] += 1
                        event(tick, i, 'supply', f'車両 {j + 1} に補給: {before} → {p["fuel"]}', target=j, amount=p['fuel']-before)
                        event(tick, j, 'receive', f'補給車 {i + 1} から補給: {before} → {p["fuel"]}', source=i)
                    elif (i, j) not in contacts:
                        event(tick, i, 'meet', f'車両 {j + 1} と合流（燃料満タン）', target=j)
            contacts = current_contacts
        if tick < steps:
            for i, a in enumerate(agents):
                if busy[i] > tick:
                    continue
                if indices[i] >= len(plans[i]):
                    busy[i] = steps
                    stats[i]['wait'] += steps - tick
                    event(tick, i, 'wait', f'行動列終了 / 残り {steps-tick} ステップ待機')
                    continue
                action = plans[i][indices[i]]
                indices[i] += 1
                if not isinstance(action, int) or isinstance(action, bool) or action == -2147483648 or action > 5:
                    raise ValueError(f'車両 {i+1} / step {tick}: 不正な行動 {action}')
                if action < 0:
                    if tick - action > steps:
                        raise ValueError(f'車両 {i+1}: 待機が日末を超えます')
                    busy[i] = tick - action
                    stats[i]['wait'] += -action
                    event(tick, i, 'wait', f'{-action} ステップ待機', until=busy[i])
                    continue
                r, c = divmod(a['pos'], width)
                dr, dc = DIRS[r % 2][action]
                nr, nc = r + dr, c + dc
                if not (0 <= nr < height and 0 <= nc < width) or cells[nr][nc] == 3:
                    raise ValueError(f'車両 {i+1} / step {tick}: 盤面外または池への移動')
                terrain = cells[r][c]
                cost, fuel = {0: (2, 1), 1: ([1, 2, 4][traffic.get(a['pos'], 0)], 2), 2: (3, 2)}[terrain]
                if tick + cost > steps or (a['kind'] == 0 and a['fuel'] < fuel):
                    raise ValueError(f'車両 {i+1} / step {tick}: 時間または燃料不足')
                dest = nr * width + nc
                busy[i] = tick + cost
                moving[i] = (dest, fuel, tick)
                stats[i]['moves'] += 1
                stats[i]['moving'] += cost
                event(tick, i, 'move', f'{NAMES[action]}: {a["pos"]} → {dest} / {cost} step / 燃料 {fuel if a["kind"] == 0 else 0}', dest=dest, until=busy[i])
        frame_agents = []
        for i, a in enumerate(agents):
            state = dict(a, status='完了' if tick == steps else '待機', until=busy[i])
            if i in moving:
                dest, fuel, start = moving[i]
                state.update(status='移動中', dest=dest, start=start)
            frame_agents.append(state)
        frames.append(dict(step=tick, agents=frame_agents, stocks=dict(stock)))
    if any(indices[i] != len(plans[i]) for i in range(len(agents))):
        raise ValueError('日末を超える未消化の行動があります')
    return dict(day=info['day'], steps=steps, info=info, plans=plans, frames=frames, events=events, stats=stats)


def read_data(path):
    content = path.read_text(encoding='utf-8-sig')
    if path.suffix == '.jsonl':
        records = []
        lines = content.splitlines()
        for i, line in enumerate(lines):
            if not line.strip():
                continue
            try:
                records.append(json.loads(line))
            except json.JSONDecodeError:
                # Only an unfinished final line is safe to ignore during live writes.
                if i != len(lines) - 1 or content.endswith('\n'):
                    raise
        return normalize(records)
    return normalize(json.loads(content))


class Handler(SimpleHTTPRequestHandler):
    def json_response(self, data, status=200):
        body = json.dumps(data, ensure_ascii=False).encode('utf-8')
        self.send_response(status)
        self.send_header('Content-Type', 'application/json; charset=utf-8')
        self.send_header('Cache-Control', 'no-store')
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        url = urlparse(self.path)
        if url.path == '/api/files':
            paths = sorted((ROOT / 'output/replays').glob('*.jsonl'), key=lambda p: p.stat().st_mtime, reverse=True)
            paths += sorted((ROOT / 'output/local').glob('*.json'))
            self.json_response([dict(path=p.relative_to(ROOT).as_posix(), name=p.name,
                                     history=p.suffix == '.jsonl') for p in paths])
        elif url.path == '/api/replay':
            try:
                requested = parse_qs(url.query).get('file', [''])[0]
                path = (ROOT / requested).resolve()
                if not path.is_relative_to((ROOT / 'output').resolve()) or path.suffix not in ('.json', '.jsonl'):
                    raise ValueError('output 内の JSON / JSONL を指定してください')
                self.json_response(read_data(path))
            except (ValueError, KeyError, IndexError, TypeError, OSError) as e:
                self.json_response(dict(error=str(e)), 400)
        else:
            super().do_GET()

    def do_POST(self):
        if self.path != '/api/import':
            self.json_response(dict(error='Not found'), 404)
            return
        try:
            size = int(self.headers.get('Content-Length', '0'))
            if not 0 < size <= 20_000_000:
                raise ValueError('ファイルの上限は 20 MB です')
            content = self.rfile.read(size).decode('utf-8-sig')
            try:
                raw = json.loads(content)
            except json.JSONDecodeError:
                raw = [json.loads(line) for line in content.splitlines() if line.strip()]
            self.json_response(normalize(raw))
        except (ValueError, KeyError, IndexError, TypeError) as e:
            self.json_response(dict(error=str(e)), 400)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', type=int, default=8765)
    args = parser.parse_args()
    server = ThreadingHTTPServer(('127.0.0.1', args.port), partial(Handler, directory=str(ROOT / 'visualizer')))
    print(f'Visualizer: http://localhost:{args.port}', flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == '__main__':
    main()
