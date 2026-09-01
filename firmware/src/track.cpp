#include "track.h"

#include <string.h>

namespace {

// A gap longer than this means the samples on either side of it belong to
// different encounters; fitting a line across the gap would invent a closing
// speed that never happened.
constexpr uint32_t HISTORY_GAP_MS = 1000;

struct Track {
  uint8_t  mac[6];
  float    meters[RATE_WINDOW];
  uint32_t stamps[RATE_WINDOW];
  uint8_t  count;
  uint8_t  next;
  bool     used;
};

Track tracks[MAX_PEERS];

bool macEqual(const uint8_t a[6], const uint8_t b[6]) {
  return memcmp(a, b, 6) == 0;
}

int find(const uint8_t mac[6]) {
  for (int i = 0; i < MAX_PEERS; i++) {
    if (tracks[i].used && macEqual(tracks[i].mac, mac)) return i;
  }
  return -1;
}

// Free slot, or the one whose newest sample is oldest.
int alloc(uint32_t nowMs) {
  int      oldestIdx = 0;
  uint32_t oldestAge = 0;
  for (int i = 0; i < MAX_PEERS; i++) {
    if (!tracks[i].used) return i;
    const uint8_t  last = (uint8_t)((tracks[i].next + RATE_WINDOW - 1) % RATE_WINDOW);
    const uint32_t age  = nowMs - tracks[i].stamps[last];
    if (age >= oldestAge) { oldestAge = age; oldestIdx = i; }
  }
  return oldestIdx;
}

void reset(Track& t, const uint8_t mac[6]) {
  memset(&t, 0, sizeof(Track));
  memcpy(t.mac, mac, 6);
  t.used = true;
}

}  // namespace

namespace track {

void init() { memset(tracks, 0, sizeof(tracks)); }

void update(const uint8_t mac[6], float meters, uint32_t nowMs) {
  int i = find(mac);
  if (i < 0) {
    i = alloc(nowMs);
    reset(tracks[i], mac);
  }
  Track& t = tracks[i];

  if (t.count > 0) {
    const uint8_t last = (uint8_t)((t.next + RATE_WINDOW - 1) % RATE_WINDOW);
    if ((uint32_t)(nowMs - t.stamps[last]) > HISTORY_GAP_MS) {
      reset(t, mac);
    }
  }

  t.meters[t.next] = meters;
  t.stamps[t.next] = nowMs;
  t.next           = (uint8_t)((t.next + 1) % RATE_WINDOW);
  if (t.count < RATE_WINDOW) t.count++;
}

bool query(const uint8_t mac[6], float& closingMps, float& ttcSeconds) {
  const int i = find(mac);
  if (i < 0) return false;
  const Track& t = tracks[i];
  if (t.count < 2) return false;

  // Least-squares slope of distance against time. A plain first-difference
  // would be far noisier: at 10 cm of ranging noise over a 100 ms step it
  // would swing by ±1 m/s on a stationary pair.
  const uint8_t oldest = (uint8_t)((t.next + RATE_WINDOW - t.count) % RATE_WINDOW);
  const uint32_t t0    = t.stamps[oldest];

  double sumT = 0, sumD = 0;
  for (uint8_t k = 0; k < t.count; k++) {
    const uint8_t j = (uint8_t)((oldest + k) % RATE_WINDOW);
    sumT += (double)(uint32_t)(t.stamps[j] - t0) / 1000.0;
    sumD += t.meters[j];
  }
  const double meanT = sumT / t.count;
  const double meanD = sumD / t.count;

  double num = 0, den = 0;
  for (uint8_t k = 0; k < t.count; k++) {
    const uint8_t j  = (uint8_t)((oldest + k) % RATE_WINDOW);
    const double  dt = (double)(uint32_t)(t.stamps[j] - t0) / 1000.0 - meanT;
    num += dt * (t.meters[j] - meanD);
    den += dt * dt;
  }
  if (den <= 0.0) return false;  // every sample landed on the same millisecond

  closingMps = (float)(-num / den);

  const uint8_t newest  = (uint8_t)((t.next + RATE_WINDOW - 1) % RATE_WINDOW);
  const float   current = t.meters[newest];
  if (closingMps > MIN_CLOSING_SPEED_MPS && current > 0.0f) {
    const float ttc = current / closingMps;
    ttcSeconds = ttc > TTC_MAX_S ? TTC_MAX_S : ttc;
  } else {
    ttcSeconds = -1.0f;
  }
  return true;
}

}  // namespace track
