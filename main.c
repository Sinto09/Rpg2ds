/*
 * RPG 2DS - Etape 6 : inventaire, armes et armures
 *
 *  - Ecran titre : Jouer / Editeurs / Quitter
 *  - Menu Editeurs : Carte, Blocs, Monstres, Classes, Attaques, Objets, Heros
 *  - Editeur de blocs : cree des blocs et dessine leurs textures (16 x 16, 32 couleurs)
 *  - En jeu : X (ou bouton Sac) ouvre l'inventaire ; les monstres laissent du butin
 *  - Dans les hautes herbes, des monstres sauvages apparaissent : contact = combat
 *  - Jeu : carte vue de dessus, deplacement (croix/stick, B = courir)
 *  - Editeur : ecran du bas = zone de dessin (stylet), ecran du haut = carte entiere
 *  - La carte est sauvegardee sur la carte SD : sdmc:/3ds/rpg2ds/map1.bin
 *  - Les donnees sont sauvegardees dans :       sdmc:/3ds/rpg2ds/ (plusieurs .bin)
 *
 * Compilation : devkitARM + libctru + citro2d (voir Makefile)
 */

#include <3ds.h>
#include <citro2d.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

/* ---------- Constantes ---------- */

#define TOP_W 400
#define TOP_H 240
#define BOT_W 320
#define BOT_H 240

#define TS    16   /* taille d'une tuile en pixels */
#define MAP_W 50
#define MAP_H 30

#define VIEW_W (BOT_W / TS)   /* tuiles visibles dans l'editeur : 20 */
#define VIEW_H (BOT_H / TS)   /* 15 */

#define PW 12      /* hitbox du joueur */
#define PH 12

#define COL(r, g, b) C2D_Color32((r), (g), (b), 255)

#define SAVE_DIR    "sdmc:/3ds/rpg2ds"
#define SAVE_FILE   "sdmc:/3ds/rpg2ds/map1.bin"
#define HEADER_SIZE 9

#define MON_FILE     "sdmc:/3ds/rpg2ds/monsters.bin"
#define MAX_MONSTERS 32
#define NAME_BYTES   64   /* place reservee pour le nom (UTF-8) */
#define NAME_CHARS   15   /* nombre maximum de lettres dans un nom */
#define MON_REC1     (NAME_BYTES + 10)   /* ancien format (version 1) */
#define MON_REC2     (NAME_BYTES + 12)   /* version 2 */
#define MON_REC      (NAME_BYTES + 14)
#define MON_HEADER   6

#define ATK_FILE     "sdmc:/3ds/rpg2ds/attacks.bin"
#define CLS_FILE     "sdmc:/3ds/rpg2ds/classes.bin"
#define HERO_FILE    "sdmc:/3ds/rpg2ds/hero.bin"
#define ITM_FILE     "sdmc:/3ds/rpg2ds/items.bin"
#define MAX_ITEMS    32
#define ITM_REC      (NAME_BYTES + 10)
#define MAX_ATTACKS  24
#define MAX_CLASSES  12
#define ATK_REC      (NAME_BYTES + 3)
#define CLS_REC      (NAME_BYTES + 20)
#define MAX_LEVEL    50

/* ---------- Tuiles (blocs) ---------- */

#define MAX_TILES    32
#define TILE_NAME    32
#define ATLAS_W      128
#define ATLAS_H      128
#define ATLAS_SLOTS  64
#define TEST_SLOT    32
#define TILES_FILE   "sdmc:/3ds/rpg2ds/tiles.bin"
#define DISPLAY_FILE "sdmc:/3ds/rpg2ds/display.bin"

/* palette fixe de 32 couleurs pour dessiner les textures */
static const u8 PAL[32][3] = {
    { 20, 20, 30 },    { 60, 60, 75 },    { 120, 120, 130 }, { 190, 190, 200 }, { 250, 250, 250 },
    { 120, 30, 30 },   { 220, 60, 60 },   { 250, 140, 130 }, { 140, 70, 20 },   { 200, 120, 50 },
    { 250, 170, 60 },  { 250, 220, 90 },  { 30, 100, 40 },   { 76, 160, 64 },   { 130, 210, 90 },
    { 55, 130, 48 },   { 30, 60, 130 },   { 50, 110, 200 },  { 110, 170, 240 }, { 60, 190, 190 },
    { 80, 30, 120 },   { 150, 80, 200 },  { 240, 130, 190 }, { 200, 170, 110 }, { 170, 130, 90 },
    { 110, 70, 30 },   { 80, 45, 15 },    { 80, 80, 90 },    { 50, 150, 60 },   { 235, 235, 240 },
    { 140, 90, 50 },   { 0, 0, 0 },
};

static u32 palCol(int i) {
    return COL(PAL[i & 31][0], PAL[i & 31][1], PAL[i & 31][2]);
}

/* les blocs de depart (leurs numeros servent aussi a la carte par defaut) */
enum { T_GRASS, T_WALL, T_WATER, T_TREE, T_PATH, T_FLOOR, T_DOOR, T_TALL, T_COUNT };

#define TILE_MAXRECT 256

typedef struct { u8 x, y, w, h, c; } PRect;

typedef struct {
    char name[TILE_NAME];
    bool solid;       /* bloque le joueur */
    bool tall;        /* hautes herbes : les monstres sauvages y apparaissent */
    u32 color;        /* couleur moyenne (mini-cartes) */
    u8 pix[16][16];   /* la texture : numeros de couleurs de la palette */
    u8 base;          /* couleur la plus frequente (fond) */
    u16 nrect;        /* nombre de rectangles a dessiner par-dessus le fond */
    PRect r[TILE_MAXRECT];
} Tile;

static Tile tiles[MAX_TILES];
static int tileCount = 0;
#define TILES tiles

static u8 map[MAP_H][MAP_W];
static int spawnTX = 12, spawnTY = 13;   /* point de depart (en tuiles) */

static void tileFill(Tile *t, int c) {
    memset(t->pix, c, sizeof t->pix);
}

static void tileRect(Tile *t, int x, int y, int w, int h, int c) {
    int xx, yy;
    for (yy = y; yy < y + h; yy++)
        for (xx = x; xx < x + w; xx++)
            if (xx >= 0 && xx < 16 && yy >= 0 && yy < 16)
                t->pix[yy][xx] = (u8)c;
}

/* recalcule tout ce qui depend des pixels : couleur moyenne et rectangles de dessin */
static void tileRecolor(Tile *t) {
    int x, y, r = 0, g = 0, b = 0, best = 0;
    int hist[32];
    bool used[16][16];

    memset(hist, 0, sizeof hist);
    for (y = 0; y < 16; y++) {
        for (x = 0; x < 16; x++) {
            const u8 *p = PAL[t->pix[y][x] & 31];
            r += p[0];
            g += p[1];
            b += p[2];
            hist[t->pix[y][x] & 31]++;
        }
    }
    t->color = COL(r / 256, g / 256, b / 256);

    /* fond = couleur la plus frequente ; le reste est decoupe en rectangles */
    for (x = 1; x < 32; x++)
        if (hist[x] > hist[best])
            best = x;
    t->base = (u8)best;
    t->nrect = 0;
    memset(used, 0, sizeof used);
    for (y = 0; y < 16; y++) {
        for (x = 0; x < 16; x++) {
            int c, w, h, xx, yy;

            if (used[y][x] || (t->pix[y][x] & 31) == best)
                continue;
            c = t->pix[y][x] & 31;
            w = 1;
            while (x + w < 16 && !used[y][x + w] && (t->pix[y][x + w] & 31) == c)
                w++;
            h = 1;
            for (;;) {
                bool ok = (y + h < 16);
                for (xx = x; ok && xx < x + w; xx++)
                    if (used[y + h][xx] || (t->pix[y + h][xx] & 31) != c)
                        ok = false;
                if (!ok)
                    break;
                h++;
            }
            for (yy = y; yy < y + h; yy++)
                for (xx = x; xx < x + w; xx++)
                    used[yy][xx] = true;
            t->r[t->nrect].x = (u8)x;
            t->r[t->nrect].y = (u8)y;
            t->r[t->nrect].w = (u8)w;
            t->r[t->nrect].h = (u8)h;
            t->r[t->nrect].c = (u8)c;
            t->nrect++;
        }
    }
}

static void tileSet(Tile *t, const char *name, bool solid, bool tall, int base) {
    memset(t, 0, sizeof *t);
    snprintf(t->name, TILE_NAME, "%s", name);
    t->solid = solid;
    t->tall = tall;
    tileFill(t, base);
}

static void initTiles(void) {
    Tile *t;
    int i;

    t = &tiles[T_GRASS];
    tileSet(t, "Herbe", false, false, 13);
    tileRect(t, 3, 4, 2, 2, 15);
    tileRect(t, 10, 9, 2, 2, 15);
    tileRect(t, 6, 13, 3, 1, 15);
    tileRect(t, 13, 3, 1, 1, 14);

    t = &tiles[T_WALL];
    tileSet(t, "Mur", true, false, 2);
    tileRect(t, 0, 7, 16, 1, 27);
    tileRect(t, 0, 15, 16, 1, 27);
    tileRect(t, 8, 0, 1, 7, 27);
    tileRect(t, 4, 8, 1, 7, 27);
    tileRect(t, 12, 8, 1, 7, 27);

    t = &tiles[T_WATER];
    tileSet(t, "Eau", true, false, 17);
    tileRect(t, 3, 6, 10, 2, 18);
    tileRect(t, 1, 12, 5, 1, 18);
    tileRect(t, 10, 3, 4, 1, 18);

    t = &tiles[T_TREE];
    tileSet(t, "Arbre", true, false, 12);
    tileRect(t, 3, 2, 10, 8, 28);
    tileRect(t, 6, 10, 4, 6, 25);

    t = &tiles[T_PATH];
    tileSet(t, "Chemin", false, false, 23);
    tileRect(t, 2, 3, 2, 1, 24);
    tileRect(t, 10, 11, 3, 1, 24);

    t = &tiles[T_FLOOR];
    tileSet(t, "Sol", false, false, 24);
    tileRect(t, 0, 5, 16, 1, 25);
    tileRect(t, 0, 11, 16, 1, 25);

    t = &tiles[T_DOOR];
    tileSet(t, "Porte", false, false, 25);
    tileRect(t, 3, 0, 10, 16, 26);

    t = &tiles[T_TALL];
    tileSet(t, "Hautes herbes", false, true, 15);
    tileRect(t, 2, 6, 2, 9, 14);
    tileRect(t, 7, 2, 2, 12, 13);
    tileRect(t, 12, 7, 2, 8, 14);
    tileRect(t, 1, 14, 14, 2, 12);

    tileCount = T_COUNT;
    for (i = 0; i < tileCount; i++)
        tileRecolor(&tiles[i]);
}

/* ---------- Textures : toutes les tuiles dans une seule image pour la console ---------- */

/* reglages d'affichage (menu "Y" des editeurs) : au cas ou les textures sortiraient
 * a l'envers ou avec de mauvaises couleurs */
static int texFlipV = 0, texBswap = 0, texSwapXY = 0;

static C3D_Tex atlasTex;
static Tex3DS_SubTexture atlasSub[ATLAS_SLOTS];
static bool atlasOK = false;
static bool atlasDirty = false;
static u8 testPix[16][16];
static Tile testTile;   /* le motif de test, pour le mode rectangles */

/* mode d'affichage : 1 = rectangles (sur, par defaut), 0 = textures de la carte graphique */
static int texMode = 1;
#define TILE_RECT_BUDGET 5500
static int rectBudget = TILE_RECT_BUDGET;   /* rectangles autorises par image */

/* position d'un point dans un carre de 8x8 (ordre en Z utilise par la console) */
static u32 mortonIndex(u32 x, u32 y) {
    u32 i = 0, b;

    for (b = 0; b < 3; b++) {
        i |= ((x >> b) & 1u) << (2 * b);
        i |= ((y >> b) & 1u) << (2 * b + 1);
    }
    return i;
}

static u32 texelOf(int idx) {
    u32 w = ((u32)PAL[idx & 31][0] << 24) | ((u32)PAL[idx & 31][1] << 16) |
            ((u32)PAL[idx & 31][2] << 8) | 0xFFu;
    return texBswap ? __builtin_bswap32(w) : w;
}

/* ecrit une texture 16x16 (256 numeros de couleurs) dans la case "slot" de l'image */
static void atlasWriteSlot(int slot, const u8 *pix) {
    u32 *data;
    int px, py;

    if (!atlasOK || slot < 0 || slot >= ATLAS_SLOTS)
        return;
    data = (u32 *)atlasTex.data;
    for (py = 0; py < 16; py++) {
        for (px = 0; px < 16; px++) {
            u32 x = (u32)((slot % 8) * 16 + px);
            u32 y = (u32)(ATLAS_H - 1 - ((slot / 8) * 16 + py));
            u32 m = texSwapXY ? mortonIndex(y & 7u, x & 7u) : mortonIndex(x & 7u, y & 7u);
            data[((y >> 3) * (ATLAS_W >> 3) + (x >> 3)) * 64u + m] = texelOf(pix[py * 16 + px]);
        }
    }
}

static void atlasFlush(void) {
    if (atlasOK)
        GSPGPU_FlushDataCache(atlasTex.data, ATLAS_W * ATLAS_H * 4);
}

static void atlasSetupSubtex(void) {
    int i;

    for (i = 0; i < ATLAS_SLOTS; i++) {
        float x0 = (float)((i % 8) * 16);
        float y0 = (float)((i / 8) * 16);
        Tex3DS_SubTexture *s = &atlasSub[i];

        s->width = 16;
        s->height = 16;
        s->left = x0 / ATLAS_W;
        s->right = (x0 + 16.0f) / ATLAS_W;
        if (!texFlipV) {
            s->top = 1.0f - y0 / ATLAS_H;
            s->bottom = 1.0f - (y0 + 16.0f) / ATLAS_H;
        } else {
            s->top = 1.0f - (y0 + 16.0f) / ATLAS_H;
            s->bottom = 1.0f - y0 / ATLAS_H;
        }
    }
}

static void atlasRebuild(void) {
    int i;

    if (!atlasOK)
        return;
    atlasSetupSubtex();
    for (i = 0; i < tileCount; i++)
        atlasWriteSlot(i, &tiles[i].pix[0][0]);
    atlasWriteSlot(TEST_SLOT, &testPix[0][0]);
    atlasFlush();
}

