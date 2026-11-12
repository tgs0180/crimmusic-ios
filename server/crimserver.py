#!/usr/bin/env python3
"""Small helper server for crimmusic on the iPhone.

The phone can't run yt-dlp or ffmpeg itself, so it asks this server (running
on the PC) to do it:

    GET /search?q=<text>&source=soundcloud|youtube  -> JSON list of tracks
    GET /download?url=<track page>                  -> the song as an MP3

Run it with:  python3 server/crimserver.py        (default port 8765)
Only uses the Python standard library, plus the yt-dlp and ffmpeg programs.
Anyone on your home network can use it while it runs.
"""
import json
import subprocess
import sys
import tempfile
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlparse

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8765


def search(query, source):
    prefix = "ytsearch10:" if source == "youtube" else "scsearch10:"
    result = subprocess.run(
        ["yt-dlp", "--flat-playlist", "--dump-json", "--no-warnings", prefix + query],
        capture_output=True, text=True, timeout=60)
    if result.returncode != 0:
        raise RuntimeError(result.stderr.strip() or "yt-dlp failed")

    tracks = []
    for line in result.stdout.splitlines():
        if not line.strip():
            continue
        info = json.loads(line)
        tracks.append({
            "title": info.get("title") or "Unknown",
            "uploader": info.get("uploader") or info.get("channel") or "Unknown",
            "url": info.get("webpage_url") or info.get("url") or "",
            "duration": info.get("duration") or 0,
        })
    return tracks


def download(url):
    """Downloads one song as MP3 (with cover + tags) and returns its bytes."""
    with tempfile.TemporaryDirectory() as folder:
        result = subprocess.run(
            ["yt-dlp", "-x", "--audio-format", "mp3", "--embed-thumbnail", "--embed-metadata",
             "--no-playlist", "--no-warnings", "-P", folder, "-o", "song.%(ext)s", "--", url],
            capture_output=True, text=True, timeout=600)
        if result.returncode != 0:
            raise RuntimeError(result.stderr.strip() or "yt-dlp failed")
        return (Path(folder) / "song.mp3").read_bytes()


class Handler(BaseHTTPRequestHandler):
    def send(self, status, body, content_type):
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def send_error_text(self, status, message):
        self.send(status, message.encode(), "text/plain; charset=utf-8")

    def do_GET(self):
        address = urlparse(self.path)
        params = {key: values[0] for key, values in parse_qs(address.query).items()}
        try:
            if address.path == "/search":
                query = params.get("q", "").strip()
                if not query:
                    return self.send_error_text(400, "missing q")
                tracks = search(query, params.get("source", "soundcloud"))
                self.send(200, json.dumps(tracks).encode(), "application/json")
            elif address.path == "/download":
                url = params.get("url", "")
                if not url.startswith(("https://", "http://")):
                    return self.send_error_text(400, "bad url")
                self.send(200, download(url), "audio/mpeg")
            else:
                self.send_error_text(404, "not found")
        except Exception as error:  # report the problem to the phone instead of crashing
            print("error:", error, file=sys.stderr)
            self.send_error_text(500, str(error)[-300:])


if __name__ == "__main__":
    server = ThreadingHTTPServer(("0.0.0.0", PORT), Handler)
    print(f"crimserver listening on port {PORT} (Ctrl+C to stop)")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
