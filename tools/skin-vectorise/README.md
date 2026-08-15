# skin-vectorise

Redraws `assets/skin/NN.png` as `assets/skin/NN.svg`.

`resources.cpp` prefers the vector where both exist, so converting a file is
adding one and reverting is deleting it. Nothing here runs as part of the
build: it is run by hand, its output is committed, and this exists so that the
next file is converted the same way the last one was -- and so the numbers
below can be re-checked rather than taken on trust.

## Rules the output has to keep

**No filters.** JUCE 8's SVG parser has no `<filter>` support at all, so
`feGaussianBlur` is not available. The glow around a lit button is concentric
paths with falling opacity. Check `juce_SVGParser.cpp` before using anything
exotic; gradients, `fill-rule`, `fill-opacity` and arcs are all supported, as
is implicit command repetition and `.5`-style compact separators
(`parseNextNumber` accepts a leading `.` and stops at a second one).

**The canvas is the `<svg>` width/height, and must match the PNG exactly.**
Widgets are laid out from `getWidth()`/`getHeight()`, so a vector that comes
back a different size moves controls around the editor. Note that JUCE gives a
`DrawableComposite` bounds that are the union of its children -- the *ink* --
which for these buttons is smaller than the page: 09's pill stops ~4px short
on every side, and sizing off `getDrawableBounds()` yields 50x24 where the skin
says 57x24. `tests/gui/skinTests.cpp` asserts the sizes.

**Text is Bitstream Vera, converted to outlines.** That is what the artwork was
drawn in -- at cap-height 6 it reproduces the original widths with ~0.2px/gap
of tracking, where Lato needed 1.3 -- and it is already the skin's font
(`assets/skin/Vera.ttf`, `VeraBd.ttf`, loaded by `regularTypeface()` /
`boldTypeface()`). Outlines rather than `<text>` so the files carry no font
dependency and do not depend on JUCE's text layout. Spacing is Vera's natural
tracking, centred on the shape, rather than stretched to the old ink width.

## Method

Nothing is eyeballed. For each button the PNG is measured and the parameters
fitted, then the result is rendered back and compared to the original:

- geometry: a rounded rect fitted to the alpha channel by supersampled
  coordinate descent (the pills land at ~0.17/255 mean alpha error)
- gradient: the brightest interior pixel per row, fitted linearly, with a flat
  run at the top where the line would exceed white
- glow and rim: fitted against the lit variant of the same button, comparing
  premultiplied colour and alpha
- text: cap height and baseline fitted by rendering candidates and comparing
  against an ink mask. The mask is *weighted*, because the body gradient runs
  down to near-black and the label is near-black throughout -- towards the
  bottom of a button there is no contrast left to read ink from, and taking
  those rows at face value reads the dark tail of the gradient as one enormous
  glyph.

## Running it

Needs `python3` with `fonttools`, `numpy` and `pillow`, and `rsvg-convert`
(`brew install librsvg`) for the render-and-compare step.

    python3 tools/skin-vectorise/run_pills.py      # the pill buttons
    python3 tools/skin-vectorise/describe.py 21    # dump one PNG's structure
    python3 tools/skin-vectorise/pairsheet.py 9,8  # PNG next to SVG, magnified

`run_pills.py` is idempotent -- re-running it reproduces the committed files
byte for byte, which is the check that a change to the fitter was intentional.

## What is converted

49 of the 50 files, one generator per family, because the families have
nothing in common but the method:

| generator | files | what they are |
|---|---|---|
| `run_pills.py` | `08`-`11`, `30`-`35` | rounded rect, vertical gradient, dark label; lit variants add a blue rim and a white outer glow |
| `run_tabs.py` | `21`-`28` | square plate rounded on one top corner, a rounded lit face inside it |
| `run_buttons.py` | `04`-`06`, `13`, `14`, `57`, `66`, `67` | LEDs, arrows, the trigger button, the user's guide pair |
| `run_lfo.py` | `40`-`53` | stroked waveform glyphs, the LEDs and the slider thumb |
| `run_panels.py` | `07`, `17`, `55`-`62` | panel backgrounds, module strip, the knob ring, combos |
| `run_background.py` | `01` | the editor background |
| `run_eject.py` | `16` | the eject button |

Each is idempotent: re-running reproduces its files byte for byte, which is the
check that a change to a fitter was intentional.

**No film strips are left.** `run_knobs.py` built five of them -- the module
knob's four (`03`, `12`, `63`, `64`) and the editor knob's (`02`) -- and it is
gone with them, as are the module knob's focus ring (`65`) and LFO disc (`68`).
Both knobs draw themselves now, out of `KnobStyle` and `EditorKnobStyle`
(`src/gui/gui.hpp`) and `ModuleKnobStyle` (`src/gui/modules/moduleUI.hpp`):
five colours and six radii for one, ten and a dozen for the other, against 415
KB of PNG for 127 frames apiece at a fixed size. A knob quantised to 127 steps
that could not follow the editor's zoom was what issue #20 was about; this is
what closed it. `58` stays -- `TriggerButton` still draws its focus ring.

Three JUCE facts shape the remaining files, and any other use of `<use>` here:

- `getLinkedID` reads **`xlink:href` only**, never the modern `href`. The bare
  spelling renders perfectly in `rsvg-convert` and draws *nothing* under JUCE.
- `parseUsePath` copies a **path**, so a `<use>` may not point at a `<g>`, and it
  is styled from the `<use>` tag -- fills belong on the `<use>` or an ancestor
  `<g>`, never on the definition, where rsvg would find them and JUCE would not.
- `getGradientFillType` does not apply the element transform to a
  `userSpaceOnUse` gradient, so gradients are in `objectBoundingBox` units and
  no gradient-filled element may be rotated.

## What is deliberately still a bitmap

**`20` the About page.** Not a background but a page: fifteen lines of baked
text, a logo mark and a swash, set in a light humanist sans that is
demonstrably not Vera. Re-setting it in the skin's own font would redraw every
glyph and still cost more than the PNG.

`16` the eject button is the one file here that is **approximated rather than
fitted**, and it is worth knowing why the rule is broken there. Its bitmap is
hand drawn and not self-consistent: the rim is full alpha down the right edge
and 218 down the left, the bottom runs two rows where the sides run one, and the
fill fades in over four rows at the top against one at the sides. A faithful
model of that does not get past 16/255, because the residual is per-edge fudge
rather than model error -- and it looked worst of anything in the skin once the
editor was drawn at 150 %. So it is redrawn as what the artwork was trying to
be: a bowl with a sharp blue rim, a flat interior and a blue cross, no gradient
and no halo. Only its silhouette is fitted, that being the part a wrong guess
shows in.

## Reading the numbers

`rsvg-convert` is the referee for the render-and-compare step, and it is not a
perfect one: cairo rasterises general paths with 15 vertical subsamples, so a
horizontal edge at y=11.5 comes back as 7/15 + 8/15 rather than exactly half.
That is a fixed ~8/255 on every horizontal edge which lives in the renderer
rather than in the file, so the deltas quoted anywhere here are upper bounds.
