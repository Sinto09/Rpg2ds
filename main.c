/*
 * RPG 2DS - Etape 3 : l'editeur de monstres
 *
 *  - Ecran titre : Jouer / Editeur de cartes / Editeur de monstres / Quitter
 *  - Jeu : carte vue de dessus, deplacement (croix/stick, B = courir)
 *  - Editeur : ecran du bas = zone de dessin (stylet), ecran du haut = carte entiere
 *  - La carte est sauvegardee sur la carte SD : sdmc:/3ds/rpg2ds/map1.bin
 *  - Les monstres sont sauvegardes dans :       sdmc:/3ds/rpg2ds/monsters.bin
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
#define MON_REC      (NAME_BYTES + 10)
#define MON_HEADER   6

/* ---------- Tuiles ---------- */

enum { T_GRASS, T_WALL, T_WATER, T_TREE, T_PATH, T_FLOOR, T_DOOR, T_COUNT };

typedef struct {
    const char *name;
    u32 color;
    bool solid;
} TileDef;

static TileDef TILES[T_COUNT];
static u8 map[MAP_H][MAP_W];
static int spawnTX = 12, spawnTY = 13;   /* point de depart (en tuiles) */

static void initTiles(void) {
    TILES[T_GRASS] = (TileDef){ "Herbe",  COL(76, 160, 64),   false };
    TILES[T_WALL]  = (TileDef){ "Mur",    COL(120, 120, 130), true };
    TILES[T_WATER] = (TileDef){ "Eau",    COL(50, 110, 200),  true };
    TILES[T_TREE]  = (TileDef){ "Arbre",  COL(30, 100, 40),   true };
    TILES[T_PATH]  = (TileDef){ "Chemin", COL(200, 170, 110), false };
    TILES[T_FLOOR] = (TileDef){ "Sol",    COL(170, 130, 90),  false };
    TILES[T_DOOR]  = (TileDef){ "Porte",  COL(110, 70, 30),   false };
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
            map[y][x] = (tmp[y][x] < T_COUNT) ? tmp[y][x] : T_GRASS;

    spawnTX = (header[7] < MAP_W) ? header[7] : 1;
    spawnTY = (header[8] < MAP_H) ? header[8] : 1;
    return true;
}

static void loadOrDefault(void) {
    if (!loadMap())
        buildMap();
}

/* ---------- Monstres : donnees ---------- */

#define N_COLORS 11
#define N_SHAPES 6

typedef struct {
    char name[NAME_BYTES];
    int hp, atk, def, spd;   /* PV : 1 a 999, le reste : 1 a 99 */
    int color;               /* 0 .. N_COLORS-1 */
    int shape;               /* 0 .. N_SHAPES-1 */
} Monster;

enum { F_NAME, F_HP, F_ATK, F_DEF, F_SPD, F_COLOR, F_SHAPE, F_COUNT };

static Monster monsters[MAX_MONSTERS];
static int monsterCount = 0;
static int monSel = 0;          /* monstre affiche dans l'editeur */
static int delTimer = 0;        /* > 0 : on attend la confirmation de suppression */
static int monHeldBtn = -1;     /* bouton garde appuye (repetition) */
static int monHeldFrames = 0;

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

static void setMonster(Monster *m, const char *name, int hp, int atk, int def,
                       int spd, int color, int shape) {
    memset(m, 0, sizeof *m);
    snprintf(m->name, NAME_BYTES, "%s", name);
    m->hp = clampInt(hp, 1, 999);
    m->atk = clampInt(atk, 1, 99);
    m->def = clampInt(def, 1, 99);
    m->spd = clampInt(spd, 1, 99);
    m->color = clampInt(color, 0, N_COLORS - 1);
    m->shape = clampInt(shape, 0, N_SHAPES - 1);
}

static void defaultMonsters(void) {
    setMonster(&monsters[0], "Gelée", 20, 5, 2, 3, 3, 0);
    setMonster(&monsters[1], "Chauve-souris", 14, 6, 1, 9, 6, 1);
    setMonster(&monsters[2], "Crâne", 28, 9, 4, 4, 10, 3);
    monsterCount = 3;
    monSel = 0;
}

