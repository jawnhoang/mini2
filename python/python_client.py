import argparse
import grpc
import sys
sys.path.insert(0, "stubs")  # so we can import route_pb2 modules

import route_pb2
import route_pb2_grpc

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--target", default="127.0.0.1:50051")
    parser.add_argument("--rid", default="Req-123")
    args = parser.parse_args()

    channel = grpc.insecure_channel(args.target)
    stub = route_pb2_grpc.RouteServiceStub(channel)

    req = route_pb2.Request(rid=args.rid)
    resp = stub.Hello(req)  # unary RPC defined in route.proto
    print(f"rspid={resp.rspid}")

if __name__ == "__main__":
    main()
