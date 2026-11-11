#!/usr/bin/env python3
import argparse
import collections
import csv
import hashlib
import json
import re
from pathlib import Path


TLS_VERSIONS = {
    0x0300: "SSL 3.0",
    0x0301: "TLS 1.0",
    0x0302: "TLS 1.1",
    0x0303: "TLS 1.2",
}

TLS_CIPHERS = {
    0x0005: "TLS_RSA_WITH_RC4_128_SHA",
    0x002F: "TLS_RSA_WITH_AES_128_CBC_SHA",
    0x0035: "TLS_RSA_WITH_AES_256_CBC_SHA",
    0x003C: "TLS_RSA_WITH_AES_128_CBC_SHA256",
    0x003D: "TLS_RSA_WITH_AES_256_CBC_SHA256",
    0x009C: "TLS_RSA_WITH_AES_128_GCM_SHA256",
    0x009D: "TLS_RSA_WITH_AES_256_GCM_SHA384",
}


def protocol_name(data):
    if len(data) >= 3 and data[0] in (0x14, 0x15, 0x16, 0x17) and data[1] == 3:
        return "tls"
    if re.match(rb"(?:GET|POST|PUT|DELETE|HEAD|OPTIONS|CONNECT) [^\r\n]+ HTTP/\d", data):
        return "http-request"
    if data.startswith(b"HTTP/"):
        return "http-response"
    if data.startswith((b"<?xml", b"<")):
        return "xml"
    return "unknown"


def safe_text_evidence(data):
    text = data.decode("utf-8", errors="ignore")
    evidence = set()
    for match in re.finditer(r"(?:GET|POST|PUT|DELETE|HEAD|OPTIONS) [^\r\n]{1,300} HTTP/\d(?:\.\d)?", text):
        evidence.add(match.group(0))
    for match in re.finditer(r"(?im)^Host:\s*([^\r\n]{1,253})", text):
        evidence.add("Host: " + match.group(1).strip())
    for field in (
        "blazeSDKVersion",
        "blazeSDKBuildDate",
        "dirtySDKVersion",
        "clientVersion",
        "clientName",
        "serviceName",
        "environment",
    ):
        patterns = (
            rf"<{field}>\s*([^<\r\n]{{1,128}})\s*</{field}>",
            rf"{field}[=:]\s*([A-Za-z0-9._-]{{1,128}})",
        )
        for pattern in patterns:
            for match in re.finditer(pattern, text, re.I):
                evidence.add(f"{field}={match.group(1)}")
    for match in re.finditer(r"[a-z0-9.-]+\.(?:ea\.com|sonyentertainmentnetwork\.com)", text, re.I):
        evidence.add(match.group(0).lower())
    return sorted(evidence)


def uint24(data):
    return int.from_bytes(data, "big")


def tls_version(value):
    return {"value": f"0x{value:04x}", "name": TLS_VERSIONS.get(value, "unknown")}


def tls_cipher(value):
    return {"value": f"0x{value:04x}", "name": TLS_CIPHERS.get(value, "unknown")}


def parse_client_hello(body):
    if len(body) < 35:
        return None
    offset = 34
    session_size = body[offset]
    offset += 1 + session_size
    if len(body) < offset + 2:
        return None
    cipher_size = int.from_bytes(body[offset : offset + 2], "big")
    offset += 2
    if cipher_size % 2 or len(body) < offset + cipher_size + 1:
        return None
    ciphers = [
        tls_cipher(int.from_bytes(body[index : index + 2], "big"))
        for index in range(offset, offset + cipher_size, 2)
    ]
    offset += cipher_size
    compression_size = body[offset]
    offset += 1 + compression_size
    server_names = []
    if len(body) >= offset + 2:
        extensions_size = int.from_bytes(body[offset : offset + 2], "big")
        offset += 2
        extensions_end = min(len(body), offset + extensions_size)
        while offset + 4 <= extensions_end:
            extension_type = int.from_bytes(body[offset : offset + 2], "big")
            extension_size = int.from_bytes(body[offset + 2 : offset + 4], "big")
            offset += 4
            extension = body[offset : offset + extension_size]
            offset += extension_size
            if extension_type != 0 or len(extension) < 5:
                continue
            name_offset = 2
            while name_offset + 3 <= len(extension):
                name_type = extension[name_offset]
                name_size = int.from_bytes(extension[name_offset + 1 : name_offset + 3], "big")
                name_offset += 3
                name = extension[name_offset : name_offset + name_size]
                name_offset += name_size
                if name_type == 0:
                    server_names.append(name.decode("ascii", errors="replace"))
    return {
        "version": tls_version(int.from_bytes(body[0:2], "big")),
        "server_names": server_names,
        "offered_ciphers": ciphers,
    }


