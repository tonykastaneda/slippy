"""Minimal Slippy client (stdlib only). Reads the port + token the plug-in
writes to ~/Library/Application Support/Slippy/session.json."""

import json
import os
import urllib.error
import urllib.request

SESSION_FILE = os.path.expanduser("~/Library/Application Support/Slippy/session.json")


class SlippyError(Exception):
    def __init__(self, message, code=None, data=None):
        super().__init__(message)
        self.code = code
        self.data = data


def session():
    try:
        with open(SESSION_FILE) as f:
            return json.load(f)
    except FileNotFoundError:
        raise SlippyError("Slippy isn't running - start Illustrator with the Slippy plug-in installed "
                        f"(no {SESSION_FILE})")


def _post(payload, timeout):
    s = session()
    req = urllib.request.Request(
        s["url"], data=json.dumps(payload).encode(),
        headers={"Content-Type": "application/json", "X-Slippy-Token": s["token"]})
    try:
        with urllib.request.urlopen(req, timeout=timeout) as r:
            return json.loads(r.read())
    except urllib.error.HTTPError as e:
        body = e.read().decode(errors="replace")
        raise SlippyError(f"HTTP {e.code}: {body}")
    except urllib.error.URLError as e:
        raise SlippyError(f"can't reach Slippy at {s['url']} ({e.reason}) - is Illustrator running?")


def call(method, params=None, timeout=60):
    """Run one command; returns its result or raises SlippyError."""
    r = _post({"jsonrpc": "2.0", "id": 1, "method": method, "params": params or {}, "timeout": timeout},
              timeout + 5)
    if "error" in r:
        e = r["error"]
        raise SlippyError(e.get("message", "error"), e.get("code"), e.get("data"))
    return r["result"]


def batch(calls, timeout=60):
    """Run [{method, params}, ...] back to back in one step (one undo).
    Stops at the first error; returns the list of JSON-RPC responses."""
    payload = [{"jsonrpc": "2.0", "id": i, "method": c["method"], "params": c.get("params", {}),
                **({"timeout": timeout} if i == 0 else {})} for i, c in enumerate(calls)]
    return _post(payload, timeout + 5)
