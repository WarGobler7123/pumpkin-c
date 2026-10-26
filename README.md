# Pumpkin-C

A C fork starter for Pumpkin, the Rust Minecraft server by Pumpkin-MC.

## Why Pumpkin, not SteelMC

Pumpkin is the better server today, on checked numbers:

- Pumpkin: 12,637 stars, 957 forks, 3,095 commits, created July 2024.
  Java and Bedrock (Bedrock work in progress), plugin support, commands,
  permissions, redstone, liquid physics, entity spawning.
- SteelMC: 795 stars, 118 forks, 929 commits, created October 2025.
  Pre-alpha, no plugin system, survival incomplete, entities limited.
  Its one clear edge is world generation parity testing, not overall
  server completeness.

Pumpkin is also the larger porting job, so this project starts with
one real slice and grows from there, instead of claiming a full port.

## What this is, honestly

This is a starter, not a finished server. It implements exactly one
slice, in C with no dependencies beyond POSIX sockets:

- Java Edition status handshake
- Status response (version, player counts, MOTD)
- Ping/pong round trip, with the payload echoed exactly
- Login attempts get an honest disconnect: login is not implemented

Not implemented yet: authentication, world loading and saving, chunk
generation, blocks, entities, mobs, redstone, commands, plugins,
Bedrock support, encryption, compression.

## Build and run

```
make
./pumpkin-c [port] [motd]
```

Defaults: port 25565, MOTD "Pumpkin-C (status only)".

## Test

```
make test
```

This starts the server on port 25566, performs a real status
handshake over a socket, checks the status JSON, checks the pong
payload, and checks that a login attempt gets a disconnect.

## Port plan

1. Status slice (done, tested).
2. Login and offline-mode play state: join, spawn, movement packets.
3. Chunk and world data: region/anvil reading, then generation.
4. Entities, gameplay systems, commands.
5. Plugin hook surface, mirroring Pumpkin's plugin API shape.
6. Bedrock support, only after the Java side is solid.

Each phase lands only with a test like the one in `tests/`, not
with an unverified claim.

## License and attribution

GPL-3.0, to match Pumpkin (see LICENSE). Pumpkin itself is
Copyright Pumpkin-MC, GPL-3.0. This code was written fresh in C
from the public Minecraft protocol framing. It is not a copy of
Pumpkin's Rust source, and it is not affiliated with Pumpkin-MC.
