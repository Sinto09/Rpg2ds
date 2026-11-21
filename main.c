/*
 * RPG 2DS - Etape 2 : l'editeur de cartes
 *
 *  - Ecran titre : Jouer / Editeur / Quitter
 *  - Jeu : carte vue de dessus, deplacement (croix/stick, B = courir)
 *  - Editeur : ecran du bas = zone de dessin (stylet), ecran du haut = carte entiere
 *  - La carte est sauvegardee sur la carte SD : sdmc:/3ds/rpg2ds/map1.bin
 *
 * Compilation : devkitARM + libctru + citro2d (voir Makefile)
 */

#include <3ds.h>
#include <citro2d.h>
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

/* ---------- Joueur et camera ---------- */

typedef struct {
    int x, y; /* position en pixels (coin haut-gauche de la hitbox) */
    int dir;  /* 0 bas, 1 haut, 2 gauche, 3 droite */
} Player;

static Player player;
static int camX = 0, camY = 0;

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

enum { S_TITLE, S_PLAY, S_EDIT };

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

static Button titleButtons[3] = {
    { 50,  40, 220, 44, "Jouer",   true },
    { 50,  95, 220, 44, "Éditeur", true },
    { 50, 150, 220, 44, "Quitter", true },
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

static void activate(int i) {
    if (i == 0) {
        loadOrDefault();
        fromEditor = false;
        startPlay();
    } else if (i == 1) {
        loadOrDefault();
        enterEditor();
    } else if (i == 2) {
        running = false;
    }
}

/* ---------- Mise a jour ---------- */

static void updateTitle(u32 kDown) {
    int i;

    if (kDown & KEY_DOWN) sel = (sel + 1) % 3;
    if (kDown & KEY_UP)   sel = (sel + 2) % 3;
    if (kDown & KEY_A)    activate(sel);

    if (kDown & KEY_TOUCH) {
        touchPosition t;
        hidTouchRead(&t);
        for (i = 0; i < 3; i++) {
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
    circlePosition cp;

    hidCircleRead(&cp);

    if ((kHeld & KEY_DLEFT)  || cp.dx < -40) dx = -1;
    if ((kHeld & KEY_DRIGHT) || cp.dx >  40) dx =  1;
    if ((kHeld & KEY_DUP)    || cp.dy >  40) dy = -1;
    if ((kHeld & KEY_DDOWN)  || cp.dy < -40) dy =  1;

    if (dx != 0) player.dir = (dx < 0) ? 2 : 3;
    if (dy != 0) player.dir = (dy < 0) ? 1 : 0;

    /* on avance pixel par pixel pour bien longer les murs */
    for (i = 0; i < speed; i++) {
        if (dx != 0 && !blocked(player.x + dx, player.y)) player.x += dx;
        if (dy != 0 && !blocked(player.x, player.y + dy)) player.y += dy;
    }

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
    drawTextCX("Étape 2 : l'éditeur de cartes", TOP_W / 2, 130, 0.5f,
               COL(150, 160, 190));
}

static void drawTitleBottom(void) {
    int i;

    for (i = 0; i < 3; i++)
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
    drawText("Croix / stick : se déplacer", 10, 172, 0.5f, gray);
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

    while (running && aptMainLoop()) {
        u32 kDown, kHeld;

        hidScanInput();
        kDown = hidKeysDown();
        kHeld = hidKeysHeld();

        if (kDown & KEY_START) {
            if (state == S_EDIT)
                saveMap();
            break;
        }

        if (state == S_TITLE)
            updateTitle(kDown);
        else if (state == S_PLAY)
            updatePlay(kDown, kHeld);
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

            C2D_TargetClear(bot, COL(30, 30, 40));
            C2D_SceneBegin(bot);
            drawPlayBottom();
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
