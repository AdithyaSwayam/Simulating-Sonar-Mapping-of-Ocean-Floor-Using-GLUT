// ============================================================
//  SONAR TERRAIN MAPPER  —  GLUT / OpenGL 1.x  /  C++98
//  Controls:
//    Arrow Keys     — pan sonar device
//    Hold LMB       — continuous scan
//    R              — reset all terrain
// ============================================================

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <GL/glut.h>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <ctime>
#include <vector>
#include <algorithm>

// ── dimensions ──────────────────────────────────────────────
static const int   GRID       = 512;          // render grid (1024 would need VBO; 512 is plenty for fixed-pipeline)
static const float WORLD      = 100.0f;       // world-space width
static const float CELL       = WORLD / GRID;

// ── sonar parameters ────────────────────────────────────────
static const float SONAR_H          = 18.0f;  // sonar altitude above sea floor mean
static const float BEAM_HALF_DEG    = 30.0f;  // half-angle of sector cone
static const float BEAM_HALF_RAD    = BEAM_HALF_DEG * 3.14159265f / 180.0f;
static const float PING_SPEED       = 55.0f;  // world units per second
static const float PAN_SPEED        = 12.0f;  // arrow key speed

// ── reveal timing ───────────────────────────────────────────
static const float T_STRUCTURE      = 3.0f;   // seconds until stage-1 complete
static const float T_DETAIL         = 10.0f;  // seconds until stage-2 (color) appears
static const float T_DETAIL_FULL    = 16.0f;  // seconds until full color blend

// ── terrain ─────────────────────────────────────────────────
static float  height[GRID][GRID];     // terrain height  [-1 .. +1] ish
static float  geo[GRID][GRID][3];     // geological base colour per vertex
static float  revealStruct[GRID][GRID]; // 0=unseen  1=structure revealed
static float  revealColor [GRID][GRID]; // 0=no-color  1=full-color
static float  dwellTime   [GRID][GRID]; // cumulative dwell seconds

// ── state ────────────────────────────────────────────────────
static float sonarX = WORLD * 0.5f;
static float sonarZ = WORLD * 0.5f;

static bool  mouseDown   = false;
static bool  arrowLeft   = false, arrowRight = false;
static bool  arrowUp     = false, arrowDown  = false;

static float pingRadius  = 0.0f;     // current ping expansion radius
static bool  pinging     = false;

static float lastTime    = 0.0f;
static float totalTime   = 0.0f;

// ── normal cache ─────────────────────────────────────────────
static float nx[GRID][GRID], ny[GRID][GRID], nz[GRID][GRID];

// ============================================================
//  NOISE
// ============================================================
static unsigned char perm[512];

static void initPerm()
{
    for (int i = 0; i < 256; ++i) perm[i] = (unsigned char)i;
    for (int i = 255; i > 0; --i) {
        int j = rand() % (i + 1);
        unsigned char t = perm[i]; perm[i] = perm[j]; perm[j] = t;
    }
    for (int i = 0; i < 256; ++i) perm[256 + i] = perm[i];
}

static float fade(float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }
static float lerp(float a, float b, float t) { return a + t * (b - a); }
static float grad(int hash, float x, float y)
{
    int h = hash & 7;
    float u = h < 4 ? x : y;
    float v = h < 4 ? y : x;
    return ((h & 1) ? -u : u) + ((h & 2) ? -v : v);
}

static float noise2(float x, float y)
{
    int X = (int)floorf(x) & 255;
    int Y = (int)floorf(y) & 255;
    x -= floorf(x); y -= floorf(y);
    float u = fade(x), v = fade(y);
    int A = perm[X] + Y, B = perm[X + 1] + Y;
    return lerp(lerp(grad(perm[A],     x,     y),
                     grad(perm[B],     x - 1, y),     u),
                lerp(grad(perm[A + 1], x,     y - 1),
                     grad(perm[B + 1], x - 1, y - 1), u), v);
}

static float fbm(float x, float y, int oct, float lac, float gain)
{
    float val = 0, amp = 0.5f, freq = 1.0f;
    for (int i = 0; i < oct; ++i) {
        val  += amp * noise2(x * freq, y * freq);
        amp  *= gain;
        freq *= lac;
    }
    return val;
}

