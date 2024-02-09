# Copyright (c) 2015-2019 Damien Ciabrini
# This file is part of ngdevkit
#
# ngdevkit is free software: you can redistribute it and/or modify
# it under the terms of the GNU Lesser General Public License as
# published by the Free Software Foundation, either version 3 of the
# License, or (at your option) any later version.
#
# ngdevkit is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public License
# along with ngdevkit.  If not, see <http://www.gnu.org/licenses/>.

all: cart nullbios

include ../Makefile.config

# ROM names and common targets
include ../Makefile.common
$(CART): $(PROM) $(CROM1) $(CROM2) $(SROM) $(VROM) $(MROM) | rom

OBJS=main
ELF=rom.elf
FIX_ASSETS=$(ASSETS)/rom/s1-shadow.bin

$(ASSETS)/rom/c1.bin $(ASSETS)/rom/s1.bin:
	$(MAKE) -C $(ASSETS)

# -------------------------------------------- #
# --- Nuages                                   #
# -------------------------------------------- #
sprites/nuages.png: gfx/nuages.png | sprites
	$(CONVERT) $^ $^ $^ +append -crop 224x48+0+0 +repage -background black -flatten $@

sprites/nuages.c1 sprites/nuages.c2: sprites/nuages.png
	$(TILETOOL) --sprite -c $< -o $@ $(@:%.c1=%).c2

sprites/nuages.pal: sprites/nuages.png
	$(PALTOOL) $< -o $@

# -------------------------------------------- #
# --- Level1 Terre1 : 320x320                  #
# -------------------------------------------- #
sprites/back.png: gfx/back.png | sprites
	$(CONVERT) $^ $^ $^ +append -crop 320x320+0+0 +repage -background black -flatten $@

sprites/back.c1 sprites/back.c2: sprites/back.png
	$(TILETOOL) --sprite -c $< -o $@ $(@:%.c1=%).c2

sprites/back.pal: sprites/back.png
	$(PALTOOL) $< -o $@

# -------------------------------------------- #
# --- Flotte                                   #
# -------------------------------------------- #
sprites/flottes.png: gfx/flottes.png | sprites
	$(CONVERT) $^ $^ $^ +append -crop 160x16+0+0 +repage -background black -flatten $@

sprites/flottes.c1 sprites/flottes.c2: sprites/flottes.png
	$(TILETOOL) --sprite -c $< -o $@ $(@:%.c1=%).c2

sprites/flottes.pal: sprites/flottes.png
	$(PALTOOL) $< -o $@

# -------------------------------------------- #
# --- Herbe                                    #
# -------------------------------------------- #
sprites/ghost_stage1_herbe.png: gfx/ghost_stage1_herbe.png | sprites
	$(CONVERT) $^ $^ $^ +append -crop 64x64+0+0 +repage -background black -flatten $@

sprites/ghost_stage1_herbe.c1 sprites/ghost_stage1_herbe.c2: sprites/ghost_stage1_herbe.png
	$(TILETOOL) --sprite -c $< -o $@ $(@:%.c1=%).c2

sprites/ghost_stage1_herbe.pal: sprites/ghost_stage1_herbe.png
	$(PALTOOL) $< -o $@

$(ELF):	$(OBJS:%=%.o)
	$(M68KGCC) -o $@ $^ `pkg-config --libs ngdevkit`

%.o: %.c
	$(M68KGCC) $(NGCFLAGS) -std=gnu99 -fomit-frame-pointer -g -c $< -o $@

main.c: \
	sprites/nuages.pal \
	sprites/back.pal \
	sprites/flottes.pal \
	sprites/ghost_stage1_herbe.pal \

# sound driver ROM: ngdevkit's nullsound
MROMSIZE:=131072
$(MROM): | rom
	$(Z80SDOBJCOPY) -I ihex -O binary $(NGDKSHAREDIR)/nullsound_driver.ihx $@ --pad-to $(MROMSIZE)

# sample ROM: empty
$(VROM): | rom
	dd if=/dev/zero bs=1024 count=512 of=$@

# sprite ROM C1 C2: parallax layers
CROMSIZE:=1048576
$(CROM1): $(ASSETS)/rom/c1.bin \
	sprites/nuages.c1 \
	sprites/back.c1 \
	sprites/flottes.c1 \
	sprites/ghost_stage1_herbe.c1 \
	| rom
	cat $(ASSETS)/rom/c1.bin $(filter %.c1,$^) > $@ && $(TRUNCATE) -s $(CROMSIZE) $@

$(CROM2): $(ASSETS)/rom/c2.bin \
	sprites/nuages.c2 \
	sprites/back.c2 \
	sprites/flottes.c2 \
	sprites/ghost_stage1_herbe.c2 \
	| rom
	cat $(ASSETS)/rom/c2.bin $(filter %.c2,$^) > $@ && $(TRUNCATE) -s $(CROMSIZE) $@

# fixed tile ROM: fonts from common assets
SROMSIZE:=131072
$(SROM): $(FIX_ASSETS) | rom
	cat $(FIX_ASSETS) > $@ && $(TRUNCATE) -s $(SROMSIZE) $@

# program ROM
PROMSIZE:=524288
$(PROM): $(ELF) | rom
	$(M68KOBJCOPY) -O binary -S -R .comment --gap-fill 0xff --pad-to $(PROMSIZE) $< $@ && dd if=$@ of=$@ conv=notrunc,swab

clean:
	rm -rf *.gif *.pal *.o *~ $(ELF) tmp.* rom sprites

sprites:
	mkdir $@

.PHONY: clean