"""Writes .env for docker-compose.yml: the server's domain and new random keys.

    python3 make_env.py games.example.org

An existing .env is kept (delete it first to make new keys: players then need the new server key).
"""
import secrets
import sys
from pathlib import Path


def main():
    if len(sys.argv) != 2 or '.' not in sys.argv[1]:
        sys.exit('usage: python3 make_env.py <domain>   (a name pointing at this machine, e.g. games.example.org)')
    env = Path(__file__).with_name('.env')
    if env.exists():
        sys.exit(f'{env} is there already')
    key = lambda n=24: secrets.token_urlsafe(n)
    values = {
        'DOMAIN': sys.argv[1],
        'POSTGRES_PASSWORD': key(),
        # in every player's settings: it says which server this is, it isn't a secret
        'SERVER_KEY': 'wkbai-' + key(9),
        'SESSION_KEY': key(32),
        'REFRESH_KEY': key(32),
        'CONSOLE_USER': 'admin',
        'CONSOLE_PASSWORD': key(18),
        'CONSOLE_SIGNING_KEY': key(32),
        'HTTP_KEY': key(24),
    }
    env.write_text(''.join(f'{k}={v}\n' for k, v in values.items()), encoding='utf-8')
    try:
        env.chmod(0o600)
    except OSError:
        pass
    print(f'Wrote {env}.')
    print('Players set, on the Options page (Online):')
    print(f'  Nakama server:      https://{values["DOMAIN"]}')
    print(f'  Nakama server key:  {values["SERVER_KEY"]}')
    print(f'The console: ssh -L 7351:127.0.0.1:7351 <this machine>, then http://127.0.0.1:7351 '
          f'(user {values["CONSOLE_USER"]}, the password is in .env)')


if __name__ == '__main__':
    main()
