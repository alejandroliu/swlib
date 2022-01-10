/*
 * Simple SSL/SSH multiplexer
 */
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <sys/wait.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <netdb.h>
#include <fcntl.h>

#define TIMEOUT_SECS	3
#define TIMEOUT_USECS	0
#define PROBE_TIME	1
#define BUFSZ		8192 // Support jumbo frames...

#ifdef DEBUG
#define DBG
#else
#define DBG		for(;0;)
#endif

struct target_t {
  int type;
  union {
    struct sockaddr_in ipv4addr;
    struct sockaddr_in6 ipv6addr;
    char **cmd;
  } x;
};
struct target_t *def_target;
struct target_t *tmout_target;
struct probe_t {
  struct probe_t *next;
  char *str;
  int len;
  struct target_t target;
} *probes;
#define TT_NONE		0
#define TT_TCP4		1
#define TT_PROXY	2
#define TT_CMD		3
#define TT_TCP6		4
#define TT_PROXY6	5

struct pipe_t {
  struct pipe_t *next;
  int inp;
  int out;
  time_t timed;
  int family;
} *pipes = NULL;

int sock4, sock6;

void init_defaults() {
  static struct target_t d_target, t_target;
  static char *def_sshd_cmd[] = { "/usr/sbin/sshd", "-i", NULL };

  def_target = &d_target;
  memset(&d_target.x,0,sizeof d_target.x);
  d_target.type = TT_TCP4;
  d_target.x.ipv4addr.sin_family = AF_INET;
  d_target.x.ipv4addr.sin_port = htons(443);
  inet_pton(AF_INET,"127.0.0.1",&d_target.x.ipv4addr.sin_addr);

  tmout_target = &t_target;
  memset(&t_target.x,0,sizeof t_target.x);
  t_target.type = TT_CMD;
  t_target.x.cmd = def_sshd_cmd;

  probes = NULL;
}


char **check_probe(char **probe, char **argv, char *str) {
  *probe = *argv;
  if (*probe == NULL) {
    fprintf(stderr,"Missing probe for %s target\n", str);
    exit(EINVAL);
  }
  return argv+1;
}

void new_probe(char *probe, struct target_t *pt) {
  if (strcmp(probe,"*") == 0) {
    //default probe...
    memcpy(def_target,pt, sizeof(*def_target));
  } else if (strcmp(probe,"") == 0 || strcmp(probe,"-") == 0) {
    // timeout probe...
    memcpy(tmout_target,pt, sizeof(*tmout_target));
  } else if (strcmp(probe,"--ssh") == 0) {
    new_probe("-", pt);
    new_probe("^SSH-2.0-", pt);
  } else if (strcmp(probe,"--ssl") == 0) {
    new_probe("*", pt);
  } else {
    struct probe_t *np = (struct probe_t *)malloc(sizeof(struct probe_t));
    if (np == NULL) {
      fprintf(stderr,"Out of memory: %s,%d\n", __FILE__,__LINE__);
      exit(ENOMEM);
    }
    np->next = probes;
    np->str = probe;
    np->len = strlen(probe);
    memcpy(&np->target,pt,sizeof(np->target));
    probes = np;
  }
}

char **new_net_target(char **argv, struct target_t *tp, int ipv4, int ipv6) {
  if (argv[0] == NULL || argv[1] == NULL) {
    fprintf(stderr,"Missing hostname and/or port for net target\n");
    exit(EINVAL);
  }
  memset(tp,0,sizeof *tp);
  if (inet_pton(AF_INET6,argv[0],&tp->x.ipv6addr.sin6_addr) != 0) {
    tp->type = ipv6;
    tp->x.ipv6addr.sin6_family = AF_INET6;
    tp->x.ipv6addr.sin6_port = htons(atoi(argv[1]));
  } else if (inet_pton(AF_INET,argv[0], &tp->x.ipv4addr.sin_addr) != 0) {
    tp->type = ipv4;
    tp->x.ipv4addr.sin_family = AF_INET;
    tp->x.ipv4addr.sin_port = htons(atoi(argv[1]));
  } else {
    fprintf(stderr,"Invalid network address: %s\n", argv[0]);
    exit(EINVAL);
  }
  return argv+2;
}