// ============================================================
//  TERRAIN GENERATION
// ============================================================
static void buildTerrain()
{
    // Ridge mask: ~1/6 of terrain is dramatic ridges
    for (int gz = 0; gz < GRID; ++gz) {
        for (int gx = 0; gx < GRID; ++gx) {
            float fx = gx / (float)GRID;
            float fz = gz / (float)GRID;

            // large-scale ridge network
            float ridgeN  = fabsf(fbm(fx * 3.0f + 40.f, fz * 3.0f + 40.f, 5, 2.1f, 0.5f));
            float ridgeMask = 1.0f - std::min(1.0f, ridgeN * 4.5f); // narrow peaks
            ridgeMask = ridgeMask * ridgeMask;

            // flat sandy floor with ripples  (dominant — 5/6)
            float sand  = fbm(fx * 18.f, fz * 18.f, 3, 2.0f, 0.45f) * 0.12f;
            // micro ripple pattern
            float ripple = sinf((fx * 80.f + fz * 20.f) * 3.14159f) * 0.03f
                         + sinf((fx * 60.f - fz * 40.f) * 3.14159f) * 0.02f;

            // dramatic peak height
            float peak = fbm(fx * 5.f + 10.f, fz * 5.f + 10.f, 6, 2.05f, 0.55f) * 1.8f;

            float h = sand + ripple + ridgeMask * peak * 0.85f;
            // overall bowl shape — deeper in middle
            float cx = fx - 0.5f, cz = fz - 0.5f;
            h -= (cx * cx + cz * cz) * 0.3f;

            height[gz][gx] = h;

            // geological colour: sandy tan → dark rock based on slope/height
            float rockiness = std::min(1.0f, ridgeMask * 3.0f + fabsf(h) * 0.8f);
            // sandy colour
            geo[gz][gx][0] = lerp(0.55f, 0.28f, rockiness);
            geo[gz][gx][1] = lerp(0.42f, 0.24f, rockiness);
            geo[gz][gx][2] = lerp(0.28f, 0.20f, rockiness);
        }
    }
    // Normalise heights to [-1, 1]
    float hmin = 1e9f, hmax = -1e9f;
    for (int i = 0; i < GRID; ++i)
        for (int j = 0; j < GRID; ++j) {
            if (height[i][j] < hmin) hmin = height[i][j];
            if (height[i][j] > hmax) hmax = height[i][j];
        }
    float hrng = hmax - hmin;
    for (int i = 0; i < GRID; ++i)
        for (int j = 0; j < GRID; ++j)
            height[i][j] = ((height[i][j] - hmin) / hrng) * 2.0f - 1.0f;
}

static void buildNormals()
{
    for (int gz = 1; gz < GRID - 1; ++gz) {
        for (int gx = 1; gx < GRID - 1; ++gx) {
            float dhdx = (height[gz][gx + 1] - height[gz][gx - 1]) / (2.0f * CELL);
            float dhdz = (height[gz + 1][gx] - height[gz - 1][gx]) / (2.0f * CELL);
            float len = sqrtf(dhdx * dhdx + 1.0f + dhdz * dhdz);
            nx[gz][gx] = -dhdx / len;
            ny[gz][gx] =  1.0f / len;
            nz[gz][gx] = -dhdz / len;
        }
    }
}

// ============================================================
//  DEPTH → RAINBOW COLOUR  (image-1 style)
// ============================================================
static void depthColor(float h, float &r, float &g, float &b)
{
    // h in [-1,1], map to hue 0(red=shallow) → 270(violet=deep)
    float t = (h + 1.0f) * 0.5f;          // 0=deep, 1=shallow
    float hue = (1.0f - t) * 270.0f;      // 0=red .. 270=violet

    // HSV→RGB  (S=1, V=1)
    float H = hue / 60.0f;
    int   i = (int)H % 6;
    float f = H - floorf(H);
    float q = 1.0f - f;
    switch (i % 6) {
        case 0: r=1; g=f; b=0; break;
        case 1: r=q; g=1; b=0; break;
        case 2: r=0; g=1; b=f; break;
        case 3: r=0; g=q; b=1; break;
        case 4: r=f; g=0; b=1; break;
        default:r=1; g=0; b=q; break;
    }
}

