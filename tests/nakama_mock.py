"""A stand-in for a Nakama server (Heroic Labs) for wkbai's matchmaking tests: the parts of
Nakama's API wkbai uses (wkbre2/matchmaking/NakamaService.cpp), on 127.0.0.1 only, Python's
standard library only.

    POST /v2/account/authenticate/device?create=true   Basic <server key>:  {"id": device id}
    PUT  /v2/storage                                    write objects (Bearer token)
    GET  /v2/storage/<collection>                       publicly readable objects of every user
    PUT  /v2/storage/delete                             delete objects
    GET  /ws?token=...                                  the realtime socket (JSON envelopes):
         match_create, match_join, match_leave, match_data_send -> match, match_data,
         match_presence_event

It follows Nakama's JSON as documented (int64 op codes as strings, bytes as base64, RFC 3339
times); it is no substitute for a test against a real server. Prints what happens.

Run: python tests/nakama_mock.py [port]   (default 7350)
"""
import asyncio
import base64
import hashlib
import json
import sys
import time
import uuid
from datetime import datetime, timezone
from urllib.parse import urlparse, parse_qs

SERVER_KEY = 'defaultkey'
users = {}       # device id: user id
tokens = {}      # token: user id
storage = {}     # (collection, key, user id): object
matches = {}     # match id: {session id: presence}
sockets = {}     # session id: (writer, presence)
stats = {'match_data': 0, 'match_data_bytes': 0}


def log(*args):
    print('NAKAMA-MOCK', *args, flush=True)


