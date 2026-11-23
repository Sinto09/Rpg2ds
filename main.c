/*
 * RPG 2DS - Etape 7 : blocs personnalises, zones et teleporteurs
 *
 *  - Ecran titre : Jouer / Editeurs / Quitter
 *  - Menu Editeurs : Carte, Monstres, Classes, Attaques, Objets, Heros
 *  - En jeu : X (ou bouton Sac) ouvre l'inventaire ; les monstres laissent du butin
 *  - Dans les hautes herbes, des monstres sauvages apparaissent : contact = combat
 *  - Jeu : carte vue de dessus, deplacement (croix/stick, B = courir)
 *  - Editeur : ecran du bas = zone de dessin (stylet), ecran du haut = carte entiere
 *  - Editeur de blocs : on redessine les textures et on cree ses propres blocs
 *  - Plusieurs zones (map1.bin, map2.bin...), agrandissables ; teleporteurs invisibles en jeu
 *  - Les zones sont sauvegardees sur la carte SD : sdmc:/3ds/rpg2ds/mapN.bin
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

/* ---------- Blocs (tuiles), palette et textures ---------- */

#define PAL_N         32
#define MAX_TILES     32
#define BASE_TILES    8      /* blocs d'origine : on peut les redessiner mais pas les supprimer */
#define MAX_RUNS      256    /* rectangles de detail par bloc (maximum possible : 16 x 16) */
#define DETAIL_BUDGET 6000   /* rectangles de detail dessines au maximum par image */
#define TILE_FILE     "sdmc:/3ds/rpg2ds/tiles.bin"
#define TILE_REC      (NAME_BYTES + 2 + TS * TS)

enum { T_GRASS, T_WALL, T_WATER, T_TREE, T_PATH, T_FLOOR, T_DOOR, T_TALL };

enum {
    P_BLACK, P_DGRAY, P_GRAY, P_LGRAY, P_WHITE,
    P_DGREEN, P_GREEN, P_GRASS, P_LGREEN, P_PGREEN,
    P_DBLUE, P_BLUE, P_LBLUE, P_PBLUE,
    P_DBROWN, P_BROWN, P_FLOOR, P_SAND, P_PSAND, P_VDBROWN,
    P_DRED, P_RED, P_SALMON, P_ORANGE, P_YELLOW, P_PYELLOW,
    P_PURPLE, P_LPURPLE, P_PINK, P_TEAL, P_LTEAL, P_SKIN
};

static const u8 PAL_RGB[PAL_N][3] = {
    {   0,   0,   0 }, {  60,  60,  70 }, { 120, 120, 130 }, { 180, 180, 190 }, { 255, 255, 255 },
    {  30, 100,  40 }, {  52, 135,  52 }, {  76, 160,  64 }, { 110, 190,  90 }, { 170, 220, 120 },
    {  30,  70, 140 }, {  50, 110, 200 }, {  90, 160, 230 }, { 170, 210, 250 },
    { 110,  70,  30 }, { 140,  95,  50 }, { 170, 130,  90 }, { 200, 170, 110 }, { 225, 205, 150 },
    {  90,  50,  20 },
    { 150,  30,  30 }, { 220,  60,  60 }, { 240, 140, 120 }, { 230, 120,  40 }, { 250, 200,  60 },
    { 255, 235, 150 },
    { 110,  50, 150 }, { 170, 100, 210 }, { 230, 120, 190 }, {  20, 130, 130 }, {  80, 200, 190 },
    { 240, 200, 160 },
};

static u32 palCol[PAL_N];

/* un rectangle de couleur uni dans un bloc (dessin rapide) */
typedef struct { u8 x, y, w, h, c; } Run;

typedef struct {
    char name[NAME_BYTES];
    bool solid;        /* bloque le joueur */
    bool wild;         /* des monstres sauvages y apparaissent */
    u8 px[TS][TS];     /* texture : numeros de couleur de la palette */
    u8 base;           /* couleur dominante (fond du bloc, couleur des mini-cartes) */
    int nRuns;
    Run runs[MAX_RUNS];
} Tile;

static Tile tiles[MAX_TILES];
static int tileCount = BASE_TILES;
static int rectBudget = DETAIL_BUDGET;   /* se remet a zero a chaque image */

/* transforme la texture en une couleur de fond + quelques rectangles */
static void compileTile(Tile *t) {
    static bool used[TS][TS];
    int cnt[PAL_N];
    int x, y, i, best = 0;

    memset(cnt, 0, sizeof cnt);
    for (y = 0; y < TS; y++) {
        for (x = 0; x < TS; x++) {
            if (t->px[y][x] >= PAL_N)
                t->px[y][x] = P_BLACK;
            cnt[t->px[y][x]]++;
        }
    }
    for (i = 1; i < PAL_N; i++)
        if (cnt[i] > cnt[best])
            best = i;
    t->base = (u8)best;

    memset(used, 0, sizeof used);
    t->nRuns = 0;
    for (y = 0; y < TS; y++) {
        for (x = 0; x < TS; x++) {
            int c = t->px[y][x], w = 1, h = 1, xx, yy;
            Run *r;

            if (c == best || used[y][x])
                continue;
            while (x + w < TS && t->px[y][x + w] == c && !used[y][x + w])
                w++;
            for (;;) {
                bool ok = (y + h < TS);
                for (xx = x; ok && xx < x + w; xx++)
                    if (t->px[y + h][xx] != c || used[y + h][xx])
                        ok = false;
                if (!ok)
                    break;
                h++;
            }
            for (yy = y; yy < y + h; yy++)
                for (xx = x; xx < x + w; xx++)
                    used[yy][xx] = true;
            r = &t->runs[t->nRuns++];
            r->x = (u8)x; r->y = (u8)y; r->w = (u8)w; r->h = (u8)h; r->c = (u8)c;
        }
    }
}

static void pxFill(Tile *t, int c) {
    memset(t->px, c, sizeof t->px);
}

static void pxRect(Tile *t, int x, int y, int w, int h, int c) {
    int xx, yy;
    for (yy = y; yy < y + h; yy++)
        for (xx = x; xx < x + w; xx++)
            if (xx >= 0 && xx < TS && yy >= 0 && yy < TS)
                t->px[yy][xx] = (u8)c;
}

/* texture d'origine d'un des 8 premiers blocs */
static void defaultTile(int id) {
    static const char *const NAMES[BASE_TILES] = {
        "Herbe", "Mur", "Eau", "Arbre", "Chemin", "Sol", "Porte", "Hautes herbes"
    };
    Tile *t = &tiles[id];

    memset(t, 0, sizeof *t);
    snprintf(t->name, NAME_BYTES, "%s", NAMES[id]);
    t->solid = (id == T_WALL || id == T_WATER || id == T_TREE);
    t->wild = (id == T_TALL);

    switch (id) {
    case T_GRASS:
        pxFill(t, P_GRASS);
        pxRect(t, 5, 7, 2, 2, P_GREEN);
        pxRect(t, 11, 3, 1, 1, P_GREEN);
        pxRect(t, 2, 12, 1, 1, P_LGREEN);
        pxRect(t, 13, 13, 2, 1, P_GREEN);
        pxRect(t, 9, 10, 1, 1, P_LGREEN);
        break;
    case T_WALL:
        pxFill(t, P_GRAY);
        pxRect(t, 0, 7, 16, 1, P_DGRAY);
        pxRect(t, 0, 15, 16, 1, P_DGRAY);
        pxRect(t, 8, 0, 1, 7, P_DGRAY);
        pxRect(t, 4, 8, 1, 7, P_DGRAY);
        pxRect(t, 12, 8, 1, 7, P_DGRAY);
        pxRect(t, 0, 0, 8, 1, P_LGRAY);
        pxRect(t, 9, 0, 7, 1, P_LGRAY);
        break;
    case T_WATER:
        pxFill(t, P_BLUE);
        pxRect(t, 3, 6, 10, 2, P_LBLUE);
        pxRect(t, 1, 12, 5, 1, P_LBLUE);
        pxRect(t, 10, 2, 5, 1, P_LBLUE);
        break;
    case T_TREE:
        pxFill(t, P_DGREEN);
        pxRect(t, 3, 2, 10, 8, P_GRASS);
        pxRect(t, 4, 1, 8, 10, P_GRASS);
        pxRect(t, 5, 3, 3, 2, P_LGREEN);
        pxRect(t, 6, 10, 4, 6, P_DBROWN);
        break;
    case T_PATH:
        pxFill(t, P_SAND);
        pxRect(t, 3, 4, 1, 1, P_FLOOR);
        pxRect(t, 10, 2, 1, 1, P_FLOOR);
        pxRect(t, 7, 9, 1, 1, P_FLOOR);
        pxRect(t, 13, 12, 1, 1, P_FLOOR);
        pxRect(t, 2, 13, 1, 1, P_FLOOR);
        pxRect(t, 5, 6, 2, 1, P_PSAND);
        pxRect(t, 11, 8, 2, 1, P_PSAND);
        break;
    case T_FLOOR:
        pxFill(t, P_FLOOR);
        pxRect(t, 0, 3, 16, 1, P_BROWN);
        pxRect(t, 0, 7, 16, 1, P_BROWN);
        pxRect(t, 0, 11, 16, 1, P_BROWN);
        pxRect(t, 0, 15, 16, 1, P_BROWN);
        pxRect(t, 4, 0, 1, 3, P_BROWN);
        pxRect(t, 11, 4, 1, 3, P_BROWN);
        pxRect(t, 6, 8, 1, 3, P_BROWN);
        pxRect(t, 13, 12, 1, 3, P_BROWN);
        break;
    case T_DOOR:
        pxFill(t, P_DBROWN);
        pxRect(t, 3, 0, 10, 16, P_VDBROWN);
        pxRect(t, 5, 2, 6, 5, P_DBROWN);
        pxRect(t, 5, 9, 6, 5, P_DBROWN);
        pxRect(t, 10, 7, 2, 2, P_YELLOW);
        break;
    default: /* T_TALL */
        pxFill(t, P_GREEN);
        pxRect(t, 2, 6, 2, 9, P_LGREEN);
        pxRect(t, 7, 2, 2, 12, P_LGREEN);
        pxRect(t, 12, 7, 2, 8, P_LGREEN);
        pxRect(t, 2, 6, 2, 2, P_PGREEN);
        pxRect(t, 7, 2, 2, 2, P_PGREEN);
        pxRect(t, 12, 7, 2, 2, P_PGREEN);
        pxRect(t, 1, 14, 14, 2, P_DGREEN);
        break;
    }
    compileTile(t);
}

