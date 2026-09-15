import json
import os
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

from badwords import ProfanityFilter

PORT = int(os.getenv("PORT", "8082"))
LANGUAGES = [item.strip() for item in os.getenv("PROFANITY_LANGUAGES", "en,ru,de,sp").split(",") if item.strip()]
THRESHOLD = float(os.getenv("PROFANITY_MATCH_THRESHOLD", "0.95"))
MAX_BYTES = 64 * 1024

profanity_filter = ProfanityFilter()
filter_lock = threading.Lock()
ready = False


def collect_texts(payload):
    texts = []
    text = payload.get("text")
    if isinstance(text, str):
        texts.append(text)

    values = payload.get("texts")
    if isinstance(values, list):
        texts.extend(item for item in values if isinstance(item, str))

    return [item.strip() for item in texts if item and item.strip()]


class Handler(BaseHTTPRequestHandler):
    def log_message(self, format, *args):
        print(format % args)

    def _json(self, code, payload):
        body = json.dumps(payload).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        path = self.path.split("?", 1)[0]
        if path != "/health":
            self._json(404, {"error": "not found"})
            return

        if not ready:
            self._json(503, {"ok": False})
            return

        self._json(200, {"ok": True})

    def do_POST(self):
        path = self.path.split("?", 1)[0]
        if path != "/moderate":
            self._json(404, {"error": "not found"})
            return

        if not ready:
            self._json(503, {"error": "filter not ready"})
            return

        try:
            length = int(self.headers.get("Content-Length") or 0)
        except ValueError:
            self._json(400, {"error": "invalid body"})
            return

        if length <= 0:
            self._json(400, {"error": "invalid body"})
            return

        if length > MAX_BYTES:
            self._json(413, {"error": "body too large"})
            return

        try:
            payload = json.loads(self.rfile.read(length))
        except json.JSONDecodeError:
            self._json(400, {"error": "invalid json"})
            return

        if not isinstance(payload, dict):
            self._json(400, {"error": "invalid json"})
            return

        texts = collect_texts(payload)
        if not texts:
            self._json(200, {"profane": False})
            return

        try:
            with filter_lock:
                profane = any(profanity_filter.filter_text(text, match_threshold=THRESHOLD) for text in texts)
        except Exception as err:
            print("profanity filter failed:", err)
            self._json(500, {"error": "filter failed"})
            return

        self._json(200, {"profane": bool(profane)})


def main():
    global ready

    profanity_filter.init(languages=LANGUAGES)
    ready = True
    print(f"profanity filter ready languages={LANGUAGES} threshold={THRESHOLD}")

    httpd = ThreadingHTTPServer(("0.0.0.0", PORT), Handler)
    print(f"profanity service listening on {PORT}")
    httpd.serve_forever()


if __name__ == "__main__":
    main()