def parse_server_hello(body):
    if len(body) < 38:
        return None
    offset = 34
    session_size = body[offset]
    offset += 1 + session_size
    if len(body) < offset + 3:
        return None
    return {
        "version": tls_version(int.from_bytes(body[0:2], "big")),
        "selected_cipher": tls_cipher(int.from_bytes(body[offset : offset + 2], "big")),
        "compression": body[offset + 2],
    }


def parse_certificates(body):
    if len(body) < 3:
        return []
    offset = 3
    certificates = []
    while offset + 3 <= len(body):
        size = uint24(body[offset : offset + 3])
        offset += 3
        certificate = body[offset : offset + size]
        if len(certificate) != size:
            break
        certificates.append(
            {
                "bytes": size,
                "sha256": hashlib.sha256(certificate).hexdigest(),
                "safe_evidence": safe_text_evidence(certificate),
            }
        )
        offset += size
    return certificates


def parse_tls_stream(data):
    records = []
    handshake_data = bytearray()
    offset = 0
    clear_handshake = True
    while offset + 5 <= len(data):
        content_type = data[offset]
        version_value = int.from_bytes(data[offset + 1 : offset + 3], "big")
        size = int.from_bytes(data[offset + 3 : offset + 5], "big")
        if content_type not in (20, 21, 22, 23) or version_value not in TLS_VERSIONS:
            break
        if size > 18432 or offset + 5 + size > len(data):
            break
        payload = data[offset + 5 : offset + 5 + size]
        records.append(
            {
                "offset": offset,
                "content_type": content_type,
                "version": tls_version(version_value),
                "bytes": size + 5,
            }
        )
        if content_type == 20:
            clear_handshake = False
        elif content_type == 22 and clear_handshake:
            handshake_data.extend(payload)
        offset += 5 + size
    if not records:
        return None
    handshakes = []
    client_hello = None
    server_hello = None
    certificates = []
    handshake_offset = 0
    while handshake_offset + 4 <= len(handshake_data):
        handshake_type = handshake_data[handshake_offset]
        size = uint24(handshake_data[handshake_offset + 1 : handshake_offset + 4])
        body_start = handshake_offset + 4
        body_end = body_start + size
        if body_end > len(handshake_data):
            break
        body = bytes(handshake_data[body_start:body_end])
        handshakes.append({"type": handshake_type, "bytes": size + 4})
        if handshake_type == 1 and client_hello is None:
            client_hello = parse_client_hello(body)
        elif handshake_type == 2 and server_hello is None:
            server_hello = parse_server_hello(body)
        elif handshake_type == 11:
            certificates.extend(parse_certificates(body))
        handshake_offset = body_end
    return {
        "records": records,
        "trailing_bytes": len(data) - offset,
        "cleartext_handshakes": handshakes,
        "client_hello": client_hello,
        "server_hello": server_hello,
        "certificates": certificates,
    }


def parse_fire2_stream(data):
    frames = []
    offset = 0
    while len(data) - offset >= 16:
        payload_size = int.from_bytes(data[offset : offset + 4], "big")
        metadata_size = int.from_bytes(data[offset + 4 : offset + 6], "big")
        if payload_size > 16 * 1024 * 1024 or metadata_size > 1024 * 1024:
            break
        frame_size = 16 + metadata_size + payload_size
        if len(data) - offset < frame_size:
            break
        raw = data[offset : offset + frame_size]
        frames.append(
            {
                "offset": offset,
                "bytes": frame_size,
                "payload_bytes": payload_size,
                "metadata_bytes": metadata_size,
                "component": int.from_bytes(raw[6:8], "big"),
                "command": int.from_bytes(raw[8:10], "big"),
                "message": int.from_bytes(raw[10:13], "big"),
                "message_type": raw[13] >> 5,
                "sha256": hashlib.sha256(raw).hexdigest(),
            }
        )
        offset += frame_size
    return frames, len(data) - offset


