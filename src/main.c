/*
 * pumpkin-c: a C reimplementation starter for Pumpkin, the Rust
 * Minecraft server by Pumpkin-MC.
 *
 * This file is a starter, not a full port. It implements one real,
 * testable slice of a Java Edition server: the status (server list
 * ping) handshake, the status response, and the ping/pong round trip.
 * Login, world loading, chunk generation, entities, and plugins are
 * not implemented yet. See README.md for the port plan.
 *
 * License: GPL-3.0, to match Pumpkin. See LICENSE.
 * This code was written fresh in C from the public Minecraft
 * protocol framing (VarInt length-prefixed packets). It is not a
 * line-by-line translation of Pumpkin's Rust source.
 *
 * Build: make
 * Run:   ./pumpkin-c [port] [motd]
 *        Defaults: port 25565, motd "Pumpkin-C (status only)".
 */

#define _POSIX_C_SOURCE 200809L

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static const char *g_motd = "Pumpkin-C (status only)";

/* The version reported in the status response. This is a test
 * default (Minecraft 1.21.1, protocol 767), not a claim about
 * which version Pumpkin currently tracks. */
static const char *g_version_name = "1.21.1";
static const int g_protocol = 767;

static int read_exact(int fd, void *buf, size_t n) {
    unsigned char *p = buf;
    while (n > 0) {
        ssize_t r = read(fd, p, n);
        if (r <= 0)
            return -1;
        p += (size_t)r;
        n -= (size_t)r;
    }
    return 0;
}

static int write_all(int fd, const void *buf, size_t n) {
    const unsigned char *p = buf;
    while (n > 0) {
        ssize_t r = write(fd, p, n);
        if (r <= 0)
            return -1;
        p += (size_t)r;
        n -= (size_t)r;
    }
    return 0;
}

/* Minecraft VarInt: 7 bits per byte, little-endian groups, MSB set
 * means another byte follows. At most 5 bytes. */
static int read_varint(int fd, int32_t *out) {
    int32_t value = 0;
    for (int i = 0; i < 5; i++) {
        unsigned char b;
        if (read_exact(fd, &b, 1) != 0)
            return -1;
        value |= (int32_t)(b & 0x7f) << (7 * i);
        if (!(b & 0x80)) {
            *out = value;
            return 0;
        }
    }
    return -1;
}

static int write_varint_buf(unsigned char *buf, int32_t value) {
    uint32_t v = (uint32_t)value;
    int n = 0;
    do {
        unsigned char b = (unsigned char)(v & 0x7f);
        v >>= 7;
        if (v)
            b |= 0x80;
        buf[n++] = b;
    } while (v);
    return n;
}

static int write_i64be_buf(unsigned char *buf, int64_t value) {
    uint64_t v = (uint64_t)value;
    for (int i = 7; i >= 0; i--) {
        buf[i] = (unsigned char)(v & 0xff);
        v >>= 8;
    }
    return 8;
}

/* Read one full packet. Returns a malloc'd payload (packet id
 * followed by fields) and its length, or -1 on error. */
static int read_packet(int fd, unsigned char **payload, int32_t *len_out) {
    int32_t len;
    if (read_varint(fd, &len) != 0 || len <= 0 || len > 1024 * 1024)
        return -1;
    unsigned char *buf = malloc((size_t)len);
    if (!buf)
        return -1;
    if (read_exact(fd, buf, (size_t)len) != 0) {
        free(buf);
        return -1;
    }
    *payload = buf;
    *len_out = len;
    return 0;
}

/* Parse a VarInt out of a buffer, advancing the offset. */
static int buf_varint(const unsigned char *buf, int32_t len, int *off,
                      int32_t *out) {
    int32_t value = 0;
    for (int i = 0; i < 5; i++) {
        if (*off >= len)
            return -1;
        unsigned char b = buf[(*off)++];
        value |= (int32_t)(b & 0x7f) << (7 * i);
        if (!(b & 0x80)) {
            *out = value;
            return 0;
        }
    }
    return -1;
}

/* Send a packet: VarInt length, then the given payload bytes. */
static int send_packet(int fd, const unsigned char *payload, size_t len) {
    unsigned char lenbuf[5];
    int n = write_varint_buf(lenbuf, (int32_t)len);
    if (write_all(fd, lenbuf, (size_t)n) != 0)
        return -1;
    return write_all(fd, payload, len);
}

/* Send a packet whose payload is: VarInt id, then VarInt-length
 * prefixed UTF-8 string data. Used for status response and for
 * the login disconnect message. */
