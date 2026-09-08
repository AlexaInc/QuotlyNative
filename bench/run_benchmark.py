#!/usr/bin/env python3
"""Fair latency / memory benchmark: QuotlyNative (C++) vs LyoSU quote-api (JS).

Both services render the SAME message content; each receives the payload in
its own native schema (that is what "drop-in replacement" means in practice).
The JS service runs with its documented defaults plus `scale` 1 and 2, and its
rate limiter is whitelisted by matching botToken so throttling never kicks in.

Usage:
    python3 bench/run_benchmark.py [--n 30] [--out bench/results.json] \
        [--cpp http://127.0.0.1:7860] [--js http://127.0.0.1:3000] \
        [--cpp-pid PID] [--js-pid PID]
"""
import argparse, json, statistics, sys, time, urllib.request
from concurrent.futures import ThreadPoolExecutor

def post(url, payload):
    req = urllib.request.Request(
        url, data=json.dumps(payload).encode(),
        headers={"Content-Type": "application/json"})
    t0 = time.perf_counter()
    with urllib.request.urlopen(req, timeout=120) as r:
        body = r.read()
        dt = (time.perf_counter() - t0) * 1000.0
        assert r.status == 200 and len(body) > 500, f"bad response {r.status} {len(body)}"
    return dt, len(body)

# ── identical message content, two schemas ──────────────────────────────────
MSGS = [
    # (from_id, first, last, text, entities, reply?)
    (1001, "Alexa", "User", "Hello! This is a simple quote test.", [], None),
    (1002, "Nimal", "Silva", "Bold and italic and code walk into a bar.",
        [{"offset": 0, "length": 4, "type": "bold"},
         {"offset": 9, "length": 6, "type": "italic"},
         {"offset": 20, "length": 4, "type": "code"}], None),
    (1001, "Alexa", "User", "Replying to you now, with a longer piece of text "
        "that will definitely wrap onto several lines inside the bubble "
        "because quote renderers must word-wrap exactly like Telegram does.",
        [{"offset": 0, "length": 8, "type": "underline"}],
        {"name": "Nimal Silva", "text": "Bold and italic and code walk into a bar."}),
    (1003, "Kasun", "Perera", "Sinhala text: සාමාන්‍ය පාඨයක් සහ යුනිකෝඩ් මිශ්ර "
        "content for complex-script shaping coverage in the benchmark.", [], None),
    (1002, "Nimal", "Silva", "Final message of the group with a spoiler and a "
        "link https://example.com to exercise entity handling end to end.",
        [{"offset": 34, "length": 7, "type": "spoiler"},
         {"offset": 50, "length": 19, "type": "url"}], None),
]

def cpp_payload(msgs, nonce=None):
    out = []
    for fid, fn, ln, text, ents, reply in msgs:
        m = {"text": text, "from": {"id": fid, "first_name": fn, "last_name": ln}}
        if ents: m["entities"] = ents
        if reply: m["reply_to"] = {"text": reply["text"],
                                   "from": {"id": 999, "first_name": reply["name"].split()[0]}}
        out.append(m)
    p = {"transparent": False, "messages": out}
    if nonce is not None: p["benchNonce"] = nonce   # ignored by the renderer
    return p

def js_payload(msgs, scale, nonce=None):
    out = []
    for fid, fn, ln, text, ents, reply in msgs:
        m = {"text": text, "from": {"id": fid, "first_name": fn, "last_name": ln}}
        if ents: m["entities"] = ents
        if reply: m["replyMessage"] = reply
        out.append(m)
    p = {"type": "quote", "ext": "png", "botToken": "benchdummy123",
         "scale": scale, "messages": out}
    # quote-api ships a 45-minute LRU *response* cache keyed on the md5 of the
    # whole body (methods/index.js). A unique nonce field — never rendered —
    # defeats it so every request does a real render, like on first contact.
    if nonce is not None: p["benchNonce"] = nonce
    return p

CASES = {"S": MSGS[:1], "M": MSGS[:3], "L": MSGS}

import subprocess

