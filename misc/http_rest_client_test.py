from http.server import ThreadingHTTPServer, BaseHTTPRequestHandler
from urllib.parse import urlparse, parse_qs
from signal import signal, SIGINT
from sys import exit
from json import loads, dumps
from argparse import ArgumentParser


class HttpHandler(BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'
    error_content_type = 'text/plain'
    error_message_format = "Error %(code)d: %(message)s"

    def do_GET(self):
        path, args = self.parse_url()

        if 'name' in args:
            name = args['name'][0]

            self.write_response(200, "text/plain", f"Hello, {name}!")
        else:
            self.send_error(404, 'Not found')

    def do_POST(self):
        body = self.read_body()
        print(str(self.headers))

        if body and self.headers['Content-Type'] == "application/json":
            json_body = self.parse_json(body)

            if json_body:
                if self.headers['Authorization'] :
                    auth_header = self.headers['Authorization']
                    print(f'Got Request from Willow with Authorization: {auth_header}')
                else:
                    print('Got Request from Willow without Authorization:')
                print(dumps(json_body))
                self.write_response(202, "text/plain", "Hello from Willow REST endpoint test server")
            else:
                self.send_error(400, 'Invalid json received')
        elif body:
            self.write_response(202, "text/plain", f"Accepted: {body}")
        else:
            self.send_error(404, 'Not found')

    def parse_url(self):
        url_components = urlparse(self.path)
        return url_components.path, parse_qs(url_components.query)

    def parse_json(self, content):
        try:
            return loads(content)
        except Exception:
            return None

    def read_body(self):
        try:
            content_length = int(self.headers['Content-Length'])
            return self.rfile.read(content_length).decode('utf-8')
        except Exception:
            return None

    def write_response(self, status_code, content_type, content):
        response = content.encode('utf-8')

        self.send_response(status_code)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(response)))
        self.end_headers()
        self.wfile.write(response)

    def version_string(self):
        return "Tiny Http Server"

    def log_error(self, format, *args):
        pass


def start_server(host, port):
    server_address = (host, port)
    httpd = ThreadingHTTPServer(server_address, HttpHandler)
    print(f"Server started on {host}:{port}")
    httpd.serve_forever()


def shutdown_handler(signum, frame):
    print('Shutting down server')
    exit(0)


def main():
    signal(SIGINT, shutdown_handler)
    parser = ArgumentParser(description='Start a tiny HTTP/1.1 server')
    parser.add_argument('--host', type=str, action='store',
                        default='127.0.0.1', help='Server host (default: 127.0.0.1)')
    parser.add_argument('--port', type=int, action='store',
                        default=8000, help='Server port (default: 8000)')
    args = parser.parse_args()
    start_server(args.host, args.port)


if __name__ == "__main__":
    main()
