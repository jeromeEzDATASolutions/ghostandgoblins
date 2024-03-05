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
#define DEBUG 0
#define START_LEVEL 1
#define DEBUG_DISPLAY_GHOSTS 1
#define GNG_LEVEL1_MAP_WIDTH 224
#define PALETTE_NUMBER 8

// Collisions
#define LEVEL1_FLOTTE_TILE 402
#define LEVEL1_ECHELLE_TILE1 93
#define LEVEL1_ECHELLE_TILE2 73
#define LEVEL1_ECHELLE_TILE3 53
#define LEVEL1_ECHELLE_TILE4 33
#define LEVEL1_ECHELLE_TILE5 13

// Action for Arthur
#define ARTHUR_DEBOUT 0
#define ARTHUR_TOMBE 100
#define ARTHUR_SAUT_VERTICAL 101
#define ARTHUR_SAUT_HORIZONTAL_GAUCHE 102
#define ARTHUR_SAUT_HORIZONTAL_DROITE 103

u16 frameCount = 0;
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
int arthur_avec_une_echelle_sous_les_pieds = 0;
int arthur_sur_echelle_count = 0;
int arthur_sommet_echelle = 0;
int arthur_tile_x = 0;
int arthur_tile_x_for_left = 0;
int arthur_tile_x_for_right = 0;
int arthur_tile_y = 12; // Par defaut Arthur evolue sur Y=12
int arthur_cpt = 0;

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
    #include "sprites/nuages.pal"
    #include "sprites/ghost.pal"
    #include "sprites/arthur1.pal"
    #include "sprites/arthur2.pal"
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
    u16 arthur_devant_echelle;
    u16 arthur_sur_echelle;
    u16 floor;
    u16 action;
    s16 x;
    s16 y;
    u16 tmx[10][16];
    s16 cpt1;
    s16 cpt2;
    s8 version; // Correspond à la ligne à partir de laquelle il faut afficher Arthur : 0 pour l'armure, 4 sans l'armure
    s8 palette;
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
    {8, 718},
    {7, 598},
    {6, 550},
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
    .arthur_devant_echelle = 0, 
    .arthur_sur_echelle = 0, 
    .floor = 0, 
    .action = 0,
    .x = 130,
    .y = -178,
    .tmx = {},
    .cpt1 = 0, 
    .cpt2 = 0, 
    .version = 0, 
    .palette = 7, 
};

// ----------------------------------
// Gestion des fantomes
// ----------------------------------
#define GHOST_NOMBRE 6
#define GHOST_HIDDEN 1
#define GHOST_APPARAIT 2
#define GHOST_MARCHE 3
#define GHOST_MORT 4
#define GHOST_DISPARAIT 5
#define GHOST_GAUCHE 2
#define GHOST_DROITE 0

typedef struct _ghost_t {
    u16 sprite;
    u16 width;
    u16 height;
    u16 floor;
    u8 state;
    s16 x;
    s16 y;
    u8 offset;
    s16 cpt;
    u8 sens;
    u16 modulo;
    u16 apparition_x1;
    u16 apparition_x2;
} ghost_t;

ghost_t ghosts[] = {
    {
        .sprite = 39,
        .width = 2,
        .height = 2,
        .floor = 0, 
        .state = GHOST_HIDDEN, 
        .x = 280,
        .y = -178,
        .offset = 0, 
        .cpt = 0,
        .sens = GHOST_GAUCHE, 
        .modulo = 55, 
        .apparition_x1 = 240, 
        .apparition_x2 = 40, 
    }, 
    {
        .sprite = 41,
        .width = 2,
        .height = 2,
        .floor = 0, 
        .state = GHOST_HIDDEN, 
        .x = 280,
        .y = -178,
        .offset = 0, 
        .cpt = 0,
        .sens = GHOST_DROITE, 
        .modulo = 139, 
        .apparition_x1 = 400, 
        .apparition_x2 = 40, 
    }, 
    {
        .sprite = 43,
        .width = 2,
        .height = 2,
        .floor = 0, 
        .state = GHOST_HIDDEN, 
        .x = 280,
        .y = -178,
        .offset = 0, 
        .cpt = 0,
        .sens = GHOST_DROITE, 
        .modulo = 600, 
        .apparition_x1 = 400, 
        .apparition_x2 = 40, 
    }, 
    {
        .sprite = 45,
        .width = 2,
        .height = 2,
        .floor = 0, 
        .state = GHOST_HIDDEN, 
        .x = 280,
        .y = -178,
        .offset = 0, 
        .cpt = 0,
        .sens = GHOST_DROITE, 
        .modulo = 800, 
        .apparition_x1 = 400, 
        .apparition_x2 = 40, 
    }, 
    {
        .sprite = 47,
        .width = 2,
        .height = 2,
        .floor = 0, 
        .state = GHOST_HIDDEN, 
        .x = 280,
        .y = -178,
        .offset = 0, 
        .cpt = 0,
        .sens = GHOST_DROITE, 
        .modulo = 1000, 
        .apparition_x1 = 400, 
        .apparition_x2 = 270, 
    }, 
    {
        .sprite = 49,
        .width = 2,
        .height = 2,
        .floor = 0, 
        .state = GHOST_HIDDEN, 
        .x = 280,
        .y = -178,
        .offset = 0, 
        .cpt = 0,
        .sens = GHOST_DROITE, 
        .modulo = 60, 
        .apparition_x1 = 400, 
        .apparition_x2 = 100, 
    }, 
};

layer_t herbe = {
    .sprite = 51,
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
    *REG_VRAMRW=0xFFF; // scaling
    *REG_VRAMRW=(arthur.y<<7)+arthur.height;
    *REG_VRAMRW=arthur.x<<7;
}