static int send_string_packet(int fd, int32_t id, const char *text) {
    size_t tlen = strlen(text);
    size_t cap = 5 + 5 + tlen + 16;
    unsigned char *payload = malloc(cap);
    if (!payload)
        return -1;
    int n = write_varint_buf(payload, id);
    n += write_varint_buf(payload + n, (int32_t)tlen);
    memcpy(payload + n, text, tlen);
    n += (int)tlen;
    int rc = send_packet(fd, payload, (size_t)n);
    free(payload);
    return rc;
}

static int send_status_response(int fd) {
    char json[1024];
    int w = snprintf(json, sizeof(json),
                     "{\"version\":{\"name\":\"%s\",\"protocol\":%d},"
                     "\"players\":{\"max\":20,\"online\":0},"
                     "\"description\":{\"text\":\"%s\"}}",
                     g_version_name, g_protocol, g_motd);
    if (w < 0 || (size_t)w >= sizeof(json))
        return -1;
    return send_string_packet(fd, 0, json);
}

static void handle_client(int fd) {
    unsigned char *pkt = NULL;
    int32_t pkt_len = 0;

    /* First packet must be the handshake. */
    if (read_packet(fd, &pkt, &pkt_len) != 0)
        return;
    int off = 0;
    int32_t packet_id, protocol, next_state;
    uint16_t port;
    if (buf_varint(pkt, pkt_len, &off, &packet_id) != 0 || packet_id != 0)
        goto done;
    if (buf_varint(pkt, pkt_len, &off, &protocol) != 0)
        goto done;
    /* Skip the server address string. */
    int32_t addr_len;
    if (buf_varint(pkt, pkt_len, &off, &addr_len) != 0 ||
        addr_len < 0 || off + addr_len > pkt_len)
        goto done;
    off += addr_len;
    if (off + 2 > pkt_len)
        goto done;
    port = (uint16_t)((pkt[off] << 8) | pkt[off + 1]);
    (void)port;
    off += 2;
    if (buf_varint(pkt, pkt_len, &off, &next_state) != 0)
        goto done;
    free(pkt);
    pkt = NULL;

    if (next_state == 1) {
        /* Status: expect a status request (id 0), answer it, then
         * expect a ping request (id 1) and echo its payload back. */
        if (read_packet(fd, &pkt, &pkt_len) != 0)
            return;
        off = 0;
        if (buf_varint(pkt, pkt_len, &off, &packet_id) != 0 ||
            packet_id != 0)
            goto done;
        free(pkt);
        pkt = NULL;
        if (send_status_response(fd) != 0)
            return;
        if (read_packet(fd, &pkt, &pkt_len) != 0)
            return;
        off = 0;
        if (buf_varint(pkt, pkt_len, &off, &packet_id) != 0 ||
            packet_id != 1 || off + 8 > pkt_len)
            goto done;
        int64_t payload = 0;
        uint64_t v = 0;
        for (int i = 0; i < 8; i++)
            v = (v << 8) | pkt[off + i];
        payload = (int64_t)v;
        unsigned char out[1 + 8];
        int n = write_varint_buf(out, 1);
        n += write_i64be_buf(out + n, payload);
        send_packet(fd, out, (size_t)n);
    } else if (next_state == 2) {
        /* Login: not implemented yet. Disconnect honestly instead
         * of hanging or pretending a world exists. */
        send_string_packet(fd, 0,
                           "{\"text\":\"Pumpkin-C is status-only for "
                           "now; login is not implemented yet.\"}");
    }
    /* Any other next state: close silently. */

done:
    free(pkt);
}

int main(int argc, char **argv) {
    int port = 25565;
    if (argc > 1)
        port = atoi(argv[1]);
    if (argc > 2)
        g_motd = argv[2];
    if (port <= 0 || port > 65535) {
        fprintf(stderr, "bad port: %s\n", argv[1]);
        return 1;
    }

    int srv = socket(AF_INET, SOCK_STREAM, 0);
    if (srv < 0) {
        perror("socket");
        return 1;
    }
    int one = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)port);
    if (bind(srv, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        perror("bind");
        return 1;
    }
    if (listen(srv, 16) != 0) {
        perror("listen");
        return 1;
    }
    printf("pumpkin-c listening on port %d (status only)\n", port);
    fflush(stdout);

    for (;;) {
        int fd = accept(srv, NULL, NULL);
        if (fd < 0) {
            if (errno == EINTR)
                continue;
            perror("accept");
            continue;
        }
        pid_t pid = fork();
        if (pid == 0) {
            close(srv);
            handle_client(fd);
            close(fd);
            _exit(0);
        }
        close(fd);
        /* Reap finished children without blocking. */
        while (waitpid(-1, NULL, WNOHANG) > 0)
            ;
    }
}
