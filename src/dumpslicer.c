/*
 * Slice a mysqldump file so that it can be more easily
 * handled by de-duplication
 *
 * mysqldump -uroot --skip-extended-insert --databases $dbname > out.txt 
 */
/* These are to handle 64 bit file sizes... i.e. more than 4GB...
 * However we really don't need these as we are not really going
 * to create 4GB+ segment files...
 */
//~ #ifndef _FILE_OFFSET_BITS
//~ #define _FILE_OFFSET_BITS 64
//~ #endif

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define FALSE 0
#define TRUE (!FALSE)
#define LINESZ	4096
#define MAXPATH 1024

const char version[] = "0.0";

int main(int argc,char **argv) {
  off_t maxsize = 4*1024*1024;
  int counter = 0;
  char *fmt = "%s%03d.sql", *prefix = "";
  int opt, verbose = FALSE, in_hdr = FALSE;
  FILE *fp = NULL;
  char buffer[LINESZ], fname[MAXPATH];

  while ((opt = getopt(argc,argv,"h?Vvf:p:s:m:k:")) != -1) {
    switch(opt) {
    case 'h':
    case '?':
      fprintf(stderr,"Usage:\n\t%s [-h?] [-v] [-V] [-f fmt] [-s size] [-k kb] [-m mb] [prefix]\n",argv[0]);
      fputs("\t-h|-?:\tshow help\n", stderr);
      fputs("\t-v:\tverbose\n", stderr);
      fputs("\t-V:\tprint version info\n", stderr);
      fprintf(stderr,"\t-f fmt:\tUse format (see printf).  Defaults to \"%s\"\n", fmt);
      fputs("\t-s bytes:\tLimit segments to this size in bytes\n",stderr);
      fputs("\t-k kb:\tLimit segments to this size in KB\n",stderr);
      fputs("\t-m mb:\tLimit segments to this size in MB\n",stderr);
      exit(EXIT_SUCCESS);
      break;
    case 'V':
      printf("%s v%s\n", argv[0], version);
      exit(EXIT_SUCCESS);
      break;
    case 'v':
      verbose = TRUE;
      break;
    case 'f':
      fmt = optarg;
      break;
    case 's':
      maxsize = (off_t)(strtoull(optarg,NULL,0));
      break;
    case 'm':
      maxsize = (off_t)(strtoull(optarg,NULL,0) * 1024 * 1024);
      break;
    case 'k':
      maxsize = (off_t)(strtoull(optarg,NULL,0) * 1024);
      break;
    default:
      fprintf(stderr,"Usage: %s [options] [prefix]\n", argv[0]);
      exit(EXIT_FAILURE);
    }
  }
  if (optind < argc) prefix = argv[optind];
  if (verbose) {
    fprintf(stderr,"%s: Prefix: %s, Fmt: %s, MaxSize: %llu\n", argv[0], prefix, fmt,(long long unsigned int) maxsize);
  }
  while ((fgets(buffer,sizeof buffer,stdin)) != NULL) {
    if (buffer[0] == '-' && buffer[1] == '-' && buffer[2]) {
      if (!in_hdr) {
	if (fp) {
	  fclose(fp);
	  fp = NULL;
	}
	in_hdr = TRUE;
      }
    } else {
      in_hdr = FALSE;
    }
    if (!fp) {
      snprintf(fname, sizeof fname, fmt, prefix, counter++);
      if (verbose) fprintf(stderr,"%s: writing %s\n", argv[0], fname);
      fp = fopen(fname,"w");
      if (fp == NULL) {
	perror(fname);
	exit(EXIT_FAILURE);
      }
    }
    fputs(buffer,fp);
    if (ftello(fp) > maxsize) {
      fclose(fp);
      fp = NULL;
    }
  }
  if (fp) fclose(fp);
  exit(EXIT_SUCCESS);
}