void display_arthur(){
    
    u8 i=0, j=0, p=0;
    u16 sprite_compteur = arthur.sprite;

    for(i=0;i<arthur.width;i++){
        *REG_VRAMMOD=1;
        *REG_VRAMADDR=ADDR_SCB1+(sprite_compteur*64);
        for(j=arthur.version;j<arthur.height+arthur.version;j++){
            *REG_VRAMRW = 256+tmx_arthur[j][i]-1;
            *REG_VRAMRW = arthur.palette<<8;
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

void arthur_display_next_tiles(){
    u8 i=0, j=0, p=0;
    *REG_VRAMMOD=1;

    if ( arthur_sens == 1 ){
        // Right
        j=0+arthur.version;
    }
    else {
        // Left
        j=2+arthur.version;
    }
    
    *REG_VRAMADDR=ADDR_SCB1+(arthur.sprite*64);
    *REG_VRAMRW = 256+tmx_arthur[j][0+(2*arthur_index)]-1;
    *REG_VRAMRW = arthur.palette<<8;
    *REG_VRAMRW = 256+tmx_arthur[j+1][0+(2*arthur_index)]-1;
    *REG_VRAMRW = arthur.palette<<8;

    *REG_VRAMADDR=ADDR_SCB1+((arthur.sprite+1)*64);
    *REG_VRAMRW = 256+tmx_arthur[j][1+(2*arthur_index)]-1;
    *REG_VRAMRW = arthur.palette<<8;
    *REG_VRAMRW = 256+tmx_arthur[j+1][1+(2*arthur_index)]-1;
    *REG_VRAMRW = arthur.palette<<8;

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

void arthur_on_echelle(){

    char str[10];

    if ( arthur.arthur_sur_echelle == 1 ){

        if ( background.tmx[arthur_tile_y][arthur_tile_x] == 13 || background.tmx[arthur_tile_y][arthur_tile_x] == 14 ){
            // -----------------------------------------------
            // --- On est au sommet de l'echelle
            // -----------------------------------------------
            arthur_sommet_echelle = 1;

            *REG_VRAMMOD=1;

            *REG_VRAMADDR=ADDR_SCB1+(arthur.sprite*64);
            *REG_VRAMRW = 256+tmx_arthur[6][4]-1;
            *REG_VRAMRW = arthur.palette<<8;
            *REG_VRAMRW = 256+tmx_arthur[7][4]-1;
            *REG_VRAMRW = arthur.palette<<8;

            *REG_VRAMADDR=ADDR_SCB1+((arthur.sprite+1)*64);
            *REG_VRAMRW = 256+tmx_arthur[6][5]-1;
            *REG_VRAMRW = arthur.palette<<8;
            *REG_VRAMRW = 256+tmx_arthur[7][5]-1;
            *REG_VRAMRW = arthur.palette<<8;
        }
        else if ( 
            background.tmx[arthur_tile_y][arthur_tile_x] == 33 || background.tmx[arthur_tile_y][arthur_tile_x] == 34 
            || background.tmx[arthur_tile_y][arthur_tile_x] == 53 || background.tmx[arthur_tile_y][arthur_tile_x] == 54 
            || background.tmx[arthur_tile_y][arthur_tile_x] == 73 || background.tmx[arthur_tile_y][arthur_tile_x] == 74 
            || background.tmx[arthur_tile_y][arthur_tile_x] == 93 || background.tmx[arthur_tile_y][arthur_tile_x] == 94 
        ){
            // -----------------------------------------------
            // --- On est sur l'echelle mais pas encore au sommet
            // -----------------------------------------------
            arthur_sommet_echelle = 0;
            arthur_sur_echelle_count++;

            if ( arthur_sur_echelle_count == 16 ) {
                arthur_sur_echelle_count = 0;
            }
            else if ( arthur_sur_echelle_count >= 8 ) {
                
                *REG_VRAMMOD=1;

                *REG_VRAMADDR=ADDR_SCB1+(arthur.sprite*64);
                *REG_VRAMRW = 256+tmx_arthur[arthur.version][22]-1;
                *REG_VRAMRW = arthur.palette<<8;
                *REG_VRAMRW = 256+tmx_arthur[arthur.version+1][23]-1;
                *REG_VRAMRW = arthur.palette<<8;

                *REG_VRAMADDR=ADDR_SCB1+((arthur.sprite+1)*64);
                *REG_VRAMRW = 256+tmx_arthur[arthur.version][22]-1;
                *REG_VRAMRW = arthur.palette<<8;
                *REG_VRAMRW = 256+tmx_arthur[arthur.version+1][23]-1;
                *REG_VRAMRW = arthur.palette<<8;
            }
            else if ( arthur_sur_echelle_count < 8 ) {

                *REG_VRAMMOD=1;

                *REG_VRAMADDR=ADDR_SCB1+(arthur.sprite*64);
                *REG_VRAMRW = 256+tmx_arthur[arthur.version+2][22]-1;
                *REG_VRAMRW = arthur.palette<<8;
                *REG_VRAMRW = 256+tmx_arthur[arthur.version+3][23]-1;
                *REG_VRAMRW = arthur.palette<<8;

                *REG_VRAMADDR=ADDR_SCB1+((arthur.sprite+1)*64);
                *REG_VRAMRW = 256+tmx_arthur[arthur.version+2][23]-1;
                *REG_VRAMRW = arthur.palette<<8;
                *REG_VRAMRW = 256+tmx_arthur[arthur.version+3][23]-1;
                *REG_VRAMRW = arthur.palette<<8;
            }
        }
        else if ( background.tmx[arthur_tile_y-1][arthur_tile_x] != 13 && background.tmx[arthur_tile_y-1][arthur_tile_x] != 14 ){
            arthur_sommet_echelle = 2;
            arthur_index=0;
            arthur_display_next_tiles(arthur_index);
            arthur.arthur_sur_echelle=0;
            arthur.arthur_devant_echelle=0;
        }
    }

    /*
    else if ( 
        background.tmx[arthur_tile_y][arthur_tile_x] != 13 && background.tmx[arthur_tile_y][arthur_tile_x] != 14 
        && background.tmx[arthur_tile_y][arthur_tile_x] != 33 && background.tmx[arthur_tile_y][arthur_tile_x] != 34 
        && background.tmx[arthur_tile_y][arthur_tile_x] != 53 && background.tmx[arthur_tile_y][arthur_tile_x] != 54 
        && background.tmx[arthur_tile_y][arthur_tile_x] != 73 && background.tmx[arthur_tile_y][arthur_tile_x] != 74 
        && background.tmx[arthur_tile_y][arthur_tile_x] != 93 && background.tmx[arthur_tile_y][arthur_tile_x] != 94
    ){
        arthur_sommet_echelle = 2;
        arthur_index=0;
        arthur_display_next_tiles(arthur_index);
    }
    else {
        arthur_sommet_echelle = 0;
        
    }
    */
}





/*********************************
 * 
 * Function to Arthur
 * 
 *********************************/
u8 ghost_display(ghost_t *g){

    if ( !DEBUG_DISPLAY_GHOSTS ){
        return(0);
    }

    if ( g->state == GHOST_HIDDEN ){
        // --- On cache les ghosts ou mortvivants quand ils ne doivent pas etre montrés à l'écran
        *REG_VRAMMOD=0x200;
        *REG_VRAMADDR=ADDR_SCB2+g->sprite;
        *REG_VRAMRW=0xFFF;
        *REG_VRAMRW=(-300<<7)+g->height;
        *REG_VRAMRW=(200<<7);
    }
    else {
        *REG_VRAMMOD=0x200;
        *REG_VRAMADDR=ADDR_SCB2+g->sprite;
        *REG_VRAMRW=0xFFF;
        *REG_VRAMRW=(g->y<<7)+g->height;
        *REG_VRAMRW=(g->x<<7);
    }
}

u8 display_ghost(ghost_t *g){

    static const u8 ghost_right_offset[]={0,1,2,3,4,5,6};
    static const u8 ghost_left_offset[]={4,3,2,1,0,0,0};

    if ( !DEBUG_DISPLAY_GHOSTS ){
        return(0);
    }

    if ( g->state == GHOST_APPARAIT ){

        // --- On fait monter le compteur
        g->cpt++;

        if ( g->cpt % 15 == 0 ){
            g->offset++;
            if ( g->offset == 6 ){
                g->state = GHOST_MARCHE;
                g->cpt = 0;
            }
        }
    
        // --- On fait apparaitre le ghost
        u16 sprite_compteur = g->sprite;
        *REG_VRAMMOD=1;

        *REG_VRAMADDR=ADDR_SCB1+(g->sprite*64);
        *REG_VRAMRW = 256+tmx_arthur[11][0+(g->width*ghost_right_offset[g->offset])]-1;
        *REG_VRAMRW = 6<<8;
        *REG_VRAMRW = 256+tmx_arthur[12][0+(g->width*ghost_right_offset[g->offset])]-1;
        *REG_VRAMRW = 6<<8;

        *REG_VRAMADDR=ADDR_SCB1+((g->sprite+1)*64);
        *REG_VRAMRW = 256+tmx_arthur[11][1+(g->width*ghost_right_offset[g->offset])]-1;
        *REG_VRAMRW = 6<<8;
        *REG_VRAMRW = 256+tmx_arthur[12][1+(g->width*ghost_right_offset[g->offset])]-1;
        *REG_VRAMRW = 6<<8;

        ghost_display(g);

        // --- On chaine l'ensemble des sprites
        for (u16 v=1; v<g->width; v++) {
            *REG_VRAMADDR=ADDR_SCB2+g->sprite+v;
            *REG_VRAMRW=0xFFF;
            *REG_VRAMRW=1<<6; // sticky
        }

        if ( g->offset == 6 ){
            g->offset = 5;
        }
    }
    else if ( g->state == GHOST_MARCHE ){

        // --- On determine le sens
        // g->sens = GHOST_DROITE;
        if ( g->x >= arthur.x+10 && arthur_absolute_y == 0 ){
            g->sens = GHOST_GAUCHE;
        }
        else if ( arthur.x >= g->x+10 && arthur_absolute_y == 0 ){
            g->sens = GHOST_DROITE;
        }

        if ( g->sens == GHOST_GAUCHE && g->x <= 0 ){
            g->sens = GHOST_DROITE;
        }
        else if ( g->sens == GHOST_DROITE && g->x >= 300 ){
            g->sens = GHOST_GAUCHE;
        }

        // --- On fait monter le compteur
        g->cpt++;

        if ( g->cpt % 2 == 0 ){
            if ( g->sens == GHOST_DROITE ){
                g->x++;
            } else {
                g->x--;
            }
        }

        if ( g->cpt % 8 == 0 ){

            u16 sprite_compteur = g->sprite;
            *REG_VRAMMOD=1;

            *REG_VRAMADDR=ADDR_SCB1+(g->sprite*64);
            *REG_VRAMRW = 256+tmx_arthur[11+g->sens][0+(g->width*g->offset)]-1;
            *REG_VRAMRW = 6<<8;
            *REG_VRAMRW = 256+tmx_arthur[12+g->sens][0+(g->width*g->offset)]-1;
            *REG_VRAMRW = 6<<8;

            *REG_VRAMADDR=ADDR_SCB1+((g->sprite+1)*64);
            *REG_VRAMRW = 256+tmx_arthur[11+g->sens][1+(g->width*g->offset)]-1;
            *REG_VRAMRW = 6<<8;
            *REG_VRAMRW = 256+tmx_arthur[12+g->sens][1+(g->width*g->offset)]-1;
            *REG_VRAMRW = 6<<8;

            if ( g->offset == 5 ){
                g->offset = 6;
            }
            else {
                g->offset = 5;
            }
        }

        if ( g->cpt == 500 ){
            g->state = GHOST_DISPARAIT;
            g->offset = 0;
            g->cpt = 0;
        }

        ghost_display(g);
    }
    else if ( g->state == GHOST_DISPARAIT ){
    
        // --- On fait monter le compteur
        g->cpt++;

        if ( g->cpt % 15 == 0 ){
            g->offset++;
            if ( g->offset == 6 ){
                g->state = GHOST_HIDDEN;
                g->cpt = 0;
            }
        }
    
        // --- On fait apparaitre le ghost
        u16 sprite_compteur = g->sprite;
        *REG_VRAMMOD=1;

        *REG_VRAMADDR=ADDR_SCB1+(g->sprite*64);
        *REG_VRAMRW = 256+tmx_arthur[11][0+(g->width*ghost_left_offset[g->offset])]-1;
        *REG_VRAMRW = 6<<8;
        *REG_VRAMRW = 256+tmx_arthur[12][0+(g->width*ghost_left_offset[g->offset])]-1;
        *REG_VRAMRW = 6<<8;

        *REG_VRAMADDR=ADDR_SCB1+((g->sprite+1)*64);
        *REG_VRAMRW = 256+tmx_arthur[11][1+(g->width*ghost_left_offset[g->offset])]-1;
        *REG_VRAMRW = 6<<8;
        *REG_VRAMRW = 256+tmx_arthur[12][1+(g->width*ghost_left_offset[g->offset])]-1;
        *REG_VRAMRW = 6<<8;

        ghost_display(g);

        // --- On chaine l'ensemble des sprites
        for (u16 v=1; v<g->width; v++) {
            *REG_VRAMADDR=ADDR_SCB2+g->sprite+v;
            *REG_VRAMRW=0xFFF;
            *REG_VRAMRW=1<<6; // sticky
        }

        if ( g->offset == 6 ){
            g->offset = 0;
        }
    }
}

u8 ghost_move_left(ghost_t *ghost1) {

    if ( !DEBUG_DISPLAY_GHOSTS ){
        return(0);
    }

    ghost1->x--;
    ghost_display(ghost1);
}

u8 ghost_move_right(ghost_t *ghost1) {

    if ( !DEBUG_DISPLAY_GHOSTS ){
        return(0);
    }

    ghost1->x++;
    ghost_display(ghost1);
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
    static const s8 saut_vertical[]={5,5,4,4,3,3,2,2,1,1,-1,-1,-2,-2,-3,-3,-4,-4,-5,-5};

    char str[10];
    u16 prendre_en_compte = 0;
    u8 i=0, j=0, s=0, t=0;

    u8 u=(bios_p1current & CNT_UP);
    u8 d=(bios_p1current & CNT_DOWN);
    u8 l=(bios_p1current & CNT_LEFT);
    u8 r=(bios_p1current & CNT_RIGHT);
    u8 b1=(bios_p1current & 16);
    u8 b2=(bios_p1current & 32);

    u8 collision = 0;

    joystate[0]=u?'1':'0';
    joystate[1]=d?'1':'0';
    joystate[2]=l?'1':'0';
    joystate[3]=r?'1':'0';

    // --- Button 1
    if ( b1 ) {
        if ( arthur.version == 0 ){
            arthur.version=4;
            arthur.palette=8;
        }
        else if ( arthur.version == 4 ){
            arthur.version=0;
            arthur.palette=7;
        }
    }

    if ( arthur.action == ARTHUR_SAUT_VERTICAL ){
        arthur.cpt1++;
        if((arthur.cpt1%2) == 0){
            *REG_VRAMMOD=1;
            *REG_VRAMADDR=ADDR_SCB1+(arthur.sprite*64);
            if ( arthur_sens == 1 ){
                *REG_VRAMRW = 256+tmx_arthur[arthur.version][18]-1;
                *REG_VRAMRW = arthur.palette<<8;
                *REG_VRAMRW = 256+tmx_arthur[arthur.version+1][18]-1;
                *REG_VRAMRW = arthur.palette<<8;
                *REG_VRAMADDR=ADDR_SCB1+((arthur.sprite+1)*64);
                *REG_VRAMRW = 256+tmx_arthur[arthur.version][19]-1;
                *REG_VRAMRW = arthur.palette<<8;
                *REG_VRAMRW = 256+tmx_arthur[arthur.version+1][19]-1;
                *REG_VRAMRW = arthur.palette<<8;
            }
            else {
                *REG_VRAMRW = 256+tmx_arthur[arthur.version+2][18]-1;
                *REG_VRAMRW = arthur.palette<<8;
                *REG_VRAMRW = 256+tmx_arthur[arthur.version+3][18]-1;
                *REG_VRAMRW = arthur.palette<<8;
                *REG_VRAMADDR=ADDR_SCB1+((arthur.sprite+1)*64);
                *REG_VRAMRW = 256+tmx_arthur[arthur.version+2][19]-1;
                *REG_VRAMRW = arthur.palette<<8;
                *REG_VRAMRW = 256+tmx_arthur[arthur.version+3][19]-1;
                *REG_VRAMRW = arthur.palette<<8;
            }

            arthur.y+=saut_vertical[arthur.cpt2];

            arthur_display();

            if ( arthur.cpt2 == 19 ){
                arthur.action=0;
                arthur.cpt2=0;
            }
            else {
                arthur.cpt2++;
            }

        }
    }
    else {

        // ------------------------------------------------------
        // --- TOP
        // ------------------------------------------------------
        if ( u && !arthur_mort && arthur.action != ARTHUR_TOMBE && arthur.action != ARTHUR_SAUT_HORIZONTAL_GAUCHE && arthur.action != ARTHUR_SAUT_HORIZONTAL_DROITE ) {

            if ( arthur.arthur_devant_echelle == 1 || arthur.arthur_sur_echelle == 1 ){

                if ( 
                    background.tmx[arthur_tile_y][arthur_tile_x] == 93 || background.tmx[arthur_tile_y][arthur_tile_x] == 94 || 
                    background.tmx[arthur_tile_y][arthur_tile_x] == 73 || background.tmx[arthur_tile_y][arthur_tile_x] == 74 || 
                    background.tmx[arthur_tile_y][arthur_tile_x] == 53 || background.tmx[arthur_tile_y][arthur_tile_x] == 54 || 
                    background.tmx[arthur_tile_y][arthur_tile_x] == 33 || background.tmx[arthur_tile_y][arthur_tile_x] == 34 || 
                    background.tmx[arthur_tile_y][arthur_tile_x] == 13 || background.tmx[arthur_tile_y][arthur_tile_x] == 14
                ){
                    arthur_sur_echelle_count++;
                    
                    if ( arthur_sur_echelle_count == 16 ) {
                        arthur_sur_echelle_count = 0;
                    }
                    else if ( arthur_sur_echelle_count >= 8 ) {

                        *REG_VRAMMOD=1;

                        *REG_VRAMADDR=ADDR_SCB1+(arthur.sprite*64);
                        *REG_VRAMRW = 256+tmx_arthur[arthur.version][22]-1;
                        *REG_VRAMRW = arthur.palette<<8;
                        *REG_VRAMRW = 256+tmx_arthur[arthur.version+1][22]-1;
                        *REG_VRAMRW = arthur.palette<<8;

                        *REG_VRAMADDR=ADDR_SCB1+((arthur.sprite+1)*64);
                        *REG_VRAMRW = 256+tmx_arthur[arthur.version][23]-1;
                        *REG_VRAMRW = arthur.palette<<8;
                        *REG_VRAMRW = 256+tmx_arthur[arthur.version+1][23]-1;
                        *REG_VRAMRW = arthur.palette<<8;
                    }
                    else if ( arthur_sur_echelle_count < 8 ) {

                        *REG_VRAMMOD=1;

                        *REG_VRAMADDR=ADDR_SCB1+(arthur.sprite*64);
                        *REG_VRAMRW = 256+tmx_arthur[arthur.version+2][22]-1;
                        *REG_VRAMRW = arthur.palette<<8;
                        *REG_VRAMRW = 256+tmx_arthur[arthur.version+3][22]-1;
                        *REG_VRAMRW = arthur.palette<<8;

                        *REG_VRAMADDR=ADDR_SCB1+((arthur.sprite+1)*64);
                        *REG_VRAMRW = 256+tmx_arthur[arthur.version+2][23]-1;
                        *REG_VRAMRW = arthur.palette<<8;
                        *REG_VRAMRW = 256+tmx_arthur[arthur.version+3][23]-1;
                        *REG_VRAMRW = arthur.palette<<8;
                    }

                    arthur.arthur_sur_echelle=1;
                    arthur_absolute_y++;
                    arthur_tile_y = 12-((arthur_absolute_y)>>4);
                    arthur.y++;
                }
                else {
                    arthur.arthur_devant_echelle=0;
                    arthur.arthur_sur_echelle=0;
                    if ( arthur_absolute_y >= 160){
                        arthur.floor=160;
                    }
                    else if ( arthur_absolute_y >= 80){
                        arthur.floor=80;
                    }
                    else {
                        arthur.floor=0;
                    }
                }

                arthur_display();

                if ( arthur_absolute_y > 140 && arthur.y%3 == 0 && 1 == 2 ) {
                    background.y--;
                    herbe.y--;
                    update_layer(&background);
                    update_layer(&herbe);
                }
            }
        }

        // ------------------------------------------------------
        // --- BOTTOM
        // ------------------------------------------------------
        if ( d && !arthur_mort && arthur.action != ARTHUR_TOMBE && arthur.action != ARTHUR_SAUT_HORIZONTAL_GAUCHE && arthur.action != ARTHUR_SAUT_HORIZONTAL_DROITE ){

            if ( arthur_absolute_y == 0 || arthur_absolute_y == 80 ){
                arthur.arthur_sur_echelle=0;
            }

            if ( background.tmx[arthur_tile_y+1][arthur_tile_x] == 13 || background.tmx[arthur_tile_y+1][arthur_tile_x] == 14 ||
                background.tmx[arthur_tile_y+1][arthur_tile_x] == 33 || background.tmx[arthur_tile_y+1][arthur_tile_x] == 34 ||
                background.tmx[arthur_tile_y+1][arthur_tile_x] == 53 || background.tmx[arthur_tile_y+1][arthur_tile_x] == 54 ||
                background.tmx[arthur_tile_y+1][arthur_tile_x] == 73 || background.tmx[arthur_tile_y+1][arthur_tile_x] == 74 ||
                background.tmx[arthur_tile_y+1][arthur_tile_x] == 93 || background.tmx[arthur_tile_y+1][arthur_tile_x] == 94 ){

                    arthur_sur_echelle_count++;
                    
                    if ( arthur_sur_echelle_count == 16 ) {
                        arthur_sur_echelle_count = 0;
                    }
                    else if ( arthur_sur_echelle_count >= 8 ) {
                        
                        *REG_VRAMMOD=1;

                        *REG_VRAMADDR=ADDR_SCB1+(arthur.sprite*64);
                        *REG_VRAMRW = 256+tmx_arthur[arthur.version][22]-1;
                        *REG_VRAMRW = arthur.palette<<8;
                        *REG_VRAMRW = 256+tmx_arthur[arthur.version+1][22]-1;
                        *REG_VRAMRW = arthur.palette<<8;

                        *REG_VRAMADDR=ADDR_SCB1+((arthur.sprite+1)*64);
                        *REG_VRAMRW = 256+tmx_arthur[arthur.version][23]-1;
                        *REG_VRAMRW = arthur.palette<<8;
                        *REG_VRAMRW = 256+tmx_arthur[arthur.version+1][23]-1;
                        *REG_VRAMRW = arthur.palette<<8;
                    }
                    else if ( arthur_sur_echelle_count < 8 ) {

                        *REG_VRAMMOD=1;

                        *REG_VRAMADDR=ADDR_SCB1+(arthur.sprite*64);
                        *REG_VRAMRW = 256+tmx_arthur[arthur.version+2][22]-1;
                        *REG_VRAMRW = arthur.palette<<8;
                        *REG_VRAMRW = 256+tmx_arthur[arthur.version+3][22]-1;
                        *REG_VRAMRW = arthur.palette<<8;

                        *REG_VRAMADDR=ADDR_SCB1+((arthur.sprite+1)*64);
                        *REG_VRAMRW = 256+tmx_arthur[arthur.version+2][23]-1;
                        *REG_VRAMRW = arthur.palette<<8;
                        *REG_VRAMRW = 256+tmx_arthur[arthur.version+3][23]-1;
                        *REG_VRAMRW = arthur.palette<<8;
                    }

                arthur.arthur_sur_echelle=1;
                arthur_absolute_y--;
                arthur_tile_y = 12-((arthur_absolute_y)>>4);
                arthur.y--;
            }
            else if ( arthur_absolute_y > arthur.floor && (background.tmx[arthur_tile_y][arthur_tile_x] == 93 || background.tmx[arthur_tile_y][arthur_tile_x] == 94) ){
                arthur.arthur_sur_echelle=1;
                arthur_absolute_y--;
                arthur_tile_y = 12-((arthur_absolute_y)>>4);
                arthur.y--;
            }

            if ( arthur_absolute_y >= 160){
                arthur.floor=160;
            }
            else if ( arthur_absolute_y >= 80){
                arthur.floor=80;
            }
            else {
                arthur.floor=0;
            }

            if ( arthur.arthur_sur_echelle == 0 && arthur_absolute_y == arthur.floor ){
                *REG_VRAMMOD=1;
                *REG_VRAMADDR=ADDR_SCB1+(arthur.sprite*64);
                *REG_VRAMRW = 256+tmx_arthur[arthur_sens==1?arthur.version:arthur.version+2][20]-1;
                *REG_VRAMRW = arthur.palette<<8;
                *REG_VRAMRW = 256+tmx_arthur[arthur_sens==1?arthur.version+1:arthur.version+3][20]-1;
                *REG_VRAMRW = arthur.palette<<8;
                *REG_VRAMADDR=ADDR_SCB1+((arthur.sprite+1)*64);
                *REG_VRAMRW = 256+tmx_arthur[arthur_sens==1?arthur.version:arthur.version+2][21]-1;
                *REG_VRAMRW = arthur.palette<<8;
                *REG_VRAMRW = 256+tmx_arthur[arthur_sens==1?arthur.version+1:arthur.version+3][21]-1;
                *REG_VRAMRW = arthur.palette<<8;
            }

            arthur_display();
        }

        // ------------------------------------------------------
        // --- LEFT
        // ------------------------------------------------------
        if ( (l && !d && !arthur_mort && !arthur.arthur_sur_echelle && arthur.action != ARTHUR_TOMBE && arthur.action != ARTHUR_SAUT_HORIZONTAL_DROITE ) || arthur.action == ARTHUR_SAUT_HORIZONTAL_GAUCHE ) {

            if ( b2 && arthur.action == ARTHUR_DEBOUT ){
                arthur.action = ARTHUR_SAUT_HORIZONTAL_GAUCHE;
                arthur.cpt1=0;
                arthur.cpt2=0;
            }

            prendre_en_compte=1;
            arthur_sens = 0;

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

                if ( arthur_absolute_y == 0 || arthur_absolute_y == 80 ){
                    arthur.arthur_devant_echelle = 0;
                    if ( background.tmx[arthur_tile_y][arthur_tile_x] == 93 || background.tmx[arthur_tile_y][arthur_tile_x] == 94 ){
                        arthur.arthur_devant_echelle = 1;
                    }
                }

                if ( arthur_absolute_y > 0 && tmx_sol[arthur_tile_y+1][arthur_tile_x] == 0 && arthur.action != ARTHUR_SAUT_HORIZONTAL_GAUCHE && arthur.action != ARTHUR_SAUT_HORIZONTAL_DROITE ){
                    arthur.action = ARTHUR_TOMBE;
                }

                update_layer(&background);
                update_layer(&herbe);

                for(i=0;i<GHOST_NOMBRE;i++){
                    ghost_move_right(&ghosts[i]);
                }

                //update_tombe(&tombes[0]);
                //update_tombe(&tombes[1]);
            }
            else if ( arthur_absolute_x > 0 ){
                arthur_absolute_x--;
                arthur.x--;
                arthur_display();
            }

            // --- Saut horizontal vers la gauche
            if ( arthur.action == ARTHUR_SAUT_HORIZONTAL_GAUCHE ){
                arthur.cpt1++;
                if((arthur.cpt1%2) == 0){
                    *REG_VRAMMOD=1;
                    *REG_VRAMADDR=ADDR_SCB1+(arthur.sprite*64);
                    *REG_VRAMRW = 256+tmx_arthur[arthur.version+2][16]-1;
                    *REG_VRAMRW = arthur.palette<<8;
                    *REG_VRAMRW = 256+tmx_arthur[arthur.version+3][16]-1;
                    *REG_VRAMRW = arthur.palette<<8;
                    *REG_VRAMADDR=ADDR_SCB1+((arthur.sprite+1)*64);
                    *REG_VRAMRW = 256+tmx_arthur[arthur.version+2][17]-1;
                    *REG_VRAMRW = arthur.palette<<8;
                    *REG_VRAMRW = 256+tmx_arthur[arthur.version+3][17]-1;
                    *REG_VRAMRW = arthur.palette<<8;
                    arthur.y+=saut_vertical[arthur.cpt2];
                    if ( arthur.cpt2 == 19 ){
                        arthur.action=0;
                        arthur.cpt2=0;
                    }
                    else {
                        arthur.cpt2++;
                    }
                }
            }            

            arthur_tile_x = FIX_TILEX(arthur_absolute_x);
            arthur_tile_x_for_left = FIX_TILEX(arthur_absolute_x-5);
        }

        // ------------------------------------------------------
        // --- RIGHT
        // ------------------------------------------------------
        if ( (r && !d && !arthur_mort && !arthur.arthur_sur_echelle && arthur.action != ARTHUR_TOMBE && arthur.action != ARTHUR_SAUT_HORIZONTAL_GAUCHE) || arthur.action == ARTHUR_SAUT_HORIZONTAL_DROITE) {

            if ( b2 && arthur.action == ARTHUR_DEBOUT ){
                arthur.action = ARTHUR_SAUT_HORIZONTAL_DROITE;
                arthur.cpt1=0;
                arthur.cpt2=0;
            }

            prendre_en_compte=1;
            arthur_sens = 1;

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

                // --- On tombe dans l'eau et on recommence au début du niveau
                if ( background.tmx[14][tile+8] == LEVEL1_FLOTTE_TILE && arthur.action != ARTHUR_SAUT_HORIZONTAL_GAUCHE && arthur.action != ARTHUR_SAUT_HORIZONTAL_DROITE ){
                    arthur.y-=1;
                    arthur_mort=1;
                }

                if ( arthur_absolute_y > 0 && tmx_sol[arthur_tile_y+1][arthur_tile_x] == 0 && arthur.action != ARTHUR_SAUT_HORIZONTAL_GAUCHE && arthur.action != ARTHUR_SAUT_HORIZONTAL_DROITE ){
                    arthur.action = ARTHUR_TOMBE;
                }
                
                if ( arthur_absolute_y == 0 || arthur_absolute_y == 80 || arthur_absolute_y == 160 ){
                    arthur.arthur_devant_echelle = 0;
                    arthur_avec_une_echelle_sous_les_pieds = 0;
                    if ( background.tmx[arthur_tile_y][arthur_tile_x] == 93 || background.tmx[arthur_tile_y][arthur_tile_x] == 94 ){
                        arthur.arthur_devant_echelle = 1;
                    }
                    if ( arthur_absolute_y == 80 || arthur_absolute_y == 160 ){
                        if ( background.tmx[arthur_tile_y+1][arthur_tile_x] == 13 || background.tmx[arthur_tile_y+1][arthur_tile_x] == 14 ){
                            arthur_avec_une_echelle_sous_les_pieds = 1;
                        }
                    }
                }

                if ( collision == 0 && arthur_mort==0 ) {

                    x++;
                    tile = FIX_TILEX(x);
                    arthur_absolute_x++;
                    background.x--;
                    herbe.x--;
                    //tombes[0].x--;
                    //tombes[1].x--;
                    //if ( x%16 == 0 ){
                        // --- Update tiles for scrolling
                        if ( tile>=12){
                            change_tiles(&background, tile-tile_distance[tile], 20);
                            change_tiles(&herbe, tile-tile_distance[tile], 20);
                        }
                    //}
                    update_layer(&background);
                    update_layer(&herbe);
                    
                    for(i=0;i<GHOST_NOMBRE;i++){
                        ghost_move_left(&ghosts[i]);
                    }
                    
                    //update_tombe(&tombes[0]);
                    //update_tombe(&tombes[1]);
                }
            }
            else if ( arthur_absolute_x <= 3555 ){
                arthur_absolute_x++;
                arthur.x++;                
                arthur_display();
            }

            // --- Saut horizontal vers la droite
            if ( arthur.action == ARTHUR_SAUT_HORIZONTAL_DROITE ){
                arthur.cpt1++;
                if((arthur.cpt1%2) == 0){
                    *REG_VRAMMOD=1;
                    *REG_VRAMADDR=ADDR_SCB1+(arthur.sprite*64);
                    *REG_VRAMRW = 256+tmx_arthur[arthur.version][16]-1;
                    *REG_VRAMRW = arthur.palette<<8;
                    *REG_VRAMRW = 256+tmx_arthur[arthur.version+1][16]-1;
                    *REG_VRAMRW = arthur.palette<<8;
                    *REG_VRAMADDR=ADDR_SCB1+((arthur.sprite+1)*64);
                    *REG_VRAMRW = 256+tmx_arthur[arthur.version][17]-1;
                    *REG_VRAMRW = arthur.palette<<8;
                    *REG_VRAMRW = 256+tmx_arthur[arthur.version+1][17]-1;
                    *REG_VRAMRW = arthur.palette<<8;
                    arthur.y+=saut_vertical[arthur.cpt2];
                    if ( arthur.cpt2 == 19 ){
                        arthur.action=0;
                        arthur.cpt2=0;
                    }
                    else {
                        arthur.cpt2++;
                    }
                }
            }

            arthur_tile_x = FIX_TILEX(arthur_absolute_x);
            arthur_tile_x_for_right = FIX_TILEX(arthur_absolute_x-8);
        }

        if ( u == 0 && d==0 && l==0 && r==0 ){

            // ------------------------------------------------------
            // --- Saut Arthur
            // ------------------------------------------------------
            if ( b2 && arthur.action == ARTHUR_DEBOUT ){
                arthur.action = ARTHUR_SAUT_VERTICAL;
                arthur.cpt1=0;
                arthur.cpt2=0;
            }

            if ( arthur.arthur_sur_echelle != 1 && arthur.action != ARTHUR_SAUT_HORIZONTAL_GAUCHE && arthur.action != ARTHUR_SAUT_HORIZONTAL_DROITE ){
                arthur_index=0;
                arthur_display_next_tiles(arthur_index);
            }
            if ( arthur_absolute_y == 80 || arthur_absolute_y == 160 ){
                if ( background.tmx[arthur_tile_y+1][arthur_tile_x] == 13 || background.tmx[arthur_tile_y+1][arthur_tile_x] == 14 ){
                    arthur_avec_une_echelle_sous_les_pieds = 1;
                }
            }
        }

        if ( prendre_en_compte == 1 ){

            if ( arthur.action == ARTHUR_DEBOUT ){
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
                //arthur_display_next_tiles();
                //display_sprite_arthur(tiles[arthur_index]);
                arthur_display_next_tiles(arthur_index);
            }
            else {
                arthur_display();
            }
        }
    }
}

int main(void) {

    char str[10];
    u16 i, j, k, l, loop, t;

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
    /*for(j = 0; j < 10; j++) {
        for(loop = 0; loop < 16; loop++) {
            tmx_arthur[j][loop] = tmx_arthur[j][loop];
        }
    }*/

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

            if ( DEBUG_DISPLAY_GHOSTS ){
                // --------------------------------------------------------
                // --- START : Affichage des ghosts
                // --------------------------------------------------------
                if ( arthur_absolute_x == 1200 ){
                    for(i=0;i<GHOST_NOMBRE;i++){
                        if ( ghosts[i].state != GHOST_HIDDEN ){
                            ghosts[i].state = GHOST_DISPARAIT;
                            ghosts[i].offset=0;
                            ghosts[i].cpt=0;
                        }
                    }
                }
                else if ( arthur_absolute_x < 1200 ){
                    for(i=0;i<GHOST_NOMBRE;i++){
                        if ( ghosts[i].state == GHOST_HIDDEN && (arthur_absolute_x%ghosts[i].modulo == 0) ){
                            if ( arthur_sens == 1 ){
                                ghosts[i].x = ghosts[i].apparition_x1;
                                ghosts[i].sens = GHOST_GAUCHE;
                            }
                            else {
                                ghosts[i].x = ghosts[i].apparition_x2;
                                ghosts[i].sens = GHOST_DROITE;
                            }
                            ghosts[i].state = GHOST_APPARAIT;
                        }
                    }
                }
                for(i=0;i<GHOST_NOMBRE;i++){
                    display_ghost(&ghosts[i]);
                }
                // --------------------------------------------------------
                // --- END : Affichage des ghosts
                // --------------------------------------------------------
            }

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

                for(i=0;i<GHOST_NOMBRE;i++){
                    ghosts[i].state = GHOST_HIDDEN;
                }

                // Pause de 5s
                for(i=0;i<700;i++){
                    for(j=0;j<320;j++){}
                }
            }
        }

        if ( arthur.action == ARTHUR_TOMBE ){

            arthur_tile_y = 12-((arthur_absolute_y)>>4);

            if ( tmx_sol[arthur_tile_y+1][arthur_tile_x] == 0 ){
                arthur.y-=4;
                arthur_absolute_y-=4;
                arthur_display();

                arthur.floor=0;
                if ( arthur_absolute_y >= 80){
                    arthur.floor=80;
                }
                else if ( arthur_absolute_y >= 160){
                    arthur.floor=160;
                }
            }
            else if ( arthur_absolute_y > arthur.floor ){
                arthur.y-=4;
                arthur_absolute_y-=4;
                arthur_display();
            }
            else {
                //arthur_absolute_y = 0;
                arthur.action = 0;
                //arthur.floor = 0;
            }

        }

        if ( DEBUG == 1 ){
            snprintf(str, 10, "A.Y %4d", arthur.y); ng_text(2, 3, 0, str);
            snprintf(str, 10, "CPT2 %4d", bios_p1current); ng_text(2, 5, 0, str);
            //snprintf(str, 10, "S %4d", ghost1.cpt); ng_text(2, 7, 0, str);
            //snprintf(str, 10, "TIL %4d", background.tmx[arthur_tile_y][arthur_tile_x]); ng_text(2, 5, 0, str);
            //snprintf(str, 10, "TIL %4d", background.tmx[arthur_tile_y+1][arthur_tile_x]); ng_text(2, 7, 0, str);
            //snprintf(str, 10, "ADE %4d", arthur.arthur_devant_echelle); ng_text(2, 9, 0, str);
            //snprintf(str, 10, "ASE %4d", arthur.arthur_sur_echelle); ng_text(2, 11, 0, str);
            //snprintf(str, 10, "ASE %5d", arthur_sur_echelle_count); ng_text(2, 13, 0, str);
            //snprintf(str, 10, "Til %5d", background.tmx[arthur_tile_y+1][tile+8]); ng_text(2, 9, 0, str);
        }
    }

    return 0;
}