static void atlasInit(void) {
    int x, y;

    /* motif de test : 4 carres de couleur, avec une barre noire a gauche de chacun */
    for (y = 0; y < 16; y++) {
        for (x = 0; x < 16; x++) {
            int c = (y < 8) ? ((x < 8) ? 6 : 13) : ((x < 8) ? 17 : 11);
            if ((x & 7) == 0)
                c = 31;
            testPix[y][x] = (u8)c;
        }
    }

    memset(&testTile, 0, sizeof testTile);
    memcpy(testTile.pix, testPix, sizeof testPix);
    tileRecolor(&testTile);

    atlasOK = C3D_TexInit(&atlasTex, ATLAS_W, ATLAS_H, GPU_RGBA8);
    if (!atlasOK)
        return;
    memset(atlasTex.data, 0, ATLAS_W * ATLAS_H * 4);
    C3D_TexSetFilter(&atlasTex, GPU_NEAREST, GPU_NEAREST);
    C3D_TexSetWrap(&atlasTex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
    atlasRebuild();
}

/* mode sur : un fond + quelques rectangles. Si l'image demande trop de rectangles,
 * les derniers blocs dessines sont remplaces par leur couleur moyenne (le jeu ne ralentit pas). */
static void drawTileRects(const Tile *t, float x, float y, float z, float s) {
    int i;

    if ((int)t->nrect + 1 > rectBudget) {
        C2D_DrawRectSolid(x, y, z, TS * s, TS * s, t->color);
        rectBudget--;
        return;
    }
    rectBudget -= (int)t->nrect + 1;
    C2D_DrawRectSolid(x, y, z, TS * s, TS * s, palCol(t->base));
    for (i = 0; i < (int)t->nrect; i++) {
        const PRect *r = &t->r[i];
        C2D_DrawRectSolid(x + r->x * s, y + r->y * s, z + 0.01f, r->w * s, r->h * s, palCol(r->c));
    }
}

/* mode textures de la carte graphique */
static bool drawSlotGPU(int slot, float x, float y, float z, float scale) {
    C2D_Image img;

    if (!atlasOK)
        return false;
    img.tex = &atlasTex;
    img.subtex = &atlasSub[slot];
    C2D_DrawImageAt(img, x, y, z, NULL, scale, scale);
    return true;
}

/* dessine la case "slot" (scale = agrandissement) */
static void drawSlot(int slot, float x, float y, float z, float scale) {
    if (texMode == 0 && drawSlotGPU(slot, x, y, z, scale))
        return;
    if (slot == TEST_SLOT)
        drawTileRects(&testTile, x, y, z, scale);
    else if (slot < tileCount)
        drawTileRects(&tiles[slot], x, y, z, scale);
}

static void drawTile(int idx, float x, float y, float z, float scale) {
    if (idx < 0 || idx >= tileCount)
        idx = 0;
    drawSlot(idx, x, y, z, scale);
}

static void saveDisplay(void) {
    u8 b[8] = { 'R', 'P', 'G', 'D', 0, 0, 0, 1 };
    FILE *f;

    b[4] = (u8)texFlipV;
    b[5] = (u8)texBswap;
    b[6] = (u8)texSwapXY;
    b[7] = (u8)texMode;
    mkdir("sdmc:/3ds", 0777);
    mkdir(SAVE_DIR, 0777);
    f = fopen(DISPLAY_FILE, "wb");
    if (f != NULL) {
        fwrite(b, 1, sizeof b, f);
        fclose(f);
    }
}

static void loadDisplay(void) {
    u8 b[8] = { 0, 0, 0, 0, 0, 0, 0, 1 };
    FILE *f = fopen(DISPLAY_FILE, "rb");

    if (f == NULL)
        return;
    if (fread(b, 1, sizeof b, f) >= 7 && memcmp(b, "RPGD", 4) == 0) {
        texMode = b[7] ? 1 : 0;
        texFlipV = b[4] ? 1 : 0;
        texBswap = b[5] ? 1 : 0;
        texSwapXY = b[6] ? 1 : 0;
    }
    fclose(f);
}

/* ---------- Tuiles : sauvegarde, suppression ---------- */

#define TILE_REC (TILE_NAME + 1 + 256)

static bool saveTiles(void) {
    u8 hdr[6] = { 'R', 'P', 'G', 'T', 1, 0 };
    u8 rec[TILE_REC];
    FILE *f;
    bool ok;
    int i;

    hdr[5] = (u8)tileCount;
    mkdir("sdmc:/3ds", 0777);
    mkdir(SAVE_DIR, 0777);
    f = fopen(TILES_FILE, "wb");
    if (f == NULL)
        return false;

    ok = (fwrite(hdr, 1, sizeof hdr, f) == sizeof hdr);
    for (i = 0; ok && i < tileCount; i++) {
        memset(rec, 0, sizeof rec);
        strncpy((char *)rec, tiles[i].name, TILE_NAME - 1);
        rec[TILE_NAME] = (u8)((tiles[i].solid ? 1 : 0) | (tiles[i].tall ? 2 : 0));
        memcpy(rec + TILE_NAME + 1, tiles[i].pix, 256);
        ok = (fwrite(rec, 1, TILE_REC, f) == TILE_REC);
    }
    fclose(f);
    return ok;
}

static bool loadTiles(void) {
    static Tile tmp[MAX_TILES];
    u8 hdr[6];
    u8 rec[TILE_REC];
    FILE *f;
    int i, k, count;

    f = fopen(TILES_FILE, "rb");
    if (f == NULL)
        return false;

    if (fread(hdr, 1, sizeof hdr, f) != sizeof hdr || memcmp(hdr, "RPGT", 4) != 0 ||
        hdr[4] != 1 || hdr[5] < 1 || hdr[5] > MAX_TILES) {
        fclose(f);
        return false;
    }

    count = hdr[5];
    for (i = 0; i < count; i++) {
        if (fread(rec, 1, TILE_REC, f) != TILE_REC) {
            fclose(f);
            return false;
        }
        memset(&tmp[i], 0, sizeof tmp[i]);
        memcpy(tmp[i].name, rec, TILE_NAME);
        tmp[i].name[TILE_NAME - 1] = '\0';
        if (tmp[i].name[0] == '\0')
            snprintf(tmp[i].name, TILE_NAME, "Bloc %d", i + 1);
        tmp[i].solid = (rec[TILE_NAME] & 1) != 0;
        tmp[i].tall = (rec[TILE_NAME] & 2) != 0;
        for (k = 0; k < 256; k++)
            ((u8 *)tmp[i].pix)[k] = rec[TILE_NAME + 1 + k] & 31;
        tileRecolor(&tmp[i]);
    }
    fclose(f);

    memcpy(tiles, tmp, sizeof(Tile) * (size_t)count);
    tileCount = count;
    return true;
}

/* aucune case de la carte ne doit pointer vers un bloc qui n'existe pas */
static void mapClampTiles(void) {
    int x, y;

    for (y = 0; y < MAP_H; y++)
        for (x = 0; x < MAP_W; x++)
            if (map[y][x] >= tileCount)
                map[y][x] = 0;
}

/* supprime un bloc : les cases de la carte qui l'utilisaient deviennent le bloc 1 */
static bool tileDelete(int k) {
    int i, x, y;

    if (tileCount <= 1 || k < 0 || k >= tileCount)
        return false;
    for (i = k; i < tileCount - 1; i++)
        tiles[i] = tiles[i + 1];
    tileCount--;

    for (y = 0; y < MAP_H; y++) {
        for (x = 0; x < MAP_W; x++) {
            int v = map[y][x];
            if (v == k)
                v = 0;
            else if (v > k)
                v--;
            map[y][x] = (u8)v;
        }
    }
    mapClampTiles();
    return true;
}

/* ---------- Carte par defaut ---------- */

static unsigned rngState = 12345;

static unsigned rnd(void) {
    rngState = rngState * 1103515245u + 12345u;
    return (rngState >> 16) & 0x7FFF;
}

static void buildMap(void) {
    int x, y, i;

    for (y = 0; y < MAP_H; y++)
        for (x = 0; x < MAP_W; x++)
            map[y][x] = T_GRASS;

    /* bordure d'arbres */
    for (x = 0; x < MAP_W; x++) {
        map[0][x] = T_TREE;
        map[MAP_H - 1][x] = T_TREE;
    }
    for (y = 0; y < MAP_H; y++) {
        map[y][0] = T_TREE;
        map[y][MAP_W - 1] = T_TREE;
    }

    /* lac */
    for (y = 0; y < MAP_H; y++) {
        for (x = 0; x < MAP_W; x++) {
            float dx = (x - 36) / 7.0f;
            float dy = (y - 22) / 4.0f;
            if (dx * dx + dy * dy <= 1.0f)
                map[y][x] = T_WATER;
        }
    }

    /* maison */
    for (y = 5; y <= 10; y++) {
        for (x = 8; x <= 16; x++) {
            bool edge = (x == 8 || x == 16 || y == 5 || y == 10);
            map[y][x] = edge ? T_WALL : T_FLOOR;
        }
    }
    map[10][12] = T_DOOR;

    /* chemin : de la porte vers le bas, puis vers la droite */
    for (y = 11; y <= 14; y++)
        map[y][12] = T_PATH;
    for (x = 12; x < MAP_W - 1; x++)
        map[14][x] = T_PATH;

    /* zones de hautes herbes (la ou les monstres apparaissent) */
    {
        static const int patch[3][4] = { { 30, 8, 6, 3 }, { 18, 21, 5, 3 }, { 44, 6, 4, 3 } };
        for (i = 0; i < 3; i++) {
            for (y = 0; y < MAP_H; y++) {
                for (x = 0; x < MAP_W; x++) {
                    float dx = (x - patch[i][0]) / (float)patch[i][2];
                    float dy = (y - patch[i][1]) / (float)patch[i][3];
                    if (dx * dx + dy * dy <= 1.0f && map[y][x] == T_GRASS)
                        map[y][x] = T_TALL;
                }
            }
        }
    }

    /* arbres au hasard (toujours les memes) */
    rngState = 12345;
    for (i = 0; i < 90; i++) {
        x = 1 + (int)(rnd() % (MAP_W - 2));
        y = 1 + (int)(rnd() % (MAP_H - 2));
        if (map[y][x] == T_GRASS)
            map[y][x] = T_TREE;
    }

    spawnTX = 12;
    spawnTY = 13;
}

/* ---------- Sauvegarde / chargement (carte SD) ---------- */

static bool saveMap(void) {
    u8 header[HEADER_SIZE] = { 'R', 'P', 'G', '2', 1, MAP_W, MAP_H, 0, 0 };
    FILE *f;
    bool ok;

    header[7] = (u8)spawnTX;
    header[8] = (u8)spawnTY;

    mkdir("sdmc:/3ds", 0777);
    mkdir(SAVE_DIR, 0777);

    f = fopen(SAVE_FILE, "wb");
    if (f == NULL)
        return false;

    ok = (fwrite(header, 1, HEADER_SIZE, f) == HEADER_SIZE) &&
         (fwrite(map, 1, sizeof map, f) == sizeof map);
    fclose(f);
    return ok;
}

static bool loadMap(void) {
    static u8 tmp[MAP_H][MAP_W];
    u8 header[HEADER_SIZE];
    FILE *f;
    int x, y;

    f = fopen(SAVE_FILE, "rb");
    if (f == NULL)
        return false;

    if (fread(header, 1, HEADER_SIZE, f) != HEADER_SIZE ||
        memcmp(header, "RPG2", 4) != 0 || header[4] != 1 ||
        header[5] != MAP_W || header[6] != MAP_H ||
        fread(tmp, 1, sizeof tmp, f) != sizeof tmp) {
        fclose(f);
        return false;
    }
    fclose(f);

    for (y = 0; y < MAP_H; y++)
        for (x = 0; x < MAP_W; x++)
            map[y][x] = (tmp[y][x] < tileCount) ? tmp[y][x] : 0;

    spawnTX = (header[7] < MAP_W) ? header[7] : 1;
    spawnTY = (header[8] < MAP_H) ? header[8] : 1;
    return true;
}

static void loadOrDefault(void) {
    if (!loadMap())
        buildMap();
    mapClampTiles();
}

/* ---------- Monstres : donnees ---------- */

#define N_COLORS 11
#define N_SHAPES 6

enum { ST_HP, ST_ATK, ST_DEF, ST_SPD, ST_COUNT };
enum { K_DAMAGE, K_HEAL, K_COUNT };
enum { IK_WEAPON, IK_ARMOR, IK_POTION, IK_COUNT };
enum { FM_MONSTERS, FM_CLASSES, FM_ATTACKS, FM_ITEMS, FM_HERO, FM_COUNT };

/* une attaque : degats, ou soin (la puissance est alors un % des PV max) */
typedef struct {
    char name[NAME_BYTES];
    int kind;      /* K_DAMAGE ou K_HEAL */
    int power;     /* 1 a 99 */
    int acc;       /* precision : 30 a 100 */
} Attack;

/* une classe : stats au niveau 1, gain par niveau, 4 attaques */
typedef struct {
    char name[NAME_BYTES];
    int base[ST_COUNT];   /* PV : 1 a 999, le reste : 1 a 99 */
    int grow[ST_COUNT];   /* gain par niveau : PV 0 a 50, le reste 0 a 9 */
    int atk[4];           /* numeros d'attaques */
} Class;

/* un objet : arme (+ATK...), armure (+DEF, +PV...) ou potion (rend des PV) */
typedef struct {
    char name[NAME_BYTES];
    int kind;             /* IK_WEAPON, IK_ARMOR ou IK_POTION */
    int bonus[ST_COUNT];  /* bonus de stats (potion : PV rendus) */
    int req;              /* niveau requis pour l'equiper */
} Item;

typedef struct {
    char name[NAME_BYTES];
    int st[ST_COUNT];   /* stats de base (niveau 1) */
    int color;          /* 0 .. N_COLORS-1 */
    int shape;          /* 0 .. N_SHAPES-1 */
    int level;          /* niveau moyen quand on le rencontre */
    int cls;            /* sa classe : donne ses attaques et sa croissance */
    int drop;           /* objet laisse a sa mort (-1 : aucun) */
    int rate;           /* chance de laisser cet objet, en % */
} Monster;

typedef struct {
    int cls, level, xp, hp;
    int weapon, armor;      /* objets equipes (-1 : rien) */
    int inv[MAX_ITEMS];     /* nombre d'exemplaires possedes de chaque objet */
} Hero;

static Attack attacks[MAX_ATTACKS];
static int attackCount = 0;
static Class classes[MAX_CLASSES];
static int classCount = 0;
static Item items[MAX_ITEMS];
static int itemCount = 0;
static Monster monsters[MAX_MONSTERS];
static int monsterCount = 0;
static Hero hero;

static int formKind = FM_MONSTERS;
static int formIdx[FM_COUNT];   /* element choisi dans chaque editeur */
static int delTimer = 0;        /* > 0 : on attend la confirmation de suppression */
static int formHeldBtn = -1;    /* bouton garde appuye (repetition) */
static int formHeldFrames = 0;

static const char *const COLOR_NAMES[N_COLORS] = {
    "Rouge", "Orange", "Jaune", "Vert", "Turquoise", "Bleu",
    "Violet", "Rose", "Gris", "Marron", "Blanc"
};

static const u8 COLOR_RGB[N_COLORS][3] = {
    { 220, 60, 60 }, { 240, 140, 40 }, { 240, 210, 60 }, { 80, 190, 80 },
    { 60, 190, 190 }, { 60, 100, 220 }, { 150, 80, 200 }, { 240, 130, 190 },
    { 150, 150, 160 }, { 140, 90, 50 }, { 235, 235, 240 }
};

static const char *const SHAPE_NAMES[N_SHAPES] = {
    "Gelée", "Chauve-souris", "Fantôme", "Crâne", "Araignée", "Golem"
};

/* dessins 16x16 : # corps, d corps fonce, w blanc, k noir, . vide */
static const char *const SHAPES[N_SHAPES][16] = {
    { /* Gelee */
        "................",
        "................",
        "................",
        "................",
        "......dddd......",
        "....dd####dd....",
        "...d########d...",
        "..d##########d..",
        "..d##ww##ww##d..",
        ".d###wk##wk###d.",
        ".d############d.",
        ".d####dddd####d.",
        ".d############d.",
        "..dd########dd..",
        "...dddddddddd...",
        "................",
    },
    { /* Chauve-souris */
        "................",
        "................",
        ".....d....d.....",
        ".....dd..dd.....",
        ".....dddddd.....",
        "d...d#w##w#d...d",
        "dd..d######d..dd",
        "ddd.d##dd##d.ddd",
        "dddddd####dddddd",
        ".ddddd#dd#ddddd.",
        "..dddd.dd.dddd..",
        "...ddd.dd.ddd...",
        "....dd....dd....",
        "................",
        "................",
        "................",
    },
    { /* Fantome */
        "................",
        "................",
        ".....dddddd.....",
        "....d######d....",
        "...d########d...",
        "..d##########d..",
        "..d##ww##ww##d..",
        "..d##wk##wk##d..",
        "..d##########d..",
        "..d####dd####d..",
        "..d##########d..",
        "..d##########d..",
        "..d#d##dd##d#d..",
        "..dd.dd..dd.dd..",
        "................",
        "................",
    },
    { /* Crane */
        "................",
        "................",
        "....dddddddd....",
        "...d########d...",
        "..d##########d..",
        "..d##########d..",
        "..d#kkk##kkk#d..",
        "..d#kkk##kkk#d..",
        "..d####kk####d..",
        "...d########d...",
        "....dwdwddwdwd..",
        "....dddddddddd..",
        "................",
        "................",
        "................",
        "................",
    },
    { /* Araignee */
        "................",
        "................",
        "................",
        "....d......d....",
        "...d........d...",
        "..d...####...d..",
        ".d...######...d.",
        "d...##wkkw##...d",
        ".d..########..d.",
        "..d..######..d..",
        "...d..####..d...",
        "....d..dd..d....",
        "................",
        "................",
        "................",
        "................",
    },
    { /* Golem */
        "................",
        "................",
        "....dddddddd....",
        "....d######d....",
        "....d#w##w#d....",
        "....d######d....",
        "....dddddddd....",
        "..dddd####dddd..",
        ".d#dd######dd#d.",
        ".d#d########d#d.",
        ".ddd########ddd.",
        "....dd####dd....",
        "...ddd....ddd...",
        "...ddd....ddd...",
        "...dddd..dddd...",
        "................",
    },
};

static int clampInt(int v, int lo, int hi) {
    return (v < lo) ? lo : ((v > hi) ? hi : v);
}

static int statMax(int s) { return (s == ST_HP) ? 999 : 99; }
static int growMax(int s) { return (s == ST_HP) ? 50 : 9; }

static void setName(char *dst, const char *src) {
    snprintf(dst, NAME_BYTES, "%s", src);
}

static void setAttack(Attack *a, const char *name, int kind, int power, int acc) {
    memset(a, 0, sizeof *a);
    setName(a->name, name);
    a->kind = clampInt(kind, 0, K_COUNT - 1);
    a->power = clampInt(power, 1, 99);
    a->acc = clampInt(acc, 30, 100);
}

static void setClass(Class *c, const char *name, const int *base, const int *grow,
                     const int *atk) {
    int s;

    memset(c, 0, sizeof *c);
    setName(c->name, name);
    for (s = 0; s < ST_COUNT; s++) {
        c->base[s] = clampInt(base[s], 1, statMax(s));
        c->grow[s] = clampInt(grow[s], 0, growMax(s));
    }
    for (s = 0; s < 4; s++)
        c->atk[s] = atk[s];
}

static void setMonster(Monster *m, const char *name, int hp, int atk, int def,
                       int spd, int color, int shape, int level, int cls) {
    memset(m, 0, sizeof *m);
    setName(m->name, name);
    m->st[ST_HP] = clampInt(hp, 1, 999);
    m->st[ST_ATK] = clampInt(atk, 1, 99);
    m->st[ST_DEF] = clampInt(def, 1, 99);
    m->st[ST_SPD] = clampInt(spd, 1, 99);
    m->color = clampInt(color, 0, N_COLORS - 1);
    m->shape = clampInt(shape, 0, N_SHAPES - 1);
    m->level = clampInt(level, 1, MAX_LEVEL);
    m->cls = cls;
    m->drop = -1;
    m->rate = 0;
}

/* ----- calculs de stats ----- */

static int classStat(const Class *c, int s, int level) {
    return c->base[s] + c->grow[s] * (level - 1);
}

static int monStat(const Monster *m, int s, int level) {
    return m->st[s] + classes[m->cls].grow[s] * (level - 1);
}

static int xpNeed(int level) {
    return 10 + level * level * 3;
}

static int itemMax(int s) { return (s == ST_HP) ? 300 : 50; }

/* stats du heros, avec l'arme et l'armure donnees (-1 : rien) */
static int heroStatEq(int s, int weapon, int armor) {
    int v = classStat(&classes[hero.cls], s, hero.level);

    if (weapon >= 0 && weapon < itemCount) v += items[weapon].bonus[s];
    if (armor >= 0 && armor < itemCount)   v += items[armor].bonus[s];
    return (v < 1) ? 1 : v;
}

static int heroStat(int s) {
    return heroStatEq(s, hero.weapon, hero.armor);
}

static void setItem(Item *it, const char *name, int kind, int hp, int atk, int def,
                    int spd, int req) {
    memset(it, 0, sizeof *it);
    setName(it->name, name);
    it->kind = clampInt(kind, 0, IK_COUNT - 1);
    it->bonus[ST_HP] = clampInt(hp, 0, itemMax(ST_HP));
    it->bonus[ST_ATK] = clampInt(atk, 0, itemMax(ST_ATK));
    it->bonus[ST_DEF] = clampInt(def, 0, itemMax(ST_DEF));
    it->bonus[ST_SPD] = clampInt(spd, 0, itemMax(ST_SPD));
    it->req = clampInt(req, 1, MAX_LEVEL);
}

/* ----- valeurs par defaut ----- */

static void defaultAttacks(void) {
    setAttack(&attacks[0], "Coup",         K_DAMAGE, 10, 100);
    setAttack(&attacks[1], "Entaille",     K_DAMAGE, 18, 90);
    setAttack(&attacks[2], "Boule de feu", K_DAMAGE, 28, 80);
    setAttack(&attacks[3], "Soin",         K_HEAL,   30, 100);
    setAttack(&attacks[4], "Griffure",     K_DAMAGE, 12, 100);
    setAttack(&attacks[5], "Écrasement",   K_DAMAGE, 35, 70);
    setAttack(&attacks[6], "Éclair",       K_DAMAGE, 22, 90);
    setAttack(&attacks[7], "Morsure",      K_DAMAGE, 14, 95);
    attackCount = 8;
}

static void defaultClasses(void) {
    setClass(&classes[0], "Guerrier", (const int[]){ 40, 10, 8, 5 },
             (const int[]){ 8, 2, 2, 1 }, (const int[]){ 0, 1, 5, 3 });
    setClass(&classes[1], "Mage", (const int[]){ 28, 12, 4, 6 },
             (const int[]){ 5, 3, 1, 1 }, (const int[]){ 0, 2, 6, 3 });
    setClass(&classes[2], "Voleur", (const int[]){ 32, 9, 5, 9 },
             (const int[]){ 6, 2, 1, 2 }, (const int[]){ 0, 4, 1, 7 });
    setClass(&classes[3], "Bête", (const int[]){ 20, 6, 3, 4 },
             (const int[]){ 4, 1, 1, 1 }, (const int[]){ 7, 4, 0, 5 });
    classCount = 4;
}

static void defaultItems(void) {
    /*                          nom                  type       PV atk def vit niv */
    setItem(&items[0], "Potion",              IK_POTION, 30,  0,  0,  0, 1);
    setItem(&items[1], "Dague",               IK_WEAPON,  0,  3,  0,  1, 1);
    setItem(&items[2], "Épée de fer",         IK_WEAPON,  0,  6,  0,  0, 3);
    setItem(&items[3], "Hache de guerre",     IK_WEAPON,  0, 10,  0,  0, 8);
    setItem(&items[4], "Rapière",             IK_WEAPON,  0,  5,  0,  3, 5);
    setItem(&items[5], "Tunique",             IK_ARMOR,   5,  0,  2,  0, 1);
    setItem(&items[6], "Cotte de mailles",    IK_ARMOR,  10,  0,  5,  0, 4);
    setItem(&items[7], "Armure de plaques",   IK_ARMOR,  20,  0,  9,  0, 8);
    setItem(&items[8], "Grande potion",       IK_POTION, 80,  0,  0,  0, 1);
    itemCount = 9;
}

static void defaultMonsters(void) {
    setMonster(&monsters[0], "Gelée", 20, 5, 2, 3, 3, 0, 3, 3);
    setMonster(&monsters[1], "Chauve-souris", 14, 6, 1, 9, 6, 1, 4, 3);
    setMonster(&monsters[2], "Crâne", 28, 9, 4, 4, 10, 3, 6, 3);
    monsters[0].drop = 0;  monsters[0].rate = 50;   /* Potion */
    monsters[1].drop = 2;  monsters[1].rate = 20;   /* Epee de fer */
    monsters[2].drop = 6;  monsters[2].rate = 20;   /* Cotte de mailles */
    monsterCount = 3;
}

/* equipement de depart : une dague, une tunique et 2 potions */
static void startKit(void) {
    memset(hero.inv, 0, sizeof hero.inv);
    hero.weapon = -1;
    hero.armor = -1;
    if (itemCount > 5) {
        hero.inv[0] = 2;
        hero.inv[1] = 1;
        hero.inv[5] = 1;
        hero.weapon = 1;
        hero.armor = 5;
    }
}

static void defaultHero(void) {
    hero.cls = 0;
    hero.level = 1;
    hero.xp = 0;
    startKit();
    hero.hp = heroStat(ST_HP);
}

/* remet toutes les references (classe d'un monstre, attaque d'une classe...) dans les limites */
static void sanitizeAll(void) {
    int i, k;

    for (i = 0; i < classCount; i++)
        for (k = 0; k < 4; k++)
            classes[i].atk[k] = clampInt(classes[i].atk[k], 0, attackCount - 1);
    for (i = 0; i < monsterCount; i++) {
        monsters[i].cls = clampInt(monsters[i].cls, 0, classCount - 1);
        monsters[i].level = clampInt(monsters[i].level, 1, MAX_LEVEL);
        if (monsters[i].drop < -1 || monsters[i].drop >= itemCount)
            monsters[i].drop = -1;
        monsters[i].rate = clampInt(monsters[i].rate, 0, 100);
    }
    hero.cls = clampInt(hero.cls, 0, classCount - 1);
    hero.level = clampInt(hero.level, 1, MAX_LEVEL);
    hero.xp = clampInt(hero.xp, 0, 99999);
    for (i = 0; i < MAX_ITEMS; i++)
        hero.inv[i] = (i < itemCount) ? clampInt(hero.inv[i], 0, 99) : 0;
    /* l'equipement doit etre du bon type et etre possede */
    if (hero.weapon < 0 || hero.weapon >= itemCount || items[hero.weapon].kind != IK_WEAPON ||
        hero.inv[hero.weapon] <= 0)
        hero.weapon = -1;
    if (hero.armor < 0 || hero.armor >= itemCount || items[hero.armor].kind != IK_ARMOR ||
        hero.inv[hero.armor] <= 0)
        hero.armor = -1;
    hero.hp = clampInt(hero.hp, 1, heroStat(ST_HP));
}

/* ----- ajouter / supprimer (on garde toujours au moins un element) ----- */

static bool monsterNew(void) {
    char name[NAME_BYTES];

    if (monsterCount >= MAX_MONSTERS)
        return false;
    snprintf(name, sizeof name, "Monstre %d", monsterCount + 1);
    setMonster(&monsters[monsterCount], name, 30, 8, 4, 5, 0, 0, 3,
               classCount > 3 ? 3 : 0);
    formIdx[FM_MONSTERS] = monsterCount;
    monsterCount++;
    return true;
}

static bool monsterDelete(void) {
    int i;

    if (monsterCount <= 1)
        return false;
    for (i = formIdx[FM_MONSTERS]; i < monsterCount - 1; i++)
        monsters[i] = monsters[i + 1];
    monsterCount--;
    formIdx[FM_MONSTERS] = clampInt(formIdx[FM_MONSTERS], 0, monsterCount - 1);
    return true;
}

static bool classNew(void) {
    char name[NAME_BYTES];

    if (classCount >= MAX_CLASSES)
        return false;
    snprintf(name, sizeof name, "Classe %d", classCount + 1);
    setClass(&classes[classCount], name, (const int[]){ 30, 8, 4, 5 },
             (const int[]){ 5, 2, 1, 1 }, (const int[]){ 0, 0, 0, 0 });
    formIdx[FM_CLASSES] = classCount;
    classCount++;
    return true;
}

static bool classDelete(void) {
    int i, d = formIdx[FM_CLASSES];

    if (classCount <= 1)
        return false;
    for (i = d; i < classCount - 1; i++)
        classes[i] = classes[i + 1];
    classCount--;
    for (i = 0; i < monsterCount; i++) {
        if (monsters[i].cls == d) monsters[i].cls = 0;
        else if (monsters[i].cls > d) monsters[i].cls--;
    }
    if (hero.cls == d) hero.cls = 0;
    else if (hero.cls > d) hero.cls--;
    formIdx[FM_CLASSES] = clampInt(d, 0, classCount - 1);
    sanitizeAll();
    return true;
}

static bool attackNew(void) {
    char name[NAME_BYTES];

    if (attackCount >= MAX_ATTACKS)
        return false;
    snprintf(name, sizeof name, "Attaque %d", attackCount + 1);
    setAttack(&attacks[attackCount], name, K_DAMAGE, 15, 90);
    formIdx[FM_ATTACKS] = attackCount;
    attackCount++;
    return true;
}

static bool attackDelete(void) {
    int i, k, d = formIdx[FM_ATTACKS];

    if (attackCount <= 1)
        return false;
    for (i = d; i < attackCount - 1; i++)
        attacks[i] = attacks[i + 1];
    attackCount--;
    for (i = 0; i < classCount; i++) {
        for (k = 0; k < 4; k++) {
            if (classes[i].atk[k] == d) classes[i].atk[k] = 0;
            else if (classes[i].atk[k] > d) classes[i].atk[k]--;
        }
    }
    formIdx[FM_ATTACKS] = clampInt(d, 0, attackCount - 1);
    sanitizeAll();
    return true;
}

static bool itemNew(void) {
    char name[NAME_BYTES];

    if (itemCount >= MAX_ITEMS)
        return false;
    snprintf(name, sizeof name, "Objet %d", itemCount + 1);
    setItem(&items[itemCount], name, IK_WEAPON, 0, 3, 0, 0, 1);
    hero.inv[itemCount] = 0;
    formIdx[FM_ITEMS] = itemCount;
    itemCount++;
    return true;
}

static bool itemDelete(void) {
    int i, d = formIdx[FM_ITEMS];

    if (itemCount <= 1)
        return false;
    for (i = d; i < itemCount - 1; i++) {
        items[i] = items[i + 1];
        hero.inv[i] = hero.inv[i + 1];
    }
    hero.inv[itemCount - 1] = 0;
    itemCount--;
    if (hero.weapon == d) hero.weapon = -1;
    else if (hero.weapon > d) hero.weapon--;
    if (hero.armor == d) hero.armor = -1;
    else if (hero.armor > d) hero.armor--;
    for (i = 0; i < monsterCount; i++) {
        if (monsters[i].drop == d) monsters[i].drop = -1;
        else if (monsters[i].drop > d) monsters[i].drop--;
    }
    formIdx[FM_ITEMS] = clampInt(d, 0, itemCount - 1);
    sanitizeAll();
    return true;
}

/* ----- modifier un champ ----- */

static void adjustMonster(Monster *m, int f, int dir, int step) {
    if (f >= 0 && f < 4)
        m->st[f] = clampInt(m->st[f] + dir * step, 1, statMax(f));
    else if (f == 4)
        m->color = (m->color + N_COLORS + dir) % N_COLORS;
    else if (f == 5)
        m->shape = (m->shape + N_SHAPES + dir) % N_SHAPES;
    else if (f == 6)
        m->level = clampInt(m->level + dir * step, 1, MAX_LEVEL);
    else if (f == 7)
        m->cls = (m->cls + classCount + dir) % classCount;
    else if (f == 8)   /* butin : -1 (aucun) puis chaque objet */
        m->drop = (m->drop + 1 + itemCount + 1 + dir) % (itemCount + 1) - 1;
    else if (f == 9)
        m->rate = clampInt(m->rate + dir * step * 5, 0, 100);
}

static void adjustClass(Class *c, int f, int dir, int step) {
    if (f >= 0 && f < 4)
        c->base[f] = clampInt(c->base[f] + dir * step, 1, statMax(f));
    else if (f >= 4 && f < 8)
        c->grow[f - 4] = clampInt(c->grow[f - 4] + dir * step, 0, growMax(f - 4));
    else if (f >= 8 && f < 12)
        c->atk[f - 8] = (c->atk[f - 8] + attackCount + dir) % attackCount;
}

static void adjustAttack(Attack *a, int f, int dir, int step) {
    if (f == 0)
        a->kind = (a->kind + K_COUNT + dir) % K_COUNT;
    else if (f == 1)
        a->power = clampInt(a->power + dir * step, 1, 99);
    else if (f == 2)
        a->acc = clampInt(a->acc + dir * step * 5, 30, 100);
}

static void adjustItem(int idx, int f, int dir, int step) {
    Item *it = &items[idx];

    if (f == 0)
        it->kind = (it->kind + IK_COUNT + dir) % IK_COUNT;
    else if (f >= 1 && f <= 4)
        it->bonus[f - 1] = clampInt(it->bonus[f - 1] + dir * step, 0, itemMax(f - 1));
    else if (f == 5)
        it->req = clampInt(it->req + dir * step, 1, MAX_LEVEL);
    else if (f == 6)   /* nombre d'exemplaires possedes (pratique pour tester) */
        hero.inv[idx] = clampInt(hero.inv[idx] + dir * step, 0, 99);
    sanitizeAll();
}

static void adjustHero(int f, int dir, int step) {
    if (f == 0)
        hero.cls = (hero.cls + classCount + dir) % classCount;
    else if (f == 1)
        hero.level = clampInt(hero.level + dir * step, 1, MAX_LEVEL);
    hero.xp = 0;
    hero.hp = heroStat(ST_HP);
}

/* clavier de la console pour choisir un nom */
static bool askName(char *name, size_t cap) {
    SwkbdState swkbd;
    SwkbdButton button;
    char buf[NAME_BYTES];

    swkbdInit(&swkbd, SWKBD_TYPE_NORMAL, 2, NAME_CHARS);
    swkbdSetInitialText(&swkbd, name);
    swkbdSetHintText(&swkbd, "Nom");
    swkbdSetButton(&swkbd, SWKBD_BUTTON_LEFT, "Annuler", false);
    swkbdSetButton(&swkbd, SWKBD_BUTTON_RIGHT, "OK", true);
    swkbdSetValidation(&swkbd, SWKBD_NOTEMPTY_NOTBLANK, 0, 0);

    button = swkbdInputText(&swkbd, buf, sizeof buf);
    if (button != SWKBD_BUTTON_RIGHT)
        return false;
    snprintf(name, cap, "%s", buf);
    return true;
}

/* ---------- Sauvegarde / chargement des donnees ---------- */

typedef void (*PackFn)(const void *item, u8 *rec);
typedef void (*UnpackFn)(void *item, const u8 *rec, int version);

static void put16(u8 *p, int v) {
    p[0] = (u8)(v & 0xFF);
    p[1] = (u8)((v >> 8) & 0xFF);
}

static int get16(const u8 *p) {
    return p[0] | (p[1] << 8);
}

static void packAttack(const void *item, u8 *rec) {
    const Attack *a = (const Attack *)item;

    memset(rec, 0, ATK_REC);
    strncpy((char *)rec, a->name, NAME_BYTES - 1);
    rec[NAME_BYTES + 0] = (u8)a->kind;
    rec[NAME_BYTES + 1] = (u8)a->power;
    rec[NAME_BYTES + 2] = (u8)a->acc;
}

static void unpackAttack(void *item, const u8 *rec, int version) {
    Attack *a = (Attack *)item;

    (void)version;
    memset(a, 0, sizeof *a);
    memcpy(a->name, rec, NAME_BYTES);
    a->name[NAME_BYTES - 1] = '\0';
    if (a->name[0] == '\0')
        setName(a->name, "Attaque");
    a->kind = rec[NAME_BYTES + 0] % K_COUNT;
    a->power = clampInt(rec[NAME_BYTES + 1], 1, 99);
    a->acc = clampInt(rec[NAME_BYTES + 2], 30, 100);
}

static void packClass(const void *item, u8 *rec) {
    const Class *c = (const Class *)item;
    int s;

    memset(rec, 0, CLS_REC);
    strncpy((char *)rec, c->name, NAME_BYTES - 1);
    for (s = 0; s < ST_COUNT; s++) {
        put16(rec + NAME_BYTES + 2 * s, c->base[s]);
        put16(rec + NAME_BYTES + 8 + 2 * s, c->grow[s]);
    }
    for (s = 0; s < 4; s++)
        rec[NAME_BYTES + 16 + s] = (u8)c->atk[s];
}

static void unpackClass(void *item, const u8 *rec, int version) {
    Class *c = (Class *)item;
    int s;

    (void)version;
    memset(c, 0, sizeof *c);
    memcpy(c->name, rec, NAME_BYTES);
    c->name[NAME_BYTES - 1] = '\0';
    if (c->name[0] == '\0')
        setName(c->name, "Classe");
    for (s = 0; s < ST_COUNT; s++) {
        c->base[s] = clampInt(get16(rec + NAME_BYTES + 2 * s), 1, statMax(s));
        c->grow[s] = clampInt(get16(rec + NAME_BYTES + 8 + 2 * s), 0, growMax(s));
    }
    for (s = 0; s < 4; s++)
        c->atk[s] = rec[NAME_BYTES + 16 + s];
}

static void packMonster(const void *item, u8 *rec) {
    const Monster *m = (const Monster *)item;
    int s;

    memset(rec, 0, MON_REC);
    strncpy((char *)rec, m->name, NAME_BYTES - 1);
    for (s = 0; s < ST_COUNT; s++)
        put16(rec + NAME_BYTES + 2 * s, m->st[s]);
    rec[NAME_BYTES + 8] = (u8)m->color;
    rec[NAME_BYTES + 9] = (u8)m->shape;
    rec[NAME_BYTES + 10] = (u8)m->level;
    rec[NAME_BYTES + 11] = (u8)m->cls;
    rec[NAME_BYTES + 12] = (u8)((m->drop < 0) ? 255 : m->drop);
    rec[NAME_BYTES + 13] = (u8)m->rate;
}

static void unpackMonster(void *item, const u8 *rec, int version) {
    Monster *m = (Monster *)item;
    int s;

    memset(m, 0, sizeof *m);
    memcpy(m->name, rec, NAME_BYTES);
    m->name[NAME_BYTES - 1] = '\0';
    if (m->name[0] == '\0')
        setName(m->name, "Monstre");
    for (s = 0; s < ST_COUNT; s++)
        m->st[s] = clampInt(get16(rec + NAME_BYTES + 2 * s), 1, statMax(s));
    m->color = rec[NAME_BYTES + 8] % N_COLORS;
    m->shape = rec[NAME_BYTES + 9] % N_SHAPES;
    if (version >= 2) {
        m->level = clampInt(rec[NAME_BYTES + 10], 1, MAX_LEVEL);
        m->cls = rec[NAME_BYTES + 11];
    } else {            /* ancien fichier : pas de niveau ni de classe */
        m->level = 3;
        m->cls = 3;
    }
    if (version >= 3) {
        m->drop = (rec[NAME_BYTES + 12] == 255) ? -1 : rec[NAME_BYTES + 12];
        m->rate = clampInt(rec[NAME_BYTES + 13], 0, 100);
    } else {            /* ancien fichier : une potion de temps en temps */
        m->drop = 0;
        m->rate = 30;
    }
}

static void packItem(const void *item, u8 *rec) {
    const Item *it = (const Item *)item;
    int s;

    memset(rec, 0, ITM_REC);
    strncpy((char *)rec, it->name, NAME_BYTES - 1);
    rec[NAME_BYTES] = (u8)it->kind;
    for (s = 0; s < ST_COUNT; s++)
        put16(rec + NAME_BYTES + 1 + 2 * s, it->bonus[s]);
    rec[NAME_BYTES + 9] = (u8)it->req;
}

static void unpackItem(void *item, const u8 *rec, int version) {
    Item *it = (Item *)item;
    int s;

    (void)version;
    memset(it, 0, sizeof *it);
    memcpy(it->name, rec, NAME_BYTES);
    it->name[NAME_BYTES - 1] = '\0';
    if (it->name[0] == '\0')
        setName(it->name, "Objet");
    it->kind = rec[NAME_BYTES] % IK_COUNT;
    for (s = 0; s < ST_COUNT; s++)
        it->bonus[s] = clampInt(get16(rec + NAME_BYTES + 1 + 2 * s), 0, itemMax(s));
    it->req = clampInt(rec[NAME_BYTES + 9], 1, MAX_LEVEL);
}

static bool saveTable(const char *path, const char *magic, int ver, const void *data,
                      size_t itemSize, int count, size_t recSize, PackFn pack) {
    u8 header[MON_HEADER];
    u8 rec[128];
    FILE *f;
    bool ok;
    int i;

    memcpy(header, magic, 4);
    header[4] = (u8)ver;
    header[5] = (u8)count;

    mkdir("sdmc:/3ds", 0777);
    mkdir(SAVE_DIR, 0777);

    f = fopen(path, "wb");
    if (f == NULL)
        return false;

    ok = (fwrite(header, 1, MON_HEADER, f) == MON_HEADER);
    for (i = 0; ok && i < count; i++) {
        pack((const u8 *)data + (size_t)i * itemSize, rec);
        ok = (fwrite(rec, 1, recSize, f) == recSize);
    }
    fclose(f);
    return ok;
}

static bool loadTable(const char *path, const char *magic, int maxVer, int maxCount,
                      void *data, size_t itemSize, const size_t *recSizes,
                      UnpackFn unpack, int *outCount) {
    static u32 tmpBuf[1024];
    u8 *tmp = (u8 *)tmpBuf;
    u8 header[MON_HEADER];
    u8 rec[128];
    FILE *f;
    int i, count, ver;
    size_t rs;

    f = fopen(path, "rb");
    if (f == NULL)
        return false;

    if (fread(header, 1, MON_HEADER, f) != MON_HEADER ||
        memcmp(header, magic, 4) != 0 || header[4] < 1 || header[4] > maxVer ||
        header[5] < 1 || header[5] > maxCount) {
        fclose(f);
        return false;
    }

    ver = header[4];
    count = header[5];
    rs = recSizes[ver - 1];
    if ((size_t)count * itemSize > sizeof tmpBuf) {
        fclose(f);
        return false;
    }

    for (i = 0; i < count; i++) {
        if (fread(rec, 1, rs, f) != rs) {
            fclose(f);
            return false;
        }
        unpack(tmp + (size_t)i * itemSize, rec, ver);
    }
    fclose(f);

    memcpy(data, tmp, (size_t)count * itemSize);
    *outCount = count;
    return true;
}

static const size_t ATK_RECS[1] = { ATK_REC };
static const size_t CLS_RECS[1] = { CLS_REC };
static const size_t ITM_RECS[1] = { ITM_REC };
static const size_t MON_RECS[3] = { MON_REC1, MON_REC2, MON_REC };

static bool saveAttacks(void) {
    return saveTable(ATK_FILE, "RPGA", 1, attacks, sizeof(Attack), attackCount, ATK_REC, packAttack);
}
static bool loadAttacks(void) {
    return loadTable(ATK_FILE, "RPGA", 1, MAX_ATTACKS, attacks, sizeof(Attack), ATK_RECS,
                     unpackAttack, &attackCount);
}
static bool saveClasses(void) {
    return saveTable(CLS_FILE, "RPGC", 1, classes, sizeof(Class), classCount, CLS_REC, packClass);
}
static bool loadClasses(void) {
    return loadTable(CLS_FILE, "RPGC", 1, MAX_CLASSES, classes, sizeof(Class), CLS_RECS,
                     unpackClass, &classCount);
}
static bool saveItems(void) {
    return saveTable(ITM_FILE, "RPGI", 1, items, sizeof(Item), itemCount, ITM_REC, packItem);
}
static bool loadItems(void) {
    return loadTable(ITM_FILE, "RPGI", 1, MAX_ITEMS, items, sizeof(Item), ITM_RECS,
                     unpackItem, &itemCount);
}
static bool saveMonsters(void) {
    return saveTable(MON_FILE, "RPGM", 3, monsters, sizeof(Monster), monsterCount, MON_REC, packMonster);
}
static bool loadMonsters(void) {
    return loadTable(MON_FILE, "RPGM", 3, MAX_MONSTERS, monsters, sizeof(Monster), MON_RECS,
                     unpackMonster, &monsterCount);
}

static bool saveHero(void) {
    u8 rec[13 + MAX_ITEMS] = { 'R', 'P', 'G', 'H', 2 };
    FILE *f;
    bool ok;
    int i;

    rec[5] = (u8)hero.cls;
    rec[6] = (u8)hero.level;
    put16(rec + 7, hero.xp);
    put16(rec + 9, hero.hp);
    rec[11] = (u8)((hero.weapon < 0) ? 255 : hero.weapon);
    rec[12] = (u8)((hero.armor < 0) ? 255 : hero.armor);
    for (i = 0; i < MAX_ITEMS; i++)
        rec[13 + i] = (u8)hero.inv[i];

    mkdir("sdmc:/3ds", 0777);
    mkdir(SAVE_DIR, 0777);

    f = fopen(HERO_FILE, "wb");
    if (f == NULL)
        return false;
    ok = (fwrite(rec, 1, sizeof rec, f) == sizeof rec);
    fclose(f);
    return ok;
}

static bool loadHero(void) {
    u8 rec[13 + MAX_ITEMS];
    size_t got;
    FILE *f = fopen(HERO_FILE, "rb");
    int i;

    if (f == NULL)
        return false;
    memset(rec, 0, sizeof rec);
    got = fread(rec, 1, sizeof rec, f);
    fclose(f);
    if (got < 11 || memcmp(rec, "RPGH", 4) != 0 || rec[4] < 1 || rec[4] > 2)
        return false;

    hero.cls = rec[5];
    hero.level = rec[6];
    hero.xp = get16(rec + 7);
    hero.hp = get16(rec + 9);
    if (rec[4] >= 2 && got >= sizeof rec) {
        hero.weapon = (rec[11] == 255) ? -1 : rec[11];
        hero.armor = (rec[12] == 255) ? -1 : rec[12];
        for (i = 0; i < MAX_ITEMS; i++)
            hero.inv[i] = rec[13 + i];
    } else {            /* ancien fichier : pas d'inventaire */
        startKit();
    }
    return true;
}

static bool saveAll(void) {
    bool a = saveAttacks();
    bool c = saveClasses();
    bool it = saveItems();
    bool m = saveMonsters();
    bool h = saveHero();
    return a && c && it && m && h;
}

static void loadAll(void) {
    if (!loadAttacks())  defaultAttacks();
    if (!loadClasses())  defaultClasses();
    if (!loadItems())    defaultItems();
    if (!loadMonsters()) defaultMonsters();
    if (!loadHero())     defaultHero();
    sanitizeAll();
}

/* ---------- Joueur et camera ---------- */

typedef struct {
    int x, y; /* position en pixels (coin haut-gauche de la hitbox) */
    int dir;  /* 0 bas, 1 haut, 2 gauche, 3 droite */
} Player;

static Player player;
static int camX = 0, camY = 0;

/* compteurs de test : FPS et vitesse reelle du joueur */
static int dbgFps = 0;            /* images par seconde (derniere seconde) */
static int dbgFrames = 0;         /* images comptees dans la seconde en cours */
static float dbgDistWindow = 0;   /* pixels parcourus dans la seconde en cours */
static float dbgSpeedPxS = 0;     /* vitesse moyenne sur la derniere seconde */
static float dbgSpeedFrame = 0;   /* pixels parcourus pendant la derniere image */
static u64 dbgLastTime = 0;

static void dbgReset(void) {
    dbgFrames = 0;
    dbgDistWindow = 0;
    dbgSpeedPxS = 0;
    dbgSpeedFrame = 0;
    dbgLastTime = osGetTime();
}

/* a appeler une fois par image */
static void dbgTick(void) {
    u64 now = osGetTime();

    dbgFrames++;
    if (now - dbgLastTime >= 1000) {
        float secs = (float)(now - dbgLastTime) / 1000.0f;
        dbgFps = (int)((float)dbgFrames / secs + 0.5f);
        dbgSpeedPxS = dbgDistWindow / secs;
        dbgFrames = 0;
        dbgDistWindow = 0;
        dbgLastTime = now;
    }
}

static bool solidAt(int px, int py) {
    int tx, ty;
    if (px < 0 || py < 0)
        return true;
    tx = px / TS;
    ty = py / TS;
    if (tx >= MAP_W || ty >= MAP_H)
        return true;
    return TILES[map[ty][tx]].solid;
}

static bool blocked(int x, int y) {
    return solidAt(x, y) || solidAt(x + PW - 1, y) ||
           solidAt(x, y + PH - 1) || solidAt(x + PW - 1, y + PH - 1);
}

static void updateCamera(void) {
    int maxX = MAP_W * TS - TOP_W;
    int maxY = MAP_H * TS - TOP_H;

    camX = player.x + PW / 2 - TOP_W / 2;
    camY = player.y + PH / 2 - TOP_H / 2;

    if (camX < 0) camX = 0;
    if (camX > maxX) camX = maxX;
    if (camY < 0) camY = 0;
    if (camY > maxY) camY = maxY;
}

/* place le joueur sur la tuile libre la plus proche de (tx, ty) */
static void placePlayerAtTile(int tx, int ty) {
    int r, dx, dy;

    for (r = 0; r < MAP_W; r++) {
        for (dy = -r; dy <= r; dy++) {
            for (dx = -r; dx <= r; dx++) {
                int x = tx + dx, y = ty + dy;
                if (x < 0 || y < 0 || x >= MAP_W || y >= MAP_H)
                    continue;
                if (!blocked(x * TS + 2, y * TS + 2)) {
                    player.x = x * TS + 2;
                    player.y = y * TS + 2;
                    return;
                }
            }
        }
    }
    player.x = TS + 2;
    player.y = TS + 2;
}

/* ---------- Monstres sauvages (apparaissent dans les hautes herbes) ---------- */

#define MAX_WILD         6
#define WILD_FAR_TILES   24   /* au dela, le monstre disparait */

typedef struct {
    bool active;
    int x, y;      /* position en pixels (coin haut-gauche du dessin 16x16) */
    int mon;       /* numero du monstre dans la liste de l'editeur */
    int dx, dy;    /* direction actuelle */
    int timer;     /* temps avant de changer de direction */
} Wild;

static Wild wild[MAX_WILD];
static int wildSpawnTimer = 0;
static int encounters = 0;
static char encMsg[96] = "";
static int encTimer = 0;

static bool tallAtPx(int px, int py) {
    int tx, ty;
    if (px < 0 || py < 0)
        return false;
    tx = px / TS;
    ty = py / TS;
    if (tx >= MAP_W || ty >= MAP_H)
        return false;
    return tiles[map[ty][tx]].tall;
}

/* le monstre reste toujours dans les hautes herbes */
static bool wildFits(int x, int y) {
    return tallAtPx(x + 2, y + 2) && tallAtPx(x + 13, y + 2) &&
           tallAtPx(x + 2, y + 13) && tallAtPx(x + 13, y + 13);
}

static void wildClear(void) {
    memset(wild, 0, sizeof wild);
    wildSpawnTimer = 60;
    encounters = 0;
    encTimer = 0;
}

static int wildCount(void) {
    int i, n = 0;
    for (i = 0; i < MAX_WILD; i++)
        if (wild[i].active)
            n++;
    return n;
}

static void wildTrySpawn(void) {
    int ptx = (player.x + PW / 2) / TS;
    int pty = (player.y + PH / 2) / TS;
    int i, k, slot = -1;

    if (monsterCount < 1)
        return;
    for (i = 0; i < MAX_WILD; i++) {
        if (!wild[i].active) {
            slot = i;
            break;
        }
    }
    if (slot < 0)
        return;

    for (k = 0; k < 40; k++) {
        int tx = ptx + (int)(rnd() % 25) - 12;
        int ty = pty + (int)(rnd() % 17) - 8;
        int ddx, ddy;
        bool taken = false;

        if (tx < 0 || ty < 0 || tx >= MAP_W || ty >= MAP_H)
            continue;
        if (!tiles[map[ty][tx]].tall)
            continue;
        ddx = tx - ptx;
        ddy = ty - pty;
        if (ddx * ddx + ddy * ddy < 25)   /* pas trop pres du joueur */
            continue;
        for (i = 0; i < MAX_WILD; i++) {
            if (wild[i].active && wild[i].x / TS == tx && wild[i].y / TS == ty)
                taken = true;
        }
        if (taken)
            continue;

        wild[slot].active = true;
        wild[slot].x = tx * TS;
        wild[slot].y = ty * TS;
        wild[slot].mon = (int)(rnd() % (unsigned)monsterCount);
        wild[slot].dx = 0;
        wild[slot].dy = 0;
        wild[slot].timer = 0;
        return;
    }
}

static bool startBattle(int mi);

static bool wildUpdate(void) {
    int i;
    int ptx = (player.x + PW / 2) / TS;
    int pty = (player.y + PH / 2) / TS;

    if (encTimer > 0)
        encTimer--;

    if (--wildSpawnTimer <= 0) {
        wildSpawnTimer = 120 + (int)(rnd() % 120);
        wildTrySpawn();
    }

    for (i = 0; i < MAX_WILD; i++) {
        Wild *w = &wild[i];
        int dtx, dty;

        if (!w->active)
            continue;

        /* trop loin : il disparait */
        dtx = w->x / TS - ptx;
        dty = w->y / TS - pty;
        if (dtx < -WILD_FAR_TILES || dtx > WILD_FAR_TILES ||
            dty < -WILD_FAR_TILES || dty > WILD_FAR_TILES) {
            w->active = false;
            continue;
        }

        /* il se balade doucement dans l'herbe */
        if (--w->timer <= 0) {
            static const int DIRS[5][2] = { { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 }, { 0, 0 } };
            int r = (int)(rnd() % 5);
            w->dx = DIRS[r][0];
            w->dy = DIRS[r][1];
            w->timer = 30 + (int)(rnd() % 60);
        }
        if ((w->timer % 2) == 0 && (w->dx != 0 || w->dy != 0)) {
            if (wildFits(w->x + w->dx, w->y + w->dy)) {
                w->x += w->dx;
                w->y += w->dy;
            } else {
                w->dx = 0;
                w->dy = 0;
            }
        }

        /* contact avec le joueur : rencontre ! */
        if (player.x < w->x + 14 && player.x + PW > w->x + 2 &&
            player.y < w->y + 14 && player.y + PH > w->y + 2) {
            w->active = false;
            encounters++;
            startBattle(w->mon);
            return true;
        }
    }
    return false;
}

/* ---------- Etat global ---------- */

enum { S_TITLE, S_PLAY, S_EDIT, S_EDMENU, S_FORM, S_BATTLE, S_INV, S_BLOCKS, S_CALIB };

static int state = S_TITLE;
static bool running = true;
static bool fromEditor = false;   /* le jeu a ete lance depuis l'editeur */
static C2D_TextBuf g_textBuf;

/* editeur */
static int editTile = T_GRASS;    /* tuile choisie */
static int ecx = 0, ecy = 0;      /* coin haut-gauche de la zone affichee (en tuiles) */
static bool painting = false;     /* le stylet a commence a toucher dans l'editeur */
static const char *editMsg = "";
static int editMsgTimer = 0;

/* ---------- Boutons ---------- */

typedef struct {
    float x, y, w, h;
    const char *label;
    bool enabled;
} Button;

#define N_TITLE_BUTTONS 3

static Button titleButtons[N_TITLE_BUTTONS] = {
    { 50,  30, 220, 44, "Jouer",     true },
    { 50,  90, 220, 44, "Éditeurs",  true },
    { 50, 150, 220, 44, "Quitter",   true },
};

#define N_ED_BUTTONS 8

static Button edButtons[N_ED_BUTTONS] = {
    { 50,   4, 220, 24, "Carte",    true },
    { 50,  32, 220, 24, "Blocs",    true },
    { 50,  60, 220, 24, "Monstres", true },
    { 50,  88, 220, 24, "Classes",  true },
    { 50, 116, 220, 24, "Attaques", true },
    { 50, 144, 220, 24, "Objets",   true },
    { 50, 172, 220, 24, "Héros",    true },
    { 50, 206, 220, 26, "Retour",   true },
};
static int edSel = 0;

static Button playTitleButton = { 222, 10, 88, 40, "Titre", true };
static Button playInvButton   = { 222, 56, 88, 40, "Sac", true };

static int sel = 0;

static bool inRect(const Button *b, int px, int py) {
    return px >= b->x && px < b->x + b->w && py >= b->y && py < b->y + b->h;
}

/* ---------- Texte ---------- */

static void drawText(const char *s, float x, float y, float sc, u32 col) {
    C2D_Text t;
    C2D_TextParse(&t, g_textBuf, s);
    C2D_TextOptimize(&t);
    C2D_DrawText(&t, C2D_WithColor, x, y, 0.5f, sc, sc, col);
}

static void drawTextCX(const char *s, float cx, float y, float sc, u32 col) {
    C2D_Text t;
    float tw, th;
    C2D_TextParse(&t, g_textBuf, s);
    C2D_TextOptimize(&t);
    C2D_TextGetDimensions(&t, sc, sc, &tw, &th);
    C2D_DrawText(&t, C2D_WithColor, cx - tw / 2, y, 0.5f, sc, sc, col);
}

static void drawTextInRect(const char *s, float x, float y, float w, float h,
                           float sc, u32 col) {
    C2D_Text t;
    float tw, th;
    C2D_TextParse(&t, g_textBuf, s);
    C2D_TextOptimize(&t);
    C2D_TextGetDimensions(&t, sc, sc, &tw, &th);
    C2D_DrawText(&t, C2D_WithColor, x + (w - tw) / 2, y + (h - th) / 2,
                 0.5f, sc, sc, col);
}

static void drawButton(const Button *b, bool selected) {
    u32 border = selected ? COL(255, 220, 80) : COL(20, 20, 30);
    u32 fill = b->enabled ? COL(60, 90, 160) : COL(70, 70, 80);
    u32 txt = b->enabled ? COL(255, 255, 255) : COL(160, 160, 170);

    C2D_DrawRectSolid(b->x - 3, b->y - 3, 0.0f, b->w + 6, b->h + 6, border);
    C2D_DrawRectSolid(b->x, b->y, 0.1f, b->w, b->h, fill);
    drawTextInRect(b->label, b->x, b->y, b->w, b->h, 0.6f, txt);
}

/* ---------- Combat ---------- */

#define MAX_MSGS 24
#define MSG_LEN  80

enum { A_NONE, A_HURT_P, A_HURT_M, A_HEAL_P, A_HEAL_M };   /* effet d'un message */
enum { BP_MENU, BP_MSG };                                   /* phase du combat */
enum { R_NONE, R_WIN, R_LOSE, R_FLEE };                     /* resultat */

typedef struct {
    char text[MSG_LEN];
    int act, val;
} BMsg;

typedef struct {
    int mon, mLevel;
    int mStat[ST_COUNT];   /* stats du monstre a son niveau (PV = PV max) */
    int mHp;
    int pHp, pMax;
    int phase, result, xpGain, dropItem;
    BMsg msgs[MAX_MSGS];
    int nMsgs, cur;
} Battle;

static Battle bt;

static Button batBtn[4] = {
    {   8,  66, 148, 44, "", true }, { 164,  66, 148, 44, "", true },
    {   8, 120, 148, 44, "", true }, { 164, 120, 148, 44, "", true },
};
static Button batFlee = { 8, 178, 100, 34, "Fuite", true };
static Button batPotion = { 116, 178, 110, 34, "Potion", true };

static void addMsg(const char *text, int act, int val) {
    if (bt.nMsgs >= MAX_MSGS)
        return;
    snprintf(bt.msgs[bt.nMsgs].text, MSG_LEN, "%s", text);
    bt.msgs[bt.nMsgs].act = act;
    bt.msgs[bt.nMsgs].val = val;
    bt.nMsgs++;
}

static void applyMsg(int i) {
    const BMsg *m = &bt.msgs[i];

    switch (m->act) {
    case A_HURT_P: bt.pHp = clampInt(bt.pHp - m->val, 0, bt.pMax); break;
    case A_HURT_M: bt.mHp = clampInt(bt.mHp - m->val, 0, bt.mStat[ST_HP]); break;
    case A_HEAL_P: bt.pHp = clampInt(bt.pHp + m->val, 0, bt.pMax); break;
    case A_HEAL_M: bt.mHp = clampInt(bt.mHp + m->val, 0, bt.mStat[ST_HP]); break;
    default: break;
    }
}

/* on commence a afficher les messages de la file */
static void beginMsgs(void) {
    bt.cur = 0;
    bt.phase = BP_MSG;
    if (bt.nMsgs > 0)
        applyMsg(0);
}

static bool startBattle(int mi) {
    const Monster *m = &monsters[mi];
    char buf[MSG_LEN];
    int s;

    memset(&bt, 0, sizeof bt);
    bt.dropItem = -1;
    bt.mon = mi;
    bt.mLevel = clampInt(m->level + (int)(rnd() % 3) - 1, 1, MAX_LEVEL);
    for (s = 0; s < ST_COUNT; s++)
        bt.mStat[s] = monStat(m, s, bt.mLevel);
    bt.mHp = bt.mStat[ST_HP];
    bt.pMax = heroStat(ST_HP);
    bt.pHp = clampInt(hero.hp, 1, bt.pMax);

    snprintf(buf, sizeof buf, "Un %s sauvage apparaît !", m->name);
    addMsg(buf, A_NONE, 0);
    beginMsgs();
    state = S_BATTLE;
    return true;
}

static int calcDamage(int lvl, int power, int atk, int def) {
    int d = ((lvl * 2 / 5 + 2) * power * atk / (def < 1 ? 1 : def)) / 10 + 1;

    d = d * (85 + (int)(rnd() % 16)) / 100;
    return (d < 1) ? 1 : d;
}

/* une attaque : ajoute les messages, et met a jour les PV "simules" */
static void doAction(bool byPlayer, const Attack *a, int *pHp, int *mHp) {
    char buf[MSG_LEN];

    if (byPlayer)
        snprintf(buf, sizeof buf, "Tu utilises %s !", a->name);
    else
        snprintf(buf, sizeof buf, "%s utilise %s !", monsters[bt.mon].name, a->name);
    addMsg(buf, A_NONE, 0);

    if ((int)(rnd() % 100) >= a->acc) {
        addMsg("Raté !", A_NONE, 0);
        return;
    }

    if (a->kind == K_HEAL) {
        int maxHp = byPlayer ? bt.pMax : bt.mStat[ST_HP];
        int *hp = byPlayer ? pHp : mHp;
        int amount = maxHp * a->power / 100;

        if (amount < 1) amount = 1;
        if (*hp + amount > maxHp) amount = maxHp - *hp;
        *hp += amount;
        snprintf(buf, sizeof buf, "+%d PV", amount);
        addMsg(buf, byPlayer ? A_HEAL_P : A_HEAL_M, amount);
    } else {
        int lvl = byPlayer ? hero.level : bt.mLevel;
        int atk = byPlayer ? heroStat(ST_ATK) : bt.mStat[ST_ATK];
        int def = byPlayer ? bt.mStat[ST_DEF] : heroStat(ST_DEF);
        int dmg = calcDamage(lvl, a->power, atk, def);
        int *hp = byPlayer ? mHp : pHp;

        if (dmg > *hp) dmg = *hp;
        *hp -= dmg;
        snprintf(buf, sizeof buf, "-%d PV", dmg);
        addMsg(buf, byPlayer ? A_HURT_M : A_HURT_P, dmg);
    }
}

/* nombre de potions de soin que le heros possede */
static int potionCount(void) {
    int i, n = 0;

    for (i = 0; i < itemCount; i++)
        if (items[i].kind == IK_POTION && items[i].bonus[ST_HP] > 0)
            n += hero.inv[i];
    return n;
}

/* la potion la plus adaptee : la plus petite qui soigne tout, sinon la plus grosse */
static int bestPotion(int missing) {
    int i, best = -1;

    for (i = 0; i < itemCount; i++) {
        int h, b;
        bool hFits, bFits;

        if (items[i].kind != IK_POTION || items[i].bonus[ST_HP] <= 0 || hero.inv[i] <= 0)
            continue;
        if (best < 0) {
            best = i;
            continue;
        }
        h = items[i].bonus[ST_HP];
        b = items[best].bonus[ST_HP];
        hFits = (h >= missing);
        bFits = (b >= missing);
        if (hFits && bFits) {
            if (h < b) best = i;
        } else if (hFits) {
            best = i;
        } else if (!bFits && h > b) {
            best = i;
        }
    }
    return best;
}

/* atkSlot : 0..3 = attaque choisie, -1 = tentative de fuite, -2 = boire une potion */
static void playTurn(int atkSlot) {
    const Class *pc = &classes[hero.cls];
    const Class *mc = &classes[monsters[bt.mon].cls];
    int pHp = bt.pHp, mHp = bt.mHp;
    int pSpd = heroStat(ST_SPD), mSpd = bt.mStat[ST_SPD];
    bool playerFirst = (pSpd >= mSpd);
    char buf[MSG_LEN];
    int i;

    bt.nMsgs = 0;

    if (atkSlot == -1) {
        int chance = clampInt(50 + (pSpd - mSpd) * 3, 20, 95);

        if ((int)(rnd() % 100) < chance) {
            addMsg("Tu t'enfuis !", A_NONE, 0);
            bt.result = R_FLEE;
            beginMsgs();
            return;
        }
        addMsg("Impossible de fuir !", A_NONE, 0);
        playerFirst = false;   /* le monstre en profite */
    }

    if (atkSlot == -2) {
        int missing = bt.pMax - pHp;
        int pi = bestPotion(missing);
        int amount;

        if (pi < 0 || missing <= 0)
            return;
        hero.inv[pi]--;
        amount = clampInt(items[pi].bonus[ST_HP], 0, missing);
        pHp += amount;
        snprintf(buf, sizeof buf, "Tu bois %s !", items[pi].name);
        addMsg(buf, A_NONE, 0);
        snprintf(buf, sizeof buf, "+%d PV", amount);
        addMsg(buf, A_HEAL_P, amount);
        playerFirst = true;   /* c'est son action : ensuite c'est au monstre */
    }

    for (i = 0; i < 2; i++) {
        bool playerActs = (i == 0) ? playerFirst : !playerFirst;

        if (playerActs) {
            if (atkSlot >= 0)
                doAction(true, &attacks[pc->atk[atkSlot]], &pHp, &mHp);
        } else {
            int slot = (int)(rnd() % 4);
            const Attack *a = &attacks[mc->atk[slot]];

            /* pas de soin s'il est en pleine forme : on retire une fois */
            if (a->kind == K_HEAL && mHp * 100 > bt.mStat[ST_HP] * 60) {
                slot = (int)(rnd() % 4);
                a = &attacks[mc->atk[slot]];
            }
            doAction(false, a, &pHp, &mHp);
        }

        if (mHp <= 0) {
            int lv = hero.level, xp = hero.xp;

            snprintf(buf, sizeof buf, "%s est vaincu !", monsters[bt.mon].name);
            addMsg(buf, A_NONE, 0);
            bt.xpGain = 8 + bt.mLevel * 5;
            snprintf(buf, sizeof buf, "Tu gagnes %d XP !", bt.xpGain);
            addMsg(buf, A_NONE, 0);
            xp += bt.xpGain;
            while (lv < MAX_LEVEL && xp >= xpNeed(lv)) {
                xp -= xpNeed(lv);
                lv++;
                snprintf(buf, sizeof buf, "Niveau %d atteint !", lv);
                addMsg(buf, A_NONE, 0);
            }
            {
                const Monster *mm = &monsters[bt.mon];

                if (mm->drop >= 0 && mm->drop < itemCount && (int)(rnd() % 100) < mm->rate) {
                    bt.dropItem = mm->drop;
                    snprintf(buf, sizeof buf, "Tu trouves : %s !", items[mm->drop].name);
                    addMsg(buf, A_NONE, 0);
                }
            }
            bt.result = R_WIN;
            break;
        }
        if (pHp <= 0) {
            addMsg("Tu es vaincu...", A_NONE, 0);
            addMsg("Tu te réveilles au départ.", A_NONE, 0);
            bt.result = R_LOSE;
            break;
        }
    }

    beginMsgs();
}

static int giveXp(int gain) {
    int oldMax = heroStat(ST_HP), gained = 0;

    hero.xp += gain;
    while (hero.level < MAX_LEVEL && hero.xp >= xpNeed(hero.level)) {
        hero.xp -= xpNeed(hero.level);
        hero.level++;
        gained++;
    }
    if (hero.level >= MAX_LEVEL)
        hero.xp = 0;
    hero.hp += heroStat(ST_HP) - oldMax;
    return gained;
}

static void finishBattle(void) {
    int maxHp;

    if (bt.result == R_WIN) {
        hero.hp = bt.pHp;
        giveXp(bt.xpGain);
        if (bt.dropItem >= 0)
            hero.inv[bt.dropItem] = clampInt(hero.inv[bt.dropItem] + 1, 0, 99);
        maxHp = heroStat(ST_HP);
        hero.hp = clampInt(hero.hp + maxHp / 4, 1, maxHp);   /* un peu de repos */
        snprintf(encMsg, sizeof encMsg, "Victoire ! +%d XP", bt.xpGain);
    } else if (bt.result == R_LOSE) {
        hero.hp = heroStat(ST_HP);
        placePlayerAtTile(spawnTX, spawnTY);
        snprintf(encMsg, sizeof encMsg, "Tu te réveilles au point de départ");
    } else {
        hero.hp = clampInt(bt.pHp, 1, heroStat(ST_HP));
        snprintf(encMsg, sizeof encMsg, "Tu as fui !");
    }
    encTimer = 180;
    saveHero();
    updateCamera();
    state = S_PLAY;
}

static void advanceMsg(void) {
    bt.cur++;
    if (bt.cur >= bt.nMsgs) {
        if (bt.result != R_NONE) {
            finishBattle();
        } else {
            bt.phase = BP_MENU;
            bt.nMsgs = 0;
            bt.cur = 0;
        }
    } else {
        applyMsg(bt.cur);
    }
}

static void updateBattle(u32 kDown) {
    int i;

    if (bt.phase == BP_MSG) {
        if (kDown & (KEY_A | KEY_TOUCH))
            advanceMsg();
        return;
    }

    if (kDown & KEY_TOUCH) {
        touchPosition t;

        hidTouchRead(&t);
        for (i = 0; i < 4; i++) {
            if (inRect(&batBtn[i], t.px, t.py)) {
                playTurn(i);
                return;
            }
        }
        if (inRect(&batFlee, t.px, t.py))
            playTurn(-1);
        else if (inRect(&batPotion, t.px, t.py) && bt.pHp < bt.pMax)
            playTurn(-2);
    }
}

/* ---------- Inventaire ---------- */

#define INV_ROWS 6

static int invList[MAX_ITEMS];   /* numeros des objets possedes, tries par type */
static int invN = 0;
static int invSel = 0;
static int invScroll = 0;
static char invMsg[64] = "";
static int invMsgTimer = 0;

static Button invUp   = { 282,  30, 34,  70, "^",       true };
static Button invDown = { 282, 116, 34,  70, "v",       true };
static Button invAct  = {   6, 196, 150, 36, "Équiper", true };
static Button invBack = { 164, 196, 150, 36, "Retour",  true };

static Button invRow(int r) {
    Button b = { 6, 30.0f + r * 26, 270, 24, "", true };
    return b;
}

static void invClamp(void) {
    invSel = clampInt(invSel, 0, invN > 0 ? invN - 1 : 0);
    if (invSel < invScroll) invScroll = invSel;
    if (invSel >= invScroll + INV_ROWS) invScroll = invSel - INV_ROWS + 1;
    invScroll = clampInt(invScroll, 0, invN > INV_ROWS ? invN - INV_ROWS : 0);
}

static void rebuildInv(void) {
    int k, i;

    invN = 0;
    for (k = 0; k < IK_COUNT; k++)
        for (i = 0; i < itemCount; i++)
            if (items[i].kind == k && hero.inv[i] > 0)
                invList[invN++] = i;
    invClamp();
}

static void enterInventory(void) {
    invMsg[0] = '\0';
    invMsgTimer = 0;
    rebuildInv();
    state = S_INV;
}

static void invAction(void) {
    int idx, maxHp;

    if (invN == 0)
        return;
    idx = invList[invSel];

    if (items[idx].kind == IK_POTION) {
        maxHp = heroStat(ST_HP);
        if (hero.hp >= maxHp) {
            snprintf(invMsg, sizeof invMsg, "Tes PV sont déjà au maximum");
        } else {
            int heal = clampInt(items[idx].bonus[ST_HP], 0, maxHp - hero.hp);
            hero.hp += heal;
            hero.inv[idx]--;
            snprintf(invMsg, sizeof invMsg, "%s : +%d PV", items[idx].name, heal);
        }
    } else {
        int *slot = (items[idx].kind == IK_WEAPON) ? &hero.weapon : &hero.armor;

        if (*slot == idx) {
            *slot = -1;
            snprintf(invMsg, sizeof invMsg, "Retiré : %s", items[idx].name);
        } else if (hero.level < items[idx].req) {
            snprintf(invMsg, sizeof invMsg, "Niveau %d requis !", items[idx].req);
        } else {
            *slot = idx;
            snprintf(invMsg, sizeof invMsg, "Équipé : %s", items[idx].name);
        }
        maxHp = heroStat(ST_HP);
        if (hero.hp > maxHp)
            hero.hp = maxHp;
    }
    invMsgTimer = 150;
    rebuildInv();
    saveHero();
}

static void updateInventory(u32 kDown) {
    int r;

    if (invMsgTimer > 0)
        invMsgTimer--;

    if (kDown & KEY_DOWN) { invSel++; invClamp(); }
    if (kDown & KEY_UP)   { invSel--; invClamp(); }
    if (kDown & KEY_A)    invAction();
    if (kDown & (KEY_B | KEY_X)) {
        saveHero();
        state = S_PLAY;
        return;
    }

    if (kDown & KEY_TOUCH) {
        touchPosition t;

        hidTouchRead(&t);
        if (inRect(&invBack, t.px, t.py)) {
            saveHero();
            state = S_PLAY;
            return;
        }
        if (inRect(&invAct, t.px, t.py))  invAction();
        if (inRect(&invUp, t.px, t.py))   { invSel--; invClamp(); }
        if (inRect(&invDown, t.px, t.py)) { invSel++; invClamp(); }
        for (r = 0; r < INV_ROWS; r++) {
            Button b = invRow(r);

            if (inRect(&b, t.px, t.py) && invScroll + r < invN) {
                invSel = invScroll + r;
                invClamp();
            }
        }
    }
}

/* ---------- Changements d'etat ---------- */

static void startPlay(void) {
    placePlayerAtTile(spawnTX, spawnTY);
    player.dir = 0;
    dbgReset();
    wildClear();
    if (hero.hp < 1 || hero.hp > heroStat(ST_HP))
        hero.hp = heroStat(ST_HP);
    rngState ^= (unsigned)osGetTime();
    playTitleButton.label = fromEditor ? "Éditeur" : "Titre";
    updateCamera();
    state = S_PLAY;
}

static void leavePlay(void) {
    saveHero();
    if (fromEditor) {
        state = S_EDIT;
        painting = false;
    } else {
        state = S_TITLE;
    }
    fromEditor = false;
}

static void clampEditor(void) {
    if (ecx < 0) ecx = 0;
    if (ecx > MAP_W - VIEW_W) ecx = MAP_W - VIEW_W;
    if (ecy < 0) ecy = 0;
    if (ecy > MAP_H - VIEW_H) ecy = MAP_H - VIEW_H;
}

static void enterEditor(void) {
    ecx = spawnTX - VIEW_W / 2;
    ecy = spawnTY - VIEW_H / 2;
    clampEditor();
    painting = false;
    editMsgTimer = 0;
    state = S_EDIT;
}

static void enterForm(int kind);

static void activate(int i) {
    if (i == 0) {
        loadOrDefault();
        fromEditor = false;
        startPlay();
    } else if (i == 1) {
        edSel = 0;
        state = S_EDMENU;
    } else if (i == 2) {
        running = false;
    }
}

static void enterBlocks(void);
static void enterCalib(int ret);

static void activateEd(int i) {
    if (i == 0) {
        loadOrDefault();
        enterEditor();
    } else if (i == 1) {
        enterBlocks();
    } else if (i <= 6) {
        enterForm(i - 2);
    } else {
        state = S_TITLE;
    }
}

/* ---------- Mise a jour ---------- */

static void updateTitle(u32 kDown) {
    int i;

    if (kDown & KEY_DOWN) sel = (sel + 1) % N_TITLE_BUTTONS;
    if (kDown & KEY_UP)   sel = (sel + N_TITLE_BUTTONS - 1) % N_TITLE_BUTTONS;
    if (kDown & KEY_A)    activate(sel);

    if (kDown & KEY_TOUCH) {
        touchPosition t;
        hidTouchRead(&t);
        for (i = 0; i < N_TITLE_BUTTONS; i++) {
            if (inRect(&titleButtons[i], t.px, t.py)) {
                sel = i;
                activate(i);
                break;
            }
        }
    }
}

static void updateEdMenu(u32 kDown) {
    int i;

    if (kDown & KEY_DOWN) edSel = (edSel + 1) % N_ED_BUTTONS;
    if (kDown & KEY_UP)   edSel = (edSel + N_ED_BUTTONS - 1) % N_ED_BUTTONS;
    if (kDown & KEY_A)    activateEd(edSel);
    if (kDown & KEY_B)    state = S_TITLE;
    if (kDown & KEY_Y)    enterCalib(S_EDMENU);

    if (kDown & KEY_TOUCH) {
        touchPosition t;
        hidTouchRead(&t);
        for (i = 0; i < N_ED_BUTTONS; i++) {
            if (inRect(&edButtons[i], t.px, t.py)) {
                edSel = i;
                activateEd(i);
                break;
            }
        }
    }
}

static void updatePlay(u32 kDown, u32 kHeld) {
    int speed = (kHeld & KEY_B) ? 3 : 2;
    int dx = 0, dy = 0, i;
    static int regenTimer = 0;
    int ox = player.x, oy = player.y;
    float mx, my;
    circlePosition cp;

    hidCircleRead(&cp);

    /* le jeu se joue uniquement au stick (la croix est reservee pour plus tard) */
    if (cp.dx < -40) dx = -1;
    if (cp.dx >  40) dx =  1;
    if (cp.dy >  40) dy = -1;
    if (cp.dy < -40) dy =  1;

    if (dx != 0) player.dir = (dx < 0) ? 2 : 3;
    if (dy != 0) player.dir = (dy < 0) ? 1 : 0;

    /* on avance pixel par pixel pour bien longer les murs */
    for (i = 0; i < speed; i++) {
        if (dx != 0 && !blocked(player.x + dx, player.y)) player.x += dx;
        if (dy != 0 && !blocked(player.x, player.y + dy)) player.y += dy;
    }

    /* distance reellement parcourue (0 si on pousse contre un mur) */
    mx = (float)(player.x - ox);
    my = (float)(player.y - oy);
    dbgSpeedFrame = sqrtf(mx * mx + my * my);
    dbgDistWindow += dbgSpeedFrame;

    updateCamera();

    /* on recupere doucement des PV en se promenant */
    if (hero.hp < heroStat(ST_HP) && ++regenTimer >= 180) {
        int maxHp = heroStat(ST_HP);
        regenTimer = 0;
        hero.hp = clampInt(hero.hp + (maxHp / 50 > 1 ? maxHp / 50 : 1), 1, maxHp);
    }

    if (wildUpdate())
        return;

    if (kDown & KEY_X) {
        enterInventory();
        return;
    }

    if (kDown & KEY_SELECT)
        leavePlay();

    if (kDown & KEY_TOUCH) {
        touchPosition t;
        hidTouchRead(&t);
        if (inRect(&playTitleButton, t.px, t.py))
            leavePlay();
        else if (inRect(&playInvButton, t.px, t.py))
            enterInventory();
    }
}

/* repetition automatique quand on garde la croix appuyee */
static bool stepRepeat(u32 kDown, u32 kHeld, u32 key, int *cnt) {
    if (kDown & key) {
        *cnt = 0;
        return true;
    }
    if (kHeld & key) {
        (*cnt)++;
        return (*cnt > 15) && (*cnt % 4 == 0);
    }
    *cnt = 0;
    return false;
}

static void updateEdit(u32 kDown, u32 kHeld) {
    static int rep[4] = { 0, 0, 0, 0 };
    int mx = 0, my = 0;

    if (editMsgTimer > 0)
        editMsgTimer--;

    /* choix de la tuile */
    if (editTile >= tileCount) editTile = 0;
    if (kDown & KEY_L) editTile = (editTile + tileCount - 1) % tileCount;
    if (kDown & KEY_R) editTile = (editTile + 1) % tileCount;

    /* defilement de la zone d'edition */
    if (stepRepeat(kDown, kHeld, KEY_LEFT,  &rep[0])) mx = -1;
    if (stepRepeat(kDown, kHeld, KEY_RIGHT, &rep[1])) mx =  1;
    if (stepRepeat(kDown, kHeld, KEY_UP,    &rep[2])) my = -1;
    if (stepRepeat(kDown, kHeld, KEY_DOWN,  &rep[3])) my =  1;
    ecx += mx;
    ecy += my;
    clampEditor();

    /* peinture au stylet (seulement si le contact a commence ici) */
    if (kDown & KEY_TOUCH) painting = true;
    if (!(kHeld & KEY_TOUCH)) painting = false;

    if (painting) {
        touchPosition t;
        int tx, ty;

        hidTouchRead(&t);
        tx = ecx + t.px / TS;
        ty = ecy + t.py / TS;

        if (tx >= 0 && tx < MAP_W && ty >= 0 && ty < MAP_H) {
            if (kHeld & KEY_Y) {
                spawnTX = tx;
                spawnTY = ty;
            } else {
                map[ty][tx] = (kHeld & KEY_B) ? (u8)T_GRASS : (u8)editTile;
            }
        }
    }

    if (kDown & KEY_X) {
        editMsg = saveMap() ? "Carte sauvegardée !" : "Erreur de sauvegarde (carte SD ?)";
        editMsgTimer = 120;
    }

    if (kDown & KEY_A) {
        saveMap();
        fromEditor = true;
        startPlay();
    }

    if (kDown & KEY_SELECT) {
        saveMap();
        state = S_EDMENU;
    }
}

/* ---------- Editeur de blocs : creer des blocs et dessiner leurs textures ---------- */

static int blkSel = 0;          /* bloc modifie */
static int blkColor = 13;       /* couleur choisie (numero de la palette) */
static int blkTool = 0;         /* 0 crayon, 1 seau, 2 pipette */
static int blkDelTimer = 0;     /* > 0 : on attend la confirmation de suppression */
static bool blkPainting = false;
static int calibReturn = S_EDMENU;

#define BK_CX    6      /* zone de dessin : position et taille d'un pixel */
#define BK_CY    30
#define BK_CELL  10
#define BK_PX    172    /* palette de 32 couleurs */
#define BK_PY    30
#define BK_PITCH 18

static Button bkPrev = {   4,   4,  40, 22, "<", true };
static Button bkNext = { 276,   4,  40, 22, ">", true };
static Button bkName = {  48,   4, 224, 22, "",  true };
static Button bkTools[3] = {
    { 172, 106, 46, 24, "Crayon",  true },
    { 221, 106, 46, 24, "Seau",    true },
    { 270, 106, 46, 24, "Pipette", true },
};
static Button bkSolid = { 172, 136, 144, 22, "", true };
static Button bkTall  = { 172, 162, 144, 22, "", true };
static Button bkNew   = {   6, 204,  74, 28, "Nouveau", true };
static Button bkCopy  = {  84, 204,  74, 28, "Copier",  true };
static Button bkDel   = { 162, 204,  74, 28, "Suppr.",  true };
static Button bkBack  = { 240, 204,  74, 28, "Retour",  true };

/* comme drawButton, avec une taille de texte au choix */
static void drawButtonSc(const Button *b, bool selected, float sc) {
    u32 border = selected ? COL(255, 220, 80) : COL(20, 20, 30);
    u32 fill = b->enabled ? COL(60, 90, 160) : COL(70, 70, 80);
    u32 txt = b->enabled ? COL(255, 255, 255) : COL(160, 160, 170);

    C2D_DrawRectSolid(b->x - 3, b->y - 3, 0.0f, b->w + 6, b->h + 6, border);
    C2D_DrawRectSolid(b->x, b->y, 0.1f, b->w, b->h, fill);
    drawTextInRect(b->label, b->x, b->y, b->w, b->h, sc, txt);
}

/* clavier de la console pour choisir un nom */
static bool askText(char *text, size_t cap, const char *hint, int maxChars) {
    SwkbdState swkbd;
    SwkbdButton button;
    char buf[NAME_BYTES];

    swkbdInit(&swkbd, SWKBD_TYPE_NORMAL, 2, maxChars);
    swkbdSetInitialText(&swkbd, text);
    swkbdSetHintText(&swkbd, hint);
    swkbdSetButton(&swkbd, SWKBD_BUTTON_LEFT, "Annuler", false);
    swkbdSetButton(&swkbd, SWKBD_BUTTON_RIGHT, "OK", true);
    swkbdSetValidation(&swkbd, SWKBD_NOTEMPTY_NOTBLANK, 0, 0);

    button = swkbdInputText(&swkbd, buf, sizeof buf);
    if (button != SWKBD_BUTTON_RIGHT)
        return false;
    snprintf(text, cap, "%s", buf);
    return true;
}

/* a appeler quand les pixels du bloc modifie ont change */
static void blkChanged(void) {
    tileRecolor(&tiles[blkSel]);
    atlasWriteSlot(blkSel, &tiles[blkSel].pix[0][0]);
    atlasDirty = true;
}

/* nouveau bloc : vide (src = NULL) ou copie d'un autre ; renvoie son numero ou -1 */
static int tileNew(const Tile *src) {
    Tile *t;
    char nm[TILE_NAME];

    if (tileCount >= MAX_TILES)
        return -1;
    t = &tiles[tileCount];
    if (src != NULL) {
        *t = *src;
        snprintf(nm, sizeof nm, "Copie %d", tileCount + 1);
    } else {
        tileSet(t, "", false, false, blkColor);
        snprintf(nm, sizeof nm, "Bloc %d", tileCount + 1);
    }
    snprintf(t->name, TILE_NAME, "%s", nm);
    tileRecolor(t);
    atlasWriteSlot(tileCount, &t->pix[0][0]);
    atlasDirty = true;
    return tileCount++;
}

/* le seau : remplit la zone de meme couleur */
static void floodFill(Tile *t, int sx, int sy, int newc) {
    static const int D[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
    int stack[256];
    int sp = 0;
    int old = t->pix[sy][sx];

    if (old == newc)
        return;
    t->pix[sy][sx] = (u8)newc;
    stack[sp++] = sy * 16 + sx;
    while (sp > 0) {
        int p = stack[--sp];
        int x = p % 16, y = p / 16, k;
        for (k = 0; k < 4; k++) {
            int nx = x + D[k][0], ny = y + D[k][1];
            if (nx < 0 || ny < 0 || nx > 15 || ny > 15)
                continue;
            if (t->pix[ny][nx] != old)
                continue;
            t->pix[ny][nx] = (u8)newc;
            stack[sp++] = ny * 16 + nx;
        }
    }
}

static void enterBlocks(void) {
    loadOrDefault();   /* la carte en memoire doit etre celle de la carte SD */
    if (blkSel >= tileCount)
        blkSel = 0;
    blkPainting = false;
    blkDelTimer = 0;
    state = S_BLOCKS;
}

/* 0 <  1 >  2 nom  3-5 outils  6 solide  7 rencontres  8 nouveau  9 copier
 * 10 supprimer  11 retour  20-51 couleurs ; -1 si rien */
static int hitTestBlk(int px, int py) {
    int i, c, r;

    if (inRect(&bkPrev, px, py)) return 0;
    if (inRect(&bkNext, px, py)) return 1;
    if (inRect(&bkName, px, py)) return 2;
    for (i = 0; i < 3; i++)
        if (inRect(&bkTools[i], px, py)) return 3 + i;
    if (inRect(&bkSolid, px, py)) return 6;
    if (inRect(&bkTall, px, py))  return 7;
    if (inRect(&bkNew, px, py))   return 8;
    if (inRect(&bkCopy, px, py))  return 9;
    if (inRect(&bkDel, px, py))   return 10;
    if (inRect(&bkBack, px, py))  return 11;

    if (px >= BK_PX && py >= BK_PY) {
        c = (px - BK_PX) / BK_PITCH;
        r = (py - BK_PY) / BK_PITCH;
        if (c < 8 && r < 4 && (px - BK_PX) % BK_PITCH < 16 && (py - BK_PY) % BK_PITCH < 16)
            return 20 + r * 8 + c;
    }
    return -1;
}

static void pressBlk(int id) {
    Tile *t = &tiles[blkSel];
    int n;

    if (id != 10)
        blkDelTimer = 0;

    switch (id) {
    case 0:
        blkSel = (blkSel + tileCount - 1) % tileCount;
        break;
    case 1:
        blkSel = (blkSel + 1) % tileCount;
        break;
    case 2:
        if (askText(t->name, TILE_NAME, "Nom du bloc", 12))
            tileRecolor(t);
        break;
    case 3:
    case 4:
    case 5:
        blkTool = id - 3;
        break;
    case 6:
        t->solid = !t->solid;
        break;
    case 7:
        t->tall = !t->tall;
        break;
    case 8:
        n = tileNew(NULL);
        if (n >= 0)
            blkSel = n;
        break;
    case 9:
        n = tileNew(t);
        if (n >= 0)
            blkSel = n;
        break;
    case 10:
        if (tileCount > 1) {
            if (blkDelTimer > 0) {
                tileDelete(blkSel);
                if (blkSel >= tileCount)
                    blkSel = tileCount - 1;
                if (editTile >= tileCount)
                    editTile = 0;
                atlasRebuild();
                blkDelTimer = 0;
            } else {
                blkDelTimer = 120;   /* il faut retoucher le bouton pour confirmer */
            }
        }
        break;
    case 11:
        saveTiles();
        saveMap();
        state = S_EDMENU;
        break;
    default:
        if (id >= 20 && id <= 51)
            blkColor = id - 20;
        break;
    }
}

/* le stylet touche la zone de dessin en (px, py) */
static void blkApply(int px, int py, bool first) {
    Tile *t = &tiles[blkSel];
    int cx, cy;

    if (px < BK_CX || py < BK_CY)
        return;
    cx = (px - BK_CX) / BK_CELL;
    cy = (py - BK_CY) / BK_CELL;
    if (cx > 15 || cy > 15)
        return;

    if (blkTool == 0) {
        if (t->pix[cy][cx] != blkColor) {
            t->pix[cy][cx] = (u8)blkColor;
            blkChanged();
        }
    } else if (blkTool == 1) {
        if (first) {
            floodFill(t, cx, cy, blkColor);
            blkChanged();
        }
    } else if (first) {
        blkColor = t->pix[cy][cx];   /* pipette : on reprend cette couleur */
        blkTool = 0;
    }
}

static void enterCalib(int ret) {
    calibReturn = ret;
    state = S_CALIB;
}

static void updateBlocks(u32 kDown, u32 kHeld) {
    touchPosition t;

    if (blkDelTimer > 0)
        blkDelTimer--;

    if (kDown & KEY_L) pressBlk(0);
    if (kDown & KEY_R) pressBlk(1);
    if (kDown & KEY_Y) {
        enterCalib(S_BLOCKS);
        return;
    }
    if (kDown & KEY_SELECT) {
        pressBlk(11);
        return;
    }

    if (kDown & KEY_TOUCH) {
        int id;
        hidTouchRead(&t);
        id = hitTestBlk(t.px, t.py);
        if (id >= 0) {
            pressBlk(id);
            blkPainting = false;
        } else if (t.px >= BK_CX && t.px < BK_CX + 16 * BK_CELL &&
                   t.py >= BK_CY && t.py < BK_CY + 16 * BK_CELL) {
            blkPainting = true;
            blkApply(t.px, t.py, true);
        }
    } else if ((kHeld & KEY_TOUCH) && blkPainting) {
        hidTouchRead(&t);
        blkApply(t.px, t.py, false);
    }

    if (!(kHeld & KEY_TOUCH))
        blkPainting = false;
}

/* ---------- Reglage d'affichage des textures ---------- */

static Button caBtn[4] = {
    { 6,  34, 308, 32, "", true },
    { 6,  74, 308, 30, "", true },
    { 6, 110, 308, 30, "", true },
    { 6, 146, 308, 30, "", true },
};
static Button caOk = { 6, 192, 308, 38, "Terminé", true };

static void updateCalib(u32 kDown) {
    touchPosition t;
    int i;

    if (kDown & (KEY_B | KEY_SELECT)) {
        saveDisplay();
        state = calibReturn;
        return;
    }
    if (kDown & KEY_TOUCH) {
        hidTouchRead(&t);
        for (i = 0; i < 4; i++) {
            if (!inRect(&caBtn[i], t.px, t.py))
                continue;
            if (i == 0) {
                texMode = !texMode;
            } else if (texMode == 0) {   /* ces trois reglages ne servent qu'au mode "carte graphique" */
                if (i == 1)      texFlipV = !texFlipV;
                else if (i == 2) texBswap = !texBswap;
                else             texSwapXY = !texSwapXY;
                atlasRebuild();
            }
        }
        if (inRect(&caOk, t.px, t.py)) {
            saveDisplay();
            state = calibReturn;
        }
    }
}

/* ---------- Mise a jour : editeurs (monstres, classes, attaques, heros) ---------- */

static int formPage = 0;

static const char *const FORM_TITLES[FM_COUNT] = { "Monstre", "Classe", "Attaque", "Objet", "Héros" };
static const int FORM_FIELDS[FM_COUNT] = { 10, 12, 3, 7, 2 };
static const int FORM_PERPAGE[FM_COUNT] = { 6, 4, 6, 6, 6 };

static const char *const FORM_LABELS[FM_COUNT][12] = {
    { "PV", "Attaque", "Défense", "Vitesse", "Couleur", "Forme", "Niveau", "Classe", "Butin", "Taux" },
    { "PV de base", "Attaque", "Défense", "Vitesse", "PV / niveau", "Atq / niveau",
      "Déf / niveau", "Vit / niveau", "Attaque 1", "Attaque 2", "Attaque 3", "Attaque 4" },
    { "Type", "Puissance", "Précision" },
    { "Type", "PV", "Attaque", "Défense", "Vitesse", "Niv. requis", "Possédés" },
    { "Classe", "Niveau" },
};

static const char *const KIND_NAMES[K_COUNT] = { "Dégâts", "Soin" };
static const char *const ITEM_KIND_NAMES[IK_COUNT] = { "Arme", "Armure", "Potion" };

static int formCount(void) {
    switch (formKind) {
    case FM_MONSTERS: return monsterCount;
    case FM_CLASSES:  return classCount;
    case FM_ATTACKS:  return attackCount;
    case FM_ITEMS:    return itemCount;
    default:          return 1;
    }
}

static int formPages(void) {
    return (FORM_FIELDS[formKind] + FORM_PERPAGE[formKind] - 1) / FORM_PERPAGE[formKind];
}

static char *formName(int i) {
    switch (formKind) {
    case FM_MONSTERS: return monsters[i].name;
    case FM_CLASSES:  return classes[i].name;
    case FM_ATTACKS:  return attacks[i].name;
    case FM_ITEMS:    return items[i].name;
    default:          return NULL;
    }
}

static bool formCanAdd(void) {
    switch (formKind) {
    case FM_MONSTERS: return monsterCount < MAX_MONSTERS;
    case FM_CLASSES:  return classCount < MAX_CLASSES;
    case FM_ATTACKS:  return attackCount < MAX_ATTACKS;
    case FM_ITEMS:    return itemCount < MAX_ITEMS;
    default:          return false;
    }
}

static bool formCanDel(void) {
    return formKind != FM_HERO && formCount() > 1;
}

/* seuls les nombres se repetent quand on garde le stylet appuye */
static bool formNumeric(int f) {
    switch (formKind) {
    case FM_MONSTERS: return f < 4 || f == 6 || f == 9;
    case FM_CLASSES:  return f < 8;
    case FM_ATTACKS:  return f == 1 || f == 2;
    case FM_ITEMS:    return f >= 1;
    default:          return f == 1;
    }
}

static void formText(int i, int f, char *buf, size_t n) {
    buf[0] = '\0';
    switch (formKind) {
    case FM_MONSTERS: {
        const Monster *m = &monsters[i];
        if (f < 4)       snprintf(buf, n, "%d", m->st[f]);
        else if (f == 4) snprintf(buf, n, "%s", COLOR_NAMES[m->color]);
        else if (f == 5) snprintf(buf, n, "%s", SHAPE_NAMES[m->shape]);
        else if (f == 6) snprintf(buf, n, "%d", m->level);
        else if (f == 7) snprintf(buf, n, "%s", classes[m->cls].name);
        else if (f == 8) snprintf(buf, n, "%s", (m->drop >= 0) ? items[m->drop].name : "Aucun");
        else             snprintf(buf, n, "%d %%", m->rate);
        break;
    }
    case FM_CLASSES: {
        const Class *c = &classes[i];
        if (f < 4)       snprintf(buf, n, "%d", c->base[f]);
        else if (f < 8)  snprintf(buf, n, "+%d", c->grow[f - 4]);
        else             snprintf(buf, n, "%s", attacks[c->atk[f - 8]].name);
        break;
    }
    case FM_ATTACKS: {
        const Attack *a = &attacks[i];
        if (f == 0)      snprintf(buf, n, "%s", KIND_NAMES[a->kind]);
        else if (f == 1) snprintf(buf, n, a->kind == K_HEAL ? "%d %%" : "%d", a->power);
        else             snprintf(buf, n, "%d %%", a->acc);
        break;
    }
    case FM_ITEMS: {
        const Item *it = &items[i];
        if (f == 0)      snprintf(buf, n, "%s", ITEM_KIND_NAMES[it->kind]);
        else if (f <= 4) snprintf(buf, n, "+%d", it->bonus[f - 1]);
        else if (f == 5) snprintf(buf, n, "%d", it->req);
        else             snprintf(buf, n, "%d", hero.inv[i]);
        break;
    }
    default:
        if (f == 0) snprintf(buf, n, "%s", classes[hero.cls].name);
        else        snprintf(buf, n, "%d", hero.level);
        break;
    }
}

static void formAdjust(int i, int f, int dir, int step) {
    switch (formKind) {
    case FM_MONSTERS: adjustMonster(&monsters[i], f, dir, step); break;
    case FM_CLASSES:  adjustClass(&classes[i], f, dir, step); break;
    case FM_ATTACKS:  adjustAttack(&attacks[i], f, dir, step); break;
    case FM_ITEMS:    adjustItem(i, f, dir, step); break;
    default:          adjustHero(f, dir, step); break;
    }
}

static void enterForm(int kind) {
    formKind = kind;
    formPage = 0;
    delTimer = 0;
    formHeldBtn = -1;
    formHeldFrames = 0;
    formIdx[kind] = clampInt(formIdx[kind], 0, formCount() - 1);
    state = S_FORM;
}

static Button fmPrev = {   6,   4,  60, 26, "<",       true };
static Button fmNext = { 254,   4,  60, 26, ">",       true };
static Button fmNew  = {   6, 208,  96, 28, "Nouveau", true };
static Button fmDel  = { 108, 208,  96, 28, "Suppr.",  true };
static Button fmBack = { 210, 208, 104, 28, "Retour",  true };

#define FM_ROW_Y(r) (62.0f + (r) * 24.0f)

static Button fmNameBtn(void) {
    Button b = { 70, 36, (formPages() > 1) ? 144.0f : 244.0f, 22, "", true };
    return b;
}

static Button fmPageBtn(void) {
    Button b = { 218, 36, 96, 22, "Page", true };
    return b;
}

static Button rowButton(int r, bool plus) {
    Button b = { plus ? 264.0f : 212.0f, FM_ROW_Y(r), plus ? 50.0f : 46.0f,
                 22.0f, plus ? "+" : "-", true };
    return b;
}

/* renvoie le numero du bouton touche (ou -1) :
 * 0 <  1 >  2 Nouveau  3 Suppr.  4 Retour  5 Nom  6 Page  100 + 2*champ (-) ou 101 + 2*champ (+) */
static int hitTestForm(int px, int py) {
    int r, per = FORM_PERPAGE[formKind];
    Button nb = fmNameBtn();
    Button pb = fmPageBtn();

    if (inRect(&fmPrev, px, py)) return 0;
    if (inRect(&fmNext, px, py)) return 1;
    if (inRect(&fmNew,  px, py)) return 2;
    if (inRect(&fmDel,  px, py)) return 3;
    if (inRect(&fmBack, px, py)) return 4;
    if (formName(formIdx[formKind]) != NULL && inRect(&nb, px, py)) return 5;
    if (formPages() > 1 && inRect(&pb, px, py)) return 6;
    for (r = 0; r < per; r++) {
        int f = formPage * per + r;
        Button minus, plus;

        if (f >= FORM_FIELDS[formKind])
            break;
        minus = rowButton(r, false);
        plus = rowButton(r, true);
        if (inRect(&minus, px, py)) return 100 + f * 2;
        if (inRect(&plus, px, py))  return 101 + f * 2;
    }
    return -1;
}

static void pressForm(int id, int step) {
    int *idx = &formIdx[formKind];
    int n = formCount();

    if (id != 3)
        delTimer = 0;

    switch (id) {
    case 0:
        *idx = (*idx + n - 1) % n;
        break;
    case 1:
        *idx = (*idx + 1) % n;
        break;
    case 2:
        if (formCanAdd()) {
            if (formKind == FM_MONSTERS) monsterNew();
            else if (formKind == FM_CLASSES) classNew();
            else if (formKind == FM_ATTACKS) attackNew();
            else if (formKind == FM_ITEMS) itemNew();
        }
        break;
    case 3:
        if (formCanDel()) {
            if (delTimer > 0) {
                if (formKind == FM_MONSTERS) monsterDelete();
                else if (formKind == FM_CLASSES) classDelete();
                else if (formKind == FM_ATTACKS) attackDelete();
                else if (formKind == FM_ITEMS) itemDelete();
                delTimer = 0;
            } else {
                delTimer = 120;   /* il faut retoucher le bouton pour confirmer */
            }
        }
        break;
    case 4:
        saveAll();
        state = S_EDMENU;
        break;
    case 5:
        if (formName(*idx) != NULL)
            askName(formName(*idx), NAME_BYTES);
        break;
    case 6:
        formPage = (formPage + 1) % formPages();
        break;
    default:
        if (id >= 100)
            formAdjust(*idx, (id - 100) / 2, (((id - 100) % 2) != 0) ? 1 : -1, step);
        break;
    }
}

static void updateForm(u32 kDown, u32 kHeld) {
    touchPosition t;

    if (delTimer > 0)
        delTimer--;

    if (kDown & KEY_L) pressForm(0, 1);
    if (kDown & KEY_R) pressForm(1, 1);

    if (kDown & KEY_SELECT) {
        saveAll();
        state = S_EDMENU;
        return;
    }

    if (kDown & KEY_TOUCH) {
        hidTouchRead(&t);
        formHeldBtn = hitTestForm(t.px, t.py);
        formHeldFrames = 0;
        if (formHeldBtn >= 0) {
            pressForm(formHeldBtn, 1);
            if (state != S_FORM)
                return;
            if (formHeldBtn < 100 || !formNumeric((formHeldBtn - 100) / 2))
                formHeldBtn = -1;
        }
    } else if ((kHeld & KEY_TOUCH) && formHeldBtn >= 100) {
        /* nombres : on garde le stylet appuye, ca accelere */
        hidTouchRead(&t);
        if (hitTestForm(t.px, t.py) != formHeldBtn) {
            formHeldBtn = -1;
        } else {
            formHeldFrames++;
            if (formHeldFrames > 20 && formHeldFrames % 3 == 0)
                pressForm(formHeldBtn,
                          formHeldFrames > 120 ? 10 : (formHeldFrames > 60 ? 5 : 1));
        }
    } else if (!(kHeld & KEY_TOUCH)) {
        formHeldBtn = -1;
    }
}

/* ---------- Dessin : ecran titre ---------- */

static void drawTitleTop(void) {
    int i;

    /* decor : sol et arbres */
    C2D_DrawRectSolid(0, 176, 0.0f, TOP_W, 64, COL(76, 160, 64));
    for (i = 0; i < 8; i++) {
        float x = 20 + i * 50;
        C2D_DrawRectSolid(x, 158, 0.05f, 32, 32, COL(30, 100, 40));
        C2D_DrawRectSolid(x + 12, 190, 0.05f, 8, 14, COL(110, 70, 30));
    }

    drawTextCX("RPG 2DS", TOP_W / 2, 30, 1.6f, COL(255, 255, 255));
    drawTextCX("Petit RPG avec éditeur intégré", TOP_W / 2, 105, 0.6f,
               COL(200, 210, 255));
    drawTextCX("Étape 6 : inventaire et équipement", TOP_W / 2, 130, 0.5f,
               COL(150, 160, 190));
}

static void drawTitleBottom(void) {
    int i;

    for (i = 0; i < N_TITLE_BUTTONS; i++)
        drawButton(&titleButtons[i], i == sel);

    drawTextCX("Croix + A, ou touche un bouton", BOT_W / 2, 208, 0.45f,
               COL(170, 170, 190));
}

/* ---------- Dessin : jeu ---------- */

static void drawMonsterArt(int shape, int colorIdx, float x, float y, float px);

static void drawWildTop(void) {
    int i;

    for (i = 0; i < MAX_WILD; i++) {
        const Wild *w = &wild[i];
        float sx, sy;

        if (!w->active || w->mon >= monsterCount)
            continue;
        sx = (float)(w->x - camX);
        sy = (float)(w->y - camY);
        if (sx < -TS || sx > TOP_W || sy < -TS || sy > TOP_H)
            continue;

        C2D_DrawRectSolid(sx + 3, sy + 13, 0.25f, 10, 3, C2D_Color32(0, 0, 0, 90));
        drawMonsterArt(monsters[w->mon].shape, monsters[w->mon].color, sx, sy, 1);
    }

    if (encTimer > 0) {
        C2D_DrawRectSolid(0, 212, 0.4f, TOP_W, 28, C2D_Color32(0, 0, 0, 170));
        drawTextCX(encMsg, TOP_W / 2, 217, 0.6f, COL(255, 220, 80));
    }
}

static void drawPlayerTop(void) {
    float sx = (float)(player.x - camX);
    float sy = (float)(player.y - camY);
    float ix = sx + PW / 2 - 1;
    float iy = sy + PH / 2 - 1;

    C2D_DrawRectSolid(sx - 1, sy - 1, 0.2f, PW + 2, PH + 2, COL(20, 20, 60));
    C2D_DrawRectSolid(sx, sy, 0.21f, PW, PH, COL(220, 60, 60));

    /* petit carre blanc qui montre la direction */
    switch (player.dir) {
    case 0: iy = sy + PH - 4; break;
    case 1: iy = sy + 1;      break;
    case 2: ix = sx + 1;      break;
    default: ix = sx + PW - 3; break;
    }
    C2D_DrawRectSolid(ix, iy, 0.22f, 3, 3, COL(255, 255, 255));
}

/* petit cadre en haut a gauche : FPS et vitesse */
static void drawDebugOverlay(void) {
    char buf[48];
    int sf10 = (int)(dbgSpeedFrame * 10.0f + 0.5f);

    C2D_DrawRectSolid(4, 4, 0.4f, 150, 56, C2D_Color32(0, 0, 0, 150));

    snprintf(buf, sizeof buf, "FPS : %d", dbgFps);
    drawText(buf, 8, 6, 0.45f, COL(255, 255, 255));

    snprintf(buf, sizeof buf, "Vitesse : %d px/s", (int)(dbgSpeedPxS + 0.5f));
    drawText(buf, 8, 20, 0.45f, COL(255, 220, 80));

    snprintf(buf, sizeof buf, "          %d.%d px/image", sf10 / 10, sf10 % 10);
    drawText(buf, 8, 32, 0.4f, COL(170, 170, 190));

    snprintf(buf, sizeof buf, "Sauvages : %d   Rencontres : %d", wildCount(), encounters);
    drawText(buf, 8, 44, 0.4f, COL(170, 170, 190));
}

static void drawPlayBottom(void) {
    const int MX = 10, MY = 10, MS = 4; /* mini-carte : position et echelle */
    int tx, ty;
    float vx, vy, vw, vh, px, py;
    char buf[64];
    u32 white = COL(255, 255, 255);
    u32 gray = COL(170, 170, 190);
    int cx = (player.x + PW / 2) / TS;
    int cy = (player.y + PH / 2) / TS;

    /* mini-carte */
    C2D_DrawRectSolid(MX - 2, MY - 2, 0.0f, MAP_W * MS + 4, MAP_H * MS + 4, white);
    for (ty = 0; ty < MAP_H; ty++)
        for (tx = 0; tx < MAP_W; tx++)
            C2D_DrawRectSolid(MX + tx * MS, MY + ty * MS, 0.05f, MS, MS,
                              TILES[map[ty][tx]].color);

    /* zone visible sur l'ecran du haut */
    vx = MX + camX * (float)MS / TS;
    vy = MY + camY * (float)MS / TS;
    vw = TOP_W * (float)MS / TS;
    vh = TOP_H * (float)MS / TS;
    C2D_DrawRectSolid(vx, vy, 0.1f, vw, 1, white);
    C2D_DrawRectSolid(vx, vy + vh - 1, 0.1f, vw, 1, white);
    C2D_DrawRectSolid(vx, vy, 0.1f, 1, vh, white);
    C2D_DrawRectSolid(vx + vw - 1, vy, 0.1f, 1, vh, white);

    /* monstres sauvages */
    {
        int k;
        for (k = 0; k < MAX_WILD; k++) {
            if (wild[k].active)
                C2D_DrawRectSolid(MX + (wild[k].x + TS / 2) * (float)MS / TS - 1,
                                  MY + (wild[k].y + TS / 2) * (float)MS / TS - 1,
                                  0.15f, 3, 3, COL(255, 220, 80));
        }
    }

    /* joueur */
    px = MX + (player.x + PW / 2) * (float)MS / TS;
    py = MY + (player.y + PH / 2) * (float)MS / TS;
    C2D_DrawRectSolid(px - 1, py - 1, 0.2f, 3, 3, COL(255, 0, 0));

    /* boutons */
    drawButton(&playTitleButton, false);
    drawButton(&playInvButton, false);

    /* infos */
    snprintf(buf, sizeof buf, "%s niv. %d   PV %d/%d", classes[hero.cls].name,
             hero.level, hero.hp, heroStat(ST_HP));
    drawText(buf, 10, 140, 0.5f, white);
    snprintf(buf, sizeof buf, "XP %d/%d   Pos %d,%d", hero.xp, xpNeed(hero.level), cx, cy);
    drawText(buf, 10, 156, 0.5f, white);
    snprintf(buf, sizeof buf, "Sur : %s", TILES[map[cy][cx]].name);
    drawText(buf, 10, 172, 0.5f, gray);
    drawText("Stick : bouger  B : courir  X : sac", 10, 188, 0.5f, gray);
    drawText(fromEditor ? "SELECT : éditeur    START : quitter"
                        : "SELECT : titre    START : quitter",
             10, 204, 0.5f, gray);
}

/* ---------- Dessin : editeur ---------- */

/* ecran du bas : la zone qu'on modifie au stylet */
static void drawEditBottom(void) {
    int x, y;
    u32 grid = C2D_Color32(0, 0, 0, 60);

    for (y = 0; y < VIEW_H; y++)
        for (x = 0; x < VIEW_W; x++)
            drawTile(map[ecy + y][ecx + x], (float)(x * TS), (float)(y * TS), 0.0f, 1.0f);

    /* grille */
    for (x = 1; x < VIEW_W; x++)
        C2D_DrawRectSolid((float)(x * TS), 0, 0.1f, 1, BOT_H, grid);
    for (y = 1; y < VIEW_H; y++)
        C2D_DrawRectSolid(0, (float)(y * TS), 0.1f, BOT_W, 1, grid);

    /* point de depart */
    if (spawnTX >= ecx && spawnTX < ecx + VIEW_W &&
        spawnTY >= ecy && spawnTY < ecy + VIEW_H) {
        float sx = (float)((spawnTX - ecx) * TS);
        float sy = (float)((spawnTY - ecy) * TS);
        C2D_DrawRectSolid(sx + 2, sy + 2, 0.2f, 12, 12, COL(20, 20, 60));
        C2D_DrawRectSolid(sx + 4, sy + 4, 0.21f, 8, 8, COL(255, 220, 80));
    }
}

/* ecran du haut : la carte entiere, la palette et l'aide */
static void drawEditTop(void) {
    const int OX = 10, OY = 10, OS = 5;   /* carte entiere : position et echelle */
    const char *help[] = {
        "Stylet : peindre",
        "L / R : bloc",
        "B + stylet : gomme",
        "Y + stylet : départ",
        "Croix : défiler",
        "A : tester",
        "X : sauver",
        "SELECT : menu",
        "START : quitter",
    };
    u32 white = COL(255, 255, 255);
    u32 gray = COL(170, 170, 190);
    u32 yellow = COL(255, 220, 80);
    char buf[96];
    int i, tx, ty, shown, first;
    float vx, vy, vw, vh;

    /* carte entiere */
    C2D_DrawRectSolid(OX - 2, OY - 2, 0.0f, MAP_W * OS + 4, MAP_H * OS + 4, white);
    for (ty = 0; ty < MAP_H; ty++)
        for (tx = 0; tx < MAP_W; tx++)
            C2D_DrawRectSolid(OX + tx * OS, OY + ty * OS, 0.05f, OS, OS,
                              TILES[map[ty][tx]].color);

    /* zone affichee sur l'ecran du bas */
    vx = (float)(OX + ecx * OS);
    vy = (float)(OY + ecy * OS);
    vw = (float)(VIEW_W * OS);
    vh = (float)(VIEW_H * OS);
    C2D_DrawRectSolid(vx, vy, 0.1f, vw, 2, yellow);
    C2D_DrawRectSolid(vx, vy + vh - 2, 0.1f, vw, 2, yellow);
    C2D_DrawRectSolid(vx, vy, 0.1f, 2, vh, yellow);
    C2D_DrawRectSolid(vx + vw - 2, vy, 0.1f, 2, vh, yellow);

    /* point de depart */
    C2D_DrawRectSolid((float)(OX + spawnTX * OS - 1), (float)(OY + spawnTY * OS - 1),
                      0.2f, OS + 2, OS + 2, COL(255, 255, 255));
    C2D_DrawRectSolid((float)(OX + spawnTX * OS), (float)(OY + spawnTY * OS),
                      0.21f, OS, OS, COL(220, 60, 60));

    /* aide */
    for (i = 0; i < 9; i++)
        drawText(help[i], 268, 10.0f + i * 14, 0.4f, gray);

    /* palette : 9 blocs visibles, le bloc choisi reste au milieu */
    if (editTile >= tileCount)
        editTile = 0;
    shown = (tileCount < 9) ? tileCount : 9;
    first = editTile - 4;
    if (first > tileCount - shown)
        first = tileCount - shown;
    if (first < 0)
        first = 0;
    for (i = 0; i < shown; i++) {
        int idx = first + i;
        float bx = 10.0f + i * 36;
        float by = 168.0f;
        if (idx == editTile)
            C2D_DrawRectSolid(bx - 3, by - 3, 0.0f, 38, 38, yellow);
        C2D_DrawRectSolid(bx, by, 0.02f, 32, 32, COL(20, 20, 30));
        drawTile(idx, bx, by, 0.1f, 2.0f);
    }
    snprintf(buf, sizeof buf, "%s - %s%s   (%d/%d)", TILES[editTile].name,
             TILES[editTile].solid ? "bloque" : "traversable",
             TILES[editTile].tall ? ", rencontres" : "", editTile + 1, tileCount);
    drawText(buf, 10, 208, 0.45f, white);

    if (editMsgTimer > 0)
        drawText(editMsg, 10, 224, 0.5f, yellow);
}

/* ---------- Dessin : editeur de blocs ---------- */

static void drawBlocksTop(void) {
    const Tile *t = &tiles[blkSel];
    u32 white = COL(255, 255, 255);
    u32 gray = COL(170, 170, 190);
    u32 yellow = COL(255, 220, 80);
    char buf[64];
    int i, j;

    /* grand apercu */
    C2D_DrawRectSolid(8, 8, 0.0f, 100, 100, COL(35, 45, 80));
    drawTile(blkSel, 10, 10, 0.3f, 6.0f);

    /* le meme bloc repete 3 x 3 : pratique pour voir si la texture se raccorde bien */
    C2D_DrawRectSolid(122, 10, 0.0f, 100, 100, COL(35, 45, 80));
    for (j = 0; j < 3; j++)
        for (i = 0; i < 3; i++)
            drawTile(blkSel, 124.0f + i * 32, 12.0f + j * 32, 0.3f, 2.0f);

    /* informations */
    drawText(t->name, 232, 10, 0.6f, white);
    snprintf(buf, sizeof buf, "Bloc %d / %d", blkSel + 1, tileCount);
    drawText(buf, 232, 36, 0.45f, gray);
    drawText(t->solid ? "Solide : oui" : "Solide : non", 232, 56, 0.45f,
             t->solid ? yellow : gray);
    drawText(t->tall ? "Rencontres : oui" : "Rencontres : non", 232, 72, 0.45f,
             t->tall ? yellow : gray);

    /* tous les blocs */
    drawText("Tes blocs", 10, 116, 0.45f, gray);
    for (i = 0; i < tileCount; i++) {
        float x = 10.0f + (i % 16) * 23;
        float y = 134.0f + (i / 16) * 26;
        if (i == blkSel)
            C2D_DrawRectSolid(x - 2, y - 2, 0.0f, 20, 20, yellow);
        drawTile(i, x, y, 0.3f, 1.0f);
    }

    drawText("Stylet : dessiner     L / R : autre bloc", 10, 192, 0.42f, gray);
    drawText("Y : réglage d'affichage (si les textures sont fausses)", 10, 207, 0.42f, gray);
    drawText("SELECT : retour", 10, 222, 0.42f, gray);
}

static void drawBlocksBottom(void) {
    const Tile *t = &tiles[blkSel];
    u32 white = COL(255, 255, 255);
    u32 yellow = COL(255, 220, 80);
    u32 grid = C2D_Color32(0, 0, 0, 70);
    Button b;
    int i, x, y;

    /* en-tete : choix du bloc et nom */
    drawButton(&bkPrev, false);
    drawButton(&bkNext, false);
    b = bkName;
    b.label = t->name;
    drawButtonSc(&b, false, 0.55f);

    /* zone de dessin : 16 x 16 pixels */
    C2D_DrawRectSolid(BK_CX - 2, BK_CY - 2, 0.0f, 16 * BK_CELL + 4, 16 * BK_CELL + 4, white);
    for (y = 0; y < 16; y++)
        for (x = 0; x < 16; x++)
            C2D_DrawRectSolid((float)(BK_CX + x * BK_CELL), (float)(BK_CY + y * BK_CELL),
                              0.05f, BK_CELL, BK_CELL, palCol(t->pix[y][x]));
    for (i = 1; i < 16; i++) {
        C2D_DrawRectSolid((float)(BK_CX + i * BK_CELL), BK_CY, 0.1f, 1, 16 * BK_CELL, grid);
        C2D_DrawRectSolid(BK_CX, (float)(BK_CY + i * BK_CELL), 0.1f, 16 * BK_CELL, 1, grid);
    }

    /* palette de couleurs */
    for (i = 0; i < 32; i++) {
        float px = (float)(BK_PX + (i % 8) * BK_PITCH);
        float py = (float)(BK_PY + (i / 8) * BK_PITCH);
        if (i == blkColor)
            C2D_DrawRectSolid(px - 2, py - 2, 0.0f, 20, 20, yellow);
        C2D_DrawRectSolid(px, py, 0.05f, 16, 16, palCol(i));
    }

    /* outils */
    for (i = 0; i < 3; i++)
        drawButtonSc(&bkTools[i], blkTool == i, 0.38f);

    /* proprietes du bloc */
    b = bkSolid;
    b.label = t->solid ? "Solide : oui" : "Solide : non";
    drawButtonSc(&b, t->solid, 0.5f);
    b = bkTall;
    b.label = t->tall ? "Rencontres : oui" : "Rencontres : non";
    drawButtonSc(&b, t->tall, 0.5f);

    /* boutons du bas */
    b = bkNew;
    b.enabled = (tileCount < MAX_TILES);
    drawButtonSc(&b, false, 0.5f);
    b = bkCopy;
    b.enabled = (tileCount < MAX_TILES);
    drawButtonSc(&b, false, 0.5f);
    b = bkDel;
    b.enabled = (tileCount > 1);
    b.label = (blkDelTimer > 0) ? "Sûr ?" : "Suppr.";
    drawButtonSc(&b, blkDelTimer > 0, 0.5f);
    drawButtonSc(&bkBack, false, 0.5f);
}

/* ---------- Dessin : reglage d'affichage ---------- */

static void drawCalibTop(void) {
    u32 white = COL(255, 255, 255);
    u32 gray = COL(170, 170, 190);

    drawTextCX("Réglage d'affichage des textures", TOP_W / 2, 4, 0.6f, white);

    /* a gauche : la reference (toujours juste) ; a droite : le rendu de la carte graphique */
    drawText("Référence", 18, 30, 0.45f, gray);
    C2D_DrawRectSolid(14, 50, 0.0f, 136, 136, COL(70, 70, 85));
    drawTileRects(&testTile, 18, 54, 0.3f, 8.0f);

    drawText("Rendu carte graphique", 214, 30, 0.45f, gray);
    C2D_DrawRectSolid(210, 50, 0.0f, 136, 136, COL(70, 70, 85));
    if (!drawSlotGPU(TEST_SLOT, 214, 54, 0.3f, 8.0f))
        drawText("indisponible", 232, 110, 0.5f, gray);

    drawText("Le mode « Rectangles » marche toujours. Pour essayer le mode", 10, 196, 0.4f, gray);
    drawText("« Carte graphique » : le carré de droite doit être identique à", 10, 210, 0.4f, gray);
    drawText("celui de gauche (touche les réglages jusqu'à y arriver).", 10, 224, 0.4f, gray);
}

static void drawCalibBottom(void) {
    static const char *const names[3] = { "Haut / bas", "Couleurs", "Carrés de 8 x 8" };
    Button b;
    char buf[64];
    int i;

    b = caBtn[0];
    snprintf(buf, sizeof buf, "Mode : %s", texMode ? "Rectangles (sûr)" : "Carte graphique (test)");
    b.label = buf;
    drawButtonSc(&b, texMode == 0, 0.55f);

    for (i = 0; i < 3; i++) {
        int v = (i == 0) ? texFlipV : (i == 1 ? texBswap : texSwapXY);
        char line[48];
        b = caBtn[i + 1];
        snprintf(line, sizeof line, "%s : %s", names[i], v ? "inversé" : "normal");
        b.label = line;
        b.enabled = (texMode == 0);
        drawButtonSc(&b, v != 0 && texMode == 0, 0.5f);
    }
    drawButton(&caOk, false);
}

/* ---------- Dessin : jeu ---------- */

static void drawMapTop(void) {
    int tx0 = camX / TS;
    int ty0 = camY / TS;
    int tx1 = (camX + TOP_W - 1) / TS;
    int ty1 = (camY + TOP_H - 1) / TS;
    int tx, ty;

    if (tx1 >= MAP_W) tx1 = MAP_W - 1;
    if (ty1 >= MAP_H) ty1 = MAP_H - 1;

    for (ty = ty0; ty <= ty1; ty++)
        for (tx = tx0; tx <= tx1; tx++)
            drawTile(map[ty][tx], (float)(tx * TS - camX), (float)(ty * TS - camY), 0.0f, 1.0f);
}

/* ---------- Dessin : editeur de monstres ---------- */

static u32 monColor(int colorIdx, bool dark) {
    int r = COLOR_RGB[colorIdx][0];
    int g = COLOR_RGB[colorIdx][1];
    int b = COLOR_RGB[colorIdx][2];

    if (dark) {
        r = r * 55 / 100;
        g = g * 55 / 100;
        b = b * 55 / 100;
    }
    return COL(r, g, b);
}

/* dessine un monstre : px = taille d'un "pixel" du dessin (16 x 16) */
static void drawMonsterArt(int shape, int colorIdx, float x, float y, float px) {
    u32 body = monColor(colorIdx, false);
    u32 dark = monColor(colorIdx, true);
    int r, c, start;

    for (r = 0; r < 16; r++) {
        const char *row = SHAPES[shape][r];
        c = 0;
        while (c < 16) {
            char ch = row[c];
            u32 col;

            start = c;
            while (c < 16 && row[c] == ch)
                c++;
            if (ch == '.')
                continue;

            if (ch == '#')      col = body;
            else if (ch == 'd') col = dark;
            else if (ch == 'w') col = COL(255, 255, 255);
            else                col = COL(10, 10, 20);
            C2D_DrawRectSolid(x + start * px, y + r * px, 0.3f,
                              (c - start) * px, px, col);
        }
    }
}

static void drawBar(float x, float y, float w, float h, int v, int cap, u32 col) {
    v = clampInt(v, 0, cap);
    C2D_DrawRectSolid(x, y, 0.0f, w, h, COL(25, 30, 55));
    C2D_DrawRectSolid(x, y, 0.05f, w * (float)v / (float)cap, h, col);
}

static void attackInfo(const Attack *a, char *buf, size_t n) {
    if (a->kind == K_HEAL)
        snprintf(buf, n, "Soin %d%% - Préc. %d%%", a->power, a->acc);
    else
        snprintf(buf, n, "Dégâts %d - Préc. %d%%", a->power, a->acc);
}

static const char *const STAT_LABELS[ST_COUNT] = { "PV", "Attaque", "Défense", "Vitesse" };

static u32 statColor(int s) {
    static const u8 c[ST_COUNT][3] = { { 220, 70, 70 }, { 240, 150, 50 },
                                       { 80, 160, 230 }, { 100, 200, 110 } };
    return COL(c[s][0], c[s][1], c[s][2]);
}

static void drawMonstersTop(void) {
    const Monster *m = &monsters[formIdx[FM_MONSTERS]];
    const int caps[ST_COUNT] = { 300, 100, 100, 100 };
    u32 white = COL(255, 255, 255);
    u32 gray = COL(170, 170, 190);
    u32 yellow = COL(255, 220, 80);
    char buf[64];
    int i;

    /* apercu */
    C2D_DrawRectSolid(10, 10, 0.0f, 130, 130, COL(35, 45, 80));
    drawMonsterArt(m->shape, m->color, 27, 27, 6);

    /* nom, niveau, classe */
    drawText(m->name, 152, 10, 0.8f, white);
    snprintf(buf, sizeof buf, "Niv. %d - %s", m->level, classes[m->cls].name);
    drawText(buf, 152, 38, 0.5f, yellow);
    snprintf(buf, sizeof buf, "%s - %s", SHAPE_NAMES[m->shape], COLOR_NAMES[m->color]);
    drawText(buf, 152, 54, 0.4f, gray);
    if (m->drop >= 0 && m->drop < itemCount)
        snprintf(buf, sizeof buf, "Butin : %s (%d %%)", items[m->drop].name, m->rate);
    else
        snprintf(buf, sizeof buf, "Butin : aucun");
    drawText(buf, 152, 64, 0.4f, gray);

    /* barres de stats (au niveau du monstre) */
    for (i = 0; i < ST_COUNT; i++) {
        float y = 76.0f + i * 16;
        int v = monStat(m, i, m->level);

        drawText(STAT_LABELS[i], 152, y - 2, 0.42f, white);
        drawBar(212, y, 150, 11, v, caps[i], statColor(i));
        snprintf(buf, sizeof buf, "%d", v);
        drawText(buf, 368, y - 2, 0.42f, white);
    }

    /* tous les monstres */
    drawText("Tes monstres", 10, 146, 0.45f, gray);
    for (i = 0; i < monsterCount; i++) {
        float x = 10.0f + (i % 16) * 23;
        float y = 166.0f + (i / 16) * 26;
        if (i == formIdx[FM_MONSTERS])
            C2D_DrawRectSolid(x - 2, y - 2, 0.0f, 20, 20, yellow);
        drawMonsterArt(monsters[i].shape, monsters[i].color, x, y, 1);
    }

    drawText("L / R : monstre précédent / suivant", 10, 222, 0.4f, gray);
}

static void drawAttackList(const Class *c, float x, float y) {
    char buf[64];
    int k;

    for (k = 0; k < 4; k++) {
        const Attack *a = &attacks[c->atk[k]];

        drawText(a->name, x, y + k * 15, 0.45f, COL(255, 255, 255));
        attackInfo(a, buf, sizeof buf);
        drawText(buf, x + 120, y + k * 15 + 1, 0.4f, COL(170, 170, 190));
    }
}

static void drawClassesTop(void) {
    const Class *c = &classes[formIdx[FM_CLASSES]];
    static const int lvls[4] = { 1, 10, 25, 50 };
    u32 white = COL(255, 255, 255);
    u32 gray = COL(170, 170, 190);
    u32 yellow = COL(255, 220, 80);
    char buf[32];
    int s, k;

    drawText(c->name, 10, 8, 0.8f, white);
    drawText("Stats selon le niveau", 10, 36, 0.4f, gray);
    for (k = 0; k < 4; k++) {
        snprintf(buf, sizeof buf, "Niv. %d", lvls[k]);
        drawText(buf, 120.0f + k * 66, 36, 0.4f, yellow);
    }
    for (s = 0; s < ST_COUNT; s++) {
        float y = 54.0f + s * 18;

        drawText(STAT_LABELS[s], 10, y, 0.5f, statColor(s));
        for (k = 0; k < 4; k++) {
            snprintf(buf, sizeof buf, "%d", classStat(c, s, lvls[k]));
            drawText(buf, 120.0f + k * 66, y, 0.5f, white);
        }
    }

    drawText("Attaques", 10, 136, 0.5f, yellow);
    drawAttackList(c, 10, 156);
    drawText("L / R : classe précédente / suivante", 10, 222, 0.4f, gray);
}

static void drawAttacksTop(void) {
    const Attack *a = &attacks[formIdx[FM_ATTACKS]];
    u32 white = COL(255, 255, 255);
    u32 gray = COL(170, 170, 190);
    u32 yellow = COL(255, 220, 80);
    char buf[128];
    size_t len = 0;
    int i, k, idx = formIdx[FM_ATTACKS];

    drawText(a->name, 10, 8, 0.8f, white);
    drawText(a->kind == K_HEAL ? "Type : Soin (en % des PV max)" : "Type : Dégâts",
             10, 40, 0.5f, yellow);

    drawText("Puissance", 10, 70, 0.5f, white);
    drawBar(110, 72, 200, 12, a->power, 99, a->kind == K_HEAL ? COL(80, 190, 110) : COL(220, 90, 70));
    snprintf(buf, sizeof buf, "%d", a->power);
    drawText(buf, 320, 70, 0.5f, white);

    drawText("Précision", 10, 96, 0.5f, white);
    drawBar(110, 98, 200, 12, a->acc, 100, COL(80, 160, 230));
    snprintf(buf, sizeof buf, "%d %%", a->acc);
    drawText(buf, 320, 96, 0.5f, white);

    /* classes qui possedent cette attaque */
    buf[0] = '\0';
    for (i = 0; i < classCount; i++) {
        bool has = false;

        for (k = 0; k < 4; k++)
            if (classes[i].atk[k] == idx)
                has = true;
        if (has && len < 60) {
            len += (size_t)snprintf(buf + len, sizeof buf - len, "%s%s",
                                    len > 0 ? ", " : "", classes[i].name);
        }
    }
    drawText("Utilisée par :", 10, 130, 0.5f, yellow);
    drawText(buf[0] ? buf : "(aucune classe)", 10, 150, 0.5f, white);

    drawText("L / R : attaque précédente / suivante", 10, 222, 0.4f, gray);
}

static void drawHeroTop(void) {
    const Class *c = &classes[hero.cls];
    const int caps[ST_COUNT] = { 300, 100, 100, 100 };
    u32 white = COL(255, 255, 255);
    u32 gray = COL(170, 170, 190);
    u32 yellow = COL(255, 220, 80);
    char buf[64];
    int s;

    drawText("Héros", 10, 8, 0.8f, white);
    snprintf(buf, sizeof buf, "%s - niveau %d", c->name, hero.level);
    drawText(buf, 10, 38, 0.55f, yellow);

    for (s = 0; s < ST_COUNT; s++) {
        float y = 66.0f + s * 18;
        int v = heroStat(s);

        drawText(STAT_LABELS[s], 10, y - 2, 0.45f, white);
        drawBar(80, y, 240, 12, v, caps[s], statColor(s));
        snprintf(buf, sizeof buf, "%d", v);
        drawText(buf, 328, y - 2, 0.45f, white);
    }

    snprintf(buf, sizeof buf, "Arme : %s    Armure : %s",
             hero.weapon >= 0 ? items[hero.weapon].name : "aucune",
             hero.armor >= 0 ? items[hero.armor].name : "aucune");
    drawText(buf, 10, 135, 0.42f, gray);

    drawText("Attaques", 10, 149, 0.45f, yellow);
    drawAttackList(c, 10, 165);
    drawText("Changer la classe ou le niveau remet l'XP à zéro", 10, 227, 0.38f, gray);
}

/* resume court des bonus d'un objet : "PV+5 DEF+2" */
static void itemSummary(const Item *it, char *buf, size_t n) {
    static const char *const tag[ST_COUNT] = { "PV", "ATK", "DEF", "VIT" };
    size_t len = 0;
    int s;

    buf[0] = '\0';
    if (it->kind == IK_POTION) {
        snprintf(buf, n, "Soigne %d PV", it->bonus[ST_HP]);
        return;
    }
    for (s = 0; s < ST_COUNT; s++) {
        if (it->bonus[s] > 0 && len < n - 12)
            len += (size_t)snprintf(buf + len, n - len, "%s%s+%d", len > 0 ? " " : "",
                                    tag[s], it->bonus[s]);
    }
    if (len == 0)
        snprintf(buf, n, "Aucun bonus");
}

static void drawItemsTop(void) {
    const Item *it = &items[formIdx[FM_ITEMS]];
    const int caps[ST_COUNT] = { 100, 25, 25, 25 };
    u32 white = COL(255, 255, 255);
    u32 gray = COL(170, 170, 190);
    u32 yellow = COL(255, 220, 80);
    char buf[64];
    int s, row = 0;

    drawText(it->name, 10, 8, 0.8f, white);
    drawText(ITEM_KIND_NAMES[it->kind], 10, 40, 0.55f, yellow);

    for (s = 0; s < ST_COUNT; s++) {
        float y = 76.0f + row * 22;

        if (it->kind == IK_POTION && s != ST_HP)
            continue;
        drawText(it->kind == IK_POTION ? "PV rendus" : STAT_LABELS[s], 10, y - 2, 0.5f, white);
        drawBar(110, y, 200, 13, it->bonus[s], caps[s], statColor(s));
        snprintf(buf, sizeof buf, "+%d", it->bonus[s]);
        drawText(buf, 320, y - 2, 0.5f, white);
        row++;
    }

    snprintf(buf, sizeof buf, "Niveau requis : %d", it->req);
    drawText(buf, 10, 176, 0.5f, it->req > hero.level ? COL(230, 100, 100) : gray);
    snprintf(buf, sizeof buf, "Possédés : %d", hero.inv[formIdx[FM_ITEMS]]);
    drawText(buf, 10, 194, 0.5f, gray);
    drawText("L / R : objet précédent / suivant", 10, 222, 0.4f, gray);
}

static void drawFormTop(void) {
    switch (formKind) {
    case FM_ITEMS:    drawItemsTop(); break;
    case FM_MONSTERS: drawMonstersTop(); break;
    case FM_CLASSES:  drawClassesTop(); break;
    case FM_ATTACKS:  drawAttacksTop(); break;
    default:          drawHeroTop(); break;
    }
}

static void drawFormBottom(void) {
    int i = formIdx[formKind];
    int n = formCount();
    int r, per = FORM_PERPAGE[formKind];
    static char pageLabel[16];
    u32 white = COL(255, 255, 255);
    u32 gray = COL(170, 170, 190);
    char buf[64];
    Button b;

    /* en-tete : choix de l'element */
    b = fmPrev; b.enabled = (n > 1); drawButton(&b, false);
    b = fmNext; b.enabled = (n > 1); drawButton(&b, false);
    if (formKind == FM_HERO)
        snprintf(buf, sizeof buf, "Héros");
    else
        snprintf(buf, sizeof buf, "%s %d / %d", FORM_TITLES[formKind], i + 1, n);
    drawTextInRect(buf, 66, 4, 188, 26, 0.55f, white);

    /* nom + page */
    if (formName(i) != NULL) {
        drawText("Nom", 10, 39, 0.5f, gray);
        b = fmNameBtn();
        b.label = formName(i);
        drawButton(&b, false);
    }
    if (formPages() > 1) {
        snprintf(pageLabel, sizeof pageLabel, "Page %d/%d", formPage + 1, formPages());
        b = fmPageBtn();
        b.label = pageLabel;
        drawButton(&b, false);
    }

    /* champs de la page */
    for (r = 0; r < per; r++) {
        int f = formPage * per + r;
        float y = FM_ROW_Y(r);

        if (f >= FORM_FIELDS[formKind])
            break;

        C2D_DrawRectSolid(6, y, 0.0f, 306, 22, (r % 2) ? COL(40, 40, 55) : COL(48, 48, 66));
        drawText(FORM_LABELS[formKind][f], 10, y + 3, 0.5f, gray);

        formText(i, f, buf, sizeof buf);
        drawTextInRect(buf, 84, y, 124, 22, 0.5f, white);

        if (formKind == FM_MONSTERS && f == 4) {
            C2D_DrawRectSolid(186, y + 3, 0.1f, 16, 16, COL(255, 255, 255));
            C2D_DrawRectSolid(187, y + 4, 0.12f, 14, 14, monColor(monsters[i].color, false));
        }

        b = rowButton(r, false);
        drawButton(&b, false);
        b = rowButton(r, true);
        drawButton(&b, false);
    }

    /* boutons du bas */
    if (formKind != FM_HERO) {
        b = fmNew;
        b.enabled = formCanAdd();
        drawButton(&b, false);

        b = fmDel;
        b.enabled = formCanDel();
        b.label = (delTimer > 0) ? "Sûr ?" : "Suppr.";
        drawButton(&b, delTimer > 0);
    }
    drawButton(&fmBack, false);
}

/* ---------- Dessin : menu des editeurs ---------- */

static void drawEdMenuTop(void) {
    static const char *const desc[N_ED_BUTTONS] = {
        "Dessine la carte au stylet avec tes blocs.",
        "Crée des blocs et dessine leurs textures pixel par pixel.",
        "Crée et modifie les monstres : stats, forme, classe, butin.",
        "Crée des classes : stats de départ, gain par niveau, 4 attaques.",
        "Crée des attaques : dégâts ou soin, puissance, précision.",
        "Crée des armes, armures et potions : bonus, niveau requis.",
        "Choisis la classe et le niveau de ton héros.",
        "Retour à l'écran titre.",
    };
    char buf[64];

    drawTextCX("Éditeurs", TOP_W / 2, 30, 1.3f, COL(255, 255, 255));
    drawTextCX(desc[edSel], TOP_W / 2, 110, 0.5f, COL(200, 210, 255));

    snprintf(buf, sizeof buf, "%d monstres - %d classes - %d attaques - %d objets",
             monsterCount, classCount, attackCount, itemCount);
    drawTextCX(buf, TOP_W / 2, 150, 0.45f, COL(150, 160, 190));
    drawTextCX("Y : réglage d'affichage des textures", TOP_W / 2, 200, 0.42f, COL(120, 130, 160));
}

static void drawEdMenuBottom(void) {
    int i;

    for (i = 0; i < N_ED_BUTTONS; i++)
        drawButton(&edButtons[i], i == edSel);
}

/* ---------- Dessin : combat ---------- */

static void drawBattleTop(void) {
    const Monster *m = &monsters[bt.mon];
    u32 white = COL(255, 255, 255);
    u32 gray = COL(170, 170, 190);
    u32 box = C2D_Color32(0, 0, 0, 170);
    u32 hpCol;
    char buf[64];
    int mMax = bt.mStat[ST_HP];

    /* decor */
    C2D_DrawRectSolid(0, 0, 0.0f, TOP_W, 120, COL(120, 175, 230));
    C2D_DrawRectSolid(0, 120, 0.0f, TOP_W, 120, COL(76, 160, 64));

    /* monstre */
    C2D_DrawRectSolid(262, 122, 0.25f, 112, 10, C2D_Color32(0, 0, 0, 70));
    drawMonsterArt(m->shape, m->color, 254, 6, 8);

    /* heros */
    C2D_DrawRectSolid(56, 150, 0.25f, 52, 8, C2D_Color32(0, 0, 0, 70));
    C2D_DrawRectSolid(62, 112, 0.3f, 40, 40, COL(20, 20, 60));
    C2D_DrawRectSolid(65, 115, 0.31f, 34, 34, COL(220, 60, 60));
    C2D_DrawRectSolid(88, 134, 0.32f, 8, 8, COL(255, 255, 255));

    /* infos du monstre */
    C2D_DrawRectSolid(10, 10, 0.4f, 176, 52, box);
    drawText(m->name, 18, 14, 0.55f, white);
    snprintf(buf, sizeof buf, "Niv. %d", bt.mLevel);
    drawText(buf, 140, 16, 0.45f, COL(255, 220, 80));
    hpCol = (bt.mHp * 100 > mMax * 50) ? COL(80, 200, 90)
          : ((bt.mHp * 100 > mMax * 20) ? COL(240, 200, 60) : COL(220, 70, 70));
    drawBar(18, 38, 160, 10, bt.mHp, mMax, hpCol);
    snprintf(buf, sizeof buf, "PV %d / %d", bt.mHp, mMax);
    drawText(buf, 18, 48, 0.38f, gray);

    /* infos du heros */
    C2D_DrawRectSolid(206, 138, 0.4f, 184, 52, box);
    snprintf(buf, sizeof buf, "%s", classes[hero.cls].name);
    drawText(buf, 214, 142, 0.55f, white);
    snprintf(buf, sizeof buf, "Niv. %d", hero.level);
    drawText(buf, 344, 144, 0.45f, COL(255, 220, 80));
    hpCol = (bt.pHp * 100 > bt.pMax * 50) ? COL(80, 200, 90)
          : ((bt.pHp * 100 > bt.pMax * 20) ? COL(240, 200, 60) : COL(220, 70, 70));
    drawBar(214, 164, 168, 10, bt.pHp, bt.pMax, hpCol);
    snprintf(buf, sizeof buf, "PV %d / %d", bt.pHp, bt.pMax);
    drawText(buf, 214, 174, 0.38f, gray);

    /* zone de message */
    C2D_DrawRectSolid(0, 196, 0.4f, TOP_W, 44, COL(15, 20, 40));
    C2D_DrawRectSolid(0, 196, 0.41f, TOP_W, 2, white);
    if (bt.phase == BP_MSG && bt.cur < bt.nMsgs)
        drawTextCX(bt.msgs[bt.cur].text, TOP_W / 2, 210, 0.6f, white);
    else
        drawTextCX("Que fais-tu ?", TOP_W / 2, 210, 0.6f, white);
}

static void drawBattleBottom(void) {
    u32 white = COL(255, 255, 255);
    u32 gray = COL(170, 170, 190);
    char buf[64], info[48];
    int i;

    snprintf(buf, sizeof buf, "%s  niv. %d", classes[hero.cls].name, hero.level);
    drawText(buf, 10, 8, 0.55f, white);
    snprintf(buf, sizeof buf, "PV %d / %d", bt.pHp, bt.pMax);
    drawText(buf, 10, 28, 0.5f, white);
    snprintf(buf, sizeof buf, "XP %d / %d", hero.xp, xpNeed(hero.level));
    drawText(buf, 10, 44, 0.45f, gray);

    if (bt.phase == BP_MSG) {
        drawTextCX("Touche l'écran ou A", BOT_W / 2, 110, 0.55f, gray);
        drawTextCX("pour continuer", BOT_W / 2, 132, 0.55f, gray);
        return;
    }

    for (i = 0; i < 4; i++) {
        const Attack *a = &attacks[classes[hero.cls].atk[i]];
        const Button *b = &batBtn[i];

        C2D_DrawRectSolid(b->x - 3, b->y - 3, 0.0f, b->w + 6, b->h + 6, COL(20, 20, 30));
        C2D_DrawRectSolid(b->x, b->y, 0.1f, b->w, b->h,
                          a->kind == K_HEAL ? COL(50, 130, 90) : COL(60, 90, 160));
        drawText(a->name, b->x + 8, b->y + 5, 0.55f, white);
        attackInfo(a, info, sizeof info);
        drawText(info, b->x + 8, b->y + 27, 0.4f, COL(215, 220, 240));
    }
    drawButton(&batFlee, false);

    {
        Button pb = batPotion;
        char pl[24];

        snprintf(pl, sizeof pl, "Potion x%d", potionCount());
        pb.label = pl;
        pb.enabled = (potionCount() > 0 && bt.pHp < bt.pMax);
        drawButton(&pb, false);
    }
}

/* ---------- Dessin : inventaire ---------- */

/* stats du heros si on equipait (ou retirait) l'objet idx */
static int previewStat(int s, int idx) {
    int w = hero.weapon, a = hero.armor;

    if (idx >= 0 && items[idx].kind == IK_WEAPON)
        w = (w == idx) ? -1 : idx;
    else if (idx >= 0 && items[idx].kind == IK_ARMOR)
        a = (a == idx) ? -1 : idx;
    return heroStatEq(s, w, a);
}

static void drawInvTop(void) {
    int idx = (invN > 0) ? invList[invSel] : -1;
    u32 white = COL(255, 255, 255);
    u32 gray = COL(170, 170, 190);
    u32 yellow = COL(255, 220, 80);
    char buf[96];
    int s, maxHp = heroStat(ST_HP);

    drawText("Inventaire", 10, 6, 0.8f, white);
    snprintf(buf, sizeof buf, "%s niv. %d", classes[hero.cls].name, hero.level);
    drawText(buf, 10, 36, 0.5f, yellow);
    drawBar(10, 56, 190, 10, hero.hp, maxHp, COL(80, 200, 90));
    snprintf(buf, sizeof buf, "PV %d / %d", hero.hp, maxHp);
    drawText(buf, 10, 67, 0.4f, gray);

    snprintf(buf, sizeof buf, "Arme : %s", hero.weapon >= 0 ? items[hero.weapon].name : "aucune");
    drawText(buf, 10, 88, 0.45f, white);
    snprintf(buf, sizeof buf, "Armure : %s", hero.armor >= 0 ? items[hero.armor].name : "aucune");
    drawText(buf, 10, 104, 0.45f, white);

    /* stats, avec l'effet de l'objet choisi */
    for (s = 0; s < ST_COUNT; s++) {
        float y = 130.0f + s * 18;
        int cur = heroStat(s);
        int pre = previewStat(s, idx);

        drawText(STAT_LABELS[s], 10, y, 0.48f, statColor(s));
        snprintf(buf, sizeof buf, "%d", cur);
        drawText(buf, 80, y, 0.48f, white);
        if (idx >= 0 && items[idx].kind != IK_POTION && pre != cur) {
            snprintf(buf, sizeof buf, "-> %d (%+d)", pre, pre - cur);
            drawText(buf, 112, y, 0.48f, pre > cur ? COL(110, 220, 120) : COL(230, 100, 100));
        }
    }

    /* fiche de l'objet choisi */
    C2D_DrawRectSolid(214, 8, 0.0f, 180, 168, COL(30, 38, 70));
    if (idx >= 0) {
        const Item *it = &items[idx];
        float y = 66;

        drawText(it->name, 222, 14, 0.55f, white);
        drawText(ITEM_KIND_NAMES[it->kind], 222, 36, 0.42f, yellow);
        if (it->kind == IK_POTION) {
            snprintf(buf, sizeof buf, "Rend %d PV", it->bonus[ST_HP]);
            drawText(buf, 222, y, 0.45f, white);
        } else {
            for (s = 0; s < ST_COUNT; s++) {
                if (it->bonus[s] > 0) {
                    snprintf(buf, sizeof buf, "%s +%d", STAT_LABELS[s], it->bonus[s]);
                    drawText(buf, 222, y, 0.45f, statColor(s));
                    y += 16;
                }
            }
            snprintf(buf, sizeof buf, "Niveau requis : %d", it->req);
            drawText(buf, 222, 144, 0.42f, hero.level < it->req ? COL(230, 100, 100) : gray);
        }
        snprintf(buf, sizeof buf, "Possédés : %d", hero.inv[idx]);
        drawText(buf, 222, 158, 0.42f, gray);
    } else {
        drawText("Ton sac est vide.", 222, 14, 0.45f, gray);
        drawText("Bats des monstres", 222, 40, 0.42f, gray);
        drawText("pour trouver du butin !", 222, 54, 0.42f, gray);
    }

    if (invMsgTimer > 0) {
        C2D_DrawRectSolid(0, 204, 0.4f, TOP_W, 28, C2D_Color32(0, 0, 0, 170));
        drawTextCX(invMsg, TOP_W / 2, 209, 0.6f, yellow);
    }
}

static void drawInvBottom(void) {
    u32 white = COL(255, 255, 255);
    u32 gray = COL(170, 170, 190);
    char buf[64];
    int r;
    Button b;

    drawText("Sac", 10, 6, 0.6f, white);
    snprintf(buf, sizeof buf, "%d type(s) d'objets", invN);
    drawText(buf, 80, 10, 0.4f, gray);

    for (r = 0; r < INV_ROWS; r++) {
        int li = invScroll + r;
        Button row = invRow(r);

        if (li >= invN)
            break;
        {
            int idx = invList[li];
            const Item *it = &items[idx];
            bool eq = (hero.weapon == idx || hero.armor == idx);
            bool chosen = (li == invSel);

            C2D_DrawRectSolid(row.x - (chosen ? 2 : 0), row.y - (chosen ? 2 : 0), 0.0f,
                              row.w + (chosen ? 4 : 0), row.h + (chosen ? 4 : 0),
                              chosen ? COL(255, 220, 80) : COL(20, 20, 30));
            C2D_DrawRectSolid(row.x, row.y, 0.1f, row.w, row.h,
                              it->kind == IK_POTION ? COL(50, 100, 80) : COL(55, 70, 130));
            if (eq) {
                C2D_DrawRectSolid(row.x + 4, row.y + 5, 0.15f, 14, 14, COL(255, 220, 80));
                drawText("E", row.x + 8, row.y + 5, 0.45f, COL(20, 20, 30));
            }
            drawText(it->name, row.x + 24, row.y + 4, 0.5f, white);
            itemSummary(it, buf, sizeof buf);
            drawText(buf, row.x + 128, row.y + 7, 0.36f, COL(215, 220, 240));
            snprintf(buf, sizeof buf, "x%d", hero.inv[idx]);
            drawText(buf, row.x + 246, row.y + 4, 0.5f, white);
        }
    }

    b = invUp;   b.enabled = (invScroll > 0);                 drawButton(&b, false);
    b = invDown; b.enabled = (invScroll + INV_ROWS < invN);   drawButton(&b, false);

    b = invAct;
    if (invN == 0) {
        b.enabled = false;
    } else {
        int idx = invList[invSel];
        if (items[idx].kind == IK_POTION)
            b.label = "Utiliser";
        else if (hero.weapon == idx || hero.armor == idx)
            b.label = "Retirer";
        else
            b.label = "Équiper";
    }
    drawButton(&b, false);
    drawButton(&invBack, false);
}

/* ---------- Programme principal ---------- */

int main(int argc, char **argv) {
    C3D_RenderTarget *top, *bot;

    (void)argc;
    (void)argv;

    gfxInitDefault();
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS * 2);
    C2D_Prepare();

    top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    bot = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
    g_textBuf = C2D_TextBufNew(4096);

    initTiles();
    buildMap();
    loadDisplay();
    loadTiles();
    mapClampTiles();
    atlasInit();
    loadAll();

    while (running && aptMainLoop()) {
        u32 kDown, kHeld;

        dbgTick();

        hidScanInput();
        kDown = hidKeysDown();
        kHeld = hidKeysHeld();

        if (kDown & KEY_START) {
            if (state == S_EDIT)
                saveMap();
            else if (state == S_FORM)
                saveAll();
            else if (state == S_BLOCKS) {
                saveTiles();
                saveMap();
            } else if (state == S_CALIB)
                saveDisplay();
            break;
        }

        if (state == S_TITLE)
            updateTitle(kDown);
        else if (state == S_PLAY)
            updatePlay(kDown, kHeld);
        else if (state == S_FORM)
            updateForm(kDown, kHeld);
        else if (state == S_EDMENU)
            updateEdMenu(kDown);
        else if (state == S_BATTLE)
            updateBattle(kDown);
        else if (state == S_INV)
            updateInventory(kDown);
        else if (state == S_BLOCKS)
            updateBlocks(kDown, kHeld);
        else if (state == S_CALIB)
            updateCalib(kDown);
        else
            updateEdit(kDown, kHeld);

        if (atlasDirty) {
            atlasFlush();
            atlasDirty = false;
        }
        rectBudget = TILE_RECT_BUDGET;

        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        C2D_TextBufClear(g_textBuf);

        if (state == S_TITLE) {
            C2D_TargetClear(top, COL(18, 24, 48));
            C2D_SceneBegin(top);
            drawTitleTop();

            C2D_TargetClear(bot, COL(30, 30, 40));
            C2D_SceneBegin(bot);
            drawTitleBottom();
        } else if (state == S_PLAY) {
            C2D_TargetClear(top, COL(0, 0, 0));
            C2D_SceneBegin(top);
            drawMapTop();
            drawWildTop();
            drawPlayerTop();
            drawDebugOverlay();

            C2D_TargetClear(bot, COL(30, 30, 40));
            C2D_SceneBegin(bot);
            drawPlayBottom();
        } else if (state == S_FORM) {
            C2D_TargetClear(top, COL(18, 24, 48));
            C2D_SceneBegin(top);
            drawFormTop();

            C2D_TargetClear(bot, COL(30, 30, 40));
            C2D_SceneBegin(bot);
            drawFormBottom();
        } else if (state == S_EDMENU) {
            C2D_TargetClear(top, COL(18, 24, 48));
            C2D_SceneBegin(top);
            drawEdMenuTop();

            C2D_TargetClear(bot, COL(30, 30, 40));
            C2D_SceneBegin(bot);
            drawEdMenuBottom();
        } else if (state == S_BATTLE) {
            C2D_TargetClear(top, COL(0, 0, 0));
            C2D_SceneBegin(top);
            drawBattleTop();

            C2D_TargetClear(bot, COL(30, 30, 40));
            C2D_SceneBegin(bot);
            drawBattleBottom();
        } else if (state == S_INV) {
            C2D_TargetClear(top, COL(18, 24, 48));
            C2D_SceneBegin(top);
            drawInvTop();

            C2D_TargetClear(bot, COL(30, 30, 40));
            C2D_SceneBegin(bot);
            drawInvBottom();
        } else if (state == S_BLOCKS) {
            C2D_TargetClear(top, COL(18, 24, 48));
            C2D_SceneBegin(top);
            drawBlocksTop();

            C2D_TargetClear(bot, COL(30, 30, 40));
            C2D_SceneBegin(bot);
            drawBlocksBottom();
        } else if (state == S_CALIB) {
            C2D_TargetClear(top, COL(18, 24, 48));
            C2D_SceneBegin(top);
            drawCalibTop();

            C2D_TargetClear(bot, COL(30, 30, 40));
            C2D_SceneBegin(bot);
            drawCalibBottom();
        } else {
            C2D_TargetClear(top, COL(18, 24, 48));
            C2D_SceneBegin(top);
            drawEditTop();

            C2D_TargetClear(bot, COL(0, 0, 0));
            C2D_SceneBegin(bot);
            drawEditBottom();
        }

        C3D_FrameEnd(0);
    }

    C2D_TextBufDelete(g_textBuf);
    C2D_Fini();
    C3D_Fini();
    gfxExit();
    return 0;
}
