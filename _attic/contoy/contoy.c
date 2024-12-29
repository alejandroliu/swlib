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
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/select.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

#define MAX_CLIENTS	8

static const char str_sock[] = "--sock=";
static const char str_lock[] = "--lock=";
static const char VERSION[] = "0.1";

static void usage(char *cmd) {
  fprintf(stderr,"%s v%s\n"
		"\nUsage:\n"
		"\t%s [-V][-h] %s[path] %s[path] cmd [args]\n"
		"\n",
		cmd, VERSION, cmd,
		str_sock, str_lock);
  exit(0);
}
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
  const char *unix_path = NULL;
  const char *unix_lock = NULL;
  int i, j, lockfd, sockfd, io[3][2], clients[MAX_CLIENTS], infd;
  struct sockaddr_un addr;

  if (argc < 2) usage(argv[0]);
  for (i=1; i < argc; i++) {
    if (!strncmp(str_sock,argv[i],sizeof(str_sock)-1)) {
      unix_path = argv[i]+sizeof(str_sock)-1;
    } else if (!strncmp(str_lock,argv[i],sizeof(str_lock)-1)) {
      unix_lock = argv[i]+sizeof(str_lock)-1;
    } else if (!strcmp("-V", argv[i])) {
      fprintf(stderr,"%s v%s\n", argv[0], VERSION);
    } else if (!strcmp("-h", argv[i])) {
      usage(argv[0]);
    } else {
      break;
    }
  }
  if (unix_path == NULL) error_msg(__LINE__,"Must specify --sock\n");
  if (unix_lock == NULL) error_msg(__LINE__,"Must specify --lock\n");
  if (i == argc) error_msg(__LINE__,"Must specify a command to run\n");
  if (strlen(unix_path) > sizeof(addr.sun_path)) error_msg(__LINE__,"socket path too long\n");

  lockfd = open(unix_lock, O_RDWR|O_CREAT, 0666);
  if (lockfd == -1) perror_msg(__LINE__, unix_lock);
  if (flock(lockfd, LOCK_EX | LOCK_NB) == -1) perror_msg(__LINE__,"flock (command already running)");

  sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (sockfd == -1) perror_msg(__LINE__, "socket");

  memset(&addr,0,sizeof(addr));
  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, unix_path, sizeof(addr.sun_path)-1);
  unlink(unix_path);

  if (bind(sockfd,(struct sockaddr *)&addr, sizeof(addr)) == -1) perror_msg(__LINE__,"bind");
  if (listen(sockfd,5) == -1) perror_msg(__LINE__,"bind");

  for (j=0;j< 3; j++) {
    if (pipe(io[j]) == -1) perror_msg(__LINE__,"pipe");
  }

  switch (fork()) {
  case -1:
    perror_msg(__LINE__,"fork");
  case 0:
    /* Child process */
    close(0); close(1); close(2);
    close(sockfd); close(lockfd);
    dup2(io[0][0],0); close(io[0][1]); close(io[0][0]);
    dup2(io[1][1],1); close(io[1][0]); close(io[1][1]);
    dup2(io[2][1],2); close(io[2][0]); close(io[2][1]);
    setsid();
    execvp(argv[j],argv+j);
    perror_msg(__LINE__, argv[j]);
  default:
    /* Parent process */
    break;
  }
  close(io[0][0]);
  close(io[1][1]);
  close(io[2][1]);
  memset(clients, 0, sizeof(clients));
  infd = 0;

  for(;;) {
    int n;
    char buf[8192];
    fd_set fds;

    FD_ZERO(&fds); n = 0;
    if (infd != -1) FD_SET(infd,&fds);
    for (j=1; j< 3; j++) {
      if (io[j][0] != -1) {
	FD_SET(io[j][0],&fds);
	if (io[j][0] > n) n = io[j][0];
      }
    }
    FD_SET(sockfd,&fds); if (sockfd > n) n = sockfd;
    for (j=0;j<MAX_CLIENTS;j++) {
      if (clients[j]) {
	FD_SET(clients[j],&fds);
	if (clients[j] > n) n = clients[j];
      }
    }
    ++n;

    if (select(n, &fds, NULL, NULL, NULL) == -1) perror_msg(__LINE__,"select");

    if (infd != -1 && FD_ISSET(infd, &fds)) {
      n = read(infd,buf, sizeof buf);
      if (n > 0) {
	do_write(io[0][1],buf,n);
      } else if (n == 0) {
	close(infd);
	infd = -1;
      }
    }
    if (FD_ISSET(sockfd, &fds)) {
      // OK, a new client!
      int cfd = accept(sockfd, NULL, NULL);
      if (cfd == -1) {
	perror("accept");
      } else {
	for (j = 0; j < MAX_CLIENTS; j++) {
	  if (!clients[j]) break;
	}
	if (j == MAX_CLIENTS) {
	  const char msg[] = "All Client Slots are FULL\n";
	  do_write(cfd, msg, sizeof(msg)-1);
	  close(cfd);
	} else {
	  const char msg[] = "[CONNECTED]\n";
	  do_write(cfd, msg, sizeof(msg)-1);
	  clients[j] = cfd;
	}
      }
    }
    for (j=1; j < 3; j++) {
      if (FD_ISSET(io[j][0], &fds)) {
	n = read(io[j][0], buf, sizeof buf);
	if (n > 0) {
	  int i;
	  do_write(j, buf, n);
	  for (i=0; i < MAX_CLIENTS; i++) {
	    if (!clients[i]) continue;
	    if (do_write(clients[i], buf, n) == -1) {
	      close(clients[i]);
	      clients[i] = 0;
	    }
	  }
	} else if (n == 0) {
	  // channel is closed
	  close(io[j][0]);
	  io[j][0] = -1;
	  if (io[1][0] == -1 && io[2][0] == -1) {
	    // All output channels closed...
	    exit(0);
	  }
	}
      }
    }
    for (j=0;j<MAX_CLIENTS;j++) {
      if (!clients[j]) continue;
      if (FD_ISSET(clients[j], &fds)) {
	n = read(clients[j], buf, sizeof buf);
	if (n > 0) {
	  do_write(io[0][1], buf, n);
	} else if (n == 0) {
	  close(clients[j]);
	  clients[j] = 0;
	}
      }
    }
  }

  exit(0);
}