def main():
    parser = argparse.ArgumentParser(description="Summarize an opt-in shadPS4 socket capture.")
    parser.add_argument("capture", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    capture = args.capture.resolve()
    manifest = capture / "events.tsv"
    rows = list(csv.DictReader(manifest.open(newline="", encoding="utf-8"), delimiter="\t"))
    counts = collections.Counter(row["event"] for row in rows)
    bytes_by_direction = collections.Counter()
    payloads = []
    streams = collections.defaultdict(bytearray)
    current_generations = {}
    lifetimes = {}
    for row in rows:
        socket = int(row["socket"])
        event = row["event"]
        if event == "socket":
            generation = current_generations.get(socket, -1) + 1
            current_generations[socket] = generation
            lifetimes[(socket, generation)] = {
                "socket": socket,
                "generation": generation,
                "start_sequence": int(row["sequence"]),
                "connect_sequence": None,
                "peer_address": None,
                "end_sequence": None,
            }
        generation = current_generations.get(socket, 0)
        lifetime = lifetimes.setdefault(
            (socket, generation),
            {
                "socket": socket,
                "generation": generation,
                "start_sequence": int(row["sequence"]),
                "connect_sequence": None,
                "peer_address": None,
                "end_sequence": None,
            },
        )
        if event == "connect":
            lifetime["connect_sequence"] = int(row["sequence"])
            lifetime["peer_address"] = row["address"]
        elif event == "close":
            lifetime["end_sequence"] = int(row["sequence"])
        payload_name = row.get("payload", "")
        if not payload_name:
            continue
        path = capture / payload_name
        data = path.read_bytes()
        direction = event
        bytes_by_direction[direction] += len(data)
        stream_key = (socket, generation, direction)
        streams[stream_key].extend(data)
        payloads.append(
            {
                "sequence": int(row["sequence"]),
                "socket": socket,
                "generation": generation,
                "peer_address": lifetime["peer_address"],
                "direction": direction,
                "bytes": len(data),
                "protocol": protocol_name(data),
                "sha256": hashlib.sha256(data).hexdigest(),
                "safe_evidence": safe_text_evidence(data),
            }
        )
    fire2_streams = []
    tls_streams = []
    tls_stream_keys = set()
    stream_evidence = []
    for (socket, generation, direction), data in sorted(streams.items()):
        lifetime = lifetimes[(socket, generation)]
        tls = parse_tls_stream(bytes(data))
        if tls:
            tls_stream_keys.add((socket, generation, direction))
            tls_streams.append(
                {
                    "socket": socket,
                    "generation": generation,
                    "peer_address": lifetime["peer_address"],
                    "direction": direction,
                    **tls,
                }
            )
        else:
            frames, trailing = parse_fire2_stream(bytes(data))
            if frames:
                fire2_streams.append(
                    {
                        "socket": socket,
                        "generation": generation,
                        "peer_address": lifetime["peer_address"],
                        "direction": direction,
                        "frames": frames,
                        "trailing_bytes": trailing,
                    }
                )
        evidence = safe_text_evidence(bytes(data))
        if evidence:
            stream_evidence.append(
                {
                    "socket": socket,
                    "generation": generation,
                    "peer_address": lifetime["peer_address"],
                    "direction": direction,
                    "evidence": evidence,
                }
            )
    for payload in payloads:
        key = (payload["socket"], payload["generation"], payload["direction"])
        if key in tls_stream_keys:
            payload["protocol"] = "tls-record" if payload["protocol"] == "tls" else "tls-fragment"
    protocols = collections.Counter(payload["protocol"] for payload in payloads)
    summary = {
        "capture": str(capture),
        "events": dict(sorted(counts.items())),
        "resolved_hosts": sorted({row["address"] for row in rows if row["event"] == "resolve"}),
        "connection_attempts": [
            {"sequence": int(row["sequence"]), "socket": int(row["socket"]), "address": row["address"]}
            for row in rows
            if row["event"] == "connect"
        ],
        "socket_lifetimes": [
            lifetime
            for _, lifetime in sorted(lifetimes.items())
            if lifetime["connect_sequence"] is not None
            or any(key[:2] == (lifetime["socket"], lifetime["generation"]) for key in streams)
        ],
        "payload_bytes": dict(sorted(bytes_by_direction.items())),
        "payload_protocols": dict(sorted(protocols.items())),
        "safe_stream_evidence": stream_evidence,
        "tls_streams": tls_streams,
        "fire2_streams": fire2_streams,
        "payloads": payloads,
        "raw_payload_warning": "Raw .bin files may contain credentials or account data; do not publish them.",
    }
    output = args.output or capture.parent / "summary.json"
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    print(output.resolve())


if __name__ == "__main__":
    main()
