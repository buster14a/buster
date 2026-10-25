/* Small localhost-only static server for the real browser acceptance checks.
 * It serves bounded regular files inside one root, including WASM MIME types.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

static bool serve_send(int client, const void *bytes, size_t size)
{
    const char *cursor = bytes;
    bool result = true;
    while (size != 0 && result)
    {
        ssize_t sent = send(client, cursor, size, MSG_NOSIGNAL);
        if (sent < 0 && errno == EINTR)
        {
            continue;
        }
        result = sent > 0;
        if (result)
        {
            cursor += sent;
            size -= (size_t)sent;
        }
    }
    return result;
}

static const char *serve_mime(const char *path)
{
    const char *suffix = strrchr(path, '.');
    const char *result = "application/octet-stream";
    if (suffix != NULL)
    {
        if (strcmp(suffix, ".html") == 0) result = "text/html; charset=utf-8";
        else if (strcmp(suffix, ".js") == 0 || strcmp(suffix, ".mjs") == 0) result = "application/javascript";
        else if (strcmp(suffix, ".css") == 0) result = "text/css";
        else if (strcmp(suffix, ".json") == 0 || strcmp(suffix, ".jsonl") == 0) result = "application/json";
        else if (strcmp(suffix, ".wasm") == 0) result = "application/wasm";
        else if (strcmp(suffix, ".svg") == 0) result = "image/svg+xml";
        else if (strcmp(suffix, ".png") == 0) result = "image/png";
        else if (strcmp(suffix, ".woff2") == 0) result = "font/woff2";
    }
    return result;
}

static void serve_client(int client, const char *root)
{
    char request[8192] = {0};
    size_t count = 0;
    bool valid = true;
    while (valid && strstr(request, "\r\n\r\n") == NULL)
    {
        ssize_t received = recv(client, request + count, sizeof(request) - 1 - count, 0);
        valid = received > 0;
        if (valid)
        {
            count += (size_t)received;
            valid = count < sizeof(request) - 1;
        }
    }
    char method[8];
    char uri[4096];
    valid = valid && sscanf(request, "%7s %4095s", method, uri) == 2;
    bool head = valid && strcmp(method, "HEAD") == 0;
    valid = valid && (strcmp(method, "GET") == 0 || head) && uri[0] == '/';
    char *query = valid ? strchr(uri, '?') : NULL;
    if (query != NULL) *query = 0;
    char path[PATH_MAX];
    char resolved[PATH_MAX];
    int length = valid ? snprintf(path, sizeof(path), "%s%s%s", root, uri, uri[strlen(uri) - 1] == '/' ? "index.html" : "") : -1;
    valid = valid && length > 0 && (size_t)length < sizeof(path) && strchr(uri, '%') == NULL && realpath(path, resolved) != NULL;
    size_t root_length = strlen(root);
    valid = valid && strncmp(resolved, root, root_length) == 0 && resolved[root_length] == '/';
    int file = valid ? open(resolved, O_RDONLY | O_NONBLOCK) : -1;
    struct stat info;
    valid = valid && file >= 0 && fstat(file, &info) == 0 && S_ISREG(info.st_mode) && info.st_size >= 0;
    char header[512];
    if (valid)
    {
        int size = snprintf(header, sizeof(header), "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %lld\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n", serve_mime(resolved), (long long)info.st_size);
        valid = size > 0 && (size_t)size < sizeof(header) && serve_send(client, header, (size_t)size);
        char buffer[65536];
        ssize_t size_read;
        while (valid && !head && (size_read = read(file, buffer, sizeof(buffer))) > 0)
        {
            valid = serve_send(client, buffer, (size_t)size_read);
        }
    }
    else
    {
        const char response[] = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        serve_send(client, response, sizeof(response) - 1);
    }
    if (file >= 0) close(file);
}

int main(int argc, char **argv)
{
    char root[PATH_MAX];
    char *end = NULL;
    long port = argc == 3 ? strtol(argv[2], &end, 10) : 0;
    bool valid = argc == 3 && end != NULL && *end == 0 && port > 1024 && port < 65536 && realpath(argv[1], root) != NULL;
    int server = valid ? socket(AF_INET, SOCK_STREAM, 0) : -1;
    valid = valid && server >= 0;
    if (valid)
    {
        int reuse = 1;
        setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
        struct sockaddr_in address = {
            .sin_family = AF_INET,
            .sin_port = htons((unsigned short)port),
            .sin_addr.s_addr = htonl(INADDR_LOOPBACK)
        };
        valid = bind(server, (struct sockaddr *)&address, sizeof(address)) == 0 && listen(server, 32) == 0;
    }
    if (valid)
    {
        printf("Serving %s at http://127.0.0.1:%ld/\n", root, port);
        fflush(stdout);
        while (valid)
        {
            int client = accept(server, NULL, NULL);
            if (client < 0 && errno == EINTR) continue;
            valid = client >= 0;
            if (valid)
            {
                struct timeval timeout = {.tv_sec = 10};
                setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
                setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
                serve_client(client, root);
                close(client);
            }
        }
    }
    if (server >= 0) close(server);
    if (!valid) fprintf(stderr, "serve: root/port/listener unavailable\n");
    return valid ? 0 : 1;
}