static bool monsterNew(void) {
    char name[NAME_BYTES];

    if (monsterCount >= MAX_MONSTERS)
        return false;
    snprintf(name, sizeof name, "Monstre %d", monsterCount + 1);
    setMonster(&monsters[monsterCount], name, 30, 8, 4, 5, 0, 0);
    monSel = monsterCount;
    monsterCount++;
    return true;
}

/* supprime le monstre choisi (on en garde toujours au moins un) */
static bool monsterDelete(void) {
    int i;

    if (monsterCount <= 1)
        return false;
    for (i = monSel; i < monsterCount - 1; i++)
        monsters[i] = monsters[i + 1];
    monsterCount--;
    if (monSel >= monsterCount)
        monSel = monsterCount - 1;
    return true;
}

static void adjustField(Monster *m, int field, int dir, int step) {
    switch (field) {
    case F_HP:    m->hp  = clampInt(m->hp  + dir * step, 1, 999); break;
    case F_ATK:   m->atk = clampInt(m->atk + dir * step, 1, 99);  break;
    case F_DEF:   m->def = clampInt(m->def + dir * step, 1, 99);  break;
    case F_SPD:   m->spd = clampInt(m->spd + dir * step, 1, 99);  break;
    case F_COLOR: m->color = (m->color + N_COLORS + dir) % N_COLORS; break;
    case F_SHAPE: m->shape = (m->shape + N_SHAPES + dir) % N_SHAPES; break;
    default: break;
    }
}

static void fieldText(const Monster *m, int field, char *buf, size_t n) {
    switch (field) {
    case F_HP:    snprintf(buf, n, "%d", m->hp); break;
    case F_ATK:   snprintf(buf, n, "%d", m->atk); break;
    case F_DEF:   snprintf(buf, n, "%d", m->def); break;
    case F_SPD:   snprintf(buf, n, "%d", m->spd); break;
    case F_COLOR: snprintf(buf, n, "%s", COLOR_NAMES[m->color]); break;
    case F_SHAPE: snprintf(buf, n, "%s", SHAPE_NAMES[m->shape]); break;
    default:      buf[0] = '\0'; break;
    }
}

/* clavier de la console pour choisir un nom */
static bool askName(char *name, size_t cap) {
    SwkbdState swkbd;
    SwkbdButton button;
    char buf[NAME_BYTES];

    swkbdInit(&swkbd, SWKBD_TYPE_NORMAL, 2, NAME_CHARS);
    swkbdSetInitialText(&swkbd, name);
    swkbdSetHintText(&swkbd, "Nom du monstre");
    swkbdSetButton(&swkbd, SWKBD_BUTTON_LEFT, "Annuler", false);
    swkbdSetButton(&swkbd, SWKBD_BUTTON_RIGHT, "OK", true);
    swkbdSetValidation(&swkbd, SWKBD_NOTEMPTY_NOTBLANK, 0, 0);

    button = swkbdInputText(&swkbd, buf, sizeof buf);
    if (button != SWKBD_BUTTON_RIGHT)
        return false;
    snprintf(name, cap, "%s", buf);
    return true;
}

/* ---------- Monstres : sauvegarde / chargement ---------- */

static void packMonster(const Monster *m, u8 *rec) {
    memset(rec, 0, MON_REC);
    strncpy((char *)rec, m->name, NAME_BYTES - 1);
    rec[NAME_BYTES + 0] = (u8)(m->hp & 0xFF);
    rec[NAME_BYTES + 1] = (u8)(m->hp >> 8);
    rec[NAME_BYTES + 2] = (u8)(m->atk & 0xFF);
    rec[NAME_BYTES + 3] = (u8)(m->atk >> 8);
    rec[NAME_BYTES + 4] = (u8)(m->def & 0xFF);
    rec[NAME_BYTES + 5] = (u8)(m->def >> 8);
    rec[NAME_BYTES + 6] = (u8)(m->spd & 0xFF);
    rec[NAME_BYTES + 7] = (u8)(m->spd >> 8);
    rec[NAME_BYTES + 8] = (u8)m->color;
    rec[NAME_BYTES + 9] = (u8)m->shape;
}