char **new_exec_target(char **argv, struct target_t *tp) {
  tp->type = TT_CMD;
  tp->x.cmd = argv;

  while (*argv && strcmp(*argv,";") != 0) {
    ++argv;
  }
  if (*argv) {
    *argv = NULL;
    ++argv;
  }
  if (tp->x.cmd[0] == NULL) {
    fprintf(stderr,"Missing command for --exec target\n");
    exit(EINVAL);
  }
  return argv;
}

void parse_args(int argc,char **argv) {
  struct target_t t;
  char *probe;

  sock4 = sock6 = 0;
  
  while (*argv) {
    if (strcmp(*argv,"--proxy") == 0) {
      argv = check_probe(&probe,argv+1,"--proxy");
      argv = new_net_target(argv,&t,TT_PROXY,TT_PROXY6);
      new_probe(probe,&t);
    } else if (strcmp(*argv,"--forward") == 0) {
      argv = check_probe(&probe,argv+1,"--forward");
      argv = new_net_target(argv,&t,TT_TCP4,TT_TCP6);
      new_probe(probe,&t);
    } else if (strcmp(*argv,"--exec") == 0) {
      argv = check_probe(&probe,argv+1,"--exec");
      argv = new_exec_target(argv,&t);
      new_probe(probe,&t);
    } else if (strcmp(*argv,"-4") == 0) {
      sock4 = 0;
      sock6 = -1;
    } else if (strcmp(*argv,"-6") == 0) {
      sock4 = -1;
      sock6 = 0;
    } else {
      fprintf(stderr,"Invalid option: %s\n", *argv);
      exit(EINVAL);
    }
  }
}


void reaper(int signo) {
  int status;
  pid_t pid;
  while ((pid = waitpid(-1, &status, WNOHANG)) > 0) ;
}

void client_new(int family,int sock) {
  struct pipe_t *p;
  int fd;

  DBG fprintf(stderr,"CHKPT(%s,%d,%s) %d,%d\n",__FILE__,__LINE__,__FUNCTION__,family,sock);
  fd = accept(sock,NULL,NULL);
  DBG fprintf(stderr,"CHKPT(%s,%d,%s) fd=%d\n",__FILE__,__LINE__,__FUNCTION__,fd);
  if (fd == -1) {
    perror("accept");
    return;
  }
  p = (struct pipe_t *)malloc(sizeof(struct pipe_t));
  DBG fprintf(stderr,"New pipe (%lx) fam=%d\n",(unsigned long)p,family);//DEBUG
  if (p == NULL) {
    fprintf(stderr,"%s,%d: Out of Memory Error\n",__FILE__,__LINE__);
    close(fd);
    return;
  }
  memset(p,0,sizeof(struct pipe_t));
  p->next = pipes;
  p->inp = fd;
  p->out = -1;
  p->timed = time(NULL);
  p->family = family;

  pipes = p;
}

void unlink_pipe(struct pipe_t *p) {
  struct pipe_t *i, *n;
  n = p->next;
  DBG fprintf(stderr,"DEALLOC: %lx\n",(unsigned long)p);//DEBUG
  free(p);
  if (p == pipes) {
    pipes = n;
    return;
  }
  i = pipes;
  while (i) {
    if (i->next == p) {
      i->next = n;
      return;
    }
    i = i->next;
  }
  // Weird, it was not in the linked list...
  DBG fprintf(stderr,"Internal Error (Pipe not in Linked List)!\n");//DEBUG
}

