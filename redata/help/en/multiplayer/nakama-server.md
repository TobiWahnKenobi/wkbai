---
title: Hosting a Nakama server
ids: [multiplayer/nakama-server]
parent: options
order: 61
---

# Hosting a Nakama server

This guide shows you how to run your own game list server for wkbai's online play, step by step. You need it only if you want players outside your local network to find each other's games in [Find Games](../ui/frontend/multiplayer/index.md#find-games). Games on your local network are found without any server.

wkbai has no official server built in. Anyone can run one, for themselves, their friends or a whole community. Each player then enters its address on the [Online options](../options/online.md) page.

## What the server does

wkbai uses Nakama, a game server by Heroic Labs. Nakama is free software under the Apache-2.0 licence. wkbai doesn't ship it: you download it from Heroic Labs when you follow this guide.

The server does three jobs:

- **Sign-in:** each wkbai signs in with a random id it makes the first time (kept in `matchmaking.json` in your user folder, `%APPDATA%\wkbai`). Nakama makes an account for it. There is no registration and no password.
- **The game list:** a player who hosts a game writes its listing to the server (name, map, players, lock, mods, version and network addresses) and writes it again every 20 seconds. Find Games leaves out a listing that hasn't been written for 60 seconds, because its host is gone.
- **Meeting and relay:** when you join, the two computers first swap their addresses through the server and try to connect directly. If that doesn't work within about 9 seconds, the game's traffic goes through the server instead (the relay).

wkbai needs nothing special on the server: no server module, no scripts, no leaderboards, only Nakama itself with a few settings. wkbai's source has a ready-made setup for it, the `server` folder. This guide uses that folder. The file `SOURCE.txt` beside `wkbai.exe` says where to get wkbai's source.

## Choose where it runs

| | A rented server (VPS) | Your own PC |
|---|---|---|
| Good for | a server many people use, always on | trying it out, a few friends, a LAN party over the internet |
| Costs | a small monthly fee (1 CPU and 1 to 2 GB of memory is plenty) | nothing, but your PC must be on |
| Address | a domain name, with HTTPS | your internet address and a port, without HTTPS |
| Needs | a Linux server and a domain name | Docker Desktop, and a port opened on your router |

If you only want to see how it works, start with [your own PC](#way-b-on-your-own-windows-pc). For a server other people rely on, use [a rented server](#way-a-on-a-rented-server).

## Way A: on a rented server

This sets up Nakama, its database (PostgreSQL), Caddy (a web server that gets a free HTTPS certificate from Let's Encrypt) and, if you want, a STUN server. All of them run in Docker.

### 1. Rent a server and a name

1. Rent a small Linux server from any hosting company. These steps assume Ubuntu 24.04 LTS (64-bit). Note its public IP address.
2. Get a domain name, or a subdomain of one you have, for example `games.example.org`.
3. In your domain's DNS settings, add an **A record** for that name that points to the server's IP address (and an AAAA record if the server has an IPv6 address). It can take a while before the name works everywhere.

### 2. Install Docker

Log in to the server with SSH and run Docker's install script, as Docker's documentation shows it:

```sh
curl -fsSL https://get.docker.com -o get-docker.sh
sudo sh get-docker.sh
```

Check that it works:

```sh
sudo docker run hello-world
```

It prints a short greeting from Docker. The script also installs Docker Compose (`docker compose`). In the steps below, put `sudo` before `docker` unless you have added your user to the `docker` group.

### 3. Open the firewall

The server needs these ports open to the internet:

| Port | What for |
|---|---|
| TCP 22 | SSH, so that you can still log in |
| TCP 80 | Caddy, to get and renew the certificate |
| TCP 443 | HTTPS: wkbai's sign-in, the game list and the realtime connection |
| UDP 3478 | the STUN server, if you run it (step 6) |

With Ubuntu's own firewall:

```sh
sudo ufw allow 22/tcp
sudo ufw allow 80/tcp
sudo ufw allow 443/tcp
sudo ufw allow 3478/udp
sudo ufw enable
```

Many hosting companies also have a firewall in their web control panel. Open the same ports there.

Never open 7350, 7351 or 5432 on this server: Nakama's own ports and the database stay inside, and only Caddy talks to the internet. See [The ports](#the-ports).

### 4. Get the server folder

Copy the `server` folder of wkbai's source to the server, for example with `git`:

```sh
git clone <the address of wkbai's source>
cd wkbai/server
```

Or copy the folder with any file transfer tool (SFTP, `scp`) and `cd` into it. It holds:

- `docker-compose.yml`: the four programs and how they start;
- `nakama.yml`: Nakama's settings for wkbai;
- `Caddyfile`: HTTPS for your domain;
- `make_env.py`: writes your keys;
- `README.md`: the same setup, in short.

### 5. Make your keys

```sh
python3 make_env.py games.example.org
```

Use your own domain name. This writes a file `.env` beside it with your domain and new random keys for everything that has a default in Nakama (see [The keys](#the-keys)). It prints what players enter:

```
Players set, on the Options page (Online):
  Nakama server:      https://games.example.org
  Nakama server key:  wkbai-...
```

Keep `.env` safe and make a copy of it somewhere else. If you lose it and make new keys, every player has to enter the new server key. `make_env.py` never overwrites an existing `.env`.

### 6. Start it

```sh
docker compose up -d
docker compose --profile stun up -d
```

The first line starts Nakama, PostgreSQL and Caddy. The second line also starts the STUN server (coturn, STUN only). It is optional but recommended: it tells players their public address, which most direct connections between home networks need. Without your own, players use a public one.

Watch Nakama start:

```sh
docker compose logs -f nakama
```

It is ready when the log says `Startup done`. Press Ctrl+C to stop watching; the server keeps running. The containers start again by themselves after the server reboots.

### 7. Check it from outside

From any computer:

```sh
curl https://games.example.org/healthcheck
```

The answer is `{}`. You can also open that address in a browser. If it fails, see [Problems](#problems).

### 8. Tell the players

Give your players the two lines that `make_env.py` printed, and the STUN address if you started it (`games.example.org:3478`). The server key is in every player's settings, so it is no secret: you can publish it with the address. Then [set up wkbai](#set-up-wkbai).

## Way B: on your own Windows PC

This runs Nakama and its database in Docker Desktop on your PC. You play on the same PC as usual.

### 1. Install Docker Desktop

1. Download Docker Desktop for Windows from docker.com and install it. It uses WSL 2; let the installer turn it on if it asks, and restart when it asks.
2. Start Docker Desktop and wait until it says the engine is running.
3. Open PowerShell and check:

   ```
   docker run hello-world
   ```

### 2. Make a folder with the files

1. Copy the `server` folder of wkbai's source to a folder of your own, for example `C:\nakama`. You need `nakama.yml` and `make_env.py` from it.
2. Make the keys. Any name with a dot will do here; the home setup doesn't use it, and you can ignore the `https://` line it prints:

   ```
   cd C:\nakama
   python make_env.py home.example
   ```

   This needs Python (from python.org or the Microsoft Store). Without Python, write the file `.env` yourself in Notepad, with the same names as in [The keys](#the-keys) and a long random value for each, made of letters, digits, `-` and `_` only:

   ```
   POSTGRES_PASSWORD=...
   SERVER_KEY=...
   SESSION_KEY=...
   REFRESH_KEY=...
   CONSOLE_USER=admin
   CONSOLE_PASSWORD=...
   CONSOLE_SIGNING_KEY=...
   HTTP_KEY=...
   ```

3. In the same folder, make a file `docker-compose.home.yml` with this text. It is the `server` folder's setup without Caddy and STUN, with Nakama's port open on your PC:

   ```yaml
   # Nakama for wkbai on a home PC (no HTTPS): docker compose -f docker-compose.home.yml up -d
   services:
     postgres:
       image: postgres:16-alpine
       restart: unless-stopped
       environment:
         POSTGRES_DB: nakama
         POSTGRES_PASSWORD: ${POSTGRES_PASSWORD:?run make_env.py first}
       volumes:
         - postgres:/var/lib/postgresql/data
       healthcheck:
         test: ["CMD", "pg_isready", "-U", "postgres", "-d", "nakama"]
         interval: 5s
         timeout: 5s
         retries: 10

     nakama:
       image: heroiclabs/nakama:3.41.0
       restart: unless-stopped
       depends_on:
         postgres:
           condition: service_healthy
       entrypoint:
         - /bin/sh
         - -ec
         - >
           /nakama/nakama migrate up --database.address "postgres:${POSTGRES_PASSWORD}@postgres:5432/nakama" &&
           exec /nakama/nakama --config /nakama/data/wkbai.yml
           --database.address "postgres:${POSTGRES_PASSWORD}@postgres:5432/nakama"
           --socket.server_key "${SERVER_KEY:?}"
           --session.encryption_key "${SESSION_KEY:?}"
           --session.refresh_encryption_key "${REFRESH_KEY:?}"
           --console.username "${CONSOLE_USER:?}"
           --console.password "${CONSOLE_PASSWORD:?}"
           --console.signing_key "${CONSOLE_SIGNING_KEY:?}"
           --runtime.http_key "${HTTP_KEY:?}"
       volumes:
         - ./nakama.yml:/nakama/data/wkbai.yml:ro
       ports:
         - "7350:7350"            # wkbai's port
         - "127.0.0.1:7351:7351"  # the console, this PC only
       healthcheck:
         test: ["CMD", "/nakama/nakama", "healthcheck"]
         interval: 10s
         timeout: 5s
         retries: 5

   volumes:
     postgres:
   ```

   Heroic Labs' own example file opens the database's port and the console to your whole network; this one doesn't.

### 3. Start it

```
cd C:\nakama
docker compose -f docker-compose.home.yml up -d
docker compose -f docker-compose.home.yml logs -f nakama
```

Wait for `Startup done`, then press Ctrl+C. Open `http://127.0.0.1:7350/healthcheck` in your browser: it shows `{}`.

If the log says Nakama can't read its config file, write the file's full path in the `volumes` line the way Heroic Labs' Docker guide shows it for Windows, for example `/c/nakama/nakama.yml:/nakama/data/wkbai.yml:ro`.

### 4. Let your friends in

On your own PC, wkbai reaches the server at `http://127.0.0.1:7350`. Players elsewhere need a way in through your router:

1. In your router's settings, forward **TCP port 7350** to your PC's local address (for example `192.168.1.20`). Look up "port forwarding" in your router's manual.
2. Allow incoming TCP 7350 in Windows Defender Firewall if it asks, or add an inbound rule for it.
3. Find your public internet address (any "what is my IP" website shows it). Your friends enter `http://<that address>:7350` as the **Nakama server**.
4. Most home connections get a new public address now and then. A free dynamic DNS name keeps one name pointing at it.

Forward only 7350. Never forward 7351 (the console).

This way has no HTTPS: the sign-in token goes over the internet unencrypted. That is fine among friends; for a public server, use [a rented server](#way-a-on-a-rented-server).

Some internet providers share one public address among many customers (carrier-grade NAT). Then port forwarding can't work: if the address your router shows for itself differs from the one websites show, you are behind one. Use a rented server instead.

## Set up wkbai

Every player, the server's owner too, does this once:

1. Open **Options** and go to the **Online** page.
2. **Nakama server:** the address, for example `https://games.example.org` (rented server), `http://203.0.113.5:7350` (someone's home PC) or `http://127.0.0.1:7350` (the server is on this PC). Always with `https://` or `http://`; with the port when it isn't the usual one.
3. **Nakama server key:** the `SERVER_KEY` from the server's `.env`.
4. **Its name in the game list:** any name, up to 40 characters, for the heading of this server's games in Find Games, for example your community's name. Empty: "Nakama".
5. **Games through the Nakama server:** leave it at **If direct fails**.
6. **STUN server (public address):** `games.example.org:3478` if the server runs the STUN server; otherwise leave it empty.
7. Press **OK**. wkbai signs in at once.

[Online](../options/online.md) explains each of these settings. They are kept in `wkconfig.json`, where they look like this:

```json
"matchmaking": {
  "nakama": { "server": "https://games.example.org", "serverKey": "wkbai-...", "title": "Example League", "relay": "auto" },
  "stun": "games.example.org:3478"
}
```

## Check that it works

1. Open **Multiplayer** and the **Find Games** tab. Below the local network's games there is a heading with your server's name. While wkbai signs in, it says "(signing in)". When it is ready and nobody hosts, it says "(no games found)". If it says "(failed: ...)", see [Problems](#problems).

wkbai signs in once when it starts and again whenever you change a setting on the Online page and press OK. It doesn't try again by itself: after a failed sign-in, fix the cause and restart wkbai (or press OK after changing the setting that was wrong).
2. Host a game: choose a map on the **New Game** tab and press **Create**. In the host's lobby, tick your server in the [Offer on](../ui/frontend/multiplayer/host.md#offer-on) row. A server that isn't ready is grey there and shows its state; hold the mouse over it to read why.
3. On another computer (with the same settings), the game appears under your server's heading within a few seconds. Your own game never shows in your own list.
4. Join it. The first try is a direct connection; if it doesn't come up within about 9 seconds, wkbai switches to the relay by itself.

wkbai also writes what happens to its console output, lines that start with `Matchmaking: Nakama` (for example `signed in at ...` or `sign-in failed: ...`).

## Problems

| What Find Games shows | What it means | What to do |
|---|---|---|
| (off: no server set ...) | The **Nakama server** field is empty. | Enter the address and press OK. |
| (failed: no answer from ...) | wkbai can't reach the address at all. | Check the address, `http` or `https`, and the port. Is the server running (`docker compose ps`)? Is the port open in the firewall, the hosting panel or the router? Does `/healthcheck` answer in a browser? |
| (failed: the server refused the sign-in (...)) | The server answered, but didn't accept the key. | Enter the server's `SERVER_KEY` exactly, without spaces. An empty field means `defaultkey`, which a server set up this way doesn't accept. |
| (failed: the server's answer to the sign-in wasn't understood) | Something else than Nakama answered at that address, such as a web page. | Check the address; with your own reverse proxy, check that it passes everything to Nakama's port 7350. |
| (failed: the realtime socket: ...) | The sign-in worked, but the realtime connection (a WebSocket on `/ws`) didn't. | A reverse proxy or firewall that blocks WebSockets. The `server` folder's Caddy passes them. |
| (failed: the connection to the server was lost) | The server stopped or the network broke. | Check the server, then restart wkbai. |
| Games show up, then vanish | Listings older than 60 seconds are left out, measured with your PC's clock. | Set Windows to set the time automatically, on the host's and the guest's PC. |
| Joining always ends after a while | No direct connection, and the relay is off or isn't answering. | Set **Games through the Nakama server** to **If direct fails** or **Always** on the joining PC. |
| A game is grey, "other version" | The host runs another version of wkbai. | Both players need the same version. |

Joining is faster, and the server carries less, when the direct connection works. These help:

- a STUN server (yours or the default public one), so that each side learns its public address;
- on the host's router, forwarding UDP port 1234, the game's own port ([hostPort](../options/config-file.md#hostport)), to the host's PC. It isn't required: wkbai opens the way itself in most routers.

## The ports

| Port | Program | Open to the internet? |
|---|---|---|
| TCP 7350 | Nakama: sign-in, game list and realtime socket (HTTP and WebSocket). The only Nakama port wkbai uses. | Home PC: yes, forward it. Rented server: no, Caddy forwards to it. |
| TCP 443, TCP 80 | Caddy: HTTPS in front of 7350, and the certificate | Rented server: yes |
| UDP 3478 | the STUN server (coturn) | yes, if you run it |
| TCP 7351 | Nakama's console | never |
| TCP 7349 | Nakama's gRPC API (wkbai doesn't use it) | no; the `server` folder doesn't publish it |
| TCP 5432 | the database | never |

## The keys

A new Nakama server starts with well-known default keys. Heroic Labs' configuration guide says to change them before going live. `make_env.py` writes a new random value for each into `.env`, and `docker-compose.yml` passes them to Nakama:

| In `.env` | Nakama's setting | Default | What it is |
|---|---|---|---|
| `SERVER_KEY` | `socket.server_key` | `defaultkey` | The key every client sends to sign in. Players enter it as **Nakama server key**. Not secret. |
| `SESSION_KEY` | `session.encryption_key` | `defaultencryptionkey` | Signs the players' session tokens. Secret. |
| `REFRESH_KEY` | `session.refresh_encryption_key` | `defaultrefreshencryptionkey` | Signs refresh tokens. Secret. |
| `CONSOLE_USER`, `CONSOLE_PASSWORD` | `console.username`, `console.password` | `admin`, `password` | Your login to the console. Secret. |
| `CONSOLE_SIGNING_KEY` | `console.signing_key` | | Signs console logins. Secret. |
| `HTTP_KEY` | `runtime.http_key` | `defaulthttpkey` | For server-to-server calls; wkbai doesn't use it, but it must not stay at its default. Secret. |
| `POSTGRES_PASSWORD` | the database password | | Only Nakama uses it. Secret. |

Changing a secret key later is harmless: players simply sign in again. Changing `SERVER_KEY` means every player must enter the new one.

## The settings in nakama.yml

The `server` folder's `nakama.yml` changes a few of Nakama's defaults for wkbai. Keep them:

- **`session.token_expiry_sec: 3600`:** a session lasts an hour; wkbai signs in again before it ends.
- **`socket.max_message_size_bytes: 8192`:** a relayed game sends messages of about 3.3 KB. Nakama's default limit is 4096 bytes.
- **`socket.outgoing_queue_size: 256`:** how many messages may wait for a slow player before Nakama drops that player. A relayed game sends bursts while it starts, more than Nakama's default allows.

## HTTPS

wkbai supports `https://` addresses. It then uses an encrypted WebSocket (`wss://`) for the realtime connection too, by itself; there is nothing else to set. It uses Windows' own HTTPS, so the certificate must be one Windows trusts, such as Let's Encrypt's. A self-signed certificate is refused.

- The `server` folder's Caddy does all of this.
- With your own reverse proxy (nginx, Apache, Traefik), forward everything to Nakama's port 7350, including WebSocket upgrades on `/ws`.
- Nakama can also serve HTTPS itself (`socket.ssl_certificate` and `socket.ssl_private_key`), but Heroic Labs' configuration guide says this is not recommended for production.

## The console

Nakama has a web console, where you see the accounts and the stored listings, and can ban an account. It listens only on the server itself (port 7351).

- **On your own PC:** open `http://127.0.0.1:7351`.
- **On a rented server:** open an SSH tunnel from your PC, then open `http://127.0.0.1:7351` on your PC:

  ```sh
  ssh -L 7351:127.0.0.1:7351 you@games.example.org
  ```

Log in with `CONSOLE_USER` and `CONSOLE_PASSWORD` from `.env`. While someone hosts a game, its listing is a storage object in the collection `wkbai_games`.

## Updating

1. Read the release notes of the new version on Nakama's GitHub page (github.com/heroiclabs/nakama/releases).
2. Make a [backup](#backups).
3. In `docker-compose.yml` (or `docker-compose.home.yml`), change the version in `image: heroiclabs/nakama:3.41.0` to the new one.
4. Restart with the new version:

   ```sh
   docker compose pull
   docker compose up -d
   ```

   (with `-f docker-compose.home.yml` on a home PC.) The database is upgraded at every start (`nakama migrate up`), before Nakama starts.

wkbai was tested with Nakama 3.41.0, the version in the `server` folder. Heroic Labs' Docker guide shows 3.37.0, and newer versions are out (3.42.0 at the time of writing). wkbai uses only Nakama's basic functions (device sign-in, storage, relayed matches), which a newer 3.x version should keep, but test before you change a server others use.

The other programs update the same way: `postgres:16-alpine` and `caddy:2` get fixes with `docker compose pull`. Don't change PostgreSQL's main version (16) on a filled database without moving the data yourself.

## Backups

Everything is in the database: the accounts and the current listings. None of it is precious, because players get a new account by signing in again. What matters is `.env`, so keep a copy of it.

To save the database anyway:

```sh
docker compose exec postgres pg_dump -U postgres nakama > nakama-backup.sql
```

- `docker compose down` stops everything and keeps the data.
- `docker compose down -v` also deletes the database. Use it only to start from scratch.

## About the database

Heroic Labs officially supports CockroachDB, and Nakama works with any PostgreSQL-compatible database. Their Docker guide uses PostgreSQL, and so does wkbai's `server` folder (PostgreSQL 16). For a game list server, PostgreSQL is all you need.

## Things to know

- **Costs:** the game list itself is tiny: one small write per hosted game every 20 seconds, and a list request every few seconds per player looking at Find Games. A relayed game carries all of that game's traffic, about 120 KB at its start and then a steady stream, so a STUN server that lets most games connect directly saves the most.
- **Addresses are visible:** a listing holds the host's public address and local network addresses, as game browsers do. Everyone using the server sees them.
- **No moderation:** anyone with the server key can sign in and list games. Each account can write only its own listing, but spam listings are possible. You can ban an account in the console.
- **One server at a time:** wkbai shows the games of one Nakama server. Players who want to meet must use the same server.

> wkbai: the original game's online play went through GameSpy, which no longer exists. This server is wkbai's own replacement. wkbai is not affiliated with Heroic Labs.
