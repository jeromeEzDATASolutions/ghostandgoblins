/*
 * Copyright (c) 2020 Damien Ciabrini
 * This file is part of ngdevkit
 *
 * ngdevkit is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * ngdevkit is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with ngdevkit.  If not, see <http://www.gnu.org/licenses/>.
 */

// Additional resources for sprites
// https://wiki.neogeodev.org/index.php?title=Sprites

#include <ngdevkit/neogeo.h>
#include <ngdevkit/ng-fix.h>
#include <ngdevkit/ng-video.h>
#include <stdio.h>

#define ADDR_SCB1      0
#define ADDR_SCB2 0x8000
#define ADDR_SCB3 0x8200

#define ATRHUR_X_DEPART 51

// Fix point logic for slow scrolling
#define FIX_FRACTIONAL_BITS 3
#define FIX_POINT(a,f) ((a<<FIX_FRACTIONAL_BITS)+f)
#define FIX_ACCUM(x) (x>>FIX_FRACTIONAL_BITS)
#define FIX_ZERO (FIX_POINT(1,0)-1)

// Transparent tile in BIOS ROM
#define SROM_EMPTY_TILE 255

// Parameters of the first level
#define GNG_LEVEL1_MAP_WIDTH 224

u16 frames = 0;
u16 x = 0;
u16 x_max = (GNG_LEVEL1_MAP_WIDTH-20)*16-1;
u16 tile = 0;

int arthur_index = 0;
int arthur_sens = 1;
int arthur_absolute_x = 0;

// Clear the 40*32 tiles of fix map
void clear_tiles() {
    *REG_VRAMADDR=ADDR_FIXMAP;
    *REG_VRAMMOD=1;
    for (u16 i=0;i<40*32;i++) {
        *REG_VRAMRW=(u16)SROM_EMPTY_TILE;
    }
}

const u16 clut[][16]= {
    /// first 16 colors palette for the fix tiles
    {0x8000, 0x0fff, 0x0333, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
     0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000},
    /// sprite palettes
    #include "sprites/nuages.pal"
    #include "sprites/back.pal"
    #include "sprites/flottes.pal"
    #include "sprites/ghost_stage1_herbe.pal"
};

void init_palette() {
    /// Initialize the two palettes in the first palette bank
    u16 *p=(u16*)clut;
    for (u16 i=0;i<sizeof(clut)/2; i++) {
        MMAP_PALBANK1[i]=p[i];
    }
    //*((volatile u16*)0x401ffe)=0x0000;
    *((volatile u16*)0x0000)=0x0000;
}

typedef struct _layer_t {
    u16 sprite;
    u16 width;
    u16 height;
    s16 x;
    s16 y;
    u16 palette;
    u16 tileset_offset;
    u16 tmx[15][GNG_LEVEL1_MAP_WIDTH];
} layer_t;

#include "include_layers.c"

u16 tile_distance[GNG_LEVEL1_MAP_WIDTH];

u16 palettes[4][2] = {
    {4, 453},
    {3, 443},
    {2, 43},
    {1, 1},
};

layer_t nuages = { // 224*48
    .sprite = 1,
    .width = 14,
    .height = 3,
    .x = 60,
    .y = -50,
    .palette = 1, 
    .tileset_offset = 256,
    .tmx = {},
};

layer_t background = {
    .sprite = 15,
    .width = 32,
    .height = 15,
    .x = 0,
    .y = 0,
    .palette = 2, 
    .tileset_offset = 256, 
    .tmx = {},
};

layer_t herbe = {
    .sprite = 47,
    .width = 32,
    .height = 2,
    .x = 0,
    .y = -208,
    .palette = 4, 
    .tileset_offset = 256,
    .tmx = {},
};

