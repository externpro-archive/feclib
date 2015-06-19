#include <stdlib.h>
#if defined (WIN32) && !defined (__CYGWIN__)
#include <windows.h>
#include <winsock.h>
#else
#include <netinet/in.h>
// in.h defines htonl as a macro whereas winsock uses funcions.
#ifdef __CYGWIN__
#include <w32api/windows.h>
#else
#include <sys/time.h>
#define max(x,y) ((x) > (y) ? (x) : (y))
#define min(x,y) ((x) < (y) ? (x) : (y))
#endif
#endif
#include "fec.h"

#if defined (WIN32) && !defined (__CYGWIN__)
#define __int32_t __int32
#define __uint32_t unsigned __int32
#endif

typedef struct headerStruct {
  __int32_t kwg, i, n;
} headerType;

#define KBITS 17
#define WBITS 9
#define GBITS 6
#if KBITS + WBITS + GBITS != 32
#error KBITS + WBITS + GBITS != 32
#endif

static unsigned poly[]={
  0x1, 0x2, 0x7, 0xb, 0x13, 0x25, 0x43, 0x83, 0x11b, 0x203, 0x409, 0x805,
  0x1009, 0x201b, 0x4021, 0x8003, 0x1002b
}; // See the second part of fectest.c

#pragma warning(disable:4018)

static void MAC (int multiplier, fecPayload *source, fecPayload *dest, int g,
  int s)
{ // Multiply with scalar and then accumulate.
  int i, j, k, l;
  for (j = 0; j < s / sizeof (*source); j += s / g / sizeof (*source)) {
    for (k = multiplier, l = 0; k; k >>= 1, l += s / g / sizeof (*source)) {
      if (k & 1) {
        for (i = 0; i < s / g / sizeof (*source); i++) {
          dest[l + i] ^= source[j + i];
        }
      }
    }
    multiplier <<= 1;
    if (multiplier >= (1<<g)) multiplier ^= poly[g];
  }
}

fecEncoder *NewFecEncoder (void *userData,
  size_t (*userSend)(void *buf, size_t size, size_t count, void *userData),
  char **errorMessage,
  int s, int n, int k, int w, int g, int b)
{
  fecEncoder *f;
  headerType *h;
  if (s % (g * sizeof (int)) != 0 || g > 16 || (g >> GBITS)) {
    if (errorMessage) *errorMessage = "FEC : Illegal Galois field size ";
    return NULL;
  }
  if (k >> KBITS) {
    if (errorMessage) *errorMessage = "FEC : k is too large";
    return NULL;
  }
  if (w >> WBITS) {
    if (errorMessage) *errorMessage = "FEC : w is too large";
    return NULL;
  }
  
  f = (fecEncoder *) malloc (sizeof (*f) + sizeof (*h) + s * (k + 1));
  if (!f) {
    if (errorMessage) *errorMessage = "FEC : Out of memory";
    free (f);
    return NULL;
  }
  f->userData = userData;
  f->userSend = userSend;
  f->e.s = s;
  f->e.k = k;
  f->e.w = w;
  f->e.g = g;
  f->b = b;
  f->e.n = n;
  f->e.i = 0;
  f->lastTime = 0;
  h = (headerType*) (s * k + (char*) (f + 1));
  h->n = htonl (n);
  h->kwg = htonl (k | (w << KBITS) | (g << (KBITS + WBITS)));
  memset (f + 1, 0, s * k); // Initialize the redundant packets.
  if (errorMessage) *errorMessage = NULL;
  return f;
}

static void AddToRedundant (fecPayload *buf, fecEncDec *e, int i)
{ // This is called by both the encoder and the decoder when they process the
// payload. But this code is also repeated where the decoder sets up the
// matrix. 
// I suppose a lot of pseudo random stuff can be tried, but in the end nature
// will add its own randomness by way of the packets it destroys.
  int row, mid = i * e->w % e->k; // e.g. mid = i * 87654321 % e->k
  __uint32_t coef = i + 1;
  for (row = max (0, min (mid, e->k - e->w) - e->w);
  row < min (e->k, max (mid, e->w) + e->w); row++) {
    coef *= 1763689789;
    MAC (coef >> (32 - e->g),
      buf, (fecPayload*) (row * e->s + (char *)(e + 1)), e->g, e->s);
  }
// What actually needs to be investigated is the problems at the edges :
// With "for (row = max (mid - e->w, 0); row < min (mid + e->w, e->k); row++)"
// there are columns with less than 2w non-zero entries which is a weakness.
//  Apart for the current solution,
// another solution would be to have row "-1" wrap around into row k - 1 and
// row "k" into row 0 etc. The matrix will not be a band matrix which
// complicates things.
}