def now_text():
    return datetime.now(timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ')


def make_token(user_id):
    def b64(d):
        return base64.urlsafe_b64encode(json.dumps(d).encode()).rstrip(b'=').decode()
    token = b64({'alg': 'HS256', 'typ': 'JWT'}) + '.' + b64({'uid': user_id, 'usn': 'u' + user_id[:8], 'exp': int(time.time()) + 3600}) + '.sig'
    tokens[token] = user_id
    return token


async def respond(writer, status, body, headers=None):
    data = json.dumps(body).encode() if not isinstance(body, bytes) else body
    reason = {200: 'OK', 400: 'Bad Request', 401: 'Unauthorized', 404: 'Not Found'}.get(status, 'OK')
    head = f'HTTP/1.1 {status} {reason}\r\nContent-Type: application/json\r\nContent-Length: {len(data)}\r\nConnection: close\r\n'
    for k, v in (headers or {}).items():
        head += f'{k}: {v}\r\n'
    writer.write(head.encode() + b'\r\n' + data)
    await writer.drain()
    writer.close()


def bearer(headers):
    auth = headers.get('authorization', '')
    return tokens.get(auth[7:]) if auth.startswith('Bearer ') else None


async def handle_http(method, path, query, headers, body, writer):
    if method == 'POST' and path == '/v2/account/authenticate/device':
        auth = headers.get('authorization', '')
        if not auth.startswith('Basic ') or base64.b64decode(auth[6:]).decode() != SERVER_KEY + ':':
            return await respond(writer, 401, {'error': 'Server key invalid', 'code': 16})
        device = json.loads(body or b'{}').get('id', '')
        if len(device) < 10:
            return await respond(writer, 400, {'error': 'Device ID invalid', 'code': 3})
        created = device not in users
        users.setdefault(device, str(uuid.uuid4()))
        log('authenticated device', device, 'user', users[device], 'created' if created else '')
        return await respond(writer, 200, {'created': created, 'token': make_token(users[device]), 'refresh_token': 'r'})
    user = bearer(headers)
    if user is None:
        return await respond(writer, 401, {'error': 'Auth token invalid', 'code': 16})
    if method == 'PUT' and path == '/v2/storage':
        acks = []
        for o in json.loads(body).get('objects', []):
            json.loads(o['value'])  # Nakama takes JSON objects only
            k = (o['collection'], o['key'], user)
            old = storage.get(k)
            storage[k] = {'collection': o['collection'], 'key': o['key'], 'user_id': user, 'value': o['value'],
                          'version': uuid.uuid4().hex, 'permission_read': o.get('permission_read', 1),
                          'permission_write': o.get('permission_write', 1),
                          'create_time': old['create_time'] if old else now_text(), 'update_time': now_text()}
            acks.append({'collection': o['collection'], 'key': o['key'], 'version': storage[k]['version'], 'user_id': user})
            log('storage write', o['collection'], o['key'], 'by', user, o['value'][:160])
        return await respond(writer, 200, {'acks': acks})
    if method == 'PUT' and path == '/v2/storage/delete':
        for o in json.loads(body).get('object_ids', []):
            storage.pop((o['collection'], o['key'], user), None)
            log('storage delete', o['collection'], o['key'], 'by', user)
        return await respond(writer, 200, {})
    if method == 'GET' and path.startswith('/v2/storage/'):
        collection = path[len('/v2/storage/'):]
        owner = query.get('user_id', [''])[0]
        objects = [o for (c, k, u), o in storage.items() if c == collection and (o['permission_read'] == 2 or u == user)
                   and (not owner or u == owner)]
        return await respond(writer, 200, {'objects': objects, 'cursor': ''})
    return await respond(writer, 404, {'error': 'Not found', 'code': 5})


# WebSocket --------------------------------------------------------------------------------

async def ws_send(writer, obj):
    data = json.dumps(obj).encode()
    head = bytes([0x81])
    n = len(data)
    if n < 126:
        head += bytes([n])
    elif n < 65536:
        head += bytes([126]) + n.to_bytes(2, 'big')
    else:
        head += bytes([127]) + n.to_bytes(8, 'big')
    writer.write(head + data)
    await writer.drain()


async def ws_read(reader):
    """The next message's text, or None when closed"""
    message = b''
    while True:
        h = await reader.readexactly(2)
        opcode, n = h[0] & 0x0F, h[1] & 0x7F
        if n == 126:
            n = int.from_bytes(await reader.readexactly(2), 'big')
        elif n == 127:
            n = int.from_bytes(await reader.readexactly(8), 'big')
        mask = await reader.readexactly(4) if h[1] & 0x80 else b'\0\0\0\0'
        payload = bytes(b ^ mask[i % 4] for i, b in enumerate(await reader.readexactly(n)))
        if opcode == 8:
            return None
        if opcode in (9, 10):
            continue
        message += payload
        if h[0] & 0x80:
            return message.decode()


async def leave(session, match_id):
    members = matches.get(match_id)
    if not members or session not in members:
        return
    presence = members.pop(session)
    for other in members:
        if other in sockets:
            await ws_send(sockets[other][0], {'match_presence_event': {'match_id': match_id, 'leaves': [presence]}})
    log('match leave', match_id, presence['user_id'])
    if not members:
        matches.pop(match_id, None)


async def handle_ws(user, reader, writer):
    session = str(uuid.uuid4())
    presence = {'user_id': user, 'session_id': session, 'username': 'u' + user[:8]}
    sockets[session] = (writer, presence)
    log('socket open', user)
    try:
        while True:
            text = await ws_read(reader)
            if text is None:
                break
            m = json.loads(text)
            cid = m.get('cid')
            if 'match_create' in m:
                match_id = str(uuid.uuid4()) + '.'
                matches[match_id] = {session: presence}
                log('match create', match_id, 'by', user)
                await ws_send(writer, {'cid': cid, 'match': {'match_id': match_id, 'authoritative': False, 'label': '', 'size': 1, 'self': presence, 'presences': []}})
            elif 'match_join' in m:
                match_id = m['match_join'].get('match_id')
                members = matches.get(match_id)
                if members is None:
                    await ws_send(writer, {'cid': cid, 'error': {'code': 4, 'message': 'Match not found'}})
                    continue
                others = list(members.values())
                members[session] = presence
                log('match join', match_id, 'by', user)
                await ws_send(writer, {'cid': cid, 'match': {'match_id': match_id, 'authoritative': False, 'label': '', 'size': len(members), 'self': presence, 'presences': others}})
                for other in members:
                    if other != session and other in sockets:
                        await ws_send(sockets[other][0], {'match_presence_event': {'match_id': match_id, 'joins': [presence]}})
            elif 'match_leave' in m:
                await leave(session, m['match_leave'].get('match_id'))
                if cid:
                    await ws_send(writer, {'cid': cid})
            elif 'match_data_send' in m:
                d = m['match_data_send']
                members = matches.get(d.get('match_id'), {})
                if session not in members:
                    continue
                targets = [p['session_id'] for p in d.get('presences') or []] or [s for s in members if s != session]
                raw = base64.b64decode(d.get('data', ''))
                op = int(d.get('op_code', 0))
                if op != 1:
                    log('match data op', op, 'from', user, raw[:200])
                else:
                    stats['match_data'] += 1
                    stats['match_data_bytes'] += len(raw)
                if op == 1 and stats['match_data'] % 200 == 1:
                    log('match data relayed', stats['match_data'], 'messages', stats['match_data_bytes'], 'bytes')
                for t in targets:
                    if t in members and t in sockets:
                        await ws_send(sockets[t][0], {'match_data': {'match_id': d['match_id'], 'presence': presence,
                                                                     'op_code': str(op), 'data': d.get('data', ''), 'reliable': True}})
            elif cid:
                await ws_send(writer, {'cid': cid, 'error': {'code': 3, 'message': 'not in the mock'}})
    except (asyncio.IncompleteReadError, ConnectionError):
        pass
    finally:
        for match_id in list(matches):
            await leave(session, match_id)
        sockets.pop(session, None)
        log('socket closed', user, 'relayed so far', stats)


async def client(reader, writer):
    try:
        request = await reader.readuntil(b'\r\n\r\n')
    except (asyncio.IncompleteReadError, asyncio.LimitOverrunError, ConnectionError):
        writer.close()
        return
    lines = request.decode('latin-1').split('\r\n')
    method, target, _ = lines[0].split(' ', 2)
    headers = {}
    for line in lines[1:]:
        if ':' in line:
            k, v = line.split(':', 1)
            headers[k.strip().lower()] = v.strip()
    url = urlparse(target)
    query = parse_qs(url.query)
    if url.path == '/ws' and headers.get('upgrade', '').lower() == 'websocket':
        user = tokens.get(query.get('token', [''])[0])
        if user is None:
            return await respond(writer, 401, {'error': 'Auth token invalid'})
        accept = base64.b64encode(hashlib.sha1((headers['sec-websocket-key'] + '258EAFA5-E914-47DA-95CA-C5AB0DC85B11').encode()).digest()).decode()
        writer.write(f'HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: {accept}\r\n\r\n'.encode())
        await writer.drain()
        return await handle_ws(user, reader, writer)
    length = int(headers.get('content-length', '0'))
    body = await reader.readexactly(length) if length else b''
    await handle_http(method, url.path, query, headers, body, writer)


async def main(port):
    server = await asyncio.start_server(client, '127.0.0.1', port)
    log('listening on 127.0.0.1:%d' % port)
    async with server:
        await server.serve_forever()


if __name__ == '__main__':
    asyncio.run(main(int(sys.argv[1]) if len(sys.argv) > 1 else 7350))