static void unpackMonster(Monster *m, const u8 *rec) {
    memset(m, 0, sizeof *m);
    memcpy(m->name, rec, NAME_BYTES);
    m->name[NAME_BYTES - 1] = '\0';
    if (m->name[0] == '\0')
        snprintf(m->name, NAME_BYTES, "Monstre");
    m->hp  = clampInt(rec[NAME_BYTES + 0] | (rec[NAME_BYTES + 1] << 8), 1, 999);
    m->atk = clampInt(rec[NAME_BYTES + 2] | (rec[NAME_BYTES + 3] << 8), 1, 99);
    m->def = clampInt(rec[NAME_BYTES + 4] | (rec[NAME_BYTES + 5] << 8), 1, 99);
    m->spd = clampInt(rec[NAME_BYTES + 6] | (rec[NAME_BYTES + 7] << 8), 1, 99);
    m->color = rec[NAME_BYTES + 8] % N_COLORS;
    m->shape = rec[NAME_BYTES + 9] % N_SHAPES;
}

static bool saveMonsters(void) {
    u8 header[MON_HEADER] = { 'R', 'P', 'G', 'M', 1, 0 };
    u8 rec[MON_REC];
    FILE *f;
    bool ok;
    int i;

    header[5] = (u8)monsterCount;

    mkdir("sdmc:/3ds", 0777);
    mkdir(SAVE_DIR, 0777);

    f = fopen(MON_FILE, "wb");
    if (f == NULL)
        return false;

    ok = (fwrite(header, 1, MON_HEADER, f) == MON_HEADER);
    for (i = 0; ok && i < monsterCount; i++) {
        packMonster(&monsters[i], rec);
        ok = (fwrite(rec, 1, MON_REC, f) == MON_REC);
    }
    fclose(f);
    return ok;
}

