#!/usr/bin/env bash
#
# Every packaged icon, from one PNG.
#
# assets/LOGO.png is what this reads. The standalone app wants an .icns on
# macOS and an .ico on Windows, the .pkg wants its icon as a Rez resource fork,
# and Inno Setup wants a tall wizard banner -- four formats, eight sizes and a
# resource fork, none of which any of the others can be derived from at build
# time. So they are generated here and committed, and this script is the record
# of how.
#
# \note **assets/LOGO.svg is the mark, and this does not use it yet.** The two
# are the same drawing; the PNG is a 509x460 composition of it on a flat dark
# field, and everything below extends that field, reads its colour out of it and
# crops against it. Pointing this at the vector is a rewrite of the cropping and
# a change to every icon, so it is a job of its own rather than a side effect of
# adding the file.
#
# **macOS only, and deliberately.** iconutil, Rez and DeRez ship with the Xcode
# command line tools and have no equivalent elsewhere, so the Windows and Linux
# CI legs could not regenerate these even if they wanted to. Run it by hand when
# the logo changes; nothing in the build depends on it.
#
# Usage:  scripts/make_icons.sh
#
# SPDX-License-Identifier: GPL-3.0-or-later

set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
logo="${root}/assets/LOGO.png"
out="${root}/assets/installer"

# LOGO.png's own background colour, read out of it rather than picked: the mark
# is white on a flat dark field, and every icon below extends that field instead
# of putting the mark on a slightly different one. Re-sample it if the logo is
# ever redrawn -- `magick assets/LOGO.png -format '%[pixel:p{2,2}]' info:`.
background="#202020"

if [ ! -f "${logo}" ]; then
    echo "No ${logo} -- this script has one input and that is it." >&2
    exit 1
fi

for tool in magick iconutil sips DeRez; do
    if ! command -v "${tool}" >/dev/null; then
        echo "${tool} not found." >&2
        echo "magick is Homebrew's imagemagick; the rest ship with macOS and the Xcode command line tools." >&2
        exit 1
    fi
done

# An explicit template: BSD mktemp ignores TMPDIR without one.
work="$(mktemp -d "${TMPDIR:-/tmp}/sw-icons.XXXXXXXX")"
trap 'rm -rf "${work}"' EXIT

# The mark alone, with the canvas it was drawn on removed. LOGO.png is 509x460
# and the mark is not quite centred in it, so every icon below places the
# trimmed mark itself rather than scaling the file.
magick "${logo}" -trim +repage "${work}/mark.png"

# The PNG32: prefix on every write is load bearing. The mark is white and the
# field is a neutral grey, so ImageMagick notices there is no colour in the
# image and helpfully writes 8-bit grey+alpha -- which iconutil and Explorer
# both handle less predictably than plain RGBA, for no saving worth having at
# these sizes.
#
# `-strip` is load bearing for a different reason: ImageMagick stamps the
# creation time into the PNG, so without it two runs of this script produce
# byte-different files from identical input and every run shows up as a change
# to commit. These are generated files in a git tree; they have to be a function
# of the logo and nothing else.
strip=(-strip)

# $1 canvas edge, $2 mark edge, $3 corner radius (0 for a square), $4 output.
#
# The mark is fitted to a square box rather than to a width, so a logo that is
# taller than it is wide keeps its proportions and its centre.
square()
{
    local canvas=$1 mark=$2 radius=$3 target=$4

    if [ "${radius}" -eq 0 ]; then
        magick -size "${canvas}x${canvas}" "xc:${background}" "PNG32:${work}/plate.png"
    else
        # Apple's Big Sur grid: the body is 824/1024 of the canvas with a
        # 185/1024 corner radius, and the rest is the transparent margin the
        # Dock expects to align against. Scaled here to whatever canvas is
        # asked for.
        local body=$(( canvas * 824 / 1024 ))
        local edge=$(( (canvas - body) / 2 ))
        magick -size "${canvas}x${canvas}" xc:none \
            -fill "${background}" -draw \
            "roundrectangle ${edge},${edge} $((canvas - edge)),$((canvas - edge)) ${radius},${radius}" \
            "PNG32:${work}/plate.png"
    fi

    magick "${work}/mark.png" -resize "${mark}x${mark}" "PNG32:${work}/scaled.png"
    magick "${work}/plate.png" "${work}/scaled.png" -gravity center -composite \
        "${strip[@]}" "PNG32:${target}"
}

mkdir -p "${out}"

################################################################################
# The master square. 1024, full bleed, no rounding -- the one an about box or a
# README can use, and the source of the .ico below.
################################################################################

square 1024 620 0 "${out}/SpectrumWorxIcon.png"

################################################################################
# macOS: .icns, rounded to the Big Sur grid.
################################################################################

iconset="${work}/SpectrumWorx.iconset"
mkdir -p "${iconset}"

# iconutil reads these names and only these names.
square 1024 470 185 "${work}/rounded.png"
for size in 16 32 128 256 512; do
    magick "${work}/rounded.png" -resize "${size}x${size}" \
        "PNG32:${iconset}/icon_${size}x${size}.png"
    magick "${work}/rounded.png" -resize "$((size * 2))x$((size * 2))" \
        "PNG32:${iconset}/icon_${size}x${size}@2x.png"
done

iconutil --convert icns --output "${out}/SpectrumWorxIcon.icns" "${iconset}"

# The installer .pkg wears its icon in a *resource fork*, which
# make_installer.sh appends with Rez, and a resource fork is not what an .icns
# file is: DeRez reads the fork and finds nothing on one. `sips -i` is the piece
# that bridges them -- it attaches a generated icon resource to a file, which
# DeRez can then read out as text. Hence the scratch copy: what gets the fork is
# a throwaway, and the .icns above stays a real .icns for the app bundle.
#
# Text matters. Git stores no resource forks, so the fork itself could not be
# committed; icns.rsrc is Rez source, which is why it is the file in the tree.
magick "${work}/rounded.png" -resize 512x512 "PNG32:${work}/forRsrc.png"
sips -i "${work}/forRsrc.png" > /dev/null
DeRez -only icns "${work}/forRsrc.png" > "${out}/icns.rsrc"

################################################################################
# Windows: .ico, square and full bleed, every size Explorer asks for.
################################################################################

magick "${out}/SpectrumWorxIcon.png" \
    -define icon:auto-resize=256,128,64,48,32,16 \
    "${out}/SpectrumWorxIcon.ico"

################################################################################
# Inno Setup: the wizard banner.
#
# 164x314 is the modern wizard's image at 100% scaling; this is that at 4x, so
# it stays sharp on a high DPI display. The mark sits in the upper third because
# the lower two thirds are behind nothing on the welcome page and behind the
# progress text later.
################################################################################

magick -size 656x1256 "xc:${background}" \
    \( "${work}/mark.png" -resize 340x340 \) \
    -gravity north -geometry +0+240 -composite \
    "${strip[@]}" "PNG32:${out}/SpectrumWorxBanner.png"

echo "Icons written to ${out}:"
cd "${out}"
ls -l SpectrumWorxIcon.png SpectrumWorxIcon.icns SpectrumWorxIcon.ico \
      SpectrumWorxBanner.png icns.rsrc
