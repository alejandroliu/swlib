/*
 * Copyright (c) 2021, Alejandro Liu
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials provided
 *    with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND
 * CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */
#include <sys/socket.h>
#include <sys/un.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/select.h>
#include <unistd.h>

static void error_msg(int ncode, const char *msg) {
  fputs(msg,stderr);
  exit(ncode);
}
static void perror_msg(int ncode,const char *msg) {
  perror(msg);
  exit(ncode);
}
static ssize_t do_write (int fd, const void *buf, size_t len) {
  while (len) {
    ssize_t wrote = write (fd, buf, len);
    if (wrote < 0) {
      perror ("write");
      return -1;
    }
    buf += wrote;
    len -= wrote;
  }
  return len;
}

int main(int argc, char **argv) {
  struct sockaddr_un addr;
  int sfd, nfd;

  sfd = socket(AF_UNIX,SOCK_STREAM,0);
  if (sfd == -1) perror_msg(__LINE__,"socket");
  if (argc != 2) error_msg(__LINE__,"Must specify socket path\n");

  memset(&addr, 0, sizeof(struct sockaddr_un));
  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, argv[1], sizeof(addr.sun_path) - 1);

  if (connect(sfd, (struct sockaddr *)&addr, sizeof(addr)) == -1)
    perror_msg(__LINE__,"connect");

  nfd = 0;
  while (sfd != -1) {
    int n;
    char buf[8192];
    fd_set fds;

    FD_ZERO(&fds); n = 0;
    if (nfd != -1) {
      FD_SET(nfd,&fds);
      if (nfd > n) n = nfd;
    }
    if (sfd != -1) {
      FD_SET(sfd,&fds);
      if (sfd > n) n = sfd;
    }
    ++n;

    if (select(n, &fds, NULL, NULL, NULL) == -1) perror_msg(__LINE__,"select");


    if (nfd != -1 && FD_ISSET(nfd, &fds)) {
      n = read(nfd,buf, sizeof buf);
      if (n > 0) {
	do_write(sfd,buf,n);
      } else if (n == 0) {
	close(nfd);
	nfd = -1;
      }
    }
    if (sfd != -1 && FD_ISSET(sfd, &fds)) {
      n = read(sfd,buf, sizeof buf);
      if (n > 0) {
	do_write(1,buf,n);
      } else if (n == 0) {
	close(sfd);
	sfd = -1;
      }
    }
  }

  exit(0);
}