static bool loadMonsters(void) {
    static Monster tmp[MAX_MONSTERS];
    u8 header[MON_HEADER];
    u8 rec[MON_REC];
    FILE *f;
    int i, count;

    f = fopen(MON_FILE, "rb");
    if (f == NULL)
        return false;

    if (fread(header, 1, MON_HEADER, f) != MON_HEADER ||
        memcmp(header, "RPGM", 4) != 0 || header[4] != 1 ||
        header[5] < 1 || header[5] > MAX_MONSTERS) {
        fclose(f);
        return false;
    }

    count = header[5];
    for (i = 0; i < count; i++) {
        if (fread(rec, 1, MON_REC, f) != MON_REC) {
            fclose(f);
            return false;
        }
        unpackMonster(&tmp[i], rec);
    }
    fclose(f);

    memcpy(monsters, tmp, sizeof(Monster) * (size_t)count);
    monsterCount = count;
    if (monSel >= monsterCount)
        monSel = 0;
    return true;
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

/* ---------- Etat global ---------- */

enum { S_TITLE, S_PLAY, S_EDIT, S_MONSTERS };

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

#define N_TITLE_BUTTONS 4

static Button titleButtons[N_TITLE_BUTTONS] = {
    { 50,  12, 220, 40, "Jouer",               true },
    { 50,  60, 220, 40, "Éditeur de cartes",   true },
    { 50, 108, 220, 40, "Éditeur de monstres", true },
    { 50, 156, 220, 40, "Quitter",             true },
};

static Button playTitleButton = { 222, 10, 88, 40, "Titre", true };

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

/* ---------- Changements d'etat ---------- */

static void startPlay(void) {
    placePlayerAtTile(spawnTX, spawnTY);
    player.dir = 0;
    dbgReset();
    playTitleButton.label = fromEditor ? "Éditeur" : "Titre";
    updateCamera();
    state = S_PLAY;
}

static void leavePlay(void) {
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

static void enterMonsters(void) {
    if (monSel >= monsterCount)
        monSel = 0;
    delTimer = 0;
    monHeldBtn = -1;
    monHeldFrames = 0;
    state = S_MONSTERS;
}

static void activate(int i) {
    if (i == 0) {
        loadOrDefault();
        fromEditor = false;
        startPlay();
    } else if (i == 1) {
        loadOrDefault();
        enterEditor();
    } else if (i == 2) {
        enterMonsters();
    } else if (i == 3) {
        running = false;
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

static void updatePlay(u32 kDown, u32 kHeld) {
    int speed = (kHeld & KEY_B) ? 3 : 2;
    int dx = 0, dy = 0, i;
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

    if (kDown & KEY_SELECT)
        leavePlay();

    if (kDown & KEY_TOUCH) {
        touchPosition t;
        hidTouchRead(&t);
        if (inRect(&playTitleButton, t.px, t.py))
            leavePlay();
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
    if (kDown & KEY_L) editTile = (editTile + T_COUNT - 1) % T_COUNT;
    if (kDown & KEY_R) editTile = (editTile + 1) % T_COUNT;

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
        state = S_TITLE;
    }
}

/* ---------- Mise a jour : editeur de monstres ---------- */

static Button monPrev = {   6,   4,  60, 26, "<",       true };
static Button monNext = { 254,   4,  60, 26, ">",       true };
static Button monName = {  70,  36, 244, 22, "",        true };
static Button monNew  = {   6, 208,  96, 28, "Nouveau", true };
static Button monDel  = { 108, 208,  96, 28, "Suppr.",  true };
static Button monBack = { 210, 208, 104, 28, "Retour",  true };

#define MON_ROW_Y(r) (36.0f + (r) * 24.0f)

static Button rowButton(int r, bool plus) {
    Button b = { plus ? 264.0f : 212.0f, MON_ROW_Y(r), plus ? 50.0f : 46.0f,
                 22.0f, plus ? "+" : "-", true };
    return b;
}

/* renvoie le numero du bouton touche (ou -1) :
 * 0 <  1 >  2 Nouveau  3 Suppr.  4 Retour  5 Nom  12..23 les boutons - et + */
static int hitTestMon(int px, int py) {
    int r;

    if (inRect(&monPrev, px, py)) return 0;
    if (inRect(&monNext, px, py)) return 1;
    if (inRect(&monNew,  px, py)) return 2;
    if (inRect(&monDel,  px, py)) return 3;
    if (inRect(&monBack, px, py)) return 4;
    if (inRect(&monName, px, py)) return 5;
    for (r = F_HP; r < F_COUNT; r++) {
        Button minus = rowButton(r, false);
        Button plus = rowButton(r, true);
        if (inRect(&minus, px, py)) return 10 + r * 2;
        if (inRect(&plus, px, py))  return 11 + r * 2;
    }
    return -1;
}

static void pressMon(int id, int step) {
    Monster *m = &monsters[monSel];

    if (id != 3)
        delTimer = 0;

    switch (id) {
    case 0:
        monSel = (monSel + monsterCount - 1) % monsterCount;
        break;
    case 1:
        monSel = (monSel + 1) % monsterCount;
        break;
    case 2:
        monsterNew();
        break;
    case 3:
        if (monsterCount > 1) {
            if (delTimer > 0) {
                monsterDelete();
                delTimer = 0;
            } else {
                delTimer = 120;   /* il faut retoucher le bouton pour confirmer */
            }
        }
        break;
    case 4:
        saveMonsters();
        state = S_TITLE;
        break;
    case 5:
        askName(m->name, NAME_BYTES);
        break;
    default:
        if (id >= 12 && id <= 23)
            adjustField(m, (id - 10) / 2, (((id - 10) % 2) != 0) ? 1 : -1, step);
        break;
    }
}

static void updateMonsters(u32 kDown, u32 kHeld) {
    touchPosition t;

    if (delTimer > 0)
        delTimer--;

    if (kDown & KEY_L) pressMon(0, 1);
    if (kDown & KEY_R) pressMon(1, 1);

    if (kDown & KEY_SELECT) {
        saveMonsters();
        state = S_TITLE;
        return;
    }

    if (kDown & KEY_TOUCH) {
        hidTouchRead(&t);
        monHeldBtn = hitTestMon(t.px, t.py);
        monHeldFrames = 0;
        if (monHeldBtn >= 0) {
            pressMon(monHeldBtn, 1);
            if (monHeldBtn < 12)   /* seuls les + et - se repetent */
                monHeldBtn = -1;
        }
    } else if ((kHeld & KEY_TOUCH) && monHeldBtn >= 12 && monHeldBtn <= 19) {
        /* PV et stats : on garde le stylet appuye, ca accelere */
        hidTouchRead(&t);
        if (hitTestMon(t.px, t.py) != monHeldBtn) {
            monHeldBtn = -1;
        } else {
            monHeldFrames++;
            if (monHeldFrames > 20 && monHeldFrames % 3 == 0)
                pressMon(monHeldBtn,
                         monHeldFrames > 120 ? 10 : (monHeldFrames > 60 ? 5 : 1));
        }
    } else if (!(kHeld & KEY_TOUCH)) {
        monHeldBtn = -1;
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
    drawTextCX("Étape 3 : l'éditeur de monstres", TOP_W / 2, 130, 0.5f,
               COL(150, 160, 190));
}

static void drawTitleBottom(void) {
    int i;

    for (i = 0; i < N_TITLE_BUTTONS; i++)
        drawButton(&titleButtons[i], i == sel);

    drawTextCX("Croix + A, ou touche un bouton", BOT_W / 2, 208, 0.45f,
               COL(170, 170, 190));
}

/* ---------- Dessin : tuiles ---------- */

static void drawTileDetail(int t, int tx, int ty, float sx, float sy) {
    switch (t) {
    case T_GRASS:
        if ((tx * 7 + ty * 13) % 5 == 0)
            C2D_DrawRectSolid(sx + 5, sy + 7, 0.05f, 2, 2, COL(55, 130, 48));
        break;
    case T_WATER:
        if ((tx + ty) % 2 == 0)
            C2D_DrawRectSolid(sx + 3, sy + 6, 0.05f, 10, 2, COL(110, 170, 240));
        break;
    case T_TREE:
        C2D_DrawRectSolid(sx + 3, sy + 2, 0.05f, 10, 8, COL(50, 150, 60));
        C2D_DrawRectSolid(sx + 6, sy + 10, 0.05f, 4, 6, COL(110, 70, 30));
        break;
    case T_WALL:
        C2D_DrawRectSolid(sx, sy + 7, 0.05f, TS, 1, COL(80, 80, 90));
        C2D_DrawRectSolid(sx + 8, sy, 0.05f, 1, 7, COL(80, 80, 90));
        C2D_DrawRectSolid(sx + 4, sy + 8, 0.05f, 1, 8, COL(80, 80, 90));
        break;
    case T_DOOR:
        C2D_DrawRectSolid(sx + 3, sy, 0.05f, 10, TS, COL(80, 45, 15));
        break;
    default:
        break;
    }
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

    for (ty = ty0; ty <= ty1; ty++) {
        for (tx = tx0; tx <= tx1; tx++) {
            float sx = (float)(tx * TS - camX);
            float sy = (float)(ty * TS - camY);
            int t = map[ty][tx];
            C2D_DrawRectSolid(sx, sy, 0.0f, TS, TS, TILES[t].color);
            drawTileDetail(t, tx, ty, sx, sy);
        }
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

    C2D_DrawRectSolid(4, 4, 0.4f, 128, 42, C2D_Color32(0, 0, 0, 150));

    snprintf(buf, sizeof buf, "FPS : %d", dbgFps);
    drawText(buf, 8, 6, 0.45f, COL(255, 255, 255));

    snprintf(buf, sizeof buf, "Vitesse : %d px/s", (int)(dbgSpeedPxS + 0.5f));
    drawText(buf, 8, 20, 0.45f, COL(255, 220, 80));

    snprintf(buf, sizeof buf, "          %d.%d px/image", sf10 / 10, sf10 % 10);
    drawText(buf, 8, 32, 0.4f, COL(170, 170, 190));
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

    /* joueur */
    px = MX + (player.x + PW / 2) * (float)MS / TS;
    py = MY + (player.y + PH / 2) * (float)MS / TS;
    C2D_DrawRectSolid(px - 1, py - 1, 0.2f, 3, 3, COL(255, 0, 0));

    /* bouton */
    drawButton(&playTitleButton, false);

    /* infos */
    snprintf(buf, sizeof buf, "Position : %d, %d", cx, cy);
    drawText(buf, 10, 140, 0.5f, white);
    snprintf(buf, sizeof buf, "Sur : %s", TILES[map[cy][cx]].name);
    drawText(buf, 10, 156, 0.5f, white);
    drawText("Stick : se déplacer", 10, 172, 0.5f, gray);
    drawText("B : courir", 10, 188, 0.5f, gray);
    drawText(fromEditor ? "SELECT : éditeur    START : quitter"
                        : "SELECT : titre    START : quitter",
             10, 204, 0.5f, gray);
}

/* ---------- Dessin : editeur ---------- */

/* ecran du bas : la zone qu'on modifie au stylet */
static void drawEditBottom(void) {
    int x, y;
    u32 grid = C2D_Color32(0, 0, 0, 60);

    for (y = 0; y < VIEW_H; y++) {
        for (x = 0; x < VIEW_W; x++) {
            int tx = ecx + x, ty = ecy + y;
            int t = map[ty][tx];
            float sx = (float)(x * TS), sy = (float)(y * TS);
            C2D_DrawRectSolid(sx, sy, 0.0f, TS, TS, TILES[t].color);
            drawTileDetail(t, tx, ty, sx, sy);
        }
    }

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
        "L / R : tuile",
        "B + stylet : gomme",
        "Y + stylet : départ",
        "Croix : défiler",
        "A : tester",
        "X : sauver",
        "SELECT : titre",
        "START : quitter",
    };
    u32 white = COL(255, 255, 255);
    u32 gray = COL(170, 170, 190);
    u32 yellow = COL(255, 220, 80);
    int i, tx, ty;
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

    /* palette */
    for (i = 0; i < T_COUNT; i++) {
        float bx = 10.0f + i * 36;
        float by = 172.0f;
        if (i == editTile)
            C2D_DrawRectSolid(bx - 3, by - 3, 0.0f, 34, 34, yellow);
        C2D_DrawRectSolid(bx, by, 0.02f, 28, 28, TILES[i].color);
        drawTileDetail(i, 0, 0, bx + 6, by + 6);
    }
    drawText(TILES[editTile].name, 270, 172, 0.6f, white);
    drawText(TILES[editTile].solid ? "Bloque le joueur" : "Traversable",
             270, 196, 0.4f, gray);

    if (editMsgTimer > 0)
        drawText(editMsg, 10, 212, 0.5f, yellow);
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

static void drawMonstersTop(void) {
    const Monster *m = &monsters[monSel];
    static const char *const labels[4] = { "PV", "Attaque", "Défense", "Vitesse" };
    int vals[4];
    const int caps[4] = { 200, 50, 50, 50 };
    u32 barCols[4] = { COL(220, 70, 70), COL(240, 150, 50),
                       COL(80, 160, 230), COL(100, 200, 110) };
    u32 white = COL(255, 255, 255);
    u32 gray = COL(170, 170, 190);
    u32 yellow = COL(255, 220, 80);
    char buf[48];
    int i;

    vals[0] = m->hp;
    vals[1] = m->atk;
    vals[2] = m->def;
    vals[3] = m->spd;

    /* apercu */
    C2D_DrawRectSolid(10, 10, 0.0f, 130, 130, COL(35, 45, 80));
    drawMonsterArt(m->shape, m->color, 27, 27, 6);

    /* nom, forme et couleur */
    drawText(m->name, 152, 12, 0.8f, white);
    snprintf(buf, sizeof buf, "%s - %s", SHAPE_NAMES[m->shape], COLOR_NAMES[m->color]);
    drawText(buf, 152, 42, 0.45f, gray);

    /* barres de stats */
    for (i = 0; i < 4; i++) {
        float y = 68.0f + i * 18;
        int v = (vals[i] > caps[i]) ? caps[i] : vals[i];
        float w = 150.0f * (float)v / (float)caps[i];

        drawText(labels[i], 152, y - 2, 0.45f, white);
        C2D_DrawRectSolid(212, y, 0.0f, 150, 12, COL(25, 30, 55));
        C2D_DrawRectSolid(212, y, 0.05f, w, 12, barCols[i]);
        snprintf(buf, sizeof buf, "%d", vals[i]);
        drawText(buf, 368, y - 2, 0.45f, white);
    }

    /* tous les monstres */
    drawText("Tes monstres", 10, 146, 0.45f, gray);
    for (i = 0; i < monsterCount; i++) {
        float x = 10.0f + (i % 16) * 23;
        float y = 166.0f + (i / 16) * 26;
        if (i == monSel)
            C2D_DrawRectSolid(x - 2, y - 2, 0.0f, 20, 20, yellow);
        drawMonsterArt(monsters[i].shape, monsters[i].color, x, y, 1);
    }

    drawText("L / R : monstre précédent / suivant", 10, 222, 0.4f, gray);
}

static void drawMonstersBottom(void) {
    const Monster *m = &monsters[monSel];
    static const char *const labels[F_COUNT] = {
        "Nom", "PV", "Attaque", "Défense", "Vitesse", "Couleur", "Forme"
    };
    u32 white = COL(255, 255, 255);
    u32 gray = COL(170, 170, 190);
    char buf[48];
    Button b;
    int r;

    /* en-tete : choix du monstre */
    drawButton(&monPrev, false);
    drawButton(&monNext, false);
    snprintf(buf, sizeof buf, "Monstre %d / %d", monSel + 1, monsterCount);
    drawTextInRect(buf, 66, 4, 188, 26, 0.55f, white);

    /* nom */
    drawText(labels[F_NAME], 10, 39, 0.5f, gray);
    b = monName;
    b.label = m->name;
    drawButton(&b, false);

    /* PV, stats, couleur, forme */
    for (r = F_HP; r < F_COUNT; r++) {
        float y = MON_ROW_Y(r);

        C2D_DrawRectSolid(6, y, 0.0f, 306, 22,
                          (r % 2) ? COL(40, 40, 55) : COL(48, 48, 66));
        drawText(labels[r], 10, y + 3, 0.5f, gray);

        fieldText(m, r, buf, sizeof buf);
        drawTextInRect(buf, 84, y, 124, 22, 0.5f, white);

        if (r == F_COLOR) {
            C2D_DrawRectSolid(186, y + 3, 0.1f, 16, 16, COL(255, 255, 255));
            C2D_DrawRectSolid(187, y + 4, 0.12f, 14, 14, monColor(m->color, false));
        }

        b = rowButton(r, false);
        drawButton(&b, false);
        b = rowButton(r, true);
        drawButton(&b, false);
    }

    /* boutons du bas */
    b = monNew;
    b.enabled = (monsterCount < MAX_MONSTERS);
    drawButton(&b, false);

    b = monDel;
    b.enabled = (monsterCount > 1);
    b.label = (delTimer > 0) ? "Sûr ?" : "Suppr.";
    drawButton(&b, delTimer > 0);

    drawButton(&monBack, false);
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
    if (!loadMonsters())
        defaultMonsters();

    while (running && aptMainLoop()) {
        u32 kDown, kHeld;

        dbgTick();

        hidScanInput();
        kDown = hidKeysDown();
        kHeld = hidKeysHeld();

        if (kDown & KEY_START) {
            if (state == S_EDIT)
                saveMap();
            else if (state == S_MONSTERS)
                saveMonsters();
            break;
        }

        if (state == S_TITLE)
            updateTitle(kDown);
        else if (state == S_PLAY)
            updatePlay(kDown, kHeld);
        else if (state == S_MONSTERS)
            updateMonsters(kDown, kHeld);
        else
            updateEdit(kDown, kHeld);

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
            drawPlayerTop();
            drawDebugOverlay();

            C2D_TargetClear(bot, COL(30, 30, 40));
            C2D_SceneBegin(bot);
            drawPlayBottom();
        } else if (state == S_MONSTERS) {
            C2D_TargetClear(top, COL(18, 24, 48));
            C2D_SceneBegin(top);
            drawMonstersTop();

            C2D_TargetClear(bot, COL(30, 30, 40));
            C2D_SceneBegin(bot);
            drawMonstersBottom();
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