#if !defined(WIN32) && !defined(__CYGWIN__)
static int GetTickCount (void)
{
  struct timeval tv;
  struct timezone tz;
  gettimeofday (&tv, &tz);
  return tv.tv_sec * 1000 + tv.tv_usec; // Overflow does not matter.
}
#endif

static void SendWithDelay (fecPayload *buf, fecEncoder *f)
{
  headerType *h = (headerType*)(f->e.k * f->e.s + (char*)(f + 1));
  int tick, s = f->e.s + sizeof (*h);
  
  memcpy (h + 1, buf, f->e.s);
  if (f->b > 0) {
    tick = GetTickCount ();
    if (f->lastTime == 0) f->lastTime = tick;
    else {
      if (tick - f->lastTime < s * 8000 / f->b) 
#if !defined(WIN32) && !defined(__CYGWIN__)
        usleep ((s * 8000 / f->b - tick + f->lastTime) * 1000);
#else
        Sleep (s * 8000 / f->b - tick + f->lastTime);
#endif
      f->lastTime += s * 8000 / f->b;
      if (tick - f->lastTime > 100) f->lastTime += (tick-f->lastTime-100) / 2;
    } // If we are more than a 10th of a second behind we reduce the bitrate.
  }
  
  h->i = htonl (f->e.i++);
  f->userSend (h, s, 1, f->userData);
}

#define i2redundant(i,k,v) ((i) + (k) % (v) >= (k) ? (i) % (v) + \
  (k) / (v) * (v) : (i) % (v) * ((k) / (v)) + (i) / (v))
/* This function is used to send the redundant packets in a permutated order,
 distributing the effects of bust losses. The weakness of this implementation
 is that the last k % v packets are not permutated. We choose v = w.
 A technique without this drawback is : (v = w*2)
      kdw = f->e->k / f->e->w / 2;
      off2nd = i - (kdw + 1) * (f->e->k % (f->e->w * 2));
      i2redundant = off2nd < 0 ? i / (kdw + 1) + i % (kdw + 1) * f->e->w * 2 :
          f->e->k % (f->e->w * 2) + off2nd / kdw + off2nd % kdw * f->e->w * 2
        
*/


void FecEncode (fecPayload *buf, fecEncoder *f)
{
  int i;
  AddToRedundant (buf, &f->e, f->e.i);

  SendWithDelay (buf, f);
  if (f->e.i % (f->e.n + f->e.k) == f->e.n) {
    for (i = 0; i < f->e.k; i++) {
      SendWithDelay ((fecPayload*) (
        i2redundant (i, f->e.k, f->e.w) * f->e.s + (char*)(f + 1)), f);
      // To Do : Ensure that the lower CPU load at this point does not
      // create timing problems
    }
  }
}

void DeleteFecEncoder (fecEncoder *f)
{
  free (f);
}

fecDecoder *NewFecDecoder (void *userData, void (*userReceive)(
  void *userData, __int64_t position, fecPayload *buf, int len))
{
  fecDecoder *f = malloc (sizeof (*f));
  f->userData = userData;
  f->userReceive = userReceive;
  f->errorMessage = NULL;
  f->lostPackets = 0;
  f->receivedPackets = 0;
  f->correctedPackets = 0;
  f->e = NULL;
  f->nmissed = 0;
  f->missed = NULL;
  return f;
}