// ============================================================
//  SONAR REVEAL UPDATE
// ============================================================
static void updateReveal(float dt)
{
    // mark cells inside current ping ring as scanned
    if (!pinging) return;

    // ping swept area: sector cone from sonarX/sonarZ pointing "down"
    // We use a top-down circular sector for simplicity
    // Direction: sonar always looks straight down — sector covers 60-deg cone
    // The "sector" sweeps the whole 360 as long as mouse held — simplified to radial coverage

    float maxR = SONAR_H * tanf(BEAM_HALF_RAD) * 3.5f; // max ground radius covered

    for (int gz = 0; gz < GRID; ++gz) {
        for (int gx = 0; gx < GRID; ++gx) {
            float wx = gx * CELL;
            float wz = gz * CELL;
            float dx = wx - sonarX;
            float dz = wz - sonarZ;
            float dist = sqrtf(dx * dx + dz * dz);

            if (dist <= maxR) {
                // radial fade: cells near center reveal faster
                float factor = 1.0f - dist / maxR;

                dwellTime[gz][gx] += dt * (0.3f + 0.7f * factor);

                // Stage 1: structural reveal (first T_STRUCTURE seconds of dwell)
                float s1 = std::min(1.0f, dwellTime[gz][gx] / T_STRUCTURE);
                revealStruct[gz][gx] = std::max(revealStruct[gz][gx], s1);

                // Stage 2: colour detail (starts at T_DETAIL, full at T_DETAIL_FULL)
                if (dwellTime[gz][gx] > T_DETAIL) {
                    float s2 = (dwellTime[gz][gx] - T_DETAIL) / (T_DETAIL_FULL - T_DETAIL);
                    s2 = std::min(1.0f, s2);
                    revealColor[gz][gx] = std::max(revealColor[gz][gx], s2);
                }
            }
        }
    }
}

// ============================================================
//  PING RING ANIMATION
// ============================================================
static void drawPingRings()
{
    if (!pinging && pingRadius <= 0.0f) return;

    float maxR = SONAR_H * tanf(BEAM_HALF_RAD) * 3.5f;
    float alpha = 1.0f - pingRadius / maxR;
    if (alpha < 0.0f) alpha = 0.0f;

    glDisable(GL_LIGHTING);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glLineWidth(1.5f);

    // draw 3 concentric rings fading out
    for (int ring = 0; ring < 3; ++ring) {
        float r = pingRadius - ring * 4.0f;
        if (r < 0.0f) continue;
        float a = alpha * (1.0f - ring * 0.3f);
        glColor4f(0.2f, 0.9f, 1.0f, a);
        glBegin(GL_LINE_LOOP);
        int segs = 64;
        for (int s = 0; s < segs; ++s) {
            float ang = s * 2.0f * 3.14159265f / segs;
            float px = sonarX + cosf(ang) * r;
            float pz = sonarZ + sinf(ang) * r;
            int igx = (int)(px / CELL);
            int igz = (int)(pz / CELL);
            igx = std::max(0, std::min(GRID - 1, igx));
            igz = std::max(0, std::min(GRID - 1, igz));
            float h = height[igz][igx] * 6.0f;
            glVertex3f(px, h + 0.15f, pz);
        }
        glEnd();
    }

    glDisable(GL_BLEND);
    glEnable(GL_LIGHTING);
}