def real_pid(pattern):
    """pid of the actual server process (start_process pids are sh wrappers)."""
    try:
        out = subprocess.run(["pgrep", "-f", pattern], capture_output=True, text=True)
        return int(out.stdout.split()[0])
    except Exception:
        return None

def mem_kb(pid, key):
    if not pid:
        return None
    try:
        with open(f"/proc/{pid}/status") as f:
            for line in f:
                if line.startswith(key):
                    return int(line.split()[1])
    except Exception:
        return None
    return None

def pct(xs, p):
    xs = sorted(xs)
    return xs[min(len(xs) - 1, int(round(p * (len(xs) - 1))))]

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--n", type=int, default=30)
    ap.add_argument("--out", default="bench/results.json")
    ap.add_argument("--cpp", default="http://127.0.0.1:7860/quote")
    ap.add_argument("--js", default="http://127.0.0.1:3000/generate")
    ap.add_argument("--cpp-pid-pattern", default="QuoteAPI")
    ap.add_argument("--js-pid-pattern", default="node index.js")
    a = ap.parse_args()

    cpp_pid = real_pid(a.cpp_pid_pattern)
    js_pid = real_pid(a.js_pid_pattern)
    print(f"server pids: cpp={cpp_pid} js={js_pid}")

    mem0 = {"cpp_rss": mem_kb(cpp_pid, "VmRSS:"),
            "js_rss": mem_kb(js_pid, "VmRSS:")}

    variants = [("cpp", None), ("js-scale1", 1), ("js-scale2", 2)]

    # warm-up (JIT, font caches, layout caches) — nonce'd too, so warm-up does
    # not prime the response cache for the measured requests
    for case, msgs in CASES.items():
        for v, scale in variants:
            p = cpp_payload(msgs, "warm") if v == "cpp" else js_payload(msgs, scale, "warm")
            url = a.cpp if v == "cpp" else a.js
            for _ in range(2):
                post(url, p)

    results = {}
    for case, msgs in CASES.items():
        for v, scale in variants:
            url = a.cpp if v == "cpp" else a.js
            lats, sizes = [], []
            for i in range(a.n):
                p = cpp_payload(msgs, i) if v == "cpp" else js_payload(msgs, scale, i)
                dt, sz = post(url, p)
                lats.append(dt); sizes.append(sz)
            results[f"{case}/{v}"] = {
                "n": a.n, "mean": statistics.fmean(lats), "p50": pct(lats, .5),
                "p95": pct(lats, .95), "min": min(lats), "max": max(lats),
                "out_bytes_mean": statistics.fmean(sizes)}
            print(f"{case}/{v}: mean={results[f'{case}/{v}']['mean']:.1f}ms "
                  f"p50={results[f'{case}/{v}']['p50']:.1f}ms p95={results[f'{case}/{v}']['p95']:.1f}ms")

    # concurrent throughput: 4 workers x 24 unique (uncached) requests of M
    thr = {}
    for v, scale in variants:
        url = a.cpp if v == "cpp" else a.js
        t0 = time.perf_counter()
        def one(i, v=v, scale=scale, url=url):
            p = cpp_payload(CASES["M"], 1000 + i) if v == "cpp" else js_payload(CASES["M"], scale, 1000 + i)
            return post(url, p)
        with ThreadPoolExecutor(4) as ex:
            list(ex.map(one, range(24)))
        dt = time.perf_counter() - t0
        thr[v] = 24 / dt
        print(f"throughput {v}: {thr[v]:.2f} req/s (4 workers)")

    mem1 = {"cpp_peak_kb": mem_kb(cpp_pid, "VmHWM:"),
            "js_peak_kb": mem_kb(js_pid, "VmHWM:"),
            "cpp_rss_kb": mem_kb(cpp_pid, "VmRSS:"),
            "js_rss_kb": mem_kb(js_pid, "VmRSS:")}

    out = {"generated": time.strftime("%Y-%m-%d %H:%M:%S"),
           "iterations": a.n, "idle_rss_before_kb": mem0,
           "memory_after_kb": mem1, "throughput_req_s": thr,
           "latency_ms": results}
    with open(a.out, "w") as f:
        json.dump(out, f, indent=2)
    print("wrote", a.out)

if __name__ == "__main__":
    main()