void client_cmd(struct pipe_t *p, char **cmd) {
  int sock;
  pid_t cpid;

  DBG fprintf(stderr,"Cmd Forking: %s (%d)\n",cmd[0],p->inp);

  sock = p->inp;
  unlink_pipe(p);

  cpid = fork();
  switch (cpid) {
  case -1:
    DBG fprintf(stderr,"CHKPT(%s,%d,%s)\n",__FILE__,__LINE__,__FUNCTION__);
    perror("fork");
    close(sock);
    break;
  case 0:
    DBG fprintf(stderr,"CHKPT(%s,%d,%s) %d,%d,%d pid=%d\n",__FILE__,__LINE__,__FUNCTION__,sock4, sock6, sock, getpid());
    if (sock4 != -1) close(sock4);
    if (sock6 != -1) close(sock6);
    for (p = pipes; p ; p = p->next) {
      close(p->inp);
      if (p->out != -1) close(p->out);
    }
    dup2(sock,fileno(stdin));
    dup2(sock,fileno(stdout));
    //dup2(sock,fileno(stderr));
    close(sock);
    execvp(cmd[0],cmd);
    perror("exec");
    exit(errno);
  default:
    DBG fprintf(stderr,"CHKPT(%s,%d,%s)\n",__FILE__,__LINE__,__FUNCTION__);
    close(sock);
  }
}

int client_connect_to(struct target_t *t, int family) {
  int sock = socket(family, SOCK_STREAM, 0);
  if (sock == -1) {
    perror("socket");
    return sock;
  }
  if (family == AF_INET) {
    struct sockaddr_in addr;
    memcpy(&addr, &t->x.ipv4addr, sizeof addr);
    DBG fprintf(stderr,"Forwarding to IPv4 (%s)\n",inet_ntoa(addr.sin_addr)); //DEBUG
    if (connect(sock, (struct sockaddr *)&addr,sizeof(addr)) != -1) return sock;
    perror("connect-v4");
  } else if (family == AF_INET6) {
    struct sockaddr_in6 addr;
    memcpy(&addr, &t->x.ipv6addr, sizeof addr);
    DBG fprintf(stderr,"Forwarding to IPv6\n");//DEBUG
    if (connect(sock, (struct sockaddr *)&addr,sizeof(addr)) != -1) return sock;
    perror("connect-v6");
  }
  close(sock);
  return -1;
}

void haproxy_hdr(int inp,int out,int family) {
  socklen_t len;
  union {
    struct sockaddr_in ipv4;
    struct sockaddr_in6 ipv6;
  } local, remote;
  char unknown[] = "PROXY UNKNOWN\r\n";
  char header[BUFSZ], lbuf[64], rbuf[64];
  char *hh;

  hh = unknown;
  len = sizeof(local);
  if (getsockname(inp,(struct sockaddr *)&local, &len) != -1) {
    len = sizeof(remote);
    if (getpeername(inp,(struct sockaddr *)&remote,&len) != -1) {
      if (family == AF_INET) {
	snprintf(header,sizeof header,"PROXY %s %s %s %d %d\r\n", "TCP4",
		inet_ntop(family, &remote.ipv4.sin_addr, rbuf, sizeof rbuf),
		inet_ntop(family, &local.ipv4.sin_addr, lbuf, sizeof lbuf),
		ntohs(remote.ipv4.sin_port),
		ntohs(local.ipv4.sin_port));
      } else {
	snprintf(header,sizeof header,"PROXY %s %s %s %d %d\r\n", "TCP6",
		inet_ntop(family, &remote.ipv6.sin6_addr, rbuf, sizeof rbuf),
		inet_ntop(family, &local.ipv6.sin6_addr, lbuf, sizeof lbuf),
		ntohs(remote.ipv6.sin6_port),
		ntohs(local.ipv6.sin6_port));
      }
      hh = header;
    }
  }
  write(fileno(stdout),hh,strlen(hh)); //DEBUG
  send(out,hh, strlen(hh),0);
}
void pump(struct pipe_t *p) {
  char buf[BUFSZ];
  int cnt, j, k;
  struct pipe_t *q;

  // Maybe we can use sendfile with 
  // ioctl(fd,FIONREAD,&bytes_available)
  cnt = recv(p->inp,buf,sizeof buf,0);
  DBG fprintf(stderr,"PUMP %d->%d (%d bytes)\n", p->inp, p->out, cnt); //DEBUG
  if (cnt > 0) {
    for (j = 0; j < cnt ; j += k) {
      k = send(p->out,buf + j,cnt - j,0);
      if (k <= 0) {
	perror("send");
	break;
      }
    }
    if (j >= cnt) return; // Succesful!
  } else {
    if (cnt != 0) perror("recv");
  }

  for (q = pipes; q ; q = q->next) {
    if (p->inp == q->out) {
      // This is the other side of the pipe... (it is still open...)
      DBG fprintf(stderr, "SHUTRD(%d) SHUTWR(%d)\n", p->inp, p->out);//DEBUG
      shutdown(p->inp,SHUT_RD);
      shutdown(p->out,SHUT_WR);
      unlink_pipe(p);
      return;
    }
  }
  // Didn't find the other side of the pipe... (was closed?)
  DBG fprintf(stderr,"CLOSING(%d and %d)\n", p->inp, p->out);//DEBUG
  close(p->inp);
  close(p->out);
  unlink_pipe(p);
}

