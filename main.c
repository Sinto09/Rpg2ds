/*
 * RPG 2DS - Etape 1 : la base
 *
 *  - Ecran titre avec menu (tactile, croix + A)
 *  - Carte vue de dessus avec camera qui suit le joueur
 *  - Deplacement : croix ou stick, B pour courir
 *  - Ecran du bas : mini-carte et infos
 *  - START quitte, SELECT retourne au titre
 *
 * Compilation : devkitARM + libctru + citro2d (voir Makefile)
 */

#include <3ds.h>
#include <citro2d.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

/* ---------- Constantes ---------- */

#define TOP_W 400
#define TOP_H 240
#define BOT_W 320
#define BOT_H 240

#define TS    16   /* taille d'une tuile en pixels */
#define MAP_W 50
#define MAP_H 30

#define PW 12      /* hitbox du joueur */
#define PH 12

#define COL(r, g, b) C2D_Color32((r), (g), (b), 255)

/* ---------- Tuiles ---------- */

enum { T_GRASS, T_WALL, T_WATER, T_TREE, T_PATH, T_FLOOR, T_DOOR, T_COUNT };

typedef struct {
    const char *name;
    u32 color;
    bool solid;
} TileDef;

static TileDef TILES[T_COUNT];
static u8 map[MAP_H][MAP_W];

static void initTiles(void) {
    TILES[T_GRASS] = (TileDef){ "Herbe",  COL(76, 160, 64),  false };
    TILES[T_WALL]  = (TileDef){ "Mur",    COL(120, 120, 130), true };
    TILES[T_WATER] = (TileDef){ "Eau",    COL(50, 110, 200),  true };
    TILES[T_TREE]  = (TileDef){ "Arbre",  COL(30, 100, 40),   true };
    TILES[T_PATH]  = (TileDef){ "Chemin", COL(200, 170, 110), false };
    TILES[T_FLOOR] = (TileDef){ "Sol",    COL(170, 130, 90),  false };
    TILES[T_DOOR]  = (TileDef){ "Porte",  COL(110, 70, 30),   false };
}

/* ---------- Generation de la carte ---------- */

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

static void startGame(void) {
    buildMap();
    player.x = 12 * TS + 2;
    player.y = 13 * TS + 2;
    player.dir = 0;
    updateCamera();
}

/* ---------- Etat global ---------- */

enum { S_TITLE, S_PLAY };

static int state = S_TITLE;
static bool running = true;
static C2D_TextBuf g_textBuf;

/* ---------- Boutons ---------- */

typedef struct {
    float x, y, w, h;
    const char *label;
    bool enabled;
} Button;

static Button titleButtons[3] = {
    { 50,  40, 220, 44, "Jouer",              true  },
    { 50,  95, 220, 44, "Éditeur (bientôt)",  false },
    { 50, 150, 220, 44, "Quitter",            true  },
};

static Button playTitleButton = { 222, 10, 88, 40, "Titre", true };

static int sel = 0;
static int msgTimer = 0;

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

/* ---------- Mise a jour ---------- */

static void activate(int i) {
    if (i == 0) {
        startGame();
        state = S_PLAY;
    } else if (i == 1) {
        msgTimer = 150;
    } else if (i == 2) {
        running = false;
    }
}

static void updateTitle(u32 kDown) {
    int i;

    if (msgTimer > 0)
        msgTimer--;

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
        state = S_TITLE;

    if (kDown & KEY_TOUCH) {
        touchPosition t;
        hidTouchRead(&t);
        if (inRect(&playTitleButton, t.px, t.py))
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
    drawTextCX("Étape 1 : la base du jeu", TOP_W / 2, 130, 0.5f,
               COL(150, 160, 190));
}

static void drawTitleBottom(void) {
    int i;

    if (msgTimer > 0)
        drawTextCX("L'éditeur arrive à l'étape 2 !", BOT_W / 2, 10, 0.5f,
                   COL(255, 220, 80));

    for (i = 0; i < 3; i++)
        drawButton(&titleButtons[i], i == sel);

    drawTextCX("Croix + A, ou touche un bouton", BOT_W / 2, 208, 0.45f,
               COL(170, 170, 190));
}

/* ---------- Dessin : jeu ---------- */

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
    drawText("SELECT : titre    START : quitter", 10, 204, 0.5f, gray);
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

        if (kDown & KEY_START)
            break;

        if (state == S_TITLE)
            updateTitle(kDown);
        else
            updatePlay(kDown, kHeld);

        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        C2D_TextBufClear(g_textBuf);

        if (state == S_TITLE) {
            C2D_TargetClear(top, COL(18, 24, 48));
            C2D_SceneBegin(top);
            drawTitleTop();

            C2D_TargetClear(bot, COL(30, 30, 40));
            C2D_SceneBegin(bot);
            drawTitleBottom();
        } else {
            C2D_TargetClear(top, COL(0, 0, 0));
            C2D_SceneBegin(top);
            drawMapTop();
            drawPlayerTop();

            C2D_TargetClear(bot, COL(30, 30, 40));
            C2D_SceneBegin(bot);
            drawPlayBottom();
        }

        C3D_FrameEnd(0);
    }

    C2D_TextBufDelete(g_textBuf);
    C2D_Fini();
    C3D_Fini();
    gfxExit();
    return 0;
}
