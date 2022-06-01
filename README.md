# text-to-stereogram
Generate autostereograms ("magic eye" pictures) from either text, or monochrome
depth buffer inputs. This has been used, amongst other things, to generate the
cover art for my second album,
[Ataraxy](https://depthbuffer.bandcamp.com/album/ataraxy).

## Example

```
text-to-stereogram.exe -t /usr/share/text-to-stereogram/gold_tile.png -f /usr/share/fonts/julietaula-montserrat-fonts/Montserrat-BlackItalic.otf -s 140 -w 1280 "Hello, world!"
```

## Options

Required options:
* `-t <filename>` to specify the tile image
* Either:
  * `-f <filename>` to specify a font, and some text at the end
  * `-m <filename>` to specify a depth map

Optional options:
* `-w` and `-h` to set output width and height
* `-o <filename>` to save the output to an image, e.g. `-o foo.png`
* `-c` to generate output for cross-eyed viewing (default is
  wall-eyed/divergent)
* `-l` to specify the pattern length divisor
  * With the default divisor of 2.0, the pattern will shorten by up to half the
    original tile width. A divisor of 4.0 means it will only shorten by up to
    one quarter of the original width (at its shortest, it will be 3/4ths of
    the original width), preserving more of the original tile, but "compressing"
    the geometry into a smaller depth range.

Additional options in text mode:
* `-s <number>` to specify font size
* `-d <number>` to specify text depth
  * 1 = far, 255 = near. With the default pattern length divisor, using the
    supplied example input tiles, good values are around 20 to 80.

# License & Copyright

Copyright 2022 Philip Allison.

This program is free software: you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the Free
Software Foundation, either version 3 of the License, or (at your option)
any later version.

This program is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see
\<[https://www.gnu.org/licenses/](https://www.gnu.org/licenses/)\>. 

# How does it work?

Our brains use multiple visual cues to determine depth in 3D space: light and
shadow, how things change relative to each other when they move or we move our
heads, learned expectations about the relative sizes of objects, and so on. But
the most direct and "mechanically obvious" is that, because of the space between
our eyes, we have to physically orient them in order to see a clear picture: to
look at something close to us, our eyes have to turn inwards towards each other;
to look at something far away, they turn outwards.

By taking a tall, thin image and repeating it horizontally at regular intervals,
you end up with something that can be viewed at "multiple distances": you can
look at it normally, or by intentionally orienting your eyes either inwards or
outwards by just the right amount, you can make each eye look at a different
repetition, but trick your brain into still holding position & maintaining sharp
focus, because it'll look like a single image again when it combines the two.
Depending on which way your eyes went (outwards or inwards), and how far (are
they looking at instances right next to each other, or did you go so cross-eyed
that they're actually looking two or three instances apart?), the image will
appear to jump forwards or backwards in space by fixed amounts.

By taking advantage of the fact that digital images are composed of grids of
tiny, individual pixels, if you take one of the image repetitions and shift
portions of it left or right by a few pixels - leaving the one to the left of
it unchanged, but treating the new, modified tile as "canonical" for any further
repetitions to the right - then, when you again make each eye look at a
different repetition, and have the brain interpret them as one, those portions
will appear slightly nearer or farther than the surrounding plane. By shifting
the horizontal strips marginally left or right, you force your eyes to have to
orient inwards/outwards to maintain the overlap that gives the illusion of a
single image; and by keeping those shifts small, it looks like an object with an
uneven surface, or a rectangle hovering in front of/behind a flat plane, as the
amount of re-orientation required is similar to when your eyes are tracking over
the surface of a real, physical object.

Taking this to its logical extreme:
* Take a tall, thin, rectangular input image, and decompose it into individual
  rows, each of which will be repeated horizontally as many times as required to
  fill the width of the final output image.
* For each row of the output image, first fill in one single "canonical" strip
  by taking an unmodified, one-pixel-high, horizontal slice of the input and
  copying it directly to the output, butting up against the left-hand edge.
  * Also, copy this horizontal strip into a ring buffer of pixels. The starting
    length of this buffer is the same as the width of the canonical strip, and
    the starting offset is zero.
* From here, go pixel by pixel, left to right, starting at the right-hand edge
  of the canonical strip just filled in.
* For each pixel:
  * You need to be able to decide: compared to its left-hand neighbour pixel, is
    it supposed to be closer to the viewer, further away, or at the same depth?
    This is where the greyscale depth map comes in.
    * If it's at the same depth, just fill in the output with the current pixel
      from the ring buffer.
    * If it's meant to be closer, you need to shorten your pattern (to force the
      eyes to orient inwards to maintain overlap when viewing). Look at the
      difference in depth between the current pixel and the one to its left:
      if you've gone from, for example, depth 10 to depth 15, where higher means
      closer, discard the next five pixels in the ring buffer (including the
      current one) before filling in the output.
      * It is important that you actually reduce the length of the ring buffer,
        not just jump forward by adding 5 to the offset.
    * If it's meant to be further away, you need to lengthen your pattern.
      You'll have to magically insert extra pixels into the ring buffer before
      taking from it to fill in the output.
      * Again, it is important that this actually lengthens the buffer, not just
        plays tricks with the offset.
  * Increment your ring buffer offset by one (wrapping around as appropriate),
    go to next pixel. When you reach the end of the row, start the whole process
    again one row down.

Problems with going deeper
==========================

The attentive reader may have noticed a glaring omission from the explanation
above: when lengthening the ring buffer, where do you get these mystery extra
pixels from? Well, the only thing that really matters is that they don't echo
anything else that might appear to their left in the final output row: if that
happens, you might create unintended short overlaps, and get "phantom objects"
that appear when viewing the image, repeating off to the right indefinitely.
If you ever look at a magic eye picture and can see the hidden object clearly,
but it has random spikes and floaty bits awkwardly repeating off to either the
left or right, someone's done a bad job of either choosing their extra pixels,
maintaining their buffer length, or both, and they should feel bad.

You can either make them up (literally, by picking random red, green, and blue
colour values), take them at random from the input tile (effectively giving you
a coherent "colour palette" to pick from), or - what I like to do - you can take
them horizontally from the input tile, in order, but not from the current row.

Initially, these extra pixels, because they don't overlap with anything when
they first appear (further to the right, they will overlap with themselves as
the pattern repeats - but at first they are unique), create "depth shadows".
If you have a rectangle hovering 5 "depth increments" above a flat plane, then
at the right-hand edge of the rectangle, where the depth drops down, there will
be a five-pixel-wide strip that might as well be filled with random garbage.
This is why large jumps in depth don't tend to work well in autostereograms:
you can't put anything immediately to the right of them (or left, depending on
your algorithm). Which explains why, aside from the obvious aesthetic
considerations, scenes in stereograms tend to comprise a single central object,
either floating in empty space, or resting on a flat surface.

Tips
====

* For text, big, bold, chunky fonts at large sizes work best. Otherwise the lack
  of light, shadow, colour, and all the other various depth cues - plus the
  depth shadows from the rendering process itself - cause the letters to simply
  get lost in the noise. This is why my example uses Montserrat Black at 140pt.
* Similarly, for 3D geometry, simple, blocky shapes work well. Again, due to the
  artefacting, don't hide something detailed behind & to the right of something
  else; it'll just get lost. Long, sweeping gradient changes in depth work
  well.
* The more colour variation in the tile, the more chance it stands of generating
  a good, clear, easy-to-view stereogram. Specifically, avoid horizontal bands
  of the same colour, or anything that repeats horizontally within the tile
  itself (unless it's also offset vertically). The less colour variation there
  is on each line, the less "raw information" there is available to the encoding
  process, and the greater the chance of getting random repeating "phantom
  objects" or regions where detail disappears because it's turned into a line
  of mostly the same colour.
* If the difference in focal distance between the nearest & furthest elements in
  a large image gets so large that it is difficult to track over the entire
  surface without breaking away, and/or you've tried everything else but still
  have some repeating phantom objects, tweaking the `-l` argument can help. By
  increasing it, you limit the amount of distance between the nearest and
  furthest elements, "compressing" the image into a smaller depth range. This
  also keeps the pattern longer at its shortest points, which can eliminate
  accidentally getting some earlier geometry "stuck" in the repeating part.
* There are two kinds of autostereogram: ones designed to be viewed by going
  cross-eyed (described above), and ones designed to be viewed by going
  "wall-eyed" (eyes go outwards until there's an overlap, instead of inwards).
  Personally I find going intentionally cross-eyed much easier than going
  wall-eyed, and that I can do it by much larger amounts; hence, cross-eyed
  images tend to be easier to view when the image itself is physically big, but
  the smaller/more-zoomed-out it gets, the easier it becomes to accidentally
  overshoot the desired overlap and end up viewing mangled garbage. If you're
  going to use this for any serious artistic purpose, try both, and consider the
  trade-offs relevant to your expected viewing conditions: are you making
  something designed to be printed as a poster/billboard, or a small image for
  the web?
* To try and create more aesthetically pleasing images when using tiles that
  themselves contain actual, recognisable pictures, the code is all fancy and
  renders things in a two-step process: once to figure out which pixels of the
  tile end up where in the output, then it mangles the input tile and goes
  again. Specifically, it tries to arrange things such that the horizontal
  center of the image will contain the clearest representation of the original
  tile, instead of starting pristine on the left and getting progressively more
  garbled to the right.
