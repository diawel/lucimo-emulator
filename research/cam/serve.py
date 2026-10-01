"""カメラの輝度を時系列で受け取り lux.log に追記する。
自動露出が絶対値を壊すので、後段では『変化の瞬間』を見る。"""
import http.server, os, time
D = os.path.dirname(os.path.abspath(__file__))
LOG = os.path.join(D, 'lux.log')
class H(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *a, **k): super().__init__(*a, directory=D, **k)
    def do_POST(self):
        v = self.rfile.read(int(self.headers['Content-Length'])).decode()
        with open(LOG, 'a') as f: f.write(f"{time.time():.3f} {v}\n")
        self.send_response(204); self.end_headers()
    def log_message(self, *a): pass
http.server.ThreadingHTTPServer(('127.0.0.1', 8765), H).serve_forever()
