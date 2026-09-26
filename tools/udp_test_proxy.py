#!/usr/bin/env python3
"""Local two-way UDP impairment proxy for Mafia1Online multiplayer tests.

Each client gets its own upstream socket, preserving the distinct peers that
MafiaNet expects. Delay, jitter and loss are applied independently to packets
in both directions. This tool does not alter system network configuration.
"""

import argparse
import heapq
import random
import selectors
import socket
import time


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--listen-port", type=int, default=27017)
    parser.add_argument("--server-port", type=int, default=27015)
    parser.add_argument("--one-way-ms", type=float, default=50.0)
    parser.add_argument("--jitter-ms", type=float, default=10.0)
    parser.add_argument("--loss-percent", type=float, default=2.0)
    parser.add_argument("--seed", type=int, default=1)
    args = parser.parse_args()
    if not 0 < args.listen_port < 65536 or not 0 < args.server_port < 65536:
        parser.error("ports must be between 1 and 65535")
    if args.listen_port == args.server_port:
        parser.error("listen and server ports must differ")
    if args.one_way_ms < 0 or args.jitter_ms < 0 or not 0 <= args.loss_percent <= 100:
        parser.error("delay, jitter and loss must be non-negative; loss must not exceed 100")

    rng = random.Random(args.seed)
    selector = selectors.DefaultSelector()
    listener = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    listener.bind(("127.0.0.1", args.listen_port))
    listener.setblocking(False)
    selector.register(listener, selectors.EVENT_READ, None)
    upstream_by_client: dict[tuple[str, int], socket.socket] = {}
    pending: list[tuple[float, int, socket.socket, bytes, tuple[str, int]]] = []
    sequence = 0
    server = ("127.0.0.1", args.server_port)

    def schedule(out_socket: socket.socket, payload: bytes, destination: tuple[str, int]) -> None:
        nonlocal sequence
        if rng.random() * 100 < args.loss_percent:
            return
        delay_ms = max(0.0, args.one_way_ms + rng.uniform(-args.jitter_ms, args.jitter_ms))
        sequence += 1
        heapq.heappush(pending, (time.monotonic() + delay_ms / 1000.0, sequence, out_socket, payload, destination))

    try:
        while True:
            now = time.monotonic()
            while pending and pending[0][0] <= now:
                _, _, out_socket, payload, destination = heapq.heappop(pending)
                out_socket.sendto(payload, destination)
            timeout = max(0.0, pending[0][0] - time.monotonic()) if pending else None
            for key, _ in selector.select(timeout):
                datagram, address = key.fileobj.recvfrom(65535)
                if key.fileobj is listener:
                    upstream = upstream_by_client.get(address)
                    if upstream is None:
                        upstream = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                        upstream.bind(("127.0.0.1", 0))
                        upstream.setblocking(False)
                        upstream_by_client[address] = upstream
                        selector.register(upstream, selectors.EVENT_READ, address)
                    schedule(upstream, datagram, server)
                elif address == server:
                    schedule(listener, datagram, key.data)
    except KeyboardInterrupt:
        pass
    finally:
        selector.close()
        for upstream in upstream_by_client.values():
            upstream.close()
        listener.close()


if __name__ == "__main__":
    main()