void display_map_from_tmx(layer_t *layer){

    u8 i=0, j=0, k=0, l=0, p=0, s=0;
    u16 tile_tmp;
    char str[10];

    u16 sprite_compteur = layer->sprite;
    for(i=0;i<layer->width;i++){
        
        // On cree un sprite pour chaque colonne
        *REG_VRAMMOD=1;
        *REG_VRAMADDR=ADDR_SCB1+(sprite_compteur*64);

        for(j=0;j<layer->height;j++){

            if ( layer->tmx[j][i] == 0 ){
                s16 temp = *REG_VRAMRW; // Get to set
                *REG_VRAMRW = SROM_EMPTY_TILE;
                temp = *REG_VRAMRW; // Palette 
                *REG_VRAMRW = temp;
            }
            else {
                tile_tmp = layer->tileset_offset+layer->tmx[j][i]-1;
                *REG_VRAMRW = tile_tmp;
                // --- On va chercher la palette dans le tableau palettes
                u16 good_palette = layer->palette;
                for(p=0;p<4;p++){
                    if ( layer->tmx[j][i] >= palettes[p][1] ){
                        good_palette = palettes[p][0];
                        break;
                    }
                }
                *REG_VRAMRW = good_palette<<8;
            }
        }

        sprite_compteur++;
    }

    // Positionnement du sprite global
    *REG_VRAMMOD=0x200;
    *REG_VRAMADDR=ADDR_SCB2+layer->sprite;
    *REG_VRAMRW=0xFFF;
    *REG_VRAMRW=(layer->y<<7)+layer->height;
    *REG_VRAMRW=(layer->x<<7);

    // --- On chaine l'ensemble des sprites
    for (u16 v=1; v<layer->width; v++) {
        *REG_VRAMADDR=ADDR_SCB2+layer->sprite+v;
        *REG_VRAMRW=0xFFF;
        *REG_VRAMRW=1<<6; // sticky
    }
}

void update_layer(layer_t *layer){
    u16 camera_sprite_start = 0;
    *REG_VRAMMOD=0x200;
    *REG_VRAMADDR=ADDR_SCB2+layer->sprite+(camera_sprite_start*3);
    *REG_VRAMRW=0xFFF;
    *REG_VRAMRW=(layer->y<<7)+layer->height;
    *REG_VRAMRW=(layer->x<<7);
}

void change_tiles(layer_t *layer, u16 colonne_to_actualize, u16 ecart){
    u8 p=0;
    colonne_to_actualize += layer->sprite;
    u8 j=0;
    *REG_VRAMMOD=1;
    *REG_VRAMADDR=ADDR_SCB1+(colonne_to_actualize*64);
    for(j=0;j<layer->height;j++){
        *REG_VRAMRW = layer->tileset_offset+layer->tmx[j][tile+ecart]-1;
        // --- On va chercher la palette dans le tableau palettes
        u16 good_palette = layer->palette;
        for(p=0;p<4;p++){
            if ( layer->tmx[j][tile+ecart] >= palettes[p][1] ){
                good_palette = palettes[p][0];
                break;
            }
        }
        *REG_VRAMRW = good_palette<<8;
    }
}

void hide_sprite_colonne(layer_t *layer, u16 colonne_to_hide){
    u8 j=0;
    colonne_to_hide += layer->sprite;
    *REG_VRAMMOD=1;
    *REG_VRAMADDR=ADDR_SCB1+(colonne_to_hide*64);
    for(j=0;j<layer->height;j++){
        *REG_VRAMRW = SROM_EMPTY_TILE;
        *REG_VRAMRW = 1<<8;
    }
}

