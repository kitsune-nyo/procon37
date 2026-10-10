"""Run a sample match using the bundled Linux server (inside WSL)."""
import json
import subprocess
import time
import urllib.request
from pathlib import Path
from tests.run_tests import fixture

ROOT = Path(__file__).resolve().parent
OUT = ROOT / "output" / "local"

def main():
    OUT.mkdir(parents=True, exist_ok=True)
    setting = fixture()["setting"]
    problem = {key: setting[key] for key in (
        "spots", "fuelLimits", "daySteps", "daySeconds", "busyThreshold", "jammedThreshold")}
    problem.update(width=8, height=8, cells=setting["map"]["cells"], agentStarts=setting["agents"])
    config = OUT / "config.json"
    config.write_text(json.dumps({"problem": problem, "teams": [{"token": "token-p0"}]}, indent=2))
    url = "http://localhost:8080/setting?token=token-p0"
    try:
        urllib.request.urlopen(url, timeout=1).close()
    except OSError:
        pass
    else:
        raise RuntimeError("Port 8080 already has a server. Stop it before starting a new match.")
    binary = OUT / "hexudon_main"
    source_files = list(ROOT.glob("*.cpp")) + list(ROOT.glob("*.hpp"))
    if not binary.exists() or any(p.stat().st_mtime > binary.stat().st_mtime for p in source_files):
        subprocess.run(["g++", "-std=c++17", "-O2", "hexudon_main.cpp", "-lcurl", "-o", str(binary)], cwd=ROOT, check=True)
    server_binary = ROOT.parent / "saba" / "procon-server-linux-amd64"
    server_binary.chmod(server_binary.stat().st_mode | 0o111)
    with (OUT / "server.log").open("w") as log:
        server = subprocess.Popen([str(server_binary), "-config", str(config), "-addr", "127.0.0.1:8080",
                                   "-kind-deadline", "5s", "-match-start-delay", "2s"], stdout=log, stderr=log)
        try:
            for _ in range(50):
                if server.poll() is not None:
                    raise RuntimeError("Server exited; see output/local/server.log")
                try:
                    urllib.request.urlopen(url, timeout=1).close()
                    break
                except OSError:
                    time.sleep(0.1)
            else:
                raise RuntimeError("Server did not become ready")
            print("Local server: http://localhost:8080 (8x8, 3 agents, 2 days)", flush=True)
            result = subprocess.run([str(binary)], cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=40)
            (OUT / "client.log").write_text(result.stdout)
            print(result.stdout, end="", flush=True)
            result.check_returncode()
            time.sleep(1)
        finally:
            server.terminate()
            server.wait(timeout=5)
    server_log = (OUT / "server.log").read_text()
    print(server_log, end="")
    if server_log.count("actions submitted") != 2 or "agent kinds submitted" not in server_log:
        raise RuntimeError("Expected accepted roles and two accepted daily plans; inspect logs")
    print("SUCCESS: roles and both daily plans accepted. Server stopped. Logs: output/local/")

if __name__ == "__main__":
    main()