void client_fwd(struct pipe_t *p, struct target_t *t, int family, int proxy) {
  p->out = client_connect_to(t, family);
  if (p->out != -1) {
    struct pipe_t *q;
    DBG fprintf(stderr,"CHKPNT(%s,%d,%s) %d\n",__FILE__,__LINE__,__FUNCTION__,p->family);
    if (proxy) haproxy_hdr(p->inp,p->out,p->family);
    // Set-up full duplex connection...
    q = (struct pipe_t*)malloc(sizeof(struct pipe_t));
    DBG fprintf(stderr,"New pipe (%lx)\n",(unsigned long)q);//DEBUG
    if (q != NULL) {
      // This is a full duplex connection
      memset(q,0,sizeof(*q));
      q->next = pipes;
      q->inp = p->out;
      q->out = p->inp;
      q->timed = time(NULL);
      q->family = family;
      pipes = q;
      DBG fprintf(stderr,"INP: %d OUT: %d\n",p->inp, p->out);//DEBUG
      // Pump data...
      pump(p);
      return;
    } else {
      fprintf(stderr,"%s,%d: Out of Memory Error\n",__FILE__,__LINE__);
    }
    close(p->out);
  }
  p->out = -1;
  close(p->inp);
  unlink_pipe(p);
}

void client_init(struct pipe_t *p, struct target_t *t) {
  switch (t->type) {
    case TT_CMD:
      client_cmd(p,t->x.cmd);
      break;
    case TT_PROXY:
      client_fwd(p,t,AF_INET,1);
      break;
    case TT_PROXY6:
      client_fwd(p,t,AF_INET6,1);
      break;
    case TT_TCP4:
      client_fwd(p,t,AF_INET,0);
      break;
    case TT_TCP6:
      client_fwd(p,t,AF_INET6,0);
      break;
    default:
      fprintf(stderr,"Invalid internal target type %d (%s,%d)\n", t->type, __FILE__,__LINE__);
      exit(EINVAL);
  }
}
void client_probe(struct pipe_t *p) {
  struct probe_t *pp;
  char buf[BUFSZ];
  int cnt;

  cnt = recv(p->inp, buf, sizeof buf, MSG_PEEK);
  if (cnt <= 0) {
    perror("recv-peek");
    close(p->inp);
    unlink_pipe(p);
    return;
  }
  for (pp = probes ; pp ; pp = pp->next) {
    if (pp->str[0] == '^') {
      // Anchored match...
      if (cnt >= pp->len-1 && memcmp(pp->str+1, buf, pp->len-1) == 0) {
	client_init(p, &pp->target);
	return;
      }
    } else {
      // string search... FIXME: this is not a binary safe search!
      buf[cnt] = 0; // Make sure things are terminated...
      if (strstr(buf,pp->str) != NULL) {
	client_init(p, &pp->target);
	return;
      }
    }
  }
  /* No match... default target */
  client_init(p, def_target);
}

void client_tmout(struct pipe_t *p) {
  // We have timed out waiting for client...
  client_init(p, tmout_target);
}

int init_sock(int family, int port) {
  int fd = socket(family,SOCK_STREAM,0);
  int enable;
  struct linger lin;

  if (fd == -1) {
    perror("socket");
    return fd;
  }
  // Set options...
  lin.l_onoff = 0;
  lin.l_linger = 0;

  if (setsockopt(fd, SOL_SOCKET, SO_LINGER, (const char *)&lin, sizeof(lin)) == -1)
    perror("setsockopt(SO_LINGER)");
#ifdef SO_REUSEADDR
  enable = 1;
  if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&enable, sizeof(enable)) == -1)
    perror("setsockopt(SO_REUSEADDR");
