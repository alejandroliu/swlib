#define _GNU_SOURCE

#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mount.h>
#include <unistd.h>

#define e(n,f) if (-1 == (f)) {perror(n);return(1);}
#define SRC "/glibc"

int main(int argc,  char  * const *argv) {

  if (getuid() == 0) {
    fprintf(stderr,"%s: permission denied (do NOT run as root)\n", argv[0]);
    return(2);
  }

  const char IN_GLIBC_ENV[] = "IN_GLIBC";
  char *env_value = getenv(IN_GLIBC_ENV);
  if (!env_value) {
    static char IN_GLIBC_TRUE[] = "IN_GLIBC=1";
    putenv(IN_GLIBC_TRUE);
    // move glibc stuff in place
    e("unshare",unshare(CLONE_NEWNS));
    e("mount",mount(SRC "/usr", "/usr", NULL, MS_BIND, NULL));
    e("mount",mount(SRC "/var/db/xbps", "/var/db/xbps", NULL, MS_BIND, NULL));

    // drop the rights suid gave us
    e("setuid",setreuid(getuid(),getuid()));
    e("setgid",setregid(getgid(),getgid()));
  } else {
    fprintf(stderr,"%s: already in \"glibc\" environment\n", argv[0]);
  }
  argv++;
  if (!argv[0]) {
    static const char *shell[] = { "/bin/sh", NULL };
    argv = (void *)shell;
  }
  e("execv",execvp(argv[0], argv));
}