// ============================================================
//  SONAR DEVICE VISUAL (vessel + cone)
// ============================================================
static void drawSonarDevice()
{
    glDisable(GL_LIGHTING);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    float devY = SONAR_H;
    float maxR = SONAR_H * tanf(BEAM_HALF_RAD);

    // Vessel body — small bright box
    glColor3f(0.9f, 0.9f, 1.0f);
    glPushMatrix();
    glTranslatef(sonarX, devY, sonarZ);
    glutSolidCube(0.7f);
    glPopMatrix();

    // Beam cone — translucent red sector (image-2 style)
    glColor4f(1.0f, 0.15f, 0.05f, 0.22f);
    glBegin(GL_TRIANGLE_FAN);
    glVertex3f(sonarX, devY, sonarZ);
    int segs = 48;
    for (int s = 0; s <= segs; ++s) {
        float ang = s * 2.0f * 3.14159265f / segs;
        glVertex3f(sonarX + cosf(ang) * maxR, 0.0f, sonarZ + sinf(ang) * maxR);
    }
    glEnd();

    // Cone outline
    glColor4f(1.0f, 0.4f, 0.1f, 0.7f);
    glLineWidth(1.2f);
    glBegin(GL_LINE_LOOP);
    for (int s = 0; s <= segs; ++s) {
        float ang = s * 2.0f * 3.14159265f / segs;
        glVertex3f(sonarX + cosf(ang) * maxR, 0.0f, sonarZ + sinf(ang) * maxR);
    }
    glEnd();
    // Lines from apex to base circle
    glBegin(GL_LINES);
    for (int s = 0; s < 8; ++s) {
        float ang = s * 2.0f * 3.14159265f / 8;
        glVertex3f(sonarX, devY, sonarZ);
        glVertex3f(sonarX + cosf(ang) * maxR, 0.0f, sonarZ + sinf(ang) * maxR);
    }
    glEnd();

    // Vertical drop line
    glColor4f(1.0f, 1.0f, 0.3f, 0.9f);
    glLineWidth(1.0f);
    glBegin(GL_LINES);
    glVertex3f(sonarX, devY, sonarZ);
    glVertex3f(sonarX, 0.0f,  sonarZ);
    glEnd();

    // Depth markers (15 / 30) like image 2
    glColor3f(1.0f, 1.0f, 1.0f);
    glRasterPos3f(sonarX + 0.3f, devY * 0.6f, sonarZ);
    const char* lbl1 = "15m";
    for (const char* c = lbl1; *c; ++c) glutBitmapCharacter(GLUT_BITMAP_HELVETICA_10, *c);
    glRasterPos3f(sonarX + 0.3f, devY * 0.2f, sonarZ);
    const char* lbl2 = "30m";
    for (const char* c = lbl2; *c; ++c) glutBitmapCharacter(GLUT_BITMAP_HELVETICA_10, *c);

    glDisable(GL_BLEND);
    glEnable(GL_LIGHTING);
}

// ============================================================
//  TERRAIN DRAW
// ============================================================
static void drawTerrain()
{
    float heightScale = 6.0f;

    for (int gz = 0; gz < GRID - 1; ++gz) {
        glBegin(GL_TRIANGLE_STRIP);
        for (int gx = 0; gx < GRID; ++gx) {
            for (int row = 0; row <= 1; ++row) {
                int gz2 = gz + row;
                float rs = revealStruct[gz2][gx];
                float rc = revealColor [gz2][gx];

                if (rs < 0.01f) {
                    // completely unseen — skip with degenerate vertex (black)
                    glColor3f(0.0f, 0.0f, 0.0f);
                    glNormal3f(0, 1, 0);
                    glVertex3f(gx * CELL, -999.0f, gz2 * CELL);
                    continue;
                }

                float h = height[gz2][gx] * heightScale;

                // Stage 1 colour: dark grey wireframe-ish tint
                float sr = 0.15f + rs * 0.2f;
                float sg = 0.18f + rs * 0.22f;
                float sb = 0.22f + rs * 0.28f;

                // Stage 2: blend geological + depth rainbow
                if (rc > 0.0f) {
                    // geological base
                    float gr = geo[gz2][gx][0];
                    float gg = geo[gz2][gx][1];
                    float gb = geo[gz2][gx][2];
                    // depth rainbow
                    float dr, dg, db;
                    depthColor(height[gz2][gx], dr, dg, db);
                    // mix: 40% geo + 60% depth (image-1 style)
                    float mr = gr * 0.4f + dr * 0.6f;
                    float mg = gg * 0.4f + dg * 0.6f;
                    float mb = gb * 0.4f + db * 0.6f;
                    // blend with stage-1 grey
                    sr = sr + rc * (mr - sr);
                    sg = sg + rc * (mg - sg);
                    sb = sb + rc * (mb - sb);
                }

                // dim unseen edges
                sr *= rs; sg *= rs; sb *= rs;

                glColor3f(sr, sg, sb);
                glNormal3f(nx[gz2][gx], ny[gz2][gx], nz[gz2][gx]);
                glVertex3f(gx * CELL, h, gz2 * CELL);
            }
        }
        glEnd();
    }
}

// ============================================================
//  OCEAN SURFACE (dark translucent plane)
// ============================================================
static void drawOceanSurface()
{
    glDisable(GL_LIGHTING);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(0.02f, 0.06f, 0.18f, 0.55f);
    float oy = SONAR_H + 4.0f;
    glBegin(GL_QUADS);
    glVertex3f(0,     oy, 0);
    glVertex3f(WORLD, oy, 0);
    glVertex3f(WORLD, oy, WORLD);
    glVertex3f(0,     oy, WORLD);
    glEnd();
    glDisable(GL_BLEND);
    glEnable(GL_LIGHTING);
}