#endif
#ifdef SO_REUSEPORT
  enable = 1;
  if (setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, (const char *)&enable, sizeof(enable)) == -1)
    perror("setsockopt(SO_REUSEPORT");
#endif
  fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);

  DBG fprintf(stderr,"CHKPNT(%s,%d,%s) %d,%d\n",__FILE__,__LINE__,__FUNCTION__,family,fd);
  if (family == AF_INET) {
    struct sockaddr_in addr;
    memset(&addr,0,sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
      perror("bind-v4");
      close(fd);
      return -1;
    }
  } else if (family == AF_INET6) {
    struct sockaddr_in6 addr;
    memset(&addr,0,sizeof(addr));
    addr.sin6_family = AF_INET6;
    addr.sin6_addr = in6addr_any;
    addr.sin6_port = htons(port);
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
      perror("bind-v6");
      close(fd);
      return -1;
    }
  } else {
    fprintf(stderr,"Internal Error: %s,%d\n", __FILE__,__LINE__);
    exit(EFAULT);
  }
  if (listen(fd,5) != -1) return fd;
  perror("listen");
  return -1;
}

void main_loop() {
  int high_port, j;
  fd_set rd_fds;
  struct timeval tmout, *tmout_p;
  struct pipe_t *p, *n;
  
  FD_ZERO(&rd_fds);
  high_port = 0;
  if (sock4 != -1) {
    if (sock4 > high_port) high_port = sock4;
    FD_SET(sock4, &rd_fds);
  }
  if (sock6 != -1) {
    if (sock6 > high_port) high_port = sock6;
    FD_SET(sock6, &rd_fds);
  }
  tmout_p = NULL;

  if (pipes) {
    p = pipes;
    while (p) {
      FD_SET(p->inp, &rd_fds);
      if (p->inp > high_port) high_port = p->inp;
      if (p->out == -1 && !tmout_p) {
	tmout_p = &tmout;
	tmout.tv_sec = TIMEOUT_SECS;
	tmout.tv_usec = TIMEOUT_USECS;
      }
      p = p->next;
    }
  }
  j = select(high_port+1,&rd_fds,NULL,NULL,tmout_p);
  if (j == -1) {
    if (errno == EINTR) return;
    perror("select");
    exit(errno);
  }

  if (j) {
    if (sock4 != -1 && FD_ISSET(sock4, &rd_fds)) client_new(AF_INET,sock4);
    if (sock6 != -1 && FD_ISSET(sock6, &rd_fds)) client_new(AF_INET6,sock6);
  }
  if (!pipes) return;

  p = pipes;
  while (p) {
    n = p->next;
    if (FD_ISSET(p->inp, &rd_fds)) {
      // we can read from this pipe...
      if (p->out == -1) {
	client_probe(p);
      } else {
	// just copy from one side to the other...
	pump(p);
      }
    } else {
      if (p->out == -1 && time(NULL) > p->timed+PROBE_TIME) {
	// Timer expired!
	client_tmout(p);
      }
    }
    p = n;
  }
}

int main(int argc,char *argv[]) {
  int port;
  if (argc < 2) {
    fprintf(stderr,"Usage:\n\t%s port [options]\n", argv[0]);
    exit(EINVAL);
  }
  init_defaults();
  parse_args(argc-2,argv+2);
  port = atoi(argv[1]);
  if (sock4 == 0) {
    sock4 = init_sock(AF_INET,port);
  }
  if (sock6 == 0) {
    sock6 = init_sock(AF_INET6,port);
  }
  if (sock4 == -1 && sock6 == -1) {
    fprintf(stderr,"Unable to listen on port %d\n", port);
    exit(ENETDOWN);
  }

  signal(SIGCHLD,reaper);
  signal(SIGPIPE,SIG_IGN);

  DBG fprintf(stderr,"Started: %d\n",getpid()); //DEBUG

  for (;;) {
    main_loop();
  }
}
