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

#define FALSE (1==0)
#define TRUE  (1==1)

#define ATRHUR_X_DEPART 51

// Fix point logic for slow scrolling
#define FIX_FRACTIONAL_BITS 3
#define FIX_POINT(a,f) ((a<<FIX_FRACTIONAL_BITS)+f) // Multiplication par 8
#define FIX_ACCUM(x) (x>>FIX_FRACTIONAL_BITS)+1
#define FIX_TILEX(x) (x>>4)+1 // On divise par 16 arthur X pour connaitre la TILE
#define FIX_ZERO (FIX_POINT(1,0)-1)

// Transparent tile in BIOS ROM
#define SROM_EMPTY_TILE 255

// Parameters of the first level
#define DEBUG 1
#define START_LEVEL 1
#define GNG_LEVEL1_MAP_WIDTH 224
#define PALETTE_NUMBER 6

// Collisions
#define LEVEL1_FLOTTE_TILE 402
#define LEVEL1_ECHELLE_TILE1 93
#define LEVEL1_ECHELLE_TILE2 73
#define LEVEL1_ECHELLE_TILE3 53
#define LEVEL1_ECHELLE_TILE4 33
#define LEVEL1_ECHELLE_TILE5 13

u16 frameCount = 0;
u16 frameCountY = 0;
u16 x = 0;
u16 x_max = (GNG_LEVEL1_MAP_WIDTH-20)*16-1;
u16 x_max2 = ((GNG_LEVEL1_MAP_WIDTH-20)*16-1)+130;
u16 tile = 0;

u16 level;

int arthur_index = 0;
int arthur_sens = 1;
int arthur_absolute_x = 130;
int arthur_absolute_y = 0;
int arthur_mort = 0;
int arthur_can_play = 0;
int arthur_devant_echelle = 0;
int arthur_sur_echelle = 0;
int arthur_sur_echelle_count = 0;
int arthur_sommet_echelle = 0;
int arthur_tile_x = 0;
int arthur_tile_x_for_left = 0;
int arthur_tile_x_for_right = 0;
int arthur_tile_y = 12; // Par defaut Arthur evolue sur Y=12

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
    #include "sprites/back.pal"
    #include "sprites/flottes.pal"
    #include "sprites/herbe.pal"
    #include "sprites/map.pal"
    #include "sprites/arthur.pal"
    #include "sprites/nuages.pal"
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
    u16 tileset_offset;
    u16 tmx[15][GNG_LEVEL1_MAP_WIDTH];
} layer_t;

typedef struct _arthur_t {
    u16 sprite;
    u16 width;
    u16 height;
    s16 x;
    s16 y;
    u16 tmx[10][16];
} arthur_t;

// --- Declaration des tombes
#define GNG_LEVEL1_TOMBES_COUNT 2

typedef struct _tombe_t {
    u16 sprite;
    u16 width;
    u16 height;
    s16 x;
    s16 y;
    s16 origin_x;
    u16 tmx[2][2];
    u16 hide_after_tile;
} tombe_t;

tombe_t tombes[] = {
    {
        .sprite = 35,
        .width = 2,
        .height = 2,
        .x = 40,
        .y = -179,
        .origin_x = 40, // Position x on the level
        .tmx = {
            {572, 573},
            {588, 589},
        },
        .hide_after_tile = 6,
    }, 
    {
        .sprite = 37,
        .width = 2,
        .height = 2,
        .x = 190, // 243
        .y = -179,
        .origin_x = 243-24,
        .tmx = {
            {574, 575},
            {590, 591},
        },
        .hide_after_tile = 18,
    }
};

#include "include_layers.c"

u16 tile_distance[GNG_LEVEL1_MAP_WIDTH];

u16 palettes[PALETTE_NUMBER][2] = {
    {6, 604},
    {5, 508},
    {4, 427},
    {3, 411},
    {2, 401},
    {1, 1},
};

layer_t background = {
    .sprite = 1,
    .width = 32,
    .height = 15,
    .x = 0,
    .y = 0,
    .tileset_offset = 256, 
    .tmx = {},
};

arthur_t arthur = {
    .sprite = 33,
    .width = 2,
    .height = 2,
    .x = 130,
    .y = -178,
    .tmx = {},
};

