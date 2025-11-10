import argparse
import base64
import os
import sys
import grpc

sys.path.insert(0, "stubs")  # so we can import route_pb2 modules

import route_pb2
import route_pb2_grpc


def fnv1a32(text: str) -> int:
    FNV_OFFSET = 2166136261
    FNV_PRIME = 16777619
    h = FNV_OFFSET
    for ch in text.encode("utf-8"):
        h ^= ch
        h = (h * FNV_PRIME) & 0xFFFFFFFF
    return h


def make_payload(n_bytes: int) -> str:
    """Return a printable payload by base64-encoding n_bytes of random data."""
    raw = os.urandom(n_bytes)
    return base64.b64encode(raw).decode("ascii")  # safe ASCII for proto string


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--target", default="127.0.0.1:50051")
    parser.add_argument("--size", type=int, default=32, help="raw payload size in bytes before base64")
    args = parser.parse_args()

    # Build test payload
    payload = make_payload(args.size)
    expected_len = len(payload)
    expected_hash = fnv1a32(payload)

    print("------------------------------------------------------------")
    print(f"[Client] Sending {expected_len}-byte payload to {args.target}")

    # Call server
    channel = grpc.insecure_channel(args.target)
    stub = route_pb2_grpc.RouteServiceStub(channel)
    req = route_pb2.Request(rid=payload)
    resp = stub.Hello(req)

    # Server returns format: 'len=...|fnv1a=0x........'
    tag = resp.rspid or ""
    len_ok = f"len={expected_len}" in tag
    hash_ok = f"fnv1a=0x{expected_hash:08x}" in tag

    print(f"[Client] Server tag: {tag}")
    print(f"[Client] VERIFY: {'OK' if (len_ok and hash_ok) else 'MISMATCH'}")
    print("------------------------------------------------------------")


if __name__ == "__main__":
    main()
