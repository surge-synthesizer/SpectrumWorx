# Latency

**SpectrumWorx delays its output by exactly one FFT window, whatever block size
the host calls it with, and that is the number it reports.**

```cpp
std::uint16_t Setup::latencyInSamples() const { return frameSize<unsigned int>(); }  // == fftSize
```

That sentence was not true before issue #83. This file is why the number is what
it is, why it cannot be smaller, and what went wrong when it was not constant.

---

## 1. Why a WOLA engine has latency at all

The engine analyses overlapping windows and overlap-adds the results back
together. With an FFT size `W` and an overlap factor `V`, the hop is

```
H = W / V
```

and frame *k* analyses input samples `[kH - W, kH)`.

An output sample is not finished when the frame covering it is computed. It is
finished when **every** frame covering it has been computed and added, because
the analysis and synthesis windows only sum to a constant across all `V` of them
— that is the COLA condition, and `Processor::calculateWindowAndWOLAGain()` is
where the two windows are built to satisfy it.

Which frames cover output sample `p`? Those with `kH - W <= p < kH`, i.e.

```
k  in  { floor(p/H) + 1  ...  floor(p/H) + V }
```

The last of them is frame `floor(p/H) + V`, which analyses input up to
`p + W`. **So output sample `p` cannot be finished until input sample `p + W - 1`
has arrived.** That is not an implementation choice; it is the overlap-add.

### The window is why you cannot read the newest hop

It is tempting to think the newest hop of the newest frame is already the answer
— run the transform, take the last `H` samples, done, latency `H`. With a
rectangular window that is true. With a real window it is not, because **the
newest hop of a frame sits at the tail of the window, where the window is
almost zero.**

Concretely, `W = 8`, `V = 4`, `H = 2`, periodic Hann:

```
w   = [0,  .146,  .5,  .854,  1,  .854,  .5,  .146]
w²  = [0,  .021,  .25, .729,  1,  .729,  .25, .021]     <- analysis x synthesis
```

`w²` sums to 1.5 across the four overlaps, at every offset — that is COLA.

Now watch one two-sample output region accumulate as the four frames that cover
it arrive:

```
                        contributes   region reads      as % of 1.5
  frame p/2+1           w²[6], w²[7]   0.25, 0.02        17 %,   1 %
  frame p/2+2           w²[4], w²[5]   1.25, 0.75        83 %,  50 %
  frame p/2+3           w²[2], w²[3]   1.50, 1.48       100 %,  99 %
  frame p/2+4           w²[0], w²[1]   1.50, 1.50       100 %, 100 %
```

Reading it after the first frame gives you 17 % and 1 % of the signal — a faded,
comb-filtered mess, not the signal. And you cannot divide the window back out:
`w[0]` is exactly zero, and near it you would be amplifying whatever the effect
did to that frame by 1/ε.

The four-frame cross-fade is also the whole point. It is what makes a *modified*
spectrum reconstruct smoothly; with a rectangular window, successive frames'
modifications disagree at every hop boundary and you get blocking artifacts. The
latency buys the cross-fade. It is the mechanism, not overhead.

---

## 2. The two primings

`Processor::resetChannelBuffers()` starts both FIFOs part-full:

```cpp
channel.reset(windowSize - stepSize,   // input:  pretend this much silence arrived
              stepSize);               // output: hand out this much silence first
```

**Input, `W - H`.** Without it the first frame would wait for a whole window of
real input. With it the first real hop completes a window immediately, and from
then on every iteration needs exactly one hop — `needed = W - held` and `held`
sits at `W - H`. This priming has been there since 2012.

**Output, `H`.** This is the one issue #83 added, and section 4 is what it is
for.

Together they put the delay at `W`.

---

## 3. The fill-and-emit pattern

`W = 512`, `H = 128`, so the input FIFO is four hop-slots wide and starts
`[Z Z Z _]`. Write `A` for input samples 0–127, `B` for 128–255, and so on.
Consuming a hop fills the last slot, fires a frame, then drops the oldest.

### Blocks of 256 — a whole number of hops

```
                 held  consume   FIFO after   frame  ready  emits
  reset           384            [Z Z Z _]            128
  blk1 iter1      384    A       [Z Z A _]    yes     256    Z*   (the primed hop)
       iter2      384    B       [Z A B _]    yes     256    Z
  blk2 iter1      384    C       [A B C _]    yes     256    Z
       iter2      384    D       [B C D _]    yes     256    Z
  blk3 iter1      384    E       [C D E _]    yes     256    A    <- first real output
       iter2      384    F       [D E F _]    yes     256    B
```

`A` emerges at output sample 512. Every iteration consumes one hop and produces
one hop; `ready` returns to 128 after each extraction.

### Blocks of 500 — not a whole number of hops

