#if defined (WIN32) || defined (__CYGWIN__)
#include <windows.h>
#include <winsock.h>
#else
#include <netdb.h>
#include <netinet/in.h>
#include <sys/time.h>
#define closesocket(x) close (x)
#endif
#include <sys/stat.h>
#include "fec.h"

size_t Send (void *buf, size_t size, size_t count, void *dummy)
{ // fdopen ing is simpler under *nix, but illegal under WinSock.
  return send (*(int*) dummy, buf, size * count, 0);
}

#ifdef _CONSOLE
#define mainreturn(x) \
    fprintf (stderr, "Press enter to exit.\n"); \
    getchar (); /* Behave acceptable to Windoze users. */ \
    return x
#else
#define mainreturn(x) return x
#endif

int main (int argc, char *argv[])
{
  fecEncoder *e;
  char *msg;
  int sock, n, doneWithSwitches = 0, r = 10, connected = 0, kbps = 64;
  fecPayload pay[256];
  struct sockaddr_in sin;
  struct hostent *he;
#if defined (WIN32) || defined (__CYGWIN__)
  WSADATA localWSA;
  if (WSAStartup (MAKEWORD(1,1),&localWSA) != 0) {
    fprintf (stderr, "Unable to load wsock32.dll\n");
    mainreturn (1);
  }
#endif

  fprintf (stderr, "%s: %s\n", argv[0], FEC_COPYRIGHT);
  if (argc < 3) {
    fprintf (stderr,
"Usage : %s a.b.c.d [-p port] [-r r] [-b kiloBitsPerSecond] [--] file1 ...\n"
"Sends the files to the specified address (which may be a multicast adress)\n"
"using the unreliable UDP protocol. The files are encoded with `forward\n"
"error correction' techniques so that the receiver will in most cases be\n"
"able to recover any data that may have been lost.\n\n"
"  r is the amount of redundancy to add, expressed as a percentage\n",
      argv[0]);
    mainreturn (5);
  }

  if ((sock = socket (PF_INET, SOCK_DGRAM, 0)) == -1) {
    fprintf (stderr, "Unable to make internet socket\n");
    mainreturn (3);
  }

  memset (&sin, 0, sizeof (sin));
  sin.sin_family = AF_INET;
  if (strspn (argv[1], "0123456789.") != strlen (argv[1])) {
    if ((he = gethostbyname (argv[1])) == NULL) {
      fprintf (stderr, "Unknown host %s\n", argv[1]);
      mainreturn (4);
    }
    sin.sin_addr = *(struct in_addr *) he->h_addr;
  }
  else *(unsigned*)&sin.sin_addr = inet_addr (argv[1]);
  sin.sin_port = SUGGESTED_FEC_UDP_PORT_NUMBER;

  for (;argc >= 3; argc--, argv++) {
    if (doneWithSwitches || argv[2][0] != '-') {
      FILE *f = fopen (argv[2], "rb");
      if (!connected) {
        connect (sock, (struct sockaddr *) &sin, sizeof (sin));
        connected = 1;
      }
      if (!f) fprintf (stderr, "Unable to open %s\n", argv[2]);
      else {
        struct stat st;
        fstat (fileno (f), &st);
        n = (st.st_size + sizeof (pay) - 1) / sizeof (pay);
        e = NewFecEncoder (&sock, Send, &msg, sizeof (pay),
          n, n * r / 100 , 40, 4, kbps * 1000);
        if (!e) fprintf (stderr, "%s\n", msg);
        else {
          while (fread (pay, 1, sizeof (pay), f) > 0) {
            FecEncode (pay, e);
          }
          DeleteFecEncoder (e);
#if defined (WIN32)
          Sleep (30000);
#else
          sleep (30);
#endif
        }
        fclose (f);
      }
    }
    else if (strcmp (argv[2], "-p") == 0) {
      sin.sin_port = htons ((short) atoi (argv[3]));
      argc--, argv++;
    }
    else if (strcmp (argv[2], "-r") == 0) {
      r = atoi (argv[3]);
      argc--, argv++;
    }
    else if (strcmp (argv[2], "-b") == 0) {
      kbps = atoi (argv[3]);
      argc--, argv++;
    }
    else if (strcmp (argv[2], "--") == 0) doneWithSwitches = 1;
    else fprintf (stderr, "Unknown option %s\n", argv[2]);
  }
  closesocket (sock);
  return 0;
}
