# Hosting a game list server for wkbai

wkbai lists and joins internet games through a [Nakama](https://heroiclabs.com/nakama/) server
(Heroic Labs, Apache-2.0). Nakama needs nothing wkbai-specific: no server modules, only these
settings. This folder runs one with Docker Compose. The server comes with its database, HTTPS
from Let's Encrypt, and, if wanted, a STUN server.

## What it takes

- A Linux machine with a public address: any small VPS (1 CPU, 1–2 GB RAM is plenty), or
  Oracle Cloud's free tier. Docker with the Compose plugin installed on it.
- A domain name (or a subdomain) pointing at that machine. Caddy needs it to get a certificate.
- Open in the machine's firewall: TCP 80 and 443, and UDP 3478 for the STUN server.

## Setting it up

```sh
git clone <wkbai> && cd wkbai/server      # or copy this folder to the server
python3 make_env.py games.example.org     # .env: your domain, new random keys
docker compose up -d                      # Nakama, PostgreSQL, Caddy
docker compose --profile stun up -d       # also the STUN server (recommended)
docker compose logs -f nakama             # "Startup done" when it is ready
```

`make_env.py` prints what players enter. Check it from anywhere:
`curl https://games.example.org/healthcheck` answers `{}`.

## What players set

On wkbai's Options page, under **Online**:

| Setting | Value |
|---|---|
| Nakama server | `https://games.example.org` |
| Nakama server key | `SERVER_KEY` from `.env` |
| Its name in the game list | what the Find Games list shows above your server's games |
| STUN server | `games.example.org:3478` with the stun profile; empty uses a public one |

Or the same in `wkconfig.json`:

```json
"matchmaking": {
  "nakama": { "server": "https://games.example.org", "serverKey": "wkbai-...", "title": "Example League" },
  "stun": "games.example.org:3478"
}
```

The server key only says which server this is. It is in every player's settings, so it is no
secret: publish it with the address.

## How it works

1. **Sign-in:** each wkbai installation signs in with a random device id, which it keeps in
   `matchmaking.json` in its user folder. Nakama makes an account for it the first time.
   There is no registration and no password.
2. **Hosting:** a host that offers its game on the server writes a public storage object
   (collection `wkbai_games`) holding the listing: name, map, players, lock, mods, version and
   addresses. It writes the object again every 20 s, and the list leaves out an object not
   written for 60 s. It also opens a realtime match, through which guests reach it.
3. **Joining:** the guest sends its addresses through the match. The host's game port sends a
   few packets towards them, which opens its router's NAT, and the guest connects directly
   over ENet. From then on the server carries nothing.
4. **Relay:** if no direct connection comes up within about 9 s, the game's packets go through
   the match instead. Players can choose this always, or never.

## What it costs

- **Listings and signalling:** a small write per hosted game every 20 s, and a list request
  every few seconds per player looking at the list. Hardly anything.
- **Relayed games:** all of that game's traffic, in and out, for every player in it. A game
  starts with about 120 KB, then a steady stream. Most games connect directly when the players
  have a STUN server, so offer one (the stun profile).

## Running it

- **Console:** Nakama's admin console listens only on the server itself. To reach it:
  `ssh -L 7351:127.0.0.1:7351 you@server`, then open `http://127.0.0.1:7351` with
  `CONSOLE_USER` and `CONSOLE_PASSWORD` from `.env`. It shows accounts and storage objects, and
  can ban an account.
- **Updates:** change the image tag in `docker-compose.yml`, then `docker compose up -d`. The
  database migration runs at every start.
- **Backups:** the `postgres` volume holds everything (accounts and listings), and none of it
  is precious. Keep `.env`: a new `SERVER_KEY` means every player has to change their settings.

## Limits to know

- **Addresses are visible:** a listing carries the host's public address and its local network
  addresses, as game browsers do. Every player using the server sees them.
- **No moderation built in:** anyone with the server key can sign in and list games. Each
  account writes only its own listing, but spam listings are possible. A Nakama server module
  could add checks and rate limits. Until then, the console can ban an account.
- **One server:** wkbai shows the games of one Nakama server at a time.