// ============================================================
//  HUD
// ============================================================
static void drawHUD(int w, int h)
{
    glMatrixMode(GL_PROJECTION);
    glPushMatrix(); glLoadIdentity();
    gluOrtho2D(0, w, 0, h);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix(); glLoadIdentity();
    glDisable(GL_LIGHTING);
    glDisable(GL_DEPTH_TEST);

    // count revealed cells
    int revealed = 0, colored = 0, total = GRID * GRID;
    for (int i = 0; i < GRID; ++i)
        for (int j = 0; j < GRID; ++j) {
            if (revealStruct[i][j] > 0.5f) ++revealed;
            if (revealColor [i][j] > 0.5f) ++colored;
        }

    char buf[128];
    glColor3f(0.3f, 1.0f, 0.7f);

    // title
    glRasterPos2i(12, h - 22);
    const char* title = "SONAR TERRAIN MAPPER  v1.0";
    for (const char* c = title; *c; ++c) glutBitmapCharacter(GLUT_BITMAP_HELVETICA_12, *c);

    // sonar position
    sprintf(buf, "Sonar XZ: %.1f, %.1f", sonarX, sonarZ);
    glRasterPos2i(12, h - 40);
    for (const char* c = buf; *c; ++c) glutBitmapCharacter(GLUT_BITMAP_HELVETICA_12, *c);

    // reveal stats
    sprintf(buf, "Mapped:  %d%%   Coloured: %d%%",
            (revealed * 100) / total, (colored * 100) / total);
    glRasterPos2i(12, h - 58);
    for (const char* c = buf; *c; ++c) glutBitmapCharacter(GLUT_BITMAP_HELVETICA_12, *c);

    // stage indicator
    const char* stage = pinging ? "[ SCANNING — STAGE 1: STRUCTURE ]" : "[ HOLD LMB TO SCAN ]";
    // check if any pixel is in stage 2
    if (colored > 0) stage = pinging ? "[ SCANNING — STAGE 2: COLOUR DETAIL ]" : "[ HOLD LMB TO SCAN ]";
    glColor3f(1.0f, 0.85f, 0.2f);
    glRasterPos2i(12, h - 76);
    for (const char* c = stage; *c; ++c) glutBitmapCharacter(GLUT_BITMAP_HELVETICA_12, *c);

    // controls reminder
    glColor3f(0.55f, 0.65f, 0.75f);
    glRasterPos2i(12, 14);
    const char* ctrl = "Arrow Keys: Pan Sonar   LMB Hold: Scan   R: Reset";
    for (const char* c = ctrl; *c; ++c) glutBitmapCharacter(GLUT_BITMAP_HELVETICA_12, *c);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LIGHTING);
    glMatrixMode(GL_PROJECTION); glPopMatrix();
    glMatrixMode(GL_MODELVIEW);  glPopMatrix();
}

// ============================================================
//  GLUT CALLBACKS
// ============================================================
static int winW = 1280, winH = 720;

static void reshape(int w, int h)
{
    winW = w; winH = h;
    glViewport(0, 0, w, h);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(45.0, (double)w / h, 0.5, 500.0);
    glMatrixMode(GL_MODELVIEW);
}