size_t FecDecode (void *buf, size_t size, size_t count, fecDecoder *f)
{
  headerType *h = (headerType*)buf;
  int i, mustSend = 0, hi = ntohl (h->i);
  if (!f->e) {
    f->e = calloc (1, sizeof (*f->e) + (size * count - sizeof (*h)) *
      (ntohl (h->kwg) & ((1 << KBITS) - 1)));
    f->e->n = ntohl (h->n);
    f->e->s = size * count - sizeof (*h);
    f->e->k = ntohl (h->kwg) & ((1 << KBITS) - 1);
    f->e->w = (ntohl (h->kwg) >> KBITS) & ((1 << WBITS) - 1);
    f->e->g = ntohl (h->kwg) >> (KBITS + WBITS);
    f->e->i = hi / (f->e->n + f->e->k) * (f->e->n + f->e->k);
    f->lostPackets += f->e->i;
  }
  else if (ntohl (h->n) != f->e->n || size*count - sizeof (*h) != f->e->s)  {
    f->errorMessage = "Changing of FEC parameters not supported";
    return 0;
  }

  // f->e->i is the one we are expecting
  if (f->e->i <= hi) { // If we got it, or a later one
    mustSend = 1;
    do {
      if (f->e->i % (f->e->n + f->e->k) == 0) FlushFecDecoder (f);
      if (f->e->i < hi) { // If we got a later one, this one is marked as awol
        f->missed = realloc (f->missed, (f->nmissed + 1) * sizeof (*f->missed));
        f->missed[f->nmissed++] = f->e->i;
      }
    } while (++f->e->i <= hi);
  }
  else {
    for (i = f->nmissed - 1; i >= 0; i--) {
      if (f->missed[i] == hi) { // One of the awols showed up late
        f->nmissed--;
        f->missed[i] = f->missed[f->nmissed];
        mustSend = 1;
        break; // Don't bother to free() the 4 bytes.
      }
    }
  }
  if (mustSend) {
    i = hi % (f->e->n + f->e->k) - f->e->n;
    if (i < 0) {
      AddToRedundant ((fecPayload*) (h + 1), f->e, hi);
      (*f->userReceive)(f->userData, (hi / (f->e->n + f->e->k) * f->e->n +
        i + f->e->n)  * (__int64_t) f->e->s, (fecPayload*)(h + 1), f->e->s);
      f->receivedPackets++;
    }
    else {
      MAC (1, (fecPayload*) (h + 1), (fecPayload*)(i2redundant (i, f->e->k,
        f->e->w) * f->e->s + (char*) (f->e + 1)), f->e->g, f->e->s);
    }
  }
  
  return size * count;
}

static int ew, ek; // To Do : make this code reentrant.

static int MissingCompare (const void *a, const void *b)
{ // One of the short comings of qsort
  return * (__int32_t *) a * ew % ek - * (__int32_t *) b * ew % ek;
}

#define BITS ((int) sizeof (int) * 8) // The # of columns stored in each *coef

