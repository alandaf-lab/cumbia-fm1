#!/bin/sh
# Las pantallas de la cumbia: build/ui_cumbia/*.png
set -e
mkdir -p build/host build/ui_cumbia
sed 's/^int main(void)/static int ui_test_main(void)/' tests/ui_test.c > build/host/ui_test_nomain.c
gcc -O1 -w -DFELUCCA_SLICE=0 -Ibuild/gen -Ifirmware/src -Ifirmware/hal -Ifirmware/bsp/chip -Ifirmware/bsp/boards/fm1 \
    -Itests -Ibuild/host -o build/host/cumbia_ui_render tests/cumbia_ui_render.c -lm
build/host/cumbia_ui_render build/ui_cumbia
python3 -c "
import glob
from PIL import Image
for f in sorted(glob.glob('build/ui_cumbia/*.ppm')):
    Image.open(f).save(f[:-4] + '.png')
"