```
                 held  consume        frame  ready  emits
  reset           384                         128
  blk1 iter1      384   A (128)        yes     256    Z*
       iter2      384   B (128)        yes     256    Z
       iter3      384   C (128)        yes     256    Z
       iter4      384   D[0..115] ←    NO      128    Z[0..115]   <- from the slack
  blk2 iter1      500   D[116..127]    yes     140    Z[116..127]
       iter2      384   E              yes     256    A           <- first real output
       iter3      384   F              yes     256    B
       iter4      384   G              yes     256    C
       iter5      384   H[0..103]      NO      128    D[0..103]
```

`A` emerges at output sample 512 again. The partial hop in `iter4` is served out
of the primed slack instead of out of nowhere.

### Why the slack has to be exactly one hop

At every point the invariant is

```
ready  =  H - (held - (W - H))  =  W - held  =  needed
```

and `sizeToConsume <= needed` by construction. So `sizeToProduce <= ready`
**always** — the zero-fill branch in `Processor::processSingleChannel` is
unreachable rather than merely unused. `held` never leaves `[W - H, W)`, so
`ready` never leaves `(0, H]`.

`H - 1` of slack is not enough: it fails the inequality by exactly one sample.

---

## 4. The bug this fixed (issue #83)

Without the output priming, `ready` starts at 0, and a block whose length is not
a whole number of hops ends on a partial hop the engine cannot produce output
for. It had nothing to hand back, so it **emitted silence into a running
stream**:

```
  blk1 iter4      384   D[0..115]      NO       0     116 ZEROS   <- inserted
```

Those 116 real samples then trailed out a block later, and everything after them
stayed late.

### It was drift, not a fixed offset

Working the bookkeeping through, after `c` samples consumed:

```
held  = (W - H) + (c mod H)
ready = zerosInserted - (c mod H)
```

`ready` must stay non-negative, so the engine inserted zeros whenever `c mod H`
exceeded every value it had seen before. **The total was a high-water mark, and
it only ever grew.** For 500-sample blocks against a 128 hop,
`500k mod 128 = 116k mod 128` and `gcd(116, 128) = 4`, so the mark climbed
through `{0, 4, 8, ... 124}` — the delay crept towards a full hop and stayed
there.

### Measured, before and after

An impulse through a bypassed chain, delay in samples:

| fft/overlap | calls of 128 | 256 | 512 | 1024 | 500 | 116 |
| --- | --- | --- | --- | --- | --- | --- |
| **before** 512/4 | 384 | 384 | 384 | 384 | 500 | 500 |
| **before** 2048/8 | 1920 | 1792 | 1792 | 1792 | 2036 | 2024 |
| **after** 512/4 | 512 | 512 | 512 | 512 | 512 | 512 |
| **after** 2048/8 | 2048 | 2048 | 2048 | 2048 | 2048 | 2048 |

Note the before-row for 2048/8 at 128-sample calls: a *sub-hop* call, which is
what a small host block with a large FFT produces on every block.

`tests/core/chunkTransparencyTests.cpp` pins both halves — that cutting a block
up does not change the sound, and that the delay is `fftSize`.

### Where the partial hops come from in practice

`SpectrumWorxCLAP::process()` cuts every host block at `engineChunkSize()`, which
is the hop, so that a parameter event can be applied at the sample the host timed
it for. The last piece of each block is therefore `frames_count % hop` — a
partial hop on every block, for any host block size that does not divide by the
hop.

---

## 5. What the change did to the goldens

The fix adds one hop of delay to every render. Verified as a **bit-exact shift**:
comparing the bypassed chain and Pitch Spring before and after, `new[n + H]`
equals `old[n]` with a worst absolute difference of `0.000e+00`. No arithmetic
changed.

Every row of `goldens.txt` and `sideChain.txt` was regenerated for that shift.
Two consequences worth knowing:

- **Peak, RMS and DC offset barely move** (worst 0.028, 0.0074 and 0.0021), and
  only because a fixed-length render loses `H` samples off the end and gains `H`
  of silence at the front.
- **Two impulse rows lost their band columns entirely.** The digest averages
  non-overlapping Hann windows, which is zero at every 1024-sample boundary; at
  2048/8 the impulse's output now lands on 7168 = 7 × 1024 exactly. Peak and RMS
  are byte-identical, so the impulse is still there — the analysis stopped seeing
  it. \see issue #88.

---

## 6. What is still not constant

Two effects — **Freqverb** and **Whisperer** — still render differently depending
on how many times `process()` was called, even with every call a whole hop.
`ModuleDSP::preProcess()` runs the LFO update and `setup()` once per call, and
chunking multiplies the call count. That is issue #86, and it is a different
quantity from anything above: not the block *length*, the call *count*.