void FlushFecDecoder (fecDecoder *f)
{
  struct {
    int start, len, pivotLog, *coef;
    // start is in terms of missed packets / bits
    // len is the number of words.
    // coef has len groups of g "ints". column "start" correspond to the
    // least significant bits in the g "ints" of the first group.
    fecPayload *redundant;
  } *r, **matrix, *best;
  int i, tmp, row, mid, j, leader, bestLeader = 0, k, *GFlog, *GFexp;
  fecPayload *final;
  __uint32_t coef;

#ifdef DEBUG_Z2 // This debugging / visualization code only works if g = 1.
  FILE *sf = fopen ("sf.txt", "w"); // For a graphical representation.
#endif

  while (f->e->i % (f->e->n + f->e->k) != 0) {
    f->missed = realloc (f->missed, (f->nmissed + 1) * sizeof (*f->missed));
    f->missed[f->nmissed++] = f->e->i++;
  }

  if (f->nmissed == 0) return; // This happens at startup
  if (f->nmissed > f->e->k) {
    for (i = 0; i < f->nmissed; i++) {
      if (f->missed[i] % (f->e->n + f->e->k) < f->e->n) f->lostPackets++;
    }
    f->nmissed = 0;
    free (f->missed);
    f->missed = NULL;
    return;
  }
  
  r = malloc (sizeof (*r) * f->e->k);
  matrix = malloc (sizeof (*matrix) * f->e->k);
  for (i = 0; i < f->e->k; i++) {
    r[i].coef = NULL;
    r[i].start = 0;
    r[i].len = 0;
    r[i].pivotLog = -1;
    r[i].redundant = (fecPayload*) (i * f->e->s + (char*) (f->e + 1));
    matrix[i] = r + i;
  }
  for (i = f->nmissed - 1; i >= 0; i--) {
    tmp = f->missed[i] % (f->e->n + f->e->k) - f->e->n;
    if (tmp >= 0) { // Drop the redundants we don't have.
      matrix[i2redundant (tmp, f->e->k, f->e->w)] = NULL;
      f->missed[--f->nmissed] = f->missed[i];
    }
  }
  
  // Now f->missed only contains the payload packets.
  ew = f->e->w;
  ek = f->e->k;
  qsort (f->missed, f->nmissed, sizeof (*f->missed), MissingCompare);
  // The sorting places all the nonzero entries in the matrix together.
  
  for (i = 0; i < f->nmissed; i++) { // Build matrix
    mid = f->missed[i] * f->e->w % f->e->k; // e.g. mid = i * 87654321 % e->k
    coef = f->missed[i] + 1;
    for (row = max (0, min (mid, f->e->k - f->e->w) - f->e->w);
    row < min (f->e->k, max (mid, f->e->w) + f->e->w); row++) {
      coef *= 1763689789;
      tmp = coef >> (32 - f->e->g);
      if (tmp == 0 || !matrix[row]) continue;
      if (r[row].start + r[row].len * BITS <= i) { // Need space ?
        if (r[row].len == 0) r[row].start = i / BITS * BITS;
        r[row].coef = realloc (r[row].coef,
          (i + BITS - r[row].start) / BITS * f->e->g * sizeof (int));
        memset (r[row].coef + r[row].len * f->e->g, 0, f->e->g * sizeof (int)
          * ((i - r[row].start) / BITS + 1 - r[row].len));
        r[row].len = (i - r[row].start) / BITS + 1;
      }
      for (j = (i - r[row].start) / BITS * f->e->g; tmp > 0; j++, tmp >>= 1) {
        if (tmp & 1) r[row].coef[j] ^= 1 << (i & (BITS - 1));
      } // Shift the bits into the matrix
    } // for each row.
  } // for each column

  // Work out the Galois field
  GFexp = malloc (sizeof (*GFexp) * ((2 << f->e->g) - 2));
  GFlog = malloc (sizeof (*GFlog) * (1 << f->e->g));
  for (i = 0, tmp = 1; i < (2 << f->e->g) - 2; i++) {
    GFexp[i] = tmp;
    if (i < (1 << f->e->g) - 1) GFlog[tmp] = i;
    
    tmp <<= 1;
    if (tmp >> f->e->g) tmp ^= poly[f->e->g];
  }
  
  // Now the slow bit : creating the pivots rows
  // Actually, if the system is overdetermined to a great degree
  // (i.e. very few packets were lost) all the pivots may already exist,
  // we just have to find them.
  for (i = 0; i < f->nmissed;) {
#ifdef DEBUG_Z2
    for (row = 0; row < f->e->k; row++) {
      best = matrix[row];//r + row;
      for (j = k = 0; j < f->nmissed; j++) {
        tmp = best->start <= j && j < best->start + BITS * best->len &&
          ((best->coef[(j - best->start)/BITS] >> (j & (BITS - 1))) & 1);
        fputc (tmp ? '*' : ' ', sf);
        if (tmp) k ^= f->missed[j] * 4;
      }
      fprintf (sf, k == best->redundant[0] ? "yes\n" : "no\n");
    }
    fprintf (sf, "Now trying to create a pivot in row %3d\n", i);
#endif
    bestLeader = -1; // The leader is the first non zero element.
    for (row = i; row < f->e->k && bestLeader < i; row++) {
      if (!matrix[row]) continue;
      for (leader = 0; leader * BITS + matrix[row]->start <= i &&
      leader < matrix[row]->len; leader++) {
        for (j = k = 0; k < f->e->g; k++) {
          j |= matrix[row]->coef[leader * f->e->g + k];
        }
        if (j == 0) continue; // Row starts with 32 zeros
        for (leader = leader * BITS + matrix[row]->start; !(j & 1);
        leader++) j >>= 1;
        if (leader > i || bestLeader >= i) break; // not a new best so break
        bestLeader = leader;
        tmp = row;
        break; // We worked out where the leader is.
      }
    }
    if (bestLeader < 0) break;
    best = matrix[tmp];
    matrix[tmp] = matrix[i];
    matrix[i] = best;
    // Now eliminate *best from bestLeader to i.
    for (j = bestLeader; ; j++) {
      leader = 0; // If j = i we calculate leader before quiting the loop.
      if ((j - best->start) / BITS < best->len) {
        for (k = ((j - best->start) / BITS + 1) * f->e->g - 1;
        k >= (j - best->start) / BITS * f->e->g; k--) {
          leader <<= 1;
          if (best->coef[k] & (1 << (j & (BITS - 1)))) leader++;
        }
      }
      if (j >= i) break; // Bail out with "leader" the pivot
      if (!leader) continue; // Multiplying with 0 has no effect
      leader = GFexp[GFlog[leader] + (1<<f->e->g) - 1 -
        matrix[j]->pivotLog];
      // Now we want *best += best[j] / matrix[j]->pivot * matrix[j].
      // Redundants are easy :
      MAC (leader, matrix[j]->redundant, best->redundant, f->e->g, f->e->s);
        
      // The matrix is itself more tricky : have to check for space first.
      // Note that with normal band matrices we can require that each
      // row's tail not end before the row above it and then the code
      // below would never excute. But this Gauss elimination is not normal,
      // and there is always a possibility that we may end up needing a
      // row that was put aside long ago.
      k = matrix[j]->start / BITS + matrix[j]->len
             - best->start / BITS - best->len;
      if (k > 0) {
        best->coef = realloc (best->coef,
          (best->len + k) * sizeof (best->coef[0]) * f->e->g);
        memset (best->coef + best->len * f->e->g, 0,
          k * f->e->g * sizeof (best->coef[0]));
        best->len += k;
      }
      
      for (k = (j - matrix[j]->start) / BITS,
      tmp = (j - best->start) / BITS; k < matrix[j]->len; k++, tmp++) {
        MAC (leader, matrix[j]->coef + k * f->e->g,
          best->coef + tmp * f->e->g, f->e->g, f->e->g * sizeof (int));
      }
    } // For each entry we eliminate
    if (leader != 0) matrix[i++]->pivotLog = GFlog[leader];
  } // For each pivot we need
  
  if (bestLeader < 0) f->lostPackets += f->nmissed;
  else { // Let's do back substitution
    f->correctedPackets += f->nmissed;
    final = malloc (f->e->s);
    for (i = f->nmissed - 1; i >= 0; i--) {
      memset (final, 0, f->e->s);
      MAC (GFexp[(1<<f->e->g) - 1 - matrix[i]->pivotLog],
        matrix[i]->redundant, final, f->e->g, f->e->s);
      // Now that we have final, we may as well back substitute into all
      for (j = 0; j < i; j++) { // the rows above
        k = (i - matrix[j]->start) / BITS;
        if (k >= matrix[j]->len) continue;
        for (leader = 0, tmp = f->e->g - 1; tmp >= 0; tmp--) {
          leader = (leader << 1) + (1 & (
            matrix[j]->coef[k * f->e->g + tmp] >> (i & (BITS - 1))));
        } // We call MAC, even if leader is 0. Is it inefficient ?
        MAC (leader, final, matrix[j]->redundant, f->e->g, f->e->s);
      }
      (*f->userReceive)(f->userData,
        (f->missed[i] / (f->e->n + f->e->k) * f->e->n + f->missed[i] %
        (f->e->n + f->e->k)) * (__int64_t) f->e->s, final, f->e->s);
    }
    free (final);
  }
#ifdef DEBUG_Z2
  fclose (sf);
#endif
  
  free (GFlog);
  free (GFexp);
  for (i = 0; i < f->e->k; i++) free (r[i].coef);
  free (matrix);
  free (f->missed);
  f->missed = NULL;
  f->nmissed = 0;
  free (r);
}

void DeleteFecDecoder (fecDecoder *f)
{
  if (f) free (f->e); // free(NULL) is valid.
  free (f);
}
