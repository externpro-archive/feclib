#ifndef FEC_COPYRIGHT
#define FEC_COPYRIGHT "Parts copyright (c) by Nic Roets 2002. No warranty."
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SUGGESTED_FEC_UDP_PORT_NUMBER htons (7837)
// This is just a suggestion. If you modify the protocol, please use a
// different port.

#if defined (WIN32) && !defined (__CYGWIN__)
typedef __int32 fecPayload;
#define __int64_t __int64
#else
typedef __int32_t fecPayload;
#endif

typedef struct {
  int k, w, g, n, i, s;
} fecEncDec;

typedef struct {
  void *userData;
  size_t (*userSend)(void *buf, size_t size, size_t count, void *userData);
  int lastTime, b;
  fecEncDec e;
} fecEncoder;

fecEncoder *NewFecEncoder (void *userData,
  size_t (*userSend)(void *buf, size_t size, size_t count, void *userData),
  char **errorMessage,
  int s, int n, int k, int w, int g, int b);

void FecEncode (fecPayload *buf, fecEncoder *f);

void DeleteFecEncoder (fecEncoder *f);

//-----------------------------------------

typedef struct {
  int lostPackets, receivedPackets, correctedPackets; // payload only
  char *errorMessage;
  // The rest is private
  void *userData;
  void (*userReceive)(void *userData, __int64_t position, fecPayload *buf,
    int len);
  fecEncDec *e; // The redudant data is at e + 1.
  int nmissed;
  fecPayload *missed; // Keeps track of both payload and redundant packets.
} fecDecoder;

fecDecoder *NewFecDecoder (void *userData, void (*userReceive)(
  void *userData, __int64_t position, fecPayload *buf, int len));

size_t FecDecode (void *buf, size_t size, size_t count,
  fecDecoder *f);

void FlushFecDecoder (fecDecoder *f);

void DeleteFecDecoder (fecDecoder *f);

#ifdef __cplusplus
}
#endif

#endif
