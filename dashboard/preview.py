#!/usr/bin/env python3
"""Serve the workbench locally, respecting its public broker override."""
import argparse
import functools
import webbrowser
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlsplit

ROOT = Path(__file__).resolve().parent / "html"


class Handler(SimpleHTTPRequestHandler):
    def do_GET(self):
        if urlsplit(self.path).path == "/assets/site-config.js":
            override = ROOT / "assets" / "site-config.local.js"
            if override.is_file():
                data = override.read_bytes()
                self.send_response(200)
                self.send_header("Content-Type", "text/javascript; charset=utf-8")
                self.send_header("Content-Length", str(len(data)))
                self.send_header("Cache-Control", "no-store")
                self.end_headers()
                self.wfile.write(data)
                return
        super().do_GET()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=8790)
    parser.add_argument("--open", action="store_true", help="Open the twin in your browser")
    parser.add_argument("--live", action="store_true", help="Start with the live rig instead of demo")
    parser.add_argument("--twin", action="store_true", help="Open the brewery demo directly")
    args = parser.parse_args()
    handler = functools.partial(Handler, directory=str(ROOT))
    with ThreadingHTTPServer(("127.0.0.1", args.port), handler) as server:
        route = "/twin/" + ("" if args.live else "?demo=1") if args.live or args.twin else "/digital-twins/"
        url = f"http://127.0.0.1:{args.port}{route}"
        print(f"Digital twins: {url}", flush=True)
        print("Live rig uses the configured public WebSocket broker. Press Ctrl+C to close.", flush=True)
        if args.open:
            webbrowser.open(url)
        try:
            server.serve_forever()
        except KeyboardInterrupt:
            pass
