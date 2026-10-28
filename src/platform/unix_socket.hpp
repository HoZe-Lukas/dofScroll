// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// POSIX helpers for user-only Unix domain sockets, shared by the macOS reload channel and the
// live feed (Linux and macOS): private owner-verified directory, stale-socket handling, 0600 bind,
// peer uid lookup. Pure POSIX; not built on Windows.
#pragma once
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>

#include <string>

namespace spn::usock {

// Real directory (no symlink) of the current user that nobody else can write to.
bool dirTrusted(const std::string &dir, std::string *err);
// Creates the socket directory private (fallback: single level, else like mkdir -p) and verifies it.
bool prepareDir(const std::string &dir, bool fallback, std::string *err);
// Fills addr; false if the path is empty or does not fit into sun_path.
bool fillAddr(const std::string &path, struct sockaddr_un *addr);
// New SOCK_STREAM socket, close-on-exec, no SIGPIPE where the OS has SO_NOSIGPIPE.
int newSocket();
// A leftover file at `path` is removed only if it is our own socket that nobody listens on; then
// binds (file mode 0600), listens and makes the socket non-blocking. Returns the fd or -1 + *err.
int listenAt(const std::string &path, int backlog, std::string *err);
// Connects to a socket after checking that it is a socket owned by the current user. Returns the
// fd or -1 + *err. Never blocks longer than 500 ms (a full listener backlog is an error).
// `nonblocking`: the returned fd is non-blocking, else blocking.
int connectTo(const std::string &path, bool nonblocking, std::string *err);
// accept() whose new fd is close-on-exec (accept4 on Linux, fcntl elsewhere); retries on EINTR.
int acceptCloexec(int listenFd);
// Effective uid of the peer (SO_PEERCRED / getpeereid).
bool peerUid(int fd, uid_t *uid);
// send() flag that suppresses SIGPIPE (0 where SO_NOSIGPIPE does that).
int sendFlags();
bool setNonBlocking(int fd);

}  // namespace spn::usock