layer_t herbe = {
    .sprite = 39,
    .width = 32,
    .height = 15,
    .x = 0,
    .y = 0, // -208
    .tileset_offset = 256,
    .tmx = {},
};

layer_t map = {
    .sprite = 1,
    .width = 32,
    .height = 15,
    .x = 40,
    .y = 30,
    .tileset_offset = 256,
    .tmx = {},
};

void clear_sprites(layer_t *layer) {
    
    u8 i=0, j=0;
    u16 sprite_compteur = layer->sprite;
    
    for(i=0;i<layer->width;i++){
        *REG_VRAMMOD=1;
        *REG_VRAMADDR=ADDR_SCB1+(sprite_compteur*64);
        for(j=0;j<layer->height;j++){
            *REG_VRAMRW=(u16)SROM_EMPTY_TILE;
            *REG_VRAMRW = 1<<8;
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

void clear_tombe(tombe_t *tombe) {
    
    u8 i=0, j=0;
    u16 sprite_compteur = tombe->sprite;
    
    for(i=0;i<tombe->width;i++){
        *REG_VRAMMOD=1;
        *REG_VRAMADDR=ADDR_SCB1+(sprite_compteur*64);
        for(j=0;j<tombe->height;j++){
            *REG_VRAMRW=(u16)SROM_EMPTY_TILE;
            *REG_VRAMRW = 1<<8;
        }
        sprite_compteur++;
    }

    // Positionnement du sprite global
    *REG_VRAMMOD=0x200;
    *REG_VRAMADDR=ADDR_SCB2+tombe->sprite;
    *REG_VRAMRW=0xFFF;
    *REG_VRAMRW=(tombe->y<<7)+tombe->height;
    *REG_VRAMRW=(tombe->x<<7);

    // --- On chaine l'ensemble des sprites
    for (u16 v=1; v<tombe->width; v++) {
        *REG_VRAMADDR=ADDR_SCB2+tombe->sprite+v;
        *REG_VRAMRW=0xFFF;
        *REG_VRAMRW=1<<6; // sticky
    }
}

/*********************************
 * 
 * Function Tombes
 * 
 *********************************/
void display_tombe(tombe_t *tombe){
    
    u8 i=0, j=0, p=0;
    u16 sprite_compteur = tombe->sprite;

    for(i=0;i<tombe->width;i++){
        *REG_VRAMMOD=1;
        *REG_VRAMADDR=ADDR_SCB1+(sprite_compteur*64);
        for(j=0;j<tombe->height;j++){
            *REG_VRAMRW = 256+tombe->tmx[j][i]-1;
            // --- On va chercher la palette dans le tableau palettes
            u16 good_palette = 1;
            for(p=0;p<PALETTE_NUMBER;p++){
                if ( tombe->tmx[j][i] >= palettes[p][1] ){
                    good_palette = palettes[p][0];
                    break;
                }
            }
            *REG_VRAMRW = good_palette<<8;
        }
        sprite_compteur++;
    }

    // Positionnement du sprite global
    *REG_VRAMMOD=0x200;
    *REG_VRAMADDR=ADDR_SCB2+tombe->sprite;
    *REG_VRAMRW=0xFFF;
    *REG_VRAMRW=(tombe->y<<7)+tombe->height;
    *REG_VRAMRW=tombe->x<<7;

    // --- On chaine l'ensemble des sprites
    for (u16 v=1; v<tombe->width; v++) {
        *REG_VRAMADDR=ADDR_SCB2+tombe->sprite+v;
        *REG_VRAMRW=0xFFF;
        *REG_VRAMRW=1<<6; // sticky
    }

    
}

void update_tombe(tombe_t *tombe){
    *REG_VRAMMOD=0x200;
    *REG_VRAMADDR=ADDR_SCB2+tombe->sprite;
    *REG_VRAMRW=0xFFF;
    // --- On regarde s'il faut cacher la tombe
    if ( tile >= tombe->hide_after_tile ){
        *REG_VRAMRW=(-250<<7)+tombe->height;
        *REG_VRAMRW=tombe->x<<7;
    }
    else {
        *REG_VRAMRW=(tombe->y<<7)+tombe->height;
        *REG_VRAMRW=tombe->x<<7;
    }
}

int collision_tombe(){
    return 0;
    u8 i=0;
    for(i=0;i<GNG_LEVEL1_TOMBES_COUNT;i++){
        if ( arthur_absolute_x == tombes[i].origin_x ){
            return 1;
        }
    }
    return 0;
}

int collision_flottes(){
    u8 i=0;
    for(i=0;i<GNG_LEVEL1_TOMBES_COUNT;i++){
        if ( arthur_absolute_x == tombes[i].origin_x ){
            return 1;
        }
    }
    return 0;
}

/*********************************
 * 
 * Function to Arthur
 * 
 *********************************/
void arthur_display(){
    *REG_VRAMMOD=0x200;
    *REG_VRAMADDR=ADDR_SCB2+arthur.sprite;
    *REG_VRAMRW=0xFFF;
    *REG_VRAMRW=(arthur.y<<7)+arthur.height;
    *REG_VRAMRW=arthur.x<<7;
}

void display_arthur(){
    
    u8 i=0, j=0, p=0;
    u16 sprite_compteur = arthur.sprite;

    for(i=0;i<arthur.width;i++){
        *REG_VRAMMOD=1;
        *REG_VRAMADDR=ADDR_SCB1+(sprite_compteur*64);
        for(j=0;j<arthur.height;j++){
            *REG_VRAMRW = 256+arthur.tmx[j][i]-1;
            // --- On va chercher la palette dans le tableau palettes
            u16 good_palette = 1;
            for(p=0;p<PALETTE_NUMBER;p++){
                if ( arthur.tmx[j][i] >= palettes[p][1] ){
                    good_palette = palettes[p][0];
                    break;
                }
            }
            *REG_VRAMRW = good_palette<<8;
        }
        sprite_compteur++;
    }

    // Positionnement du sprite global
    arthur_display();

    // --- On chaine l'ensemble des sprites
    for (u16 v=1; v<arthur.width; v++) {
        *REG_VRAMADDR=ADDR_SCB2+arthur.sprite+v;
        *REG_VRAMRW=0xFFF;
        *REG_VRAMRW=1<<6; // sticky
    }
}

void arthur_on_echelle(){

    if ( arthur_sur_echelle == 1 ){        

        if ( 
            background.tmx[arthur_tile_y][arthur_tile_x] == 13 || 
            background.tmx[arthur_tile_y][arthur_tile_x] == 14 
        ){
            arthur_sommet_echelle = 1;

            *REG_VRAMMOD=1;

            *REG_VRAMADDR=ADDR_SCB1+(arthur.sprite*64);
            *REG_VRAMRW = 256+arthur.tmx[6][4]-1;
            *REG_VRAMRW = 5<<8;
            *REG_VRAMRW = 256+arthur.tmx[7][4]-1;
            *REG_VRAMRW = 5<<8;

            *REG_VRAMADDR=ADDR_SCB1+((arthur.sprite+1)*64);
            *REG_VRAMRW = 256+arthur.tmx[6][5]-1;
            *REG_VRAMRW = 5<<8;
            *REG_VRAMRW = 256+arthur.tmx[7][5]-1;
            *REG_VRAMRW = 5<<8;

            arthur_display();
        }
        else if ( 
            background.tmx[arthur_tile_y][arthur_tile_x] == 0 || 
            background.tmx[arthur_tile_y][arthur_tile_x] == 0 
        ){
            arthur_sommet_echelle = 2;
            arthur_index=0;
            arthur_display_next_tiles(arthur_index);
            //arthur_display();
        }
        else {
            arthur_sommet_echelle = 0;
            arthur_sur_echelle_count++;

            if ( arthur_sur_echelle_count == 16 ) {
                arthur_sur_echelle_count = 0;
            }
            else if ( arthur_sur_echelle_count >= 8 ) {
                
                *REG_VRAMMOD=1;

                *REG_VRAMADDR=ADDR_SCB1+(arthur.sprite*64);
                *REG_VRAMRW = 256+arthur.tmx[6][0]-1;
                *REG_VRAMRW = 5<<8;
                *REG_VRAMRW = 256+arthur.tmx[7][0]-1;
                *REG_VRAMRW = 5<<8;

                *REG_VRAMADDR=ADDR_SCB1+((arthur.sprite+1)*64);
                *REG_VRAMRW = 256+arthur.tmx[6][1]-1;
                *REG_VRAMRW = 5<<8;
                *REG_VRAMRW = 256+arthur.tmx[7][1]-1;
                *REG_VRAMRW = 5<<8;

                arthur_display();
            }
            else if ( arthur_sur_echelle_count < 8 ) {

                *REG_VRAMMOD=1;

                *REG_VRAMADDR=ADDR_SCB1+(arthur.sprite*64);
                *REG_VRAMRW = 256+arthur.tmx[6][2]-1;
                *REG_VRAMRW = 5<<8;
                *REG_VRAMRW = 256+arthur.tmx[7][2]-1;
                *REG_VRAMRW = 5<<8;

                *REG_VRAMADDR=ADDR_SCB1+((arthur.sprite+1)*64);
                *REG_VRAMRW = 256+arthur.tmx[6][3]-1;
                *REG_VRAMRW = 5<<8;
                *REG_VRAMRW = 256+arthur.tmx[7][3]-1;
                *REG_VRAMRW = 5<<8;

                arthur_display();
            }
        }
    }
}

void arthur_display_next_tiles(){
    u8 i=0, j=0, p=0;
    *REG_VRAMMOD=1;

    if ( arthur_sens == 1 ){
        // Right
        j=0;
    }
    else {
        // Left
        j=2;
    }
    
    *REG_VRAMADDR=ADDR_SCB1+(arthur.sprite*64);
    *REG_VRAMRW = 256+arthur.tmx[j][0+(2*arthur_index)]-1;
    *REG_VRAMRW = 5<<8;
    *REG_VRAMRW = 256+arthur.tmx[j+1][0+(2*arthur_index)]-1;
    *REG_VRAMRW = 5<<8;

    *REG_VRAMADDR=ADDR_SCB1+((arthur.sprite+1)*64);
    *REG_VRAMRW = 256+arthur.tmx[j][1+(2*arthur_index)]-1;
    *REG_VRAMRW = 5<<8;
    *REG_VRAMRW = 256+arthur.tmx[j+1][1+(2*arthur_index)]-1;
    *REG_VRAMRW = 5<<8;

    arthur_display();
}

u16 athur_collisions(){
    return 0;
    char str[10];
    u16 diff=0;
    if ( arthur_sens == 1 ){
        if ( background.tmx[arthur_tile_y][tile] == 121 ) {
            // On calcule le nombre de pixels qu'il reste avant la tuile de collision
            diff = ((tile)*16)+1-(x);
            if ( DEBUG == 1 ){
                //snprintf(str, 10, "T %5d", diff); ng_text(2, 9, 0, str);
            }
            if ( diff <= 12 ){
                //return background.tmx[arthur_tile_y][tile];
            }
        }
    }
    return 0;
}

void display_map_from_tmx(layer_t *layer){

    u8 i=0, j=0, k=0, l=0, p=0, s=0;
    u16 tile_tmp;

    u16 sprite_compteur = layer->sprite;
    for(i=0;i<layer->width;i++){

        // On cree un sprite pour chaque colonne
        *REG_VRAMMOD=1;
        *REG_VRAMADDR=ADDR_SCB1+(sprite_compteur*64);

        for(j=0;j<layer->height;j++){

            if ( layer->tmx[j][i] == 0 ){
                s16 temp = *REG_VRAMRW; // Get to set
                *REG_VRAMRW = SROM_EMPTY_TILE;
                temp = *REG_VRAMRW;
                *REG_VRAMRW = temp;
            }
            else {
                tile_tmp = layer->tileset_offset+layer->tmx[j][i]-1;
                *REG_VRAMRW = tile_tmp;
                // --- On va chercher la palette dans le tableau palettes
                u16 good_palette = 1;
                for(p=0;p<PALETTE_NUMBER;p++){
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
    u16 p=0;
    colonne_to_actualize += layer->sprite;
    u16 j=0;
    *REG_VRAMMOD=1;
    *REG_VRAMADDR=ADDR_SCB1+(colonne_to_actualize*64);
    for(j=0;j<layer->height;j++){
        *REG_VRAMRW = layer->tileset_offset+layer->tmx[j][tile+ecart]-1;
        // --- On va chercher la palette dans le tableau palettes
        u16 good_palette = 1; // layer->palette
        for(p=0;p<PALETTE_NUMBER;p++){
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

    u8 collision = 0;

    joystate[0]=u?'1':'0';
    joystate[1]=d?'1':'0';
    joystate[2]=l?'1':'0';
    joystate[3]=r?'1':'0';

    // ------------------------------------------------------
    // --- TOP
    // ------------------------------------------------------
    if ( u && !arthur_mort) {

        if ( DEBUG == 1 ){
            //snprintf(str, 20, "%3d %3d %3d", arthur_tile_y, tile+8, background.tmx[arthur_tile_y][tile+8]); ng_text(2, 11, 0, str);
        }

        if ( arthur_devant_echelle == 1){

            frameCountY++;
            
            if(background.tmx[arthur_tile_y][tile+8] == 13 || background.tmx[arthur_tile_y][tile+8] == 14 
            || background.tmx[arthur_tile_y][tile+8] == 33 || background.tmx[arthur_tile_y][tile+8] == 34 
            || background.tmx[arthur_tile_y][tile+8] == 53 || background.tmx[arthur_tile_y][tile+8] == 54 
            || background.tmx[arthur_tile_y][tile+8] == 73 || background.tmx[arthur_tile_y][tile+8] == 74 
            || background.tmx[arthur_tile_y][tile+8] == 93 || background.tmx[arthur_tile_y][tile+8] == 94 ){

                // --- On positionne Arthur au centre de l'echelle
                //arthur.x = (arthur_tile_x*16)+8;

                arthur_tile_y = 12-((arthur_absolute_y)>>4);
                arthur_sur_echelle=1;
                frameCountY=0;
                arthur_absolute_y++;

                // --- On anime Arthur qui monte sur l'échelle
                arthur_on_echelle();

                if ( arthur_sommet_echelle == 0 ){
                    arthur.y++;
                }
                else if ( arthur_sommet_echelle == 2 ){
                    arthur.y+=17;
                }
                
                /*if ( arthur.y%3 == 0 && 1==2 ){
                    background.y--;
                    herbe.y--;
                    update_layer(&background);
                    update_layer(&herbe);
                }*/
            }
            else {
                arthur_sur_echelle=0;
            }
        }

        arthur_display();
    }

    // ------------------------------------------------------
    // --- BOTTOM
    // ------------------------------------------------------
    if ( d && !arthur_mort) {
        if ( arthur_absolute_y > 0 ) {
            if ( arthur_devant_echelle == 1 ){

                frameCountY++;

                // On affiche Arthur montant à l'échelle
                // TODO

                if(
                    background.tmx[arthur_tile_y+1][tile+8] == 13 || background.tmx[arthur_tile_y+1][tile+8] == 14 
                || background.tmx[arthur_tile_y+1][tile+8] == 33 || background.tmx[arthur_tile_y+1][tile+8] == 34 
                || background.tmx[arthur_tile_y+1][tile+8] == 53 || background.tmx[arthur_tile_y+1][tile+8] == 54 
                || background.tmx[arthur_tile_y+1][tile+8] == 73 || background.tmx[arthur_tile_y+1][tile+8] == 74 
                || background.tmx[arthur_tile_y+1][tile+8] == 93 || background.tmx[arthur_tile_y+1][tile+8] == 94 
                || (arthur_absolute_y>0 && arthur_absolute_y < 16)
                || (arthur_absolute_y>80 && arthur_absolute_y < 96)
                ){
                    arthur_sur_echelle=1;
                    frameCountY=0;
                    arthur_absolute_y--;
                    arthur_tile_y = 12-((arthur_absolute_y)>>4);
                    

                    // --- On anime Arthur qui monte sur l'échelle
                    arthur_on_echelle();

                    if ( arthur_sommet_echelle == 0 ){
                        arthur.y--;
                    }
                    else if ( arthur_sommet_echelle == 2 ){
                        arthur.y-=17;
                    }

                    if ( arthur_absolute_y == 0 ) {
                        arthur_sur_echelle=0;
                    }

                    if ( arthur.y%3 ==0 && 1==2 ){
                        background.y++;
                        herbe.y++;
                        tombes[0].y++;
                        tombes[1].y++;
                        update_layer(&background);
                        update_layer(&herbe);
                        update_tombe(&tombes[0]);
                        update_tombe(&tombes[1]);
                    }
                }
                else {
                    arthur_sur_echelle=0;
                }
            }
            else {
                // On affiche Arthur en position 8
                *REG_VRAMMOD=1;

                *REG_VRAMADDR=ADDR_SCB1+(arthur.sprite*64);
                *REG_VRAMRW = 256+arthur.tmx[8][arthur_sens==1?0:2]-1;
                *REG_VRAMRW = 5<<8;
                *REG_VRAMRW = 256+arthur.tmx[9][arthur_sens==1?0:2]-1;
                *REG_VRAMRW = 5<<8;

                *REG_VRAMADDR=ADDR_SCB1+((arthur.sprite+1)*64);
                *REG_VRAMRW = 256+arthur.tmx[8][arthur_sens==1?1:3]-1;
                *REG_VRAMRW = 5<<8;
                *REG_VRAMRW = 256+arthur.tmx[9][arthur_sens==1?1:3]-1;
                *REG_VRAMRW = 5<<8;
            }
        }
        else {
            // On affiche Arthur en position 8
            *REG_VRAMMOD=1;

            *REG_VRAMADDR=ADDR_SCB1+(arthur.sprite*64);
            *REG_VRAMRW = 256+arthur.tmx[8][arthur_sens==1?0:2]-1;
            *REG_VRAMRW = 5<<8;
            *REG_VRAMRW = 256+arthur.tmx[9][arthur_sens==1?0:2]-1;
            *REG_VRAMRW = 5<<8;

            *REG_VRAMADDR=ADDR_SCB1+((arthur.sprite+1)*64);
            *REG_VRAMRW = 256+arthur.tmx[8][arthur_sens==1?1:3]-1;
            *REG_VRAMRW = 5<<8;
            *REG_VRAMRW = 256+arthur.tmx[9][arthur_sens==1?1:3]-1;
            *REG_VRAMRW = 5<<8;
        }

        arthur_display();
    }

    // ------------------------------------------------------
    // --- LEFT
    // ------------------------------------------------------
    if ( l && !d && !arthur_mort && !arthur_sur_echelle ) {

        prendre_en_compte=1;
        arthur_sens = 0;
        arthur_devant_echelle = 0;

        if ( x > 0 && arthur_absolute_x < x_max2 ){

            x--;
            tile = FIX_TILEX(x);
            arthur_absolute_x--;
            background.x++;
            herbe.x++;
            //tombes[0].x++;
            //tombes[1].x++;

            // --- Update tiles for scrolling
            //if ( (tile-1)<<4 == x ){
                //snprintf(str, 10, "T %5d", tile); ng_text(2, 7, 0, str);
                if ( tile>=12 && x%16 == 0){
                    change_tiles(&background, tile-tile_distance[tile], -12);
                    change_tiles(&herbe, tile-tile_distance[tile], -12);
                }
            //}

            if ( 
                background.tmx[arthur_tile_y][arthur_tile_x_for_left] == LEVEL1_ECHELLE_TILE1 // Echelle pour monter
                || background.tmx[arthur_tile_y][arthur_tile_x_for_left] == LEVEL1_ECHELLE_TILE2 
                || background.tmx[arthur_tile_y][arthur_tile_x_for_left] == LEVEL1_ECHELLE_TILE3 
                || background.tmx[arthur_tile_y][arthur_tile_x_for_left] == LEVEL1_ECHELLE_TILE4 
                || background.tmx[arthur_tile_y][arthur_tile_x_for_left] == LEVEL1_ECHELLE_TILE5 
                || background.tmx[arthur_tile_y+1][arthur_tile_x_for_left] == LEVEL1_ECHELLE_TILE1 // Echelle sous les pieds pour descendre
                || background.tmx[arthur_tile_y+1][arthur_tile_x_for_left] == LEVEL1_ECHELLE_TILE2 
                || background.tmx[arthur_tile_y+1][arthur_tile_x_for_left] == LEVEL1_ECHELLE_TILE3 
                || background.tmx[arthur_tile_y+1][arthur_tile_x_for_left] == LEVEL1_ECHELLE_TILE4 
                || background.tmx[arthur_tile_y+1][arthur_tile_x_for_left] == LEVEL1_ECHELLE_TILE5 
            ){
                arthur_devant_echelle = 1;
            }

            //snprintf(str, 10, "ECH %5d", arthur_devant_echelle); ng_text(2, 10, 0, str);

            update_layer(&background);
            update_layer(&herbe);
            //update_tombe(&tombes[0]);
            //update_tombe(&tombes[1]);
        }
        else if ( arthur_absolute_x > 0 ){
            arthur_absolute_x--;
            arthur.x--;
            arthur_display();
        }

        arthur_tile_x = FIX_TILEX(arthur_absolute_x);
        arthur_tile_x_for_left = FIX_TILEX(arthur_absolute_x-5);
    }

    // ------------------------------------------------------
    // --- RIGHT
    // ------------------------------------------------------
    if ( r && !d && !arthur_mort && !arthur_sur_echelle ) {

        prendre_en_compte=1;
        arthur_sens = 1;
        arthur_devant_echelle = 0;

        if ( arthur_absolute_x < 130 ) {
            if ( arthur_absolute_x > 130 ){
                x++;
            }
            arthur.x++;
            arthur_absolute_x++;
        }
        else if ( x < x_max ){

            // --- Gestion des collisions
            // collision = athur_collisions();
            collision = collision_tombe();

            if ( background.tmx[14][tile+8] == LEVEL1_FLOTTE_TILE ){
                // --- On tombe dans l'eau et on recommence au début du niveau
                arthur.y-=1;
                arthur_mort=1;
            }
            
            if ( 
                background.tmx[arthur_tile_y][arthur_tile_x_for_right] == LEVEL1_ECHELLE_TILE1 // Echelle pour monter
                || background.tmx[arthur_tile_y][arthur_tile_x_for_right] == LEVEL1_ECHELLE_TILE2 
                || background.tmx[arthur_tile_y][arthur_tile_x_for_right] == LEVEL1_ECHELLE_TILE3 
                || background.tmx[arthur_tile_y][arthur_tile_x_for_right] == LEVEL1_ECHELLE_TILE4 
                || background.tmx[arthur_tile_y][arthur_tile_x_for_right] == LEVEL1_ECHELLE_TILE5 
                || background.tmx[arthur_tile_y+1][arthur_tile_x_for_right] == LEVEL1_ECHELLE_TILE1 // Echelle sous les pieds pour descendre
                || background.tmx[arthur_tile_y+1][arthur_tile_x_for_right] == LEVEL1_ECHELLE_TILE2 
                || background.tmx[arthur_tile_y+1][arthur_tile_x_for_right] == LEVEL1_ECHELLE_TILE3 
                || background.tmx[arthur_tile_y+1][arthur_tile_x_for_right] == LEVEL1_ECHELLE_TILE4 
                || background.tmx[arthur_tile_y+1][arthur_tile_x_for_right] == LEVEL1_ECHELLE_TILE5 
            ){
                // --- On tombe dans l'eau et on recommence au début du niveau
                arthur_devant_echelle = 1;
            }

            if ( DEBUG == 1 ){
                //snprintf(str, 10, "C %5d", collision); ng_text(2, 9, 0, str);
            }

            if ( collision == 0 && arthur_mort==0) {

                x++;
                tile = FIX_TILEX(x);
                arthur_absolute_x++;
                background.x--;
                herbe.x--;
                //tombes[0].x--;
                //tombes[1].x--;

//                if ( x%16 == 0 ){
                    // --- Update tiles for scrolling
                    if ( tile>=12){
                        change_tiles(&background, tile-tile_distance[tile], 20);
                        change_tiles(&herbe, tile-tile_distance[tile], 20);
                    }
//                }

                update_layer(&background);
                update_layer(&herbe);
                //update_tombe(&tombes[0]);
                //update_tombe(&tombes[1]);
            }
        }
        else if ( arthur_absolute_x <= 3555 ){
            arthur_absolute_x++;
            arthur.x++;
            arthur_display();
        }

        arthur_tile_x = FIX_TILEX(arthur_absolute_x);
        arthur_tile_x_for_right = FIX_TILEX(arthur_absolute_x-8);
    }

    if ( u == 0 && d==0 && l==0 && r==0 ){
        if ( arthur_sur_echelle != 1 ){
            arthur_index=0;
            arthur_display_next_tiles(arthur_index);
        }
    }

    if ( prendre_en_compte == 1 ){

        frameCount++;
        if (frameCount>3){
            arthur_index++;
            frameCount=0;
        }
        const u8 *tiles = arthur_sens?right_tiles:left_tiles;
        if ( arthur_index == 7 ){
            arthur_index=0;
        }

        // --- On fait courir Arthur
        arthur_display_next_tiles(arthur_index);
        //arthur_display_next_tiles();
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
    level = START_LEVEL;

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
            background.tmx[j][loop] = tmx_background[j][loop];
            herbe.tmx[j][loop] = tmx_herbe[j][loop];
        }
    }

    // Init tmx map
    for(j = 0; j < 15; j++) {
        for(loop = 0; loop < map.width; loop++) {
            map.tmx[j][loop] = tmx_map[j][loop];
        }
    }

    // Init tmx arthur
    for(j = 0; j < 10; j++) {
        for(loop = 0; loop < 16; loop++) {
            arthur.tmx[j][loop] = tmx_arthur[j][loop];
        }
    }

    arthur_tile_x = FIX_TILEX(arthur_absolute_x);

    for(;;) {
        ng_wait_vblank();

        if ( arthur_can_play == 0 ){

            if ( level == 0 ){
                display_map_from_tmx(&map);
                // Pause de 5s
                for(i=0;i<1000;i++){
                    for(j=0;j<250;j++){}
                }
                for(i=0;i<110;i++){
                    map.x-=1;
                    update_layer(&map);
                    ng_wait_vblank();
                }
                // Pause de 5s
                for(i=0;i<1000;i++){
                    for(j=0;j<320;j++){}
                }
                level=1;
            }

            if ( level == 1 ){
                display_map_from_tmx(&background);
                display_map_from_tmx(&herbe);
                display_arthur();
                for(i=0;i<GNG_LEVEL1_TOMBES_COUNT;i++){
                    //display_tombe(&tombes[i]);
                }
            }

            arthur_can_play=1;
        }

        if ( arthur_can_play == 1 ){

            if ( arthur_mort == 0 ) {
                check_move_arthur();
            }
            else if ( arthur_mort == 1){
                for(i=0;i<6000;i++){
                    if(i%90==0){
                        arthur.y-=1;
                        arthur_display();
                    }
                }

                // Pause de 5s
                for(i=0;i<1000;i++){
                    for(j=0;j<320;j++){}
                }

                // Init all
                level = START_LEVEL;
                x = 0;
                arthur_mort = 0;
                arthur.y = -178;
                arthur_can_play = 0;
                arthur_absolute_x = 130;
                arthur_tile_y = 12;
                background.x = 0;
                herbe.x = 0;
                map.x = 40;
                tombes[0].x = 40;
                tombes[1].x = 243;
                tile = 0;

                // Erase sprites
                clear_sprites(&background);
                clear_sprites(&herbe);
                clear_sprites(&map);
                clear_tombe(&tombes[0]);
                clear_tombe(&tombes[1]);

                // Pause de 5s
                for(i=0;i<700;i++){
                    for(j=0;j<320;j++){}
                }
            }
        }

        if ( DEBUG == 1 ){
            snprintf(str, 10, "XXX %3d", x); ng_text(2, 3, 0, str);
            snprintf(str, 10, "TTT %5d", arthur_absolute_x); ng_text(2, 5, 0, str);
            snprintf(str, 10, "T %5d", arthur_tile_x); ng_text(2, 7, 0, str);
            snprintf(str, 10, "T %5d", background.tmx[arthur_tile_y][arthur_tile_x]); ng_text(2, 9, 0, str);
            snprintf(str, 10, "ADE %5d", arthur_sommet_echelle); ng_text(2, 11, 0, str);
            //snprintf(str, 10, "ASE %5d", arthur_sur_echelle_count); ng_text(2, 13, 0, str);
            //snprintf(str, 10, "Til %5d", background.tmx[arthur_tile_y+1][tile+8]); ng_text(2, 9, 0, str);
        }
    }

    return 0;
}