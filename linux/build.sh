#!/bin/sh
# Build the Linux (GTK4) launcher: one self-contained executable, all images/video/font embedded.
# Needs: g++, pkg-config, libgtk-4-dev (GTK >= 4.10), libfontconfig-dev.
set -e
cd "$(dirname "$0")"
mkdir -p build
A=../shared/assets
{
  echo '.section .rodata'
  for pair in hero:$A/img/hero.jpg navbar:$A/img/wm_navbar.png navlogo:$A/img/wm_navlogo.png wotlk:$A/img/wotlk.png \
      ftl:$A/img/frame_tl.png ftr:$A/img/frame_tr.png fbl:$A/img/frame_bl.png fbr:$A/img/frame_br.png \
      fheader:$A/img/frame_header.png ffooter:$A/img/frame_footer.png flc:$A/img/frame_lc.png frc:$A/img/frame_rc.png \
      fcenter:$A/img/frame_center.png bmin:$A/img/btn-min.png bminh:$A/img/btn-min-h.png bclose:$A/img/btn-close.png \
      bcloseh:$A/img/btn-close-h.png futura:$A/futura.ttf video:$A/woods.bin g_lua:$A/glue/AccountLogin.lua \
      g_xml:$A/glue/AccountLogin.xml g_luao:$A/glue/AccountLogin.lua.origin g_xmlo:$A/glue/AccountLogin.xml.origin; do
    n=${pair%%:*}; f=${pair#*:}
    echo ".global asset_${n}_start"; echo ".global asset_${n}_end"; echo ".balign 16"
    echo "asset_${n}_start:"; echo ".incbin \"$f\""; echo "asset_${n}_end:"
  done
  echo '.section .note.GNU-stack,"",@progbits'
} > build/assets.S
cc -c build/assets.S -o build/assets.o
g++ -O2 -std=gnu++17 -Wall -Isrc -I../shared src/main_gtk.cpp build/assets.o -o build/unofficial-warmane-launcher \
  -static-libstdc++ -static-libgcc $(pkg-config --cflags --libs gtk4 pangocairo fontconfig) -s
ls -l build/unofficial-warmane-launcher