void check_move_arthur()
{
    static char joystate[5]={'0','0','0','0',0};
    static const u8 right_tiles[]={1,2,3,4,5,6,7};
    static const u8 left_tiles[]={14,13,12,11,10,9,8};

    char str[10];
    u16 prendre_en_compte = 0;
    u8 i=0, j=0, s=0, t=0;

    u8 u=(bios_p1current & CNT_UP);
    u8 d=(bios_p1current & CNT_DOWN);
    u8 l=(bios_p1current & CNT_LEFT);
    u8 r=(bios_p1current & CNT_RIGHT);

    joystate[0]=u?'1':'0';
    joystate[1]=d?'1':'0';
    joystate[2]=l?'1':'0';
    joystate[3]=r?'1':'0';

    if (u) { // Top
    }

    if (d) { // Bottom
    }

    if (l ) { // Gauche

        prendre_en_compte=1;
        arthur_sens = 0;

        /*if ( arthur_absolute_x > 0 && arthur_absolute_x <= 130 ){
            //arthur.x--;
            arthur_absolute_x--;
        }
        else if ( arthur_absolute_x > 130 ){*/
        if ( x > 0 ){

            x--;
            tile = (x>>4)+1;

            arthur_absolute_x--;
            background.x++;
            herbe.x++;
            nuages.x++;

            // --- Update tiles for scrolling
            if ( (tile-1)<<4 == x ){
                //change_tiles(&background, tile+10, -12);
                /*for(t=12;t<236;t+=32){
                    if ( tile >= t && tile < t+32 ){
                        //change_tiles(&background, tile-t, -12);
                        //change_tiles(&herbe, tile-t, -12);
                    }
                }*/
            }

            update_layer(&background);
            update_layer(&herbe);
            update_layer(&nuages);
        }
    }

    if (r) { // Droite

        prendre_en_compte=1;
        arthur_sens = 1;

        /*if ( arthur_absolute_x < 130 ) {
            if ( arthur_absolute_x > 130 ){
                x++;
            }
            //arthur.x++;
            arthur_absolute_x++;
        }
        else if ( x < 2110 ){*/

        if ( x <= x_max ){

            x++;
            tile = (x>>4)+1;
            arthur_absolute_x++;
            background.x--;
            herbe.x--;
            nuages.x--;

            // --- Update tiles for scrolling
            if ( (tile-1)<<4 == x ){
                if ( tile>=12 ){
                    change_tiles(&background, tile-tile_distance[tile], 20);
                    change_tiles(&herbe, tile-tile_distance[tile], 20);
                    hide_sprite_colonne(&nuages, tile-tile_distance[tile]);
                }
            }

            update_layer(&nuages);
            update_layer(&background);
            update_layer(&herbe);
        }
    }

    if ( u == 0 && d==0 && l==0 && r==0 ){
        //standby_arthur();
    }

    if ( prendre_en_compte == 1 ){
        frames=(frames+1)%60;
        if ( frames == 1 || frames == 4 || frames == 8 || frames == 12 || frames == 16 || frames == 20 || frames == 24 || frames == 28 || frames == 32 || frames == 36 || frames == 40 || frames == 44 || frames == 48 || frames == 52 || frames == 56 ) {
            arthur_index++;
        }
        const u8 *tiles = arthur_sens?right_tiles:left_tiles;
        if ( arthur_index == 7 ){
            arthur_index=0;
        }
        //display_sprite_arthur(tiles[arthur_index]);
    }
}

int main(void) {

    char str[10];
    u16 i, j, loop, t;

    ng_cls();
    clear_tiles();
    init_palette();

    //arthur_absolute_x = arthur.x;
    x = 0;

    // Init array of tileindex for scrolling
    for(i=0;i<44;i++)
        tile_distance[i] = 12;
    for(i=44;i<76;i++)
        tile_distance[i] = 44;
    for(i=76;i<108;i++)
        tile_distance[i] = 76;
    for(i=108;i<140;i++)
        tile_distance[i] = 108;
    for(i=140;i<172;i++)
        tile_distance[i] = 140;
    for(i=172;i<204;i++)
        tile_distance[i] = 172;

    // Init tmx
    for(j = 0; j < 15; j++) {
        for(loop = 0; loop < GNG_LEVEL1_MAP_WIDTH; loop++) {
            nuages.tmx[j][loop] = tmx_nuages[j][loop];
            background.tmx[j][loop] = tmx_background[j][loop];
            herbe.tmx[j][loop] = tmx_herbe[j][loop];
        }
    }

    display_map_from_tmx(&nuages);
    display_map_from_tmx(&background);
    display_map_from_tmx(&herbe);

    for(;;) {
        ng_wait_vblank();
        check_move_arthur();
        //snprintf(str, 15, "Tile %5d", tile); ng_text(2, 2, 0, str);
        //snprintf(str, 15, "Ecart %5d", tile_distance[tile]); ng_text(2, 4, 0, str);
    }

    return 0;
}