static void initTiles(void) {
    int i;

    for (i = 0; i < PAL_N; i++)
        palCol[i] = COL(PAL_RGB[i][0], PAL_RGB[i][1], PAL_RGB[i][2]);
    for (i = 0; i < BASE_TILES; i++)
        defaultTile(i);
    tileCount = BASE_TILES;
}

/* dessine un bloc (sc = 1 : taille normale 16 x 16) */
static void drawTile(int t, float sx, float sy, float sc, float z) {
    const Tile *tl;
    int i;

    if (t < 0 || t >= tileCount)
        t = 0;
    tl = &tiles[t];
    C2D_DrawRectSolid(sx, sy, z, TS * sc, TS * sc, palCol[tl->base]);
    if (tl->nRuns == 0 || tl->nRuns > rectBudget)
        return;
    rectBudget -= tl->nRuns;
    for (i = 0; i < tl->nRuns; i++) {
        const Run *r = &tl->runs[i];
        C2D_DrawRectSolid(sx + r->x * sc, sy + r->y * sc, z + 0.01f,
                          r->w * sc, r->h * sc, palCol[r->c]);
    }
}

/* ---------- Sauvegarde / chargement des blocs ---------- */

static bool saveTiles(void) {
    u8 header[MON_HEADER] = { 'R', 'P', 'G', 'B', 1, 0 };
    u8 rec[TILE_REC];
    FILE *f;
    bool ok;
    int i;

    header[5] = (u8)tileCount;
    mkdir("sdmc:/3ds", 0777);
    mkdir(SAVE_DIR, 0777);

    f = fopen(TILE_FILE, "wb");
    if (f == NULL)
        return false;
    ok = (fwrite(header, 1, MON_HEADER, f) == MON_HEADER);
    for (i = 0; ok && i < tileCount; i++) {
        memset(rec, 0, sizeof rec);
        memcpy(rec, tiles[i].name, NAME_BYTES - 1);
        rec[NAME_BYTES] = tiles[i].solid ? 1 : 0;
        rec[NAME_BYTES + 1] = tiles[i].wild ? 1 : 0;
        memcpy(rec + NAME_BYTES + 2, tiles[i].px, TS * TS);
        ok = (fwrite(rec, 1, TILE_REC, f) == TILE_REC);
    }
    fclose(f);
    return ok;
}

static bool loadTiles(void) {
    u8 header[MON_HEADER];
    u8 rec[TILE_REC];
    FILE *f;
    long size;
    int i, count;

    f = fopen(TILE_FILE, "rb");
    if (f == NULL)
        return false;
    if (fread(header, 1, MON_HEADER, f) != MON_HEADER ||
        memcmp(header, "RPGB", 4) != 0 || header[4] != 1 ||
        header[5] < BASE_TILES || header[5] > MAX_TILES) {
        fclose(f);
        return false;
    }
    count = header[5];

    /* on verifie la taille avant de toucher aux blocs en memoire */
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    if (size != (long)(MON_HEADER + count * TILE_REC)) {
        fclose(f);
        return false;
    }
    fseek(f, MON_HEADER, SEEK_SET);

    for (i = 0; i < count; i++) {
        Tile *t = &tiles[i];

        if (fread(rec, 1, TILE_REC, f) != TILE_REC) {
            fclose(f);
            return false;
        }
        memset(t, 0, sizeof *t);
        memcpy(t->name, rec, NAME_BYTES);
        t->name[NAME_BYTES - 1] = '\0';
        if (t->name[0] == '\0')
            snprintf(t->name, NAME_BYTES, "Bloc %d", i);
        t->solid = rec[NAME_BYTES] != 0;
        t->wild = rec[NAME_BYTES + 1] != 0;
        memcpy(t->px, rec + NAME_BYTES + 2, TS * TS);
        compileTile(t);
    }
    fclose(f);
    tileCount = count;
    return true;
}

/* ---------- Zones (cartes) ---------- */

#define ZMIN_W      25     /* au moins la taille de l'ecran du haut */
#define ZMIN_H      15
#define ZMAX_W      100
#define ZMAX_H      100
#define ZONE_DEF_W  30
#define ZONE_DEF_H  20
#define MAX_ZONES   16

static u8 mapData[ZMAX_W * ZMAX_H];
static int mapW = 50, mapH = 30;          /* taille de la zone chargee */
static int curZone = 1, zoneCount = 1;    /* numeros a partir de 1 */
static int spawnTX = 12, spawnTY = 13;    /* point de depart (en tuiles) */

#define MAP(x, y) mapData[(y) * mapW + (x)]

/* ---------- Carte par defaut ---------- */

static unsigned rngState = 12345;

static unsigned rnd(void) {
    rngState = rngState * 1103515245u + 12345u;
    return (rngState >> 16) & 0x7FFF;
}

/* la zone 1 par defaut : 50 x 30 avec lac, maison, chemin et hautes herbes */
static void buildMap(void) {
    int x, y, i;

    mapW = 50;
    mapH = 30;
    for (y = 0; y < mapH; y++)
        for (x = 0; x < mapW; x++)
            MAP(x, y) = T_GRASS;

    /* bordure d'arbres */
    for (x = 0; x < mapW; x++) {
        MAP(x, 0) = T_TREE;
        MAP(x, mapH - 1) = T_TREE;
    }
    for (y = 0; y < mapH; y++) {
        MAP(0, y) = T_TREE;
        MAP(mapW - 1, y) = T_TREE;
    }

    /* lac */
    for (y = 0; y < mapH; y++) {
        for (x = 0; x < mapW; x++) {
            float dx = (x - 36) / 7.0f;
            float dy = (y - 22) / 4.0f;
            if (dx * dx + dy * dy <= 1.0f)
                MAP(x, y) = T_WATER;
        }
    }

    /* maison */
    for (y = 5; y <= 10; y++) {
        for (x = 8; x <= 16; x++) {
            bool edge = (x == 8 || x == 16 || y == 5 || y == 10);
            MAP(x, y) = edge ? T_WALL : T_FLOOR;
        }
    }
    MAP(12, 10) = T_DOOR;

    /* chemin : de la porte vers le bas, puis vers la droite */
    for (y = 11; y <= 14; y++)
        MAP(12, y) = T_PATH;
    for (x = 12; x < mapW - 1; x++)
        MAP(x, 14) = T_PATH;

    /* zones de hautes herbes (la ou les monstres apparaissent) */
    {
        static const int patch[3][4] = { { 30, 8, 6, 3 }, { 18, 21, 5, 3 }, { 44, 6, 4, 3 } };
        for (i = 0; i < 3; i++) {
            for (y = 0; y < mapH; y++) {
                for (x = 0; x < mapW; x++) {
                    float dx = (x - patch[i][0]) / (float)patch[i][2];
                    float dy = (y - patch[i][1]) / (float)patch[i][3];
                    if (dx * dx + dy * dy <= 1.0f && MAP(x, y) == T_GRASS)
                        MAP(x, y) = T_TALL;
                }
            }
        }
    }

    /* arbres au hasard (toujours les memes) */
    rngState = 12345;
    for (i = 0; i < 90; i++) {
        x = 1 + (int)(rnd() % (mapW - 2));
        y = 1 + (int)(rnd() % (mapH - 2));
        if (MAP(x, y) == T_GRASS)
            MAP(x, y) = T_TREE;
    }

    spawnTX = 12;
    spawnTY = 13;
}

/* une nouvelle zone vide : de l'herbe entouree d'arbres */
static void buildBlank(int w, int h) {
    int x, y;

    mapW = w;
    mapH = h;
    for (y = 0; y < mapH; y++) {
        for (x = 0; x < mapW; x++) {
            bool edge = (x == 0 || y == 0 || x == mapW - 1 || y == mapH - 1);
            MAP(x, y) = edge ? T_TREE : T_GRASS;
        }
    }
    spawnTX = 2;
    spawnTY = 2;
}

/* ---------- Sauvegarde / chargement des zones (carte SD) ---------- */

/* une zone lue dans un fichier, sans toucher a la zone chargee */
typedef struct {
    int w, h, sx, sy;
    u8 d[ZMAX_W * ZMAX_H];
} ZoneBuf;

static ZoneBuf zb;

static void zonePath(char *buf, size_t n, int z) {
    snprintf(buf, n, SAVE_DIR "/map%d.bin", z);
}

static bool zoneExists(int z) {
    char path[64];
    struct stat st;

    zonePath(path, sizeof path, z);
    return stat(path, &st) == 0;
}

static int countZones(void) {
    int n = 0;

    while (n < MAX_ZONES && zoneExists(n + 1))
        n++;
    return (n < 1) ? 1 : n;
}

static bool readZoneFile(int z, ZoneBuf *b) {
    u8 header[HEADER_SIZE];
    char path[64];
    FILE *f;
    int w, h, i;

    zonePath(path, sizeof path, z);
    f = fopen(path, "rb");
    if (f == NULL)
        return false;

    if (fread(header, 1, HEADER_SIZE, f) != HEADER_SIZE ||
        memcmp(header, "RPG2", 4) != 0 || header[4] != 1) {
        fclose(f);
        return false;
    }
    w = header[5];
    h = header[6];
    if (w < ZMIN_W || w > ZMAX_W || h < ZMIN_H || h > ZMAX_H ||
        fread(b->d, 1, (size_t)w * h, f) != (size_t)w * h) {
        fclose(f);
        return false;
    }
    fclose(f);

    b->w = w;
    b->h = h;
    b->sx = (header[7] < w) ? header[7] : 1;
    b->sy = (header[8] < h) ? header[8] : 1;
    for (i = 0; i < w * h; i++)
        if (b->d[i] >= tileCount)
            b->d[i] = T_GRASS;
    return true;
}

static bool writeZoneFile(int z, const ZoneBuf *b) {
    u8 header[HEADER_SIZE] = { 'R', 'P', 'G', '2', 1, 0, 0, 0, 0 };
    char path[64];
    FILE *f;
    bool ok;

    header[5] = (u8)b->w;
    header[6] = (u8)b->h;
    header[7] = (u8)b->sx;
    header[8] = (u8)b->sy;

    mkdir("sdmc:/3ds", 0777);
    mkdir(SAVE_DIR, 0777);

    zonePath(path, sizeof path, z);
    f = fopen(path, "wb");
    if (f == NULL)
        return false;
    ok = (fwrite(header, 1, HEADER_SIZE, f) == HEADER_SIZE) &&
         (fwrite(b->d, 1, (size_t)b->w * b->h, f) == (size_t)b->w * b->h);
    fclose(f);
    return ok;
}

/* sauvegarde la zone chargee */
static bool saveZone(void) {
    zb.w = mapW;
    zb.h = mapH;
    zb.sx = spawnTX;
    zb.sy = spawnTY;
    memcpy(zb.d, mapData, (size_t)mapW * mapH);
    return writeZoneFile(curZone, &zb);
}

/* charge la zone z (la zone 1 est creee par defaut si elle n'existe pas) */
static bool loadZone(int z) {
    if (readZoneFile(z, &zb)) {
        mapW = zb.w;
        mapH = zb.h;
        spawnTX = zb.sx;
        spawnTY = zb.sy;
        memcpy(mapData, zb.d, (size_t)zb.w * zb.h);
        curZone = z;
        return true;
    }
    if (z == 1) {
        buildMap();
        curZone = 1;
        return true;
    }
    return false;
}

/* ---------- Teleporteurs (invisibles en jeu) ---------- */

#define MAX_TELES   64
#define MAX_LINK    99
#define TELE_FILE   "sdmc:/3ds/rpg2ds/teles.bin"

/* deux teleporteurs avec le meme numero de lien sont relies entre eux */
typedef struct { u8 zone, x, y, link; } Tele;

static Tele teles[MAX_TELES];
static int teleCount = 0;

static int teleFind(int zone, int x, int y) {
    int i;
    for (i = 0; i < teleCount; i++)
        if (teles[i].zone == zone && teles[i].x == x && teles[i].y == y)
            return i;
    return -1;
}

static int teleLinkCount(int link) {
    int i, n = 0;
    for (i = 0; i < teleCount; i++)
        if (teles[i].link == link)
            n++;
    return n;
}

static void teleRemove(int i) {
    if (i < 0 || i >= teleCount)
        return;
    memmove(&teles[i], &teles[i + 1], (size_t)(teleCount - i - 1) * sizeof(Tele));
    teleCount--;
}

/* pose (ou modifie) un teleporteur ; faux si la table est pleine */
static bool teleSet(int zone, int x, int y, int link) {
    int i = teleFind(zone, x, y);

    if (i < 0) {
        if (teleCount >= MAX_TELES)
            return false;
        i = teleCount++;
        teles[i].zone = (u8)zone;
        teles[i].x = (u8)x;
        teles[i].y = (u8)y;
    }
    teles[i].link = (u8)link;
    return true;
}

/* le teleporteur ou on arrive : le suivant (dans l'ordre de la table) avec le meme lien */
static int teleDest(int i) {
    int k, j;
    for (k = 1; k < teleCount; k++) {
        j = (i + k) % teleCount;
        if (teles[j].link == teles[i].link)
            return j;
    }
    return -1;
}

/* numero de lien conseille : celui d'un teleporteur encore seul, sinon un numero libre */
static int teleSuggestLink(void) {
    int l;
    for (l = 1; l <= MAX_LINK; l++)
        if (teleLinkCount(l) == 1)
            return l;
    for (l = 1; l <= MAX_LINK; l++)
        if (teleLinkCount(l) == 0)
            return l;
    return 1;
}

/* la zone z est supprimee : ses teleporteurs disparaissent, les zones suivantes reculent */
static void teleDropZone(int z) {
    int i = 0;
    while (i < teleCount) {
        if (teles[i].zone == z) {
            teleRemove(i);
        } else {
            if (teles[i].zone > z)
                teles[i].zone--;
            i++;
        }
    }
}

/* la zone a change de taille : on decale les teleporteurs et on retire ceux qui sortent */
static void teleShift(int zone, int dx, int dy, int w, int h) {
    int i = 0;
    while (i < teleCount) {
        if (teles[i].zone == zone) {
            int nx = teles[i].x + dx, ny = teles[i].y + dy;
            if (nx < 0 || ny < 0 || nx >= w || ny >= h) {
                teleRemove(i);
                continue;
            }
            teles[i].x = (u8)nx;
            teles[i].y = (u8)ny;
        }
        i++;
    }
}

static bool saveTeles(void) {
    u8 header[MON_HEADER] = { 'R', 'P', 'G', 'P', 1, 0 };
    FILE *f;
    bool ok;
    int i;

    header[5] = (u8)teleCount;
    mkdir("sdmc:/3ds", 0777);
    mkdir(SAVE_DIR, 0777);

    f = fopen(TELE_FILE, "wb");
    if (f == NULL)
        return false;
    ok = (fwrite(header, 1, MON_HEADER, f) == MON_HEADER);
    for (i = 0; ok && i < teleCount; i++) {
        u8 rec[4];
        rec[0] = teles[i].zone;
        rec[1] = teles[i].x;
        rec[2] = teles[i].y;
        rec[3] = teles[i].link;
        ok = (fwrite(rec, 1, 4, f) == 4);
    }
    fclose(f);
    return ok;
}

static bool loadTeles(void) {
    u8 header[MON_HEADER];
    FILE *f;
    int i, count, n = 0;

    teleCount = 0;
    f = fopen(TELE_FILE, "rb");
    if (f == NULL)
        return false;
    if (fread(header, 1, MON_HEADER, f) != MON_HEADER ||
        memcmp(header, "RPGP", 4) != 0 || header[4] != 1 || header[5] > MAX_TELES) {
        fclose(f);
        return false;
    }
    count = header[5];
    for (i = 0; i < count; i++) {
        u8 rec[4];
        if (fread(rec, 1, 4, f) != 4)
            break;
        if (rec[0] < 1 || rec[0] > MAX_ZONES || rec[1] >= ZMAX_W || rec[2] >= ZMAX_H ||
            rec[3] < 1 || rec[3] > MAX_LINK)
            continue;
        teles[n].zone = rec[0];
        teles[n].x = rec[1];
        teles[n].y = rec[2];
        teles[n].link = rec[3];
        n++;
    }
    fclose(f);
    teleCount = n;
    return true;
}

/* ---------- Operations sur les zones ---------- */

/* agrandit (ou reduit) la zone chargee : l, r, t, b = colonnes/lignes ajoutees (ou retirees)
   a gauche, a droite, en haut, en bas ; les nouvelles cases recoivent le bloc fill */
static bool resizeMap(int l, int r, int t, int b, int fill) {
    static u8 tmp[ZMAX_W * ZMAX_H];
    int nw = mapW + l + r, nh = mapH + t + b;
    int x, y;

    if (nw < ZMIN_W || nw > ZMAX_W || nh < ZMIN_H || nh > ZMAX_H)
        return false;
    if (fill < 0 || fill >= tileCount)
        fill = T_GRASS;

    memset(tmp, fill, (size_t)nw * nh);
    for (y = 0; y < nh; y++) {
        int sy = y - t;
        if (sy < 0 || sy >= mapH)
            continue;
        for (x = 0; x < nw; x++) {
            int sx = x - l;
            if (sx >= 0 && sx < mapW)
                tmp[y * nw + x] = MAP(sx, sy);
        }
    }
    memcpy(mapData, tmp, (size_t)nw * nh);
    mapW = nw;
    mapH = nh;

    teleShift(curZone, l, t, nw, nh);
    spawnTX += l;
    spawnTY += t;
    if (spawnTX < 0) spawnTX = 0;
    if (spawnTX >= nw) spawnTX = nw - 1;
    if (spawnTY < 0) spawnTY = 0;
    if (spawnTY >= nh) spawnTY = nh - 1;
    return true;
}

/* ajoute une zone vide a la fin et la charge (l'appelant sauvegarde avant) */
static bool newZone(void) {
    if (zoneCount >= MAX_ZONES)
        return false;
    zoneCount++;
    curZone = zoneCount;
    buildBlank(ZONE_DEF_W, ZONE_DEF_H);
    return saveZone();
}

/* supprime la zone z : les suivantes reculent d'un numero ; charge ensuite une zone voisine */
static bool deleteZone(int z) {
    char path[64];
    int i;

    if (zoneCount <= 1 || z < 1 || z > zoneCount)
        return false;

    teleDropZone(z);
    for (i = z + 1; i <= zoneCount; i++) {
        if (readZoneFile(i, &zb))
            writeZoneFile(i - 1, &zb);
    }
    zonePath(path, sizeof path, zoneCount);
    remove(path);
    zoneCount--;

    curZone = (z <= zoneCount) ? z : zoneCount;
    loadZone(curZone);
    saveTeles();
    return true;
}

/* supprime le bloc id (>= BASE_TILES) : dans toutes les zones, il devient de l'herbe
   et les blocs suivants reculent d'un numero */
static void remapCells(u8 *d, int n, int id) {
    int i;
    for (i = 0; i < n; i++) {
        if (d[i] == id)
            d[i] = T_GRASS;
        else if (d[i] > id)
            d[i]--;
    }
}

static void tileDelete(int id) {
    int i, z;

    if (id < BASE_TILES || id >= tileCount)
        return;

    /* d'abord les zones (avec l'ancien nombre de blocs), puis la liste des blocs */
    for (z = 1; z <= zoneCount; z++) {
        if (readZoneFile(z, &zb)) {
            remapCells(zb.d, zb.w * zb.h, id);
            writeZoneFile(z, &zb);
        }
    }
    remapCells(mapData, mapW * mapH, id);

    for (i = id; i < tileCount - 1; i++)
        tiles[i] = tiles[i + 1];
    tileCount--;
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
    if (tx >= mapW || ty >= mapH)
        return true;
    return tiles[MAP(tx, ty)].solid;
}

static bool blocked(int x, int y) {
    return solidAt(x, y) || solidAt(x + PW - 1, y) ||
           solidAt(x, y + PH - 1) || solidAt(x + PW - 1, y + PH - 1);
}

static void updateCamera(void) {
    int maxX = mapW * TS - TOP_W;
    int maxY = mapH * TS - TOP_H;

    camX = player.x + PW / 2 - TOP_W / 2;
    camY = player.y + PH / 2 - TOP_H / 2;

    if (camX > maxX) camX = maxX;
    if (camX < 0) camX = 0;
    if (camY > maxY) camY = maxY;
    if (camY < 0) camY = 0;
}

/* place le joueur sur la tuile libre la plus proche de (tx, ty) */
static void placePlayerAtTile(int tx, int ty) {
    int r, dx, dy;
    int rmax = (mapW > mapH) ? mapW : mapH;

    for (r = 0; r < rmax; r++) {
        for (dy = -r; dy <= r; dy++) {
            for (dx = -r; dx <= r; dx++) {
                int x = tx + dx, y = ty + dy;
                if (x < 0 || y < 0 || x >= mapW || y >= mapH)
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
    if (tx >= mapW || ty >= mapH)
        return false;
    return tiles[MAP(tx, ty)].wild;
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

/* on change de zone : les monstres sauvages de l'ancienne zone disparaissent */
static void wildReset(void) {
    memset(wild, 0, sizeof wild);
    wildSpawnTimer = 60;
}

/* ---------- Zones en jeu : point de depart et teleporteurs ---------- */

static int playStartZone = 1;     /* la zone ou la partie a commence */
static bool teleLock = false;     /* on vient d'arriver sur un teleporteur : il ne repart pas */

/* retour au point de depart (apres une defaite) */
static void respawnHero(void) {
    if (curZone != playStartZone) {
        loadZone(playStartZone);
        wildReset();
    }
    placePlayerAtTile(spawnTX, spawnTY);
    teleLock = true;
}

/* a appeler apres chaque deplacement : marcher sur un teleporteur envoie sur son jumeau */
static void checkTeleport(void) {
    int tx = (player.x + PW / 2) / TS;
    int ty = (player.y + PH / 2) / TS;
    int i = teleFind(curZone, tx, ty);
    int d;

    if (i < 0) {
        teleLock = false;
        return;
    }
    if (teleLock)
        return;
    d = teleDest(i);
    if (d < 0)
        return;   /* pas de jumeau : rien ne se passe */

    if (teles[d].zone != curZone) {
        if (!loadZone(teles[d].zone))
            return;
        wildReset();
    }
    placePlayerAtTile(teles[d].x, teles[d].y);
    teleLock = true;
    updateCamera();
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

        if (tx < 0 || ty < 0 || tx >= mapW || ty >= mapH)
            continue;
        if (!tiles[MAP(tx, ty)].wild)
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

enum { S_TITLE, S_PLAY, S_EDIT, S_EDMENU, S_FORM, S_BATTLE, S_INV, S_TILEEDIT, S_ZONE };

static int state = S_TITLE;
static bool running = true;
static bool fromEditor = false;   /* le jeu a ete lance depuis l'editeur */
static C2D_TextBuf g_textBuf;

/* editeur */
static int editTile = T_GRASS;    /* bloc choisi (tileCount = l'outil teleporteur) */
static int editLink = 1;          /* numero de lien du prochain teleporteur */
static int edZone = 1;            /* zone ouverte dans l'editeur */
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
    { 50, 208, 220, 24, "Retour",   true },
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

static void drawButtonSc(const Button *b, bool selected, float sc) {
    u32 border = selected ? COL(255, 220, 80) : COL(20, 20, 30);
    u32 fill = b->enabled ? COL(60, 90, 160) : COL(70, 70, 80);
    u32 txt = b->enabled ? COL(255, 255, 255) : COL(160, 160, 170);

    C2D_DrawRectSolid(b->x - 3, b->y - 3, 0.0f, b->w + 6, b->h + 6, border);
    C2D_DrawRectSolid(b->x, b->y, 0.1f, b->w, b->h, fill);
    drawTextInRect(b->label, b->x, b->y, b->w, b->h, sc, txt);
}

static void drawButton(const Button *b, bool selected) {
    drawButtonSc(b, selected, 0.6f);
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
        respawnHero();
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
    playStartZone = curZone;
    teleLock = true;   /* si le depart est sur un teleporteur, il ne se declenche pas */
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

static void clampEditor(void);

static void leavePlay(void) {
    saveHero();
    if (fromEditor) {
        /* on revient dans la zone ou le test a commence */
        if (curZone != playStartZone)
            loadZone(playStartZone);
        edZone = curZone;
        clampEditor();
        state = S_EDIT;
        painting = false;
    } else {
        state = S_TITLE;
    }
    fromEditor = false;
}

static void clampEditor(void) {
    if (ecx > mapW - VIEW_W) ecx = mapW - VIEW_W;
    if (ecx < 0) ecx = 0;
    if (ecy > mapH - VIEW_H) ecy = mapH - VIEW_H;
    if (ecy < 0) ecy = 0;
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
static void enterTileEd(void);

static void activate(int i) {
    if (i == 0) {
        loadZone(1);
        fromEditor = false;
        startPlay();
    } else if (i == 1) {
        edSel = 0;
        state = S_EDMENU;
    } else if (i == 2) {
        running = false;
    }
}

static void activateEd(int i) {
    if (i == 0) {
        if (!loadZone(edZone)) {
            edZone = 1;
            loadZone(1);
        }
        enterEditor();
    } else if (i == 1) {
        enterTileEd();
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
    checkTeleport();

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

static void enterZoneTools(void);

static bool saveWorld(void) {
    bool a = saveZone();
    bool b = saveTeles();
    return a && b;
}

static void updateEdit(u32 kDown, u32 kHeld) {
    static int rep[4] = { 0, 0, 0, 0 };
    int mx = 0, my = 0;
    int nTool = tileCount + 1;   /* les blocs, plus l'outil teleporteur */

    if (editMsgTimer > 0)
        editMsgTimer--;
    if (editTile > tileCount)
        editTile = T_GRASS;

    /* choix du bloc (le dernier de la liste est le teleporteur) */
    if (kDown & KEY_L) {
        editTile = (editTile + nTool - 1) % nTool;
        if (editTile == tileCount)
            editLink = teleSuggestLink();
    }
    if (kDown & KEY_R) {
        editTile = (editTile + 1) % nTool;
        if (editTile == tileCount)
            editLink = teleSuggestLink();
    }

    if (kHeld & KEY_Y) {
        /* Y + croix : numero de lien des teleporteurs */
        if (kDown & KEY_UP)    editLink = editLink % MAX_LINK + 1;
        if (kDown & KEY_DOWN)  editLink = (editLink + MAX_LINK - 2) % MAX_LINK + 1;
        if (kDown & KEY_RIGHT) editLink = (editLink + 9) % MAX_LINK + 1;
        if (kDown & KEY_LEFT)  editLink = (editLink - 11 + 2 * MAX_LINK) % MAX_LINK + 1;
    } else {
        /* defilement de la zone d'edition */
        if (stepRepeat(kDown, kHeld, KEY_LEFT,  &rep[0])) mx = -1;
        if (stepRepeat(kDown, kHeld, KEY_RIGHT, &rep[1])) mx =  1;
        if (stepRepeat(kDown, kHeld, KEY_UP,    &rep[2])) my = -1;
        if (stepRepeat(kDown, kHeld, KEY_DOWN,  &rep[3])) my =  1;
        ecx += mx;
        ecy += my;
    }
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

        if (tx >= 0 && tx < mapW && ty >= 0 && ty < mapH) {
            if (kHeld & KEY_Y) {
                spawnTX = tx;
                spawnTY = ty;
            } else if (editTile == tileCount) {
                /* outil teleporteur : B + stylet l'enleve ; un seul appui = un teleporteur */
                if (kHeld & KEY_B) {
                    teleRemove(teleFind(curZone, tx, ty));
                } else if (kDown & KEY_TOUCH) {
                    if (!teleSet(curZone, tx, ty, editLink)) {
                        editMsg = "Trop de téléporteurs (64 maximum)";
                        editMsgTimer = 120;
                    } else if (teleLinkCount(editLink) >= 2) {
                        editLink = teleSuggestLink();   /* la paire est complete */
                    }
                }
            } else {
                MAP(tx, ty) = (kHeld & KEY_B) ? (u8)T_GRASS : (u8)editTile;
            }
        }
    }

    if (kDown & KEY_X) {
        saveWorld();
        enterZoneTools();
        return;
    }

    if (kDown & KEY_A) {
        saveWorld();
        fromEditor = true;
        startPlay();
        return;
    }

    if (kDown & KEY_SELECT) {
        saveWorld();
        state = S_EDMENU;
    }
}

/* ---------- Mise a jour : editeur de blocs (textures) ---------- */

#define TE_GX 6      /* grille de dessin : coin haut-gauche et taille d'un pixel */
#define TE_GY 6
#define TE_GC 10
#define TE_PX 176    /* palette de couleurs */
#define TE_PY 6
#define TE_PC 17

enum { TOOL_PENCIL, TOOL_FILL, TOOL_PICK };
enum { TB_PENCIL, TB_FILL, TB_PICK, TB_SOLID, TB_WILD, TB_NAME, TB_UNDO, TB_DEL, TB_BACK,
       TB_PREV, TB_NEXT, TB_NEW, TB_COPY, TB_N };

static Button teBtn[TB_N] = {
    [TB_PENCIL] = { 176,  80,  44, 22, "Crayon",  true },
    [TB_FILL]   = { 223,  80,  44, 22, "Pot",     true },
    [TB_PICK]   = { 270,  80,  44, 22, "Pipette", true },
    [TB_SOLID]  = { 176, 108, 138, 22, "Solide",  true },
    [TB_WILD]   = { 176, 136, 138, 22, "Monstres", true },
    [TB_NAME]   = { 176, 164,  66, 22, "Nom",     true },
    [TB_UNDO]   = { 248, 164,  66, 22, "Annuler", true },
    [TB_DEL]    = { 176, 192,  66, 22, "Suppr.",  true },
    [TB_BACK]   = { 248, 192,  66, 22, "Retour",  true },
    [TB_PREV]   = {   6, 176,  40, 22, "<",       true },
    [TB_NEXT]   = { 126, 176,  40, 22, ">",       true },
    [TB_NEW]    = {   6, 206,  76, 24, "Nouveau", true },
    [TB_COPY]   = {  90, 206,  76, 24, "Copie",   true },
};

static int teSel = 0;                 /* bloc modifie */
static int teColor = P_BLACK;         /* couleur choisie dans la palette */
static int teTool = TOOL_PENCIL;
static u8 teUndo[TS][TS];
static bool teHasUndo = false;
static bool teStroke = false;         /* le stylet dessine (contact commence dans la grille) */
static int teDelTimer = 0;
static const char *teMsg = "";
static int teMsgTimer = 0;

static void teSetMsg(const char *s) {
    teMsg = s;
    teMsgTimer = 150;
}

static void teSelect(int i) {
    teSel = ((i % tileCount) + tileCount) % tileCount;
    teHasUndo = false;
    teStroke = false;
    teDelTimer = 0;
}

static void enterTileEd(void) {
    teSelect((editTile < tileCount) ? editTile : 0);
    teColor = tiles[teSel].base;
    teTool = TOOL_PENCIL;
    teMsgTimer = 0;
    state = S_TILEEDIT;
}

static void leaveTileEd(void) {
    saveTiles();
    state = S_EDMENU;
}

/* pot de peinture : remplit les pixels voisins de la meme couleur */
static void teFill(int x, int y, int c) {
    Tile *t = &tiles[teSel];
    int stack[TS * TS];
    int sp = 0, target = t->px[y][x];

    if (target == c)
        return;
    t->px[y][x] = (u8)c;
    stack[sp++] = y * TS + x;
    while (sp > 0) {
        int p = stack[--sp];
        int cx = p % TS, cy = p / TS, k;
        static const int D[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };

        for (k = 0; k < 4; k++) {
            int nx = cx + D[k][0], ny = cy + D[k][1];
            if (nx < 0 || ny < 0 || nx >= TS || ny >= TS)
                continue;
            if (t->px[ny][nx] != target)
                continue;
            t->px[ny][nx] = (u8)c;
            stack[sp++] = ny * TS + nx;
        }
    }
}

static void teApply(int gx, int gy, bool fresh) {
    Tile *t = &tiles[teSel];

    if (teTool == TOOL_PICK) {
        if (fresh) {
            teColor = t->px[gy][gx];
            teTool = TOOL_PENCIL;
        }
        return;
    }
    if (fresh) {
        memcpy(teUndo, t->px, sizeof teUndo);
        teHasUndo = true;
    }
    if (teTool == TOOL_FILL) {
        if (fresh)
            teFill(gx, gy, teColor);
    } else {
        t->px[gy][gx] = (u8)teColor;
    }
    compileTile(t);
}

static void teCreate(bool copy) {
    Tile *t;

    if (tileCount >= MAX_TILES) {
        teSetMsg("Maximum : 32 blocs");
        return;
    }
    t = &tiles[tileCount];
    if (copy) {
        *t = tiles[teSel];
    } else {
        memset(t, 0, sizeof *t);
        pxFill(t, teColor);
    }
    snprintf(t->name, NAME_BYTES, "Bloc %d", tileCount - BASE_TILES + 1);
    compileTile(t);
    teSel = tileCount;
    tileCount++;
    teHasUndo = false;
    teDelTimer = 0;
    saveTiles();
    teSetMsg(copy ? "Copie créée" : "Nouveau bloc créé");
}

static void teButton(int id) {
    Tile *t = &tiles[teSel];

    switch (id) {
    case TB_PENCIL: teTool = TOOL_PENCIL; break;
    case TB_FILL:   teTool = TOOL_FILL;   break;
    case TB_PICK:   teTool = TOOL_PICK;   break;
    case TB_SOLID:  t->solid = !t->solid; break;
    case TB_WILD:   t->wild = !t->wild;   break;
    case TB_NAME:
        askName(t->name, NAME_BYTES);
        break;
    case TB_UNDO:
        if (teHasUndo) {
            u8 tmp[TS][TS];
            memcpy(tmp, t->px, sizeof tmp);
            memcpy(t->px, teUndo, sizeof tmp);
            memcpy(teUndo, tmp, sizeof tmp);
            compileTile(t);
        }
        break;
    case TB_PREV: teSelect(teSel - 1); break;
    case TB_NEXT: teSelect(teSel + 1); break;
    case TB_NEW:  teCreate(false);     break;
    case TB_COPY: teCreate(true);      break;
    case TB_DEL:
        if (teDelTimer <= 0) {
            teDelTimer = 120;   /* il faut retoucher le bouton pour confirmer */
        } else if (teSel < BASE_TILES) {
            defaultTile(teSel);
            teDelTimer = 0;
            teHasUndo = false;
            saveTiles();
            teSetMsg("Texture d'origine rétablie");
        } else {
            int id = teSel;

            tileDelete(id);
            if (editTile == id)
                editTile = T_GRASS;
            else if (editTile > id)
                editTile--;
            teSelect(id - 1);
            saveTiles();
            teSetMsg("Bloc supprimé (remplacé par de l'herbe)");
        }
        break;
    case TB_BACK:
        leaveTileEd();
        break;
    default:
        break;
    }
}

static void updateTileEd(u32 kDown, u32 kHeld) {
    if (teMsgTimer > 0) teMsgTimer--;
    if (teDelTimer > 0) teDelTimer--;

    if (kDown & KEY_L) teSelect(teSel - 1);
    if (kDown & KEY_R) teSelect(teSel + 1);
    if (kDown & KEY_B) {
        leaveTileEd();
        return;
    }

    if (!(kHeld & KEY_TOUCH))
        teStroke = false;

    if (kHeld & KEY_TOUCH) {
        touchPosition t;
        int px, py;

        hidTouchRead(&t);
        px = t.px;
        py = t.py;

        if (px >= TE_GX && px < TE_GX + TS * TE_GC && py >= TE_GY && py < TE_GY + TS * TE_GC) {
            int gx = (px - TE_GX) / TE_GC, gy = (py - TE_GY) / TE_GC;

            if (kDown & KEY_TOUCH) {
                teApply(gx, gy, true);
                teStroke = (teTool != TOOL_PICK);
            } else if (teStroke && teTool == TOOL_PENCIL) {
                teApply(gx, gy, false);
            }
        } else if (kDown & KEY_TOUCH) {
            int i;

            if (px >= TE_PX && px < TE_PX + 8 * TE_PC && py >= TE_PY && py < TE_PY + 4 * TE_PC) {
                teColor = ((py - TE_PY) / TE_PC) * 8 + (px - TE_PX) / TE_PC;
            } else {
                for (i = 0; i < TB_N; i++) {
                    if (inRect(&teBtn[i], px, py)) {
                        if (i != TB_DEL)
                            teDelTimer = 0;
                        teButton(i);
                        break;
                    }
                }
            }
        }
    }
}

/* ---------- Mise a jour : outils de zone (agrandir, nouvelle zone...) ---------- */

enum { ZB_APPLY = 8, ZB_NEW, ZB_DEL, ZB_BACK, ZB_PREV, ZB_NEXT, ZB_N };

static Button zBtn[ZB_N] = {
    { 118,  44, 44, 28, "-", true }, { 214,  44, 44, 28, "+", true },
    { 118,  78, 44, 28, "-", true }, { 214,  78, 44, 28, "+", true },
    { 118, 112, 44, 28, "-", true }, { 214, 112, 44, 28, "+", true },
    { 118, 146, 44, 28, "-", true }, { 214, 146, 44, 28, "+", true },
    {  10, 184, 148, 24, "Appliquer",     true },
    { 162, 184, 148, 24, "Nouvelle zone", true },
    {  10, 212, 148, 24, "Suppr. zone",   true },
    { 162, 212, 148, 24, "Retour",        true },
    { 196,   6,  52, 26, "<", true },
    { 258,   6,  52, 26, ">", true },
};

static const char *const ZSIDE[4] = { "Gauche", "Droite", "Haut", "Bas" };

static int zDelta[4] = { 0, 0, 0, 0 };    /* colonnes/lignes a ajouter (ou retirer) : G, D, H, B */
static int zHoldId = -1, zHoldCnt = 0;
static int zDelTimer = 0;
static const char *zMsg = "";
static int zMsgTimer = 0;

static void zSetMsg(const char *s) {
    zMsg = s;
    zMsgTimer = 150;
}

static void enterZoneTools(void) {
    memset(zDelta, 0, sizeof zDelta);
    zHoldId = -1;
    zDelTimer = 0;
    zMsgTimer = 0;
    if (editTile >= tileCount)
        editTile = T_GRASS;
    state = S_ZONE;
}

static void leaveZoneTools(void) {
    saveWorld();
    edZone = curZone;
    clampEditor();
    painting = false;
    state = S_EDIT;
}

static void zoneStep(int id) {
    int side = id / 2, dir = (id % 2) ? 1 : -1;
    int nw = mapW + zDelta[0] + zDelta[1];
    int nh = mapH + zDelta[2] + zDelta[3];

    if (side < 2) nw += dir; else nh += dir;
    if (nw < ZMIN_W || nw > ZMAX_W || nh < ZMIN_H || nh > ZMAX_H)
        return;
    zDelta[side] += dir;
}

static void zoneSwitch(int z) {
    if (z < 1 || z > zoneCount || z == curZone)
        return;
    saveWorld();
    if (loadZone(z)) {
        edZone = z;
        memset(zDelta, 0, sizeof zDelta);
        zDelTimer = 0;
    } else {
        loadZone(edZone);
        zSetMsg("Zone illisible");
    }
}

static void zoneButton(int id) {
    if (id != ZB_DEL)
        zDelTimer = 0;

    switch (id) {
    case ZB_APPLY:
        if (zDelta[0] == 0 && zDelta[1] == 0 && zDelta[2] == 0 && zDelta[3] == 0) {
            zSetMsg("Rien à changer");
        } else if (resizeMap(zDelta[0], zDelta[1], zDelta[2], zDelta[3], editTile)) {
            memset(zDelta, 0, sizeof zDelta);
            saveWorld();
            zSetMsg("Zone redimensionnée !");
        } else {
            zSetMsg("Taille impossible");
        }
        break;
    case ZB_NEW:
        saveWorld();
        if (newZone()) {
            edZone = curZone;
            memset(zDelta, 0, sizeof zDelta);
            saveWorld();
            zSetMsg("Nouvelle zone créée !");
        } else {
            zSetMsg("Maximum : 16 zones");
        }
        break;
    case ZB_DEL:
        if (zoneCount <= 1) {
            zSetMsg("Impossible : il faut au moins une zone");
        } else if (zDelTimer <= 0) {
            zDelTimer = 120;   /* il faut retoucher le bouton pour confirmer */
        } else {
            zDelTimer = 0;
            if (deleteZone(curZone)) {
                edZone = curZone;
                memset(zDelta, 0, sizeof zDelta);
                zSetMsg("Zone supprimée");
            }
        }
        break;
    case ZB_BACK:
        leaveZoneTools();
        break;
    case ZB_PREV:
        zoneSwitch(curZone - 1);
        break;
    case ZB_NEXT:
        zoneSwitch(curZone + 1);
        break;
    default:
        break;
    }
}

static void updateZone(u32 kDown, u32 kHeld) {
    int id = -1, i;

    if (zMsgTimer > 0) zMsgTimer--;
    if (zDelTimer > 0) zDelTimer--;

    /* L / R : bloc qui remplit les nouvelles cases */
    if (kDown & KEY_L) editTile = (editTile + tileCount - 1) % tileCount;
    if (kDown & KEY_R) editTile = (editTile + 1) % tileCount;
    if (kDown & KEY_B) {
        leaveZoneTools();
        return;
    }

    if (kHeld & KEY_TOUCH) {
        touchPosition t;

        hidTouchRead(&t);
        for (i = 0; i < ZB_N; i++) {
            if (inRect(&zBtn[i], t.px, t.py)) {
                id = i;
                break;
            }
        }
    }

    if (id >= 0 && id < 8) {
        /* + et - : un appui = 1 case, en gardant le doigt la repetition accelere */
        bool fire = false;

        if (id != zHoldId || (kDown & KEY_TOUCH)) {
            zHoldId = id;
            zHoldCnt = 0;
            fire = true;
        } else {
            zHoldCnt++;
            fire = (zHoldCnt > 15 && zHoldCnt % 3 == 0);
        }
        if (fire)
            zoneStep(id);
    } else {
        zHoldId = -1;
        if (id >= 8 && (kDown & KEY_TOUCH))
            zoneButton(id);
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
    drawTextCX("Étape 7 : blocs, zones et téléporteurs", TOP_W / 2, 130, 0.5f,
               COL(150, 160, 190));
}

static void drawTitleBottom(void) {
    int i;

    for (i = 0; i < N_TITLE_BUTTONS; i++)
        drawButton(&titleButtons[i], i == sel);

    drawTextCX("Croix + A, ou touche un bouton", BOT_W / 2, 208, 0.45f,
               COL(170, 170, 190));
}

/* ---------- Dessin : mini-cartes et teleporteurs ---------- */

/* mini-carte : chaque case dessinee mesure s pixels et represente k x k tuiles
   (les grandes zones sont resumees pour garder un dessin rapide) */
typedef struct { int x0, y0, k, s, cw, ch; } Ov;

static Ov ovLayout(int x0, int y0, int w, int h) {
    Ov o;

    o.x0 = x0;
    o.y0 = y0;
    o.k = 1;
    for (;;) {
        o.cw = (mapW + o.k - 1) / o.k;
        o.ch = (mapH + o.k - 1) / o.k;
        if ((o.cw * 2 <= w && o.ch * 2 <= h && o.cw * o.ch <= 2000) || o.k >= 16)
            break;
        o.k++;
    }
    o.s = w / o.cw;
    if (h / o.ch < o.s)
        o.s = h / o.ch;
    if (o.s < 1)
        o.s = 1;
    return o;
}

static float ovX(const Ov *o, float tx) { return (float)o->x0 + tx * o->s / o->k; }
static float ovY(const Ov *o, float ty) { return (float)o->y0 + ty * o->s / o->k; }

static void ovDraw(const Ov *o) {
    int cx, cy;

    C2D_DrawRectSolid((float)(o->x0 - 2), (float)(o->y0 - 2), 0.0f,
                      (float)(o->cw * o->s + 4), (float)(o->ch * o->s + 4), COL(255, 255, 255));
    for (cy = 0; cy < o->ch; cy++) {
        for (cx = 0; cx < o->cw; cx++) {
            int t = MAP(cx * o->k, cy * o->k);
            C2D_DrawRectSolid((float)(o->x0 + cx * o->s), (float)(o->y0 + cy * o->s), 0.05f,
                              (float)o->s, (float)o->s, palCol[tiles[t].base]);
        }
    }
}

/* un teleporteur dans l'editeur : violet s'il a un jumeau, orange sinon */
static u32 teleColor(int link) {
    return (teleLinkCount(link) >= 2) ? COL(140, 60, 220) : COL(235, 130, 30);
}

static void ovTeles(const Ov *o) {
    float ts = (float)o->s / o->k;
    int i;

    if (ts < 2.0f)
        ts = 2.0f;
    for (i = 0; i < teleCount; i++) {
        if (teles[i].zone != curZone)
            continue;
        C2D_DrawRectSolid(ovX(o, (float)teles[i].x) - 1, ovY(o, (float)teles[i].y) - 1, 0.15f,
                          ts + 2, ts + 2, COL(255, 255, 255));
        C2D_DrawRectSolid(ovX(o, (float)teles[i].x), ovY(o, (float)teles[i].y), 0.16f,
                          ts, ts, teleColor(teles[i].link));
    }
}

/* la marque d'un teleporteur sur une case de 16 x 16 (editeur seulement) */
static void drawTeleMark(float sx, float sy, int link) {
    char buf[8];

    C2D_DrawRectSolid(sx + 1, sy + 1, 0.3f, 14, 14, COL(255, 255, 255));
    C2D_DrawRectSolid(sx + 2, sy + 2, 0.31f, 12, 12, teleColor(link));
    snprintf(buf, sizeof buf, "%d", link);
    drawTextCX(buf, sx + 8, sy + 1, 0.36f, COL(255, 255, 255));
}

/* ---------- Dessin : jeu ---------- */

static void drawMapTop(void) {
    int tx0 = camX / TS;
    int ty0 = camY / TS;
    int tx1 = (camX + TOP_W - 1) / TS;
    int ty1 = (camY + TOP_H - 1) / TS;
    int tx, ty;

    if (tx1 >= mapW) tx1 = mapW - 1;
    if (ty1 >= mapH) ty1 = mapH - 1;

    /* les teleporteurs ne sont volontairement jamais dessines en jeu */
    for (ty = ty0; ty <= ty1; ty++) {
        for (tx = tx0; tx <= tx1; tx++)
            drawTile(MAP(tx, ty), (float)(tx * TS - camX), (float)(ty * TS - camY), 1.0f, 0.0f);
    }
}

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

/* largeur d'un texte en pixels (pour ajuster les cadres au contenu) */
static float textWidth(const char *s, float sc) {
    C2D_Text t;
    float tw, th;

    C2D_TextParse(&t, g_textBuf, s);
    C2D_TextOptimize(&t);
    C2D_TextGetDimensions(&t, sc, sc, &tw, &th);
    return tw;
}

/* petit cadre en haut a gauche : FPS et vitesse (la largeur suit le texte) */
static void drawDebugOverlay(void) {
    char l1[48], l2[48], l3[48], l4[64];
    int sf10 = (int)(dbgSpeedFrame * 10.0f + 0.5f);
    float w, w2;

    snprintf(l1, sizeof l1, "FPS : %d", dbgFps);
    snprintf(l2, sizeof l2, "Vitesse : %d px/s", (int)(dbgSpeedPxS + 0.5f));
    snprintf(l3, sizeof l3, "(%d.%d px/image)", sf10 / 10, sf10 % 10);
    snprintf(l4, sizeof l4, "Sauvages : %d   Rencontres : %d", wildCount(), encounters);

    w = textWidth(l4, 0.4f);
    w2 = textWidth(l2, 0.45f);
    if (w2 > w)
        w = w2;
    C2D_DrawRectSolid(4, 4, 0.4f, w + 12, 56, C2D_Color32(0, 0, 0, 150));

    drawText(l1, 8, 6, 0.45f, COL(255, 255, 255));
    drawText(l2, 8, 20, 0.45f, COL(255, 220, 80));
    drawText(l3, 8, 32, 0.4f, COL(170, 170, 190));
    drawText(l4, 8, 44, 0.4f, COL(170, 170, 190));
}

static void drawPlayBottom(void) {
    Ov o = ovLayout(10, 10, 200, 124);   /* mini-carte */
    float vx, vy, vw, vh, px, py;
    char buf[64];
    u32 white = COL(255, 255, 255);
    u32 gray = COL(170, 170, 190);
    int cx = (player.x + PW / 2) / TS;
    int cy = (player.y + PH / 2) / TS;

    if (cx >= mapW) cx = mapW - 1;
    if (cy >= mapH) cy = mapH - 1;

    ovDraw(&o);

    /* zone visible sur l'ecran du haut */
    vx = ovX(&o, (float)camX / TS);
    vy = ovY(&o, (float)camY / TS);
    vw = (float)(TOP_W / TS) * o.s / o.k;
    vh = (float)(TOP_H / TS) * o.s / o.k;
    C2D_DrawRectSolid(vx, vy, 0.1f, vw, 1, white);
    C2D_DrawRectSolid(vx, vy + vh - 1, 0.1f, vw, 1, white);
    C2D_DrawRectSolid(vx, vy, 0.1f, 1, vh, white);
    C2D_DrawRectSolid(vx + vw - 1, vy, 0.1f, 1, vh, white);

    /* monstres sauvages */
    {
        int k;
        for (k = 0; k < MAX_WILD; k++) {
            if (wild[k].active)
                C2D_DrawRectSolid(ovX(&o, (float)(wild[k].x + TS / 2) / TS) - 1,
                                  ovY(&o, (float)(wild[k].y + TS / 2) / TS) - 1,
                                  0.15f, 3, 3, COL(255, 220, 80));
        }
    }

    /* joueur */
    px = ovX(&o, (float)(player.x + PW / 2) / TS);
    py = ovY(&o, (float)(player.y + PH / 2) / TS);
    C2D_DrawRectSolid(px - 1, py - 1, 0.2f, 3, 3, COL(255, 0, 0));

    /* boutons */
    drawButton(&playTitleButton, false);
    drawButton(&playInvButton, false);

    /* infos */
    snprintf(buf, sizeof buf, "%s niv. %d   PV %d/%d", classes[hero.cls].name,
             hero.level, hero.hp, heroStat(ST_HP));
    drawText(buf, 10, 140, 0.5f, white);
    snprintf(buf, sizeof buf, "XP %d/%d   Zone %d  Pos %d,%d", hero.xp, xpNeed(hero.level),
             curZone, cx, cy);
    drawText(buf, 10, 156, 0.5f, white);
    snprintf(buf, sizeof buf, "Sur : %s", tiles[MAP(cx, cy)].name);
    drawText(buf, 10, 172, 0.5f, gray);
    drawText("Stick : bouger  B : courir  X : sac", 10, 188, 0.5f, gray);
    drawText(fromEditor ? "SELECT : éditeur    START : quitter"
                        : "SELECT : titre    START : quitter",
             10, 204, 0.5f, gray);
}

/* ---------- Dessin : editeur ---------- */

/* ecran du bas : la zone qu'on modifie au stylet */
static void drawEditBottom(void) {
    int x, y, i;
    u32 grid = C2D_Color32(0, 0, 0, 60);

    for (y = 0; y < VIEW_H; y++)
        for (x = 0; x < VIEW_W; x++)
            drawTile(MAP(ecx + x, ecy + y), (float)(x * TS), (float)(y * TS), 1.0f, 0.0f);

    /* grille */
    for (x = 1; x < VIEW_W; x++)
        C2D_DrawRectSolid((float)(x * TS), 0, 0.1f, 1, BOT_H, grid);
    for (y = 1; y < VIEW_H; y++)
        C2D_DrawRectSolid(0, (float)(y * TS), 0.1f, BOT_W, 1, grid);

    /* teleporteurs (visibles uniquement dans l'editeur) */
    for (i = 0; i < teleCount; i++) {
        int tx = teles[i].x - ecx, ty = teles[i].y - ecy;
        if (teles[i].zone == curZone && tx >= 0 && tx < VIEW_W && ty >= 0 && ty < VIEW_H)
            drawTeleMark((float)(tx * TS), (float)(ty * TS), teles[i].link);
    }

    /* point de depart */
    if (spawnTX >= ecx && spawnTX < ecx + VIEW_W &&
        spawnTY >= ecy && spawnTY < ecy + VIEW_H) {
        float sx = (float)((spawnTX - ecx) * TS);
        float sy = (float)((spawnTY - ecy) * TS);
        C2D_DrawRectSolid(sx + 2, sy + 2, 0.2f, 12, 12, COL(20, 20, 60));
        C2D_DrawRectSolid(sx + 4, sy + 4, 0.21f, 8, 8, COL(255, 220, 80));
    }
}

/* ecran du haut : la zone entiere, la palette de blocs et l'aide */
static void drawEditTop(void) {
    const char *help[] = {
        "Stylet : peindre",
        "L / R : bloc",
        "B + stylet : gomme",
        "Y + stylet : départ",
        "Y + croix : n° lien",
        "Croix : défiler",
        "X : zones",
        "A : tester",
        "SELECT : menu",
        "START : quitter",
    };
    Ov o = ovLayout(10, 10, 250, 150);
    u32 white = COL(255, 255, 255);
    u32 gray = COL(170, 170, 190);
    u32 yellow = COL(255, 220, 80);
    char buf[96];
    int i;
    float vx, vy, vw, vh;

    /* zone entiere */
    ovDraw(&o);
    ovTeles(&o);

    /* partie affichee sur l'ecran du bas */
    vx = ovX(&o, (float)ecx);
    vy = ovY(&o, (float)ecy);
    vw = (float)VIEW_W * o.s / o.k;
    vh = (float)VIEW_H * o.s / o.k;
    C2D_DrawRectSolid(vx, vy, 0.1f, vw, 2, yellow);
    C2D_DrawRectSolid(vx, vy + vh - 2, 0.1f, vw, 2, yellow);
    C2D_DrawRectSolid(vx, vy, 0.1f, 2, vh, yellow);
    C2D_DrawRectSolid(vx + vw - 2, vy, 0.1f, 2, vh, yellow);

    /* point de depart */
    {
        float ts = (float)o.s / o.k;
        if (ts < 3.0f)
            ts = 3.0f;
        C2D_DrawRectSolid(ovX(&o, (float)spawnTX) - 1, ovY(&o, (float)spawnTY) - 1,
                          0.2f, ts + 2, ts + 2, white);
        C2D_DrawRectSolid(ovX(&o, (float)spawnTX), ovY(&o, (float)spawnTY),
                          0.21f, ts, ts, COL(220, 60, 60));
    }

    /* aide */
    for (i = 0; i < 10; i++)
        drawText(help[i], 268, 10.0f + i * 14, 0.4f, gray);

    /* palette : tous les blocs, puis le teleporteur */
    for (i = 0; i <= tileCount; i++) {
        float bx = 6.0f + (i % 18) * 22;
        float by = 168.0f + (i / 18) * 22;

        if (i == editTile)
            C2D_DrawRectSolid(bx - 2, by - 2, 0.0f, 20, 20, yellow);
        if (i < tileCount) {
            drawTile(i, bx, by, 1.0f, 0.02f);
        } else {
            C2D_DrawRectSolid(bx, by, 0.02f, 16, 16, teleColor(editLink));
            drawTextCX("T", bx + 8, by + 1, 0.45f, white);
        }
    }

    /* info sur le bloc choisi */
    if (editTile < tileCount) {
        drawText(tiles[editTile].name, 10, 214, 0.5f, white);
        snprintf(buf, sizeof buf, "%s%s", tiles[editTile].solid ? "Bloque le joueur" : "Traversable",
                 tiles[editTile].wild ? " - monstres sauvages" : "");
        drawText(buf, 150, 216, 0.4f, gray);
    } else {
        int n = teleLinkCount(editLink);
        snprintf(buf, sizeof buf, "Téléporteur : lien %d", editLink);
        drawText(buf, 10, 214, 0.5f, white);
        if (n == 0)
            snprintf(buf, sizeof buf, "Libre : pose-en un ici, puis le jumeau (même n°)");
        else if (n == 1)
            snprintf(buf, sizeof buf, "1 sortie : pose le jumeau (même n°)");
        else
            snprintf(buf, sizeof buf, "%d sorties reliées en anneau", n);
        drawText(buf, 10, 230, 0.4f, gray);
    }

    if (editMsgTimer > 0)
        drawText(editMsg, 220, 226, 0.45f, yellow);
}

/* ---------- Dessin : editeur de blocs ---------- */

static void drawTileEdBottom(void) {
    const Tile *t = &tiles[teSel];
    u32 white = COL(255, 255, 255);
    u32 gray = COL(170, 170, 190);
    u32 yellow = COL(255, 220, 80);
    u32 grid = C2D_Color32(0, 0, 0, 70);
    char buf[64];
    int x, y, i;

    /* grille de dessin */
    C2D_DrawRectSolid(TE_GX - 2, TE_GY - 2, 0.0f, TS * TE_GC + 4, TS * TE_GC + 4, white);
    for (y = 0; y < TS; y++)
        for (x = 0; x < TS; x++)
            C2D_DrawRectSolid((float)(TE_GX + x * TE_GC), (float)(TE_GY + y * TE_GC), 0.05f,
                              TE_GC, TE_GC, palCol[t->px[y][x]]);
    for (i = 1; i < TS; i++) {
        C2D_DrawRectSolid((float)(TE_GX + i * TE_GC), TE_GY, 0.1f, 1, TS * TE_GC, grid);
        C2D_DrawRectSolid(TE_GX, (float)(TE_GY + i * TE_GC), 0.1f, TS * TE_GC, 1, grid);
    }

    /* palette */
    for (i = 0; i < PAL_N; i++) {
        float bx = (float)(TE_PX + (i % 8) * TE_PC);
        float by = (float)(TE_PY + (i / 8) * TE_PC);
        if (i == teColor)
            C2D_DrawRectSolid(bx - 1, by - 1, 0.05f, TE_PC + 1, TE_PC + 1, yellow);
        C2D_DrawRectSolid(bx, by, 0.1f, TE_PC - 2, TE_PC - 2, palCol[i]);
    }

    /* boutons */
    for (i = 0; i < TB_N; i++) {
        Button b = teBtn[i];
        bool selected = false;

        if (i == TB_PENCIL || i == TB_FILL || i == TB_PICK)
            selected = (teTool == i - TB_PENCIL);
        else if (i == TB_SOLID)
            b.label = t->solid ? "Solide : oui" : "Solide : non";
        else if (i == TB_WILD)
            b.label = t->wild ? "Monstres : oui" : "Monstres : non";
        else if (i == TB_DEL) {
            if (teDelTimer > 0) {
                b.label = "Sûr ?";
                selected = true;
            } else {
                b.label = (teSel < BASE_TILES) ? "Réinit." : "Suppr.";
            }
        } else if (i == TB_UNDO) {
            b.enabled = teHasUndo;
        }
        drawButtonSc(&b, selected, 0.45f);
    }

    snprintf(buf, sizeof buf, "%d / %d", teSel + 1, tileCount);
    drawTextCX(buf, 86, 179, 0.5f, white);

    if (teMsgTimer > 0)
        drawText(teMsg, 176, 220, 0.4f, yellow);
    else
        drawText("Touche la grille pour dessiner", 176, 220, 0.4f, gray);
}

static void drawTileEdTop(void) {
    const Tile *t = &tiles[teSel];
    u32 white = COL(255, 255, 255);
    u32 gray = COL(170, 170, 190);
    u32 yellow = COL(255, 220, 80);
    char buf[96];
    int i, x, y;
    const char *rating;
    u32 ratingCol;

    /* grand apercu */
    C2D_DrawRectSolid(10, 10, 0.0f, 132, 132, white);
    drawTile(teSel, 12, 12, 8.0f, 0.02f);

    /* le bloc repete 3 x 3, comme sur la carte (en double taille) */
    C2D_DrawRectSolid(158, 10, 0.0f, 100, 100, white);
    for (y = 0; y < 3; y++)
        for (x = 0; x < 3; x++)
            drawTile(teSel, (float)(160 + x * 32), (float)(12 + y * 32), 2.0f, 0.02f);

    /* aide */
    drawText("Stylet : dessiner", 268, 10, 0.4f, gray);
    drawText("L / R : autre bloc", 268, 24, 0.4f, gray);
    drawText("B : retour", 268, 38, 0.4f, gray);
    drawText("START : quitter", 268, 52, 0.4f, gray);

    /* tous les blocs */
    for (i = 0; i < tileCount; i++) {
        float bx = 8.0f + (i % 16) * 24;
        float by = 150.0f + (i / 16) * 24;
        if (i == teSel)
            C2D_DrawRectSolid(bx - 2, by - 2, 0.0f, 20, 20, yellow);
        drawTile(i, bx, by, 1.0f, 0.02f);
    }

    /* infos */
    drawText(t->name, 10, 198, 0.55f, white);
    snprintf(buf, sizeof buf, "%s - %s - %s", (teSel < BASE_TILES) ? "Bloc d'origine" : "Bloc perso",
             t->solid ? "bloque le joueur" : "traversable",
             t->wild ? "monstres sauvages" : "sans monstres");
    drawText(buf, 10, 218, 0.4f, gray);

    if (t->nRuns <= 12) {
        rating = "léger";
        ratingCol = COL(120, 220, 120);
    } else if (t->nRuns <= 40) {
        rating = "moyen";
        ratingCol = COL(255, 220, 80);
    } else {
        rating = "lourd : peut ralentir le jeu";
        ratingCol = COL(255, 140, 80);
    }
    snprintf(buf, sizeof buf, "Détail : %d rectangles (%s)", t->nRuns, rating);
    drawText(buf, 10, 230, 0.4f, ratingCol);
}

/* ---------- Dessin : outils de zone ---------- */

static void drawZoneTop(void) {
    Ov o = ovLayout(10, 10, 250, 150);
    u32 white = COL(255, 255, 255);
    u32 gray = COL(170, 170, 190);
    u32 yellow = COL(255, 220, 80);
    char buf[96];
    float ts;
    int link, n = 0, nw, nh;

    ovDraw(&o);
    ovTeles(&o);
    ts = (float)o.s / o.k;
    if (ts < 3.0f)
        ts = 3.0f;
    C2D_DrawRectSolid(ovX(&o, (float)spawnTX) - 1, ovY(&o, (float)spawnTY) - 1,
                      0.2f, ts + 2, ts + 2, white);
    C2D_DrawRectSolid(ovX(&o, (float)spawnTX), ovY(&o, (float)spawnTY),
                      0.21f, ts, ts, COL(220, 60, 60));

    /* infos */
    nw = mapW + zDelta[0] + zDelta[1];
    nh = mapH + zDelta[2] + zDelta[3];
    snprintf(buf, sizeof buf, "Zone %d / %d", curZone, zoneCount);
    drawText(buf, 268, 10, 0.5f, white);
    snprintf(buf, sizeof buf, "Taille : %d x %d", mapW, mapH);
    drawText(buf, 268, 30, 0.42f, gray);
    if (nw != mapW || nh != mapH) {
        snprintf(buf, sizeof buf, "Après : %d x %d", nw, nh);
        drawText(buf, 268, 46, 0.42f, yellow);
    }
    drawText("Nouvelles cases :", 268, 70, 0.4f, gray);
    drawTile(editTile, 268, 86, 1.0f, 0.02f);
    drawText(tiles[editTile].name, 290, 88, 0.42f, white);
    drawText("L / R : changer", 268, 108, 0.4f, gray);
    drawText("B : retour", 268, 122, 0.4f, gray);

    if (zMsgTimer > 0)
        drawText(zMsg, 10, 166, 0.45f, yellow);

    /* liste des liens de teleporteurs */
    drawText("Liens de téléporteurs :", 10, 180, 0.4f, gray);
    for (link = 1; link <= MAX_LINK && n < 8; link++) {
        int cnt = teleLinkCount(link), a = -1, b = -1, i;
        u32 col = white;

        if (cnt == 0)
            continue;
        for (i = 0; i < teleCount; i++) {
            if (teles[i].link != link)
                continue;
            if (a < 0) a = i;
            else if (b < 0) b = i;
        }
        if (cnt == 1) {
            snprintf(buf, sizeof buf, "%d : Z%d (%d,%d) sans jumeau !", link,
                     teles[a].zone, teles[a].x, teles[a].y);
            col = COL(255, 150, 70);
        } else if (cnt == 2) {
            snprintf(buf, sizeof buf, "%d : Z%d (%d,%d) <> Z%d (%d,%d)", link,
                     teles[a].zone, teles[a].x, teles[a].y,
                     teles[b].zone, teles[b].x, teles[b].y);
        } else {
            snprintf(buf, sizeof buf, "%d : %d sorties en anneau", link, cnt);
        }
        drawText(buf, 10.0f + (n / 4) * 200, 194.0f + (n % 4) * 11, 0.36f, col);
        n++;
    }
    if (n == 0)
        drawText("(aucun : choisis l'outil T dans la palette de la carte)", 10, 194, 0.36f, gray);
}

static void drawZoneBottom(void) {
    u32 white = COL(255, 255, 255);
    u32 gray = COL(170, 170, 190);
    char buf[48];
    int i;

    snprintf(buf, sizeof buf, "Zone %d / %d", curZone, zoneCount);
    drawText(buf, 10, 8, 0.6f, white);
    drawText("Ajouter / retirer des cases :", 10, 27, 0.42f, gray);

    for (i = 0; i < 4; i++) {
        float y = 44.0f + i * 34;
        drawText(ZSIDE[i], 10, y + 5, 0.55f, white);
        snprintf(buf, sizeof buf, "%+d", zDelta[i]);
        drawTextCX(buf, 188, y + 4, 0.6f, zDelta[i] != 0 ? COL(255, 220, 80) : white);
        drawButton(&zBtn[i * 2], false);
        drawButton(&zBtn[i * 2 + 1], false);
    }

    for (i = ZB_APPLY; i < ZB_N; i++) {
        Button b = zBtn[i];
        if (i == ZB_DEL) {
            b.label = (zDelTimer > 0) ? "Sûr ?" : "Suppr. zone";
            b.enabled = (zoneCount > 1);
        } else if (i == ZB_PREV) {
            b.enabled = (curZone > 1);
        } else if (i == ZB_NEXT) {
            b.enabled = (curZone < zoneCount);
        } else if (i == ZB_NEW) {
            b.enabled = (zoneCount < MAX_ZONES);
        }
        drawButtonSc(&b, i == ZB_DEL && zDelTimer > 0, 0.5f);
    }
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
        "Dessine les zones au stylet. X : agrandir, nouvelle zone, téléporteurs.",
        "Redessine les textures et crée tes propres blocs.",
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
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS * 4);
    C2D_Prepare();

    top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    bot = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
    g_textBuf = C2D_TextBufNew(4096);

    initTiles();
    loadTiles();
    loadTeles();
    zoneCount = countZones();
    loadZone(1);
    loadAll();

    while (running && aptMainLoop()) {
        u32 kDown, kHeld;

        dbgTick();

        hidScanInput();
        kDown = hidKeysDown();
        kHeld = hidKeysHeld();

        if (kDown & KEY_START) {
            if (state == S_EDIT || state == S_ZONE)
                saveWorld();
            else if (state == S_TILEEDIT)
                saveTiles();
            else if (state == S_FORM)
                saveAll();
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
        else if (state == S_TILEEDIT)
            updateTileEd(kDown, kHeld);
        else if (state == S_ZONE)
            updateZone(kDown, kHeld);
        else
            updateEdit(kDown, kHeld);

        rectBudget = DETAIL_BUDGET;
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
        } else if (state == S_TILEEDIT) {
            C2D_TargetClear(top, COL(18, 24, 48));
            C2D_SceneBegin(top);
            drawTileEdTop();

            C2D_TargetClear(bot, COL(30, 30, 40));
            C2D_SceneBegin(bot);
            drawTileEdBottom();
        } else if (state == S_ZONE) {
            C2D_TargetClear(top, COL(18, 24, 48));
            C2D_SceneBegin(top);
            drawZoneTop();

            C2D_TargetClear(bot, COL(30, 30, 40));
            C2D_SceneBegin(bot);
            drawZoneBottom();
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
