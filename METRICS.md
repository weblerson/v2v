# V2V Project Metrics

Three metrics chosen to reflect the three layers this project actually has —
the **radio link** (ESP-NOW broadcasts), the **sensing layer** (GPS/UWB
positioning + MPU6050 classification), and the **end-to-end safety pipeline**
(remote event → local alert). Each layer has a single dominant failure mode,
and each metric below is picked to expose it.

---

## 1. Peer Packet Delivery Ratio (PDR)

**Definition.** For a given peer *p* observed over a time window *W*, let
`expected(p, W)` be the number of `V2VPacket`s the peer should have sent
(derived from the `seq` field: `max(seq) − min(seq) + 1` within the window)
and `received(p, W)` the number actually accepted by `peers::upsert`. Then:

```
PDR(p, W) = received(p, W) / expected(p, W)
```

Aggregate across fresh peers with an unweighted mean. Report as a rolling
value over the last 5 seconds.

**What it measures.** The effective reliability of the ESP-NOW broadcast
channel between a specific pair of devices, *after* the firmware's own
sequence-number deduplication (`firmware/src/peers.cpp:59`) has rejected
out-of-order and duplicate frames.

**Why it matters.**
- Every higher-level signal in this system — motion state, distance,
  bearing — rides on top of these broadcasts. If PDR collapses, the radar
  display in `monitor/radar/radar.go` silently drops the peer once
  `REMOTE_TIMEOUT_MS` (500 ms) elapses without a packet
  (`firmware/src/config.h:19`). A low-but-nonzero PDR is worse than total
  loss: the peer blinks in and out, and the driver sees a flickering
  indicator instead of a steady warning.
- The `seq` field is already on the wire and already inspected, so this
  metric is essentially free to compute — no new protocol fields, no extra
  RAM beyond the existing `PeerState.lastSeq`.
- PDR is the right signal to trigger tuning work (antenna orientation,
  channel selection, transmit power, range testing). Raw RSSI alone
  cannot tell you whether packets are arriving on time.

**Target.** ≥ 95 % PDR at 10 Hz broadcast cadence within realistic V2V
range (≤ 50 m line-of-sight). Below 80 %, the system should be considered
degraded and the driver notified.

---

## 2. Positioning Error (Distance RMSE vs. Ground Truth)

**Definition.** Let `d̂ᵢ` be the distance reported by the firmware to a
specific peer at sample *i* (emitted on the NDJSON line as
`"distance":12.3`, see `firmware/src/main.cpp:85`), and `dᵢ` the true
distance measured by an independent reference (tape measure for static
tests, RTK-GPS track for driving tests). Then:

```
RMSE = sqrt( (1/N) · Σ (d̂ᵢ − dᵢ)² )
```

Report separately for each positioning backend (`PositionGPSHandler` and
the forthcoming `PositionDWMHandler`) and across distance bands (0–5 m,
5–20 m, > 20 m), since those are the exact bands the radar renderer
colour-codes in `monitor/README.md`.

**What it measures.** The end-to-end accuracy of the relative-distance
pipeline: GPS fix quality, Haversine computation
(`firmware/src/position_gps.cpp:31`), broadcast freshness, and the
`POSITION_MAX_AGE_MS = 1500` staleness cutoff combined.

**Why it matters.**
- The user-visible behaviour of the monitor is zone-based: < 5 m is red,
  5–20 m is yellow, > 20 m is green. If the RMSE approaches the width of a
  zone, peers will be drawn in the wrong zone — and the wrong zone at
  < 5 m is the difference between "caution" and "imminent collision."
  This is the metric that tells you whether the radar is actually
  trustworthy at the distances where trust matters most.
- It is also the metric that justifies the hardware decision in
  `PLAN_DISTANCE.md`. GPS-based ranging is documented as 2–5 m error, UWB
  as ~10 cm. RMSE is the concrete number that confirms whether switching
  to UWB is worth the extra BOM cost, or whether GPS is "good enough" for
  the target use case.
- Distance-banded reporting catches a failure mode a single global RMSE
  would hide: GPS error is roughly constant in absolute metres, so its
  *relative* error explodes at close range — exactly where safety matters
  most.

**Target.** RMSE ≤ 1 m in the 0–5 m band (the red zone). In the
5–20 m band, RMSE ≤ 2 m. Outside 20 m, RMSE ≤ 5 m is acceptable.

---

## 3. Brake-Alert Latency (p95, End-to-End)

**Definition.** The elapsed wall-clock time from the instant a peer
vehicle physically begins braking (ground truth: a synchronized
timestamp on a button press wired to the peer's brake event, or the
peer-side MPU6050 crossing `ACCEL_THRESHOLD`) until the local device
classifies that peer as `BRAKING` in `motion::classify`
(`firmware/src/motion.cpp:66`) and drives `LED_BUILTIN` HIGH
(`firmware/src/main.cpp:99`). Reported as the 95th percentile over at
least 100 trial events.

```
latency = t_local_braking_asserted − t_peer_physical_brake
```

This folds together: peer-side sensor smoothing
(`SMOOTHING_WINDOW = 5`), peer-side `LOOP_INTERVAL_MS = 100` cadence,
on-air ESP-NOW transit, receiver-side `peers::upsert`, and local loop
cadence.

**What it measures.** The total reaction-time budget the system consumes
before the driver can even begin to react. It is the single most
safety-critical number in the project because every other metric above
is only useful insofar as it contributes to a low value here.

**Why it matters.**
- At 50 km/h (≈ 14 m/s), 100 ms of added latency is 1.4 m of extra
  closing distance. The current architecture has at least two 100 ms
  loop periods stacked (peer's broadcast loop and local's read loop),
  plus a 5-sample moving average on the accelerometer. A p95 latency
  measurement is the only way to verify that the sum of these stacked
  delays is actually bounded — worst-case reasoning from the config
  constants alone understates what can go wrong when retransmits or
  stale-peer fallbacks kick in.
- p95 (not mean) is the right statistic for a safety system. A mean of
  120 ms with an occasional 400 ms outlier is a bug report, not a spec;
  the p95 catches the tail that the driver will actually experience and
  remember.
- This metric is the natural regression gate for any change to
  `SMOOTHING_WINDOW`, `LOOP_INTERVAL_MS`, or the motion classifier's
  hysteresis logic. Tightening the smoothing to reduce latency trades
  against false-brake rate — exposing the tradeoff with a hard number
  prevents silent regressions either way.

**Target.** p95 brake-alert latency ≤ 250 ms, p99 ≤ 400 ms. Any
individual sample > 500 ms is treated as an incident and investigated.

---

## How These Three Fit Together

| Metric | Layer | Failure it catches |
|---|---|---|
| PDR | Radio | Silent peer dropout, flickering radar |
| Distance RMSE | Sensing | Wrong-zone rendering, bad UWB/GPS decision |
| Brake-Alert Latency (p95) | End-to-end | Slow reaction, safety-spec regression |

PDR without RMSE tells you packets arrive but not whether they carry
useful numbers. RMSE without latency tells you the numbers are right
but not whether they arrive in time. Latency without PDR looks fine
right up until the peer drops off the radar entirely. The three together
cover the project's actual safety contract.
