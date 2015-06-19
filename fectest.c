#include <stdlib.h>
#include "fec.h"

void PayloadCheck (void *dummy, __int64_t pos, fecPayload *b, int len)
{
  if ((int) pos != b[0]) fprintf (stderr, "*** Bug in FEC code ! ***\n");
}

static int c = 0;
enum { n = 5000, k = n } dummy;

size_t ChannelSimulate (void *buf, size_t size, size_t count, void *d)
{
  if (rand () < RAND_MAX / 3) return 0;
  return FecDecode (buf, size, count, (fecDecoder*) d);
}

int main (void)
{
#if 1
  char *err;
  fecPayload msg[4];
  fecDecoder *d = NewFecDecoder (NULL, PayloadCheck);
  fecEncoder *e = NewFecEncoder (d, ChannelSimulate, &err, sizeof (msg), n,
    k, 20, sizeof (msg) / sizeof (msg[0]), 0);
  if (!e || !d) {
    fprintf (stderr, "%s\n", err);
    return 1;
  }
  for (msg[0] = 0; msg[0] < n * (int) sizeof (msg);
    msg[0] += sizeof (msg)) FecEncode (msg, e);
  printf ("Starting recovery...\n");
  FlushFecDecoder (d);
  printf ("Received %d, Corrected %d, Lost %d\n",
    d->receivedPackets, d->correctedPackets, d->lostPackets);
#else // This code generates prime "polynomials".
  unsigned i, j, divisor, q, m;
  for (i = 1; i < 0x20000; i <<= 1) {
    for (j = i; j < 2 * i; j++) { // Going to test j for primeness.
      for (divisor = 2; divisor < i; divisor++) {
        for (q = j, m = i; m > 0; m >>= 1) {
          if ((q ^ (m * divisor)) < q &&
          (q ^ (m * divisor)) < m * divisor) q ^= m * divisor;
        }
        if (q == 0) break; // Not prime
      }
      if (divisor >= i) break; // Prime
    }
    printf ("0x%x, ", j);
  }
#endif
#ifdef WIN32
  getchar ();
#endif
  return 0;
}