static void display()
{
    glClearColor(0.01f, 0.02f, 0.06f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glLoadIdentity();
    // Fixed cinematic camera — 3/4 dramatic angle matching image 1
    float cx = WORLD * 0.5f, cz = WORLD * 0.5f;
    gluLookAt(cx - 55, 65, cz - 55,   // eye
              cx,      0,  cz,          // look-at terrain centre
              0,        1,  0);          // up

    // ── lighting ──────────────────────────────────────────
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    GLfloat lpos[] = { WORLD * 0.3f, 80.0f, WORLD * 0.2f, 1.0f };
    GLfloat lamb[] = { 0.08f, 0.10f, 0.14f, 1.0f };
    GLfloat ldif[] = { 0.80f, 0.85f, 0.90f, 1.0f };
    GLfloat lspe[] = { 0.60f, 0.65f, 0.70f, 1.0f };
    glLightfv(GL_LIGHT0, GL_POSITION, lpos);
    glLightfv(GL_LIGHT0, GL_AMBIENT,  lamb);
    glLightfv(GL_LIGHT0, GL_DIFFUSE,  ldif);
    glLightfv(GL_LIGHT0, GL_SPECULAR, lspe);
    glEnable(GL_COLOR_MATERIAL);
    glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
    GLfloat mspec[] = { 0.3f, 0.3f, 0.4f, 1.0f };
    glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, mspec);
    glMateriali(GL_FRONT_AND_BACK, GL_SHININESS, 32);

    glEnable(GL_DEPTH_TEST);
    glShadeModel(GL_SMOOTH);

    drawTerrain();
    drawPingRings();
    drawSonarDevice();
    drawOceanSurface();
    drawHUD(winW, winH);

    glutSwapBuffers();
}

static void timer(int)
{
    float now  = (float)glutGet(GLUT_ELAPSED_TIME) * 0.001f;
    float dt   = now - lastTime;
    if (dt > 0.1f) dt = 0.1f;
    lastTime   = now;
    totalTime += dt;

    // pan sonar
    if (arrowLeft)  sonarX -= PAN_SPEED * dt;
    if (arrowRight) sonarX += PAN_SPEED * dt;
    if (arrowUp)    sonarZ -= PAN_SPEED * dt;
    if (arrowDown)  sonarZ += PAN_SPEED * dt;
    sonarX = std::max(2.0f, std::min(WORLD - 2.0f, sonarX));
    sonarZ = std::max(2.0f, std::min(WORLD - 2.0f, sonarZ));

    // ping ring expand
    if (mouseDown) {
        pinging = true;
        float maxR = SONAR_H * tanf(BEAM_HALF_RAD) * 3.5f;
        pingRadius += PING_SPEED * dt;
        if (pingRadius > maxR) pingRadius = 0.0f;
    } else {
        pinging = false;
        pingRadius = 0.0f;
    }

    updateReveal(dt);

    glutPostRedisplay();
    glutTimerFunc(16, timer, 0);   // ~60 fps
}

static void mouseButton(int button, int state, int, int)
{
    if (button == GLUT_LEFT_BUTTON)
        mouseDown = (state == GLUT_DOWN);
}

static void specialKey(int key, int, int)
{
    if (key == GLUT_KEY_LEFT)  arrowLeft  = true;
    if (key == GLUT_KEY_RIGHT) arrowRight = true;
    if (key == GLUT_KEY_UP)    arrowUp    = true;
    if (key == GLUT_KEY_DOWN)  arrowDown  = true;
}

static void specialKeyUp(int key, int, int)
{
    if (key == GLUT_KEY_LEFT)  arrowLeft  = false;
    if (key == GLUT_KEY_RIGHT) arrowRight = false;
    if (key == GLUT_KEY_UP)    arrowUp    = false;
    if (key == GLUT_KEY_DOWN)  arrowDown  = false;
}

static void keyboard(unsigned char key, int, int)
{
    if (key == 'r' || key == 'R') {
        memset(revealStruct, 0, sizeof(revealStruct));
        memset(revealColor,  0, sizeof(revealColor));
        memset(dwellTime,    0, sizeof(dwellTime));
    }
    if (key == 27) exit(0); // ESC
}

// ============================================================
//  MAIN
// ============================================================
int main(int argc, char** argv)
{
    srand((unsigned)time(0));
    initPerm();
    buildTerrain();
    buildNormals();
    memset(revealStruct, 0, sizeof(revealStruct));
    memset(revealColor,  0, sizeof(revealColor));
    memset(dwellTime,    0, sizeof(dwellTime));

    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGB | GLUT_DEPTH);
    glutInitWindowSize(winW, winH);
    glutCreateWindow("Sonar Terrain Mapper");

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_NORMALIZE);

    glutDisplayFunc(display);
    glutReshapeFunc(reshape);
    glutMouseFunc(mouseButton);
    glutSpecialFunc(specialKey);
    glutSpecialUpFunc(specialKeyUp);
    glutKeyboardFunc(keyboard);
    glutTimerFunc(16, timer, 0);

    lastTime = (float)glutGet(GLUT_ELAPSED_TIME) * 0.001f;
    glutMainLoop();
    return 0;
}
