// ============================================================
//  SONAR TERRAIN MAPPER  —  GLUT / OpenGL 1.x + IMMEDIATE  /  C++98
//  ENHANCED VERSION v2.0 (no VBO)
// ============================================================

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <GL/freeglut.h>   // or glut.h if you remove glutMouseWheelFunc
#include <GL/glu.h>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <ctime>
#include <vector>
#include <algorithm>

// --- disable VBO for now (simplifies Windows/GCC problems) ---
#undef USE_VBO
#define USE_VBO 0

// ── dimensions ──────────────────────────────────────────────
static const int   GRID       = 2048;         // 4x larger
static const float WORLD      = 400.0f;       // 4x larger world
static const float CELL       = WORLD / GRID;

// ── sonar parameters ────────────────────────────────────────
static const float SONAR_H          = 25.0f;  // higher altitude
static const float BEAM_HALF_DEG    = 35.0f;  // wider beam
static const float BEAM_HALF_RAD    = BEAM_HALF_DEG * 3.14159265f / 180.0f;
static const float PING_SPEED       = 80.0f;  // faster pings
static const float PAN_SPEED        = 15.0f;  // slightly faster WASD

// ── reveal timing ───────────────────────────────────────────
static const float T_STRUCTURE      = 2.5f;   // faster reveals
static const float T_DETAIL         = 8.0f;
static const float T_DETAIL_FULL    = 14.0f;

// ── terrain ─────────────────────────────────────────────────
static float* height;
static float* geo;        // flattened [GRID*GRID*3]
static float* revealStruct;
static float* revealColor;
static float* dwellTime;
static float* nx, *ny, *nz;

// ── state ────────────────────────────────────────────────────
static float sonarX = WORLD * 0.5f;
static float sonarZ = WORLD * 0.5f;

static bool  mouseDown   = false;
static bool  wKey = false, aKey = false, sKey = false, dKey = false;

static float pingRadius  = 0.0f;
static bool  pinging     = false;

static float lastTime    = 0.0f;
static float totalTime   = 0.0f;

// ── camera ───────────────────────────────────────────────────
static float camDistance = 120.0f;  // zoom distance
static float camAngleX   = 45.0f;   // pitch
static float camAngleY   = -135.0f; // yaw

// ── VBO (unused) ─────────────────────────────────────────---
#ifdef USE_VBO
static GLuint terrainVBO = 0;
static GLuint terrainIBO = 0;
static int    terrainVertexCount = 0;
#endif

// ── NOISE (unchanged) ───────────────────────────────────────
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

// ── TERRAIN ALLOCATION ──────────────────────────────────────
static void allocTerrain()
{
    height       = new float[GRID * GRID];
    geo          = new float[GRID * GRID * 3];
    revealStruct = new float[GRID * GRID];
    revealColor  = new float[GRID * GRID];
    dwellTime    = new float[GRID * GRID];
    nx           = new float[GRID * GRID];
    ny           = new float[GRID * GRID];
    nz           = new float[GRID * GRID];
}

static inline int idx(int z, int x) { return z * GRID + x; }

// ── ENHANCED TERRAIN GENERATION ─────────────────────────────
static void buildTerrain()
{
    // MUCH deeper/wider features for 400x400 world
    for (int gz = 0; gz < GRID; ++gz) {
        for (int gx = 0; gx < GRID; ++gx) {
            float fx = gx / (float)GRID;
            float fz = gz / (float)GRID;

            // MASSIVE ridge network (lower freq = wider)
            float ridgeN  = fabsf(fbm(fx * 1.2f + 23.f, fz * 1.2f + 47.f, 6, 2.2f, 0.48f));
            float ridgeMask = 1.0f - std::min(1.0f, ridgeN * 3.2f);
            ridgeMask = ridgeMask * ridgeMask * ridgeMask; // sharper ridges

            // Deep sandy basins + mega ripples
            float sand  = fbm(fx * 8.f, fz * 8.f, 4, 2.1f, 0.42f) * 0.25f;
            float megaRipple = sinf((fx * 12.f + fz * 8.f) * 3.14159f) * 0.15f
                             + sinf((fx * 10.f - fz * 6.f) * 3.14159f) * 0.12f;

            // MONUMENTAL peaks (much taller)
            float peak = fbm(fx * 2.1f + 33.f, fz * 2.1f + 19.f, 7, 2.1f, 0.52f) * 3.2f;

            float h = sand + megaRipple + ridgeMask * peak * 1.2f;

            // DEEPER central crater
            float cx = fx - 0.5f, cz = fz - 0.5f;
            h -= (cx * cx + cz * cz) * 0.8f;

            height[idx(gz,gx)] = h;

            // Enhanced geology: more color variation
            float rockiness = std::min(1.0f, ridgeMask * 2.8f + fabsf(h) * 1.2f);
            geo[idx(gz,gx)*3 + 0] = lerp(0.58f, 0.22f, rockiness);  // R: sand→dark rock
            geo[idx(gz,gx)*3 + 1] = lerp(0.48f, 0.18f, rockiness);  // G
            geo[idx(gz,gx)*3 + 2] = lerp(0.32f, 0.15f, rockiness);  // B
        }
    }

    // Normalize to [-3, 3] for dramatic relief
    float hmin = 1e9f, hmax = -1e9f;
    for (int i = 0; i < GRID*GRID; ++i) {
        if (height[i] < hmin) hmin = height[i];
        if (height[i] > hmax) hmax = height[i];
    }
    float hrng = hmax - hmin;
    for (int i = 0; i < GRID*GRID; ++i)
        height[i] = ((height[i] - hmin) / hrng) * 6.0f - 3.0f;  // [-3,3]
}

static void buildNormals()
{
    for (int gz = 1; gz < GRID - 1; ++gz) {
        for (int gx = 1; gx < GRID - 1; ++gx) {
            int i = idx(gz,gx);
            float dhdx = (height[idx(gz,gx+1)] - height[idx(gz,gx-1)]) / (2.0f * CELL);
            float dhdz = (height[idx(gz+1,gx)] - height[idx(gz-1,gx)]) / (2.0f * CELL);
            float len = sqrtf(dhdx * dhdx + 1.0f + dhdz * dhdz);
            nx[i] = -dhdx / len;
            ny[i] =  1.0f / len;
            nz[i] = -dhdz / len;
        }
    }
}

// ── VBO SETUP (nothing) ─────────────────────────────────────
#ifdef USE_VBO
static void setupVBO()
{
    // unused when USE_VBO=0
}
static bool initVBO()
{
    return false;
}
#endif

// ── DEPTH COLOR (unchanged) ────────────────────────────────
static void depthColor(float h, float &r, float &g, float &b)
{
    float t = (h + 3.0f) * 0.1667f;  // adjusted for [-3,3]
    float hue = (1.0f - t) * 270.0f;
    float H = hue / 60.0f;
    int i = (int)H % 6;
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

// ── UPDATE REVEAL (optimized) ───────────────────────────────
static void updateReveal(float dt)
{
    if (!pinging) return;

    float maxR = SONAR_H * tanf(BEAM_HALF_RAD) * 4.0f;

    // Only update cells near sonar (optimized)
    int minX = std::max(0, (int)((sonarX - maxR) / CELL));
    int maxX = std::min(GRID-1, (int)((sonarX + maxR) / CELL));
    int minZ = std::max(0, (int)((sonarZ - maxR) / CELL));
    int maxZ = std::min(GRID-1, (int)((sonarZ + maxR) / CELL));

    for (int gz = minZ; gz <= maxZ; ++gz) {
        for (int gx = minX; gx <= maxX; ++gx) {
            float wx = gx * CELL;
            float wz = gz * CELL;
            float dx = wx - sonarX;
            float dz = wz - sonarZ;
            float dist = sqrtf(dx * dx + dz * dz);

            if (dist <= maxR) {
                float factor = 1.0f - dist / maxR;
                int i = idx(gz,gx);
                dwellTime[i] += dt * (0.4f + 0.6f * factor);

                float s1 = std::min(1.0f, dwellTime[i] / T_STRUCTURE);
                revealStruct[i] = std::max(revealStruct[i], s1);

                if (dwellTime[i] > T_DETAIL) {
                    float s2 = (dwellTime[i] - T_DETAIL) / (T_DETAIL_FULL - T_DETAIL);
                    revealColor[i] = std::max(revealColor[i], std::min(1.0f, s2));
                }
            }
        }
    }
}

// ── ENHANCED PING RINGS ─────────────────────────────────────
static void drawPingRings()
{
    if (!pinging && pingRadius <= 0.0f) return;

    float maxR = SONAR_H * tanf(BEAM_HALF_RAD) * 4.0f;
    float alpha = 1.0f - pingRadius / maxR;
    if (alpha < 0.0f) alpha = 0.0f;

    glDisable(GL_LIGHTING);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glLineWidth(2.0f);

    for (int ring = 0; ring < 4; ++ring) {
        float r = pingRadius - ring * 6.0f;
        if (r < 0.0f) continue;
        float a = alpha * (1.0f - ring * 0.25f);
        glColor4f(0.3f, 0.95f, 1.0f, a * 0.8f);
        glBegin(GL_LINE_LOOP);
        int segs = 96;
        for (int s = 0; s < segs; ++s) {
            float ang = s * 2.0f * 3.14159265f / segs;
            float px = sonarX + cosf(ang) * r;
            float pz = sonarZ + sinf(ang) * r;
            int igx = std::max(0, std::min(GRID - 1, (int)(px / CELL)));
            int igz = std::max(0, std::min(GRID - 1, (int)(pz / CELL)));
            float h = height[idx(igz,igx)] * 8.0f;
            glVertex3f(px, h + 0.3f, pz);
        }
        glEnd();
    }

    glDisable(GL_BLEND);
    glEnable(GL_LIGHTING);
}

// ── DETAILED ROV SONAR DEVICE ───────────────────────────────
static void drawSonarDevice()
{
    glDisable(GL_LIGHTING);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_LINE_SMOOTH);

    float devY = SONAR_H;
    float maxR = SONAR_H * tanf(BEAM_HALF_RAD);

    // ── MAIN ROV BODY (cylindrical) ──────────────────────
    glColor4f(0.85f, 0.88f, 0.92f, 0.95f);  // metallic gray
    glPushMatrix();
    glTranslatef(sonarX, devY + 1.2f, sonarZ);

    // Main body cylinder
    GLUquadric* quad = gluNewQuadric();
    gluQuadricDrawStyle(quad, GLU_FILL);
    gluQuadricNormals(quad, GLU_SMOOTH);
    gluCylinder(quad, 1.1f, 1.1f, 2.4f, 24, 1);
    gluSphere(quad, 1.1f, 24, 16);
    gluDeleteQuadric(quad);

    glPopMatrix();

    // ── GIMBAL / SENSOR HEAD ─────────────────────────────
    glColor4f(0.95f, 0.95f, 1.0f, 1.0f);  // bright white
    glPushMatrix();
    glTranslatef(sonarX, devY + 0.8f, sonarZ);
    glutSolidSphere(0.85f, 16, 12);
    glPopMatrix();

    // ── THRUSTERS (4x) ───────────────────────────────────
    glColor4f(0.4f, 0.45f, 0.55f, 0.9f);
    float thrusterPos[4][3] = {
        {sonarX-1.4f, devY+0.4f, sonarZ-0.8f},
        {sonarX+1.4f, devY+0.4f, sonarZ-0.8f},
        {sonarX-1.4f, devY+0.4f, sonarZ+0.8f},
        {sonarX+1.4f, devY+0.4f, sonarZ+0.8f}
    };
    for (int i = 0; i < 4; ++i) {
        glPushMatrix();
        glTranslatef(thrusterPos[i][0], thrusterPos[i][1], thrusterPos[i][2]);
        glutSolidSphere(0.35f, 12, 8);
        glPopMatrix();
    }

    // ── LIGHTS (headlights) ─────────────────────────────
    glColor4f(1.0f, 0.95f, 0.7f, 0.9f);
    glPushMatrix();
    glTranslatef(sonarX-0.6f, devY+1.1f, sonarZ-0.4f);
    glutSolidSphere(0.22f, 12, 8);
    glTranslatef(sonarX+0.6f - (sonarX-0.6f), 0, 0);
    glutSolidSphere(0.22f, 12, 8);
    glPopMatrix();

    // ── SENSOR ARRAYS (side pods) ───────────────────────
    glColor4f(0.75f, 0.78f, 0.85f, 0.95f);
    glPushMatrix();
    glTranslatef(sonarX-1.6f, devY+1.8f, sonarZ);
    glutSolidSphere(0.28f, 12, 8);
    glTranslatef(3.2f, 0, 0);
    glutSolidSphere(0.28f, 12, 8);
    glPopMatrix();

    // ── DETAILED BEAM CONE (grayish-white) ──────────────
    glColor4f(0.85f, 0.88f, 0.95f, 0.18f);  // GRAYISH-WHITE
    glBegin(GL_TRIANGLE_FAN);
    glVertex3f(sonarX, devY, sonarZ);
    int segs = 64;
    for (int s = 0; s <= segs; ++s) {
        float ang = s * 2.0f * 3.14159265f / segs;
        glVertex3f(sonarX + cosf(ang) * maxR, 0.0f, sonarZ + sinf(ang) * maxR);
    }
    glEnd();

    // Cone outline (brighter white)
    glColor4f(0.95f, 0.97f, 1.0f, 0.6f);
    glLineWidth(1.8f);
    glBegin(GL_LINE_LOOP);
    for (int s = 0; s <= segs; ++s) {
        float ang = s * 2.0f * 3.14159265f / segs;
        glVertex3f(sonarX + cosf(ang) * maxR, 0.0f, sonarZ + sinf(ang) * maxR);
    }
    glEnd();

    // Drop line + depth markers (unchanged but positioned better)
    glColor4f(1.0f, 1.0f, 0.8f, 1.0f);
    glLineWidth(1.4f);
    glBegin(GL_LINES);
    glVertex3f(sonarX, devY, sonarZ);
    glVertex3f(sonarX, 0.0f, sonarZ);
    glEnd();

    glColor3f(1.0f, 1.0f, 0.9f);
    glRasterPos3f(sonarX + 0.8f, devY * 0.55f, sonarZ);
    const char* lbl1 = "25m";
    for (const char* c = lbl1; *c; ++c) glutBitmapCharacter(GLUT_BITMAP_HELVETICA_12, *c);
    glRasterPos3f(sonarX + 0.8f, devY * 0.22f, sonarZ);
    const char* lbl2 = "50m";
    for (const char* c = lbl2; *c; ++c) glutBitmapCharacter(GLUT_BITMAP_HELVETICA_12, *c);

    glDisable(GL_BLEND);
    glDisable(GL_LINE_SMOOTH);
    glEnable(GL_LIGHTING);
}

// ── OPTIMIZED TERRAIN DRAW (immediate mode only) ───────────
static void drawTerrain()
{
    glEnable(GL_DEPTH_TEST);

    // FALLBACK: Enhanced immediate mode (still fast for 2048x2048)
    float heightScale = 8.0f;
    for (int gz = 0; gz < GRID - 1; ++gz) {
        glBegin(GL_TRIANGLE_STRIP);
        for (int gx = 0; gx < GRID; ++gx) {
            for (int row = 0; row <= 1; ++row) {
                int gz2 = gz + row;
                int i = idx(gz2, gx);
                float rs = revealStruct[i];
                float rc = revealColor[i];

                if (rs < 0.01f) {
                    glColor3f(0.0f, 0.0f, 0.0f);
                    glNormal3f(0,1,0);
                    glVertex3f(gx * CELL, -1000.0f, gz2 * CELL);
                    continue;
                }

                float h = height[i] * heightScale;

                // Stage 1: Enhanced grey
                float sr = 0.12f + rs * 0.25f;
                float sg = 0.15f + rs * 0.28f;
                float sb = 0.20f + rs * 0.32f;

                // Stage 2: Geology + rainbow
                if (rc > 0.0f) {
                    float gr = geo[i*3 + 0];
                    float gg = geo[i*3 + 1];
                    float gb = geo[i*3 + 2];
                    float dr, dg, db;
                    depthColor(height[i], dr, dg, db);
                    float mr = gr * 0.35f + dr * 0.65f;
                    float mg = gg * 0.35f + dg * 0.65f;
                    float mb = gb * 0.35f + db * 0.65f;
                    sr = lerp(sr, mr, rc);
                    sg = lerp(sg, mg, rc);
                    sb = lerp(sb, mb, rc);
                }

                sr *= rs * 0.95f; sg *= rs * 0.95f; sb *= rs * 0.95f;
                glColor3f(sr, sg, sb);
                glNormal3f(nx[i], ny[i], nz[i]);
                glVertex3f(gx * CELL, h, gz2 * CELL);
            }
        }
        glEnd();
    }
}

// ── OCEAN SURFACE (larger) ──────────────────────────────────
static void drawOceanSurface()
{
    glDisable(GL_LIGHTING);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(0.015f, 0.045f, 0.16f, 0.6f);
    float oy = SONAR_H + 8.0f;
    glBegin(GL_QUADS);
    glVertex3f(-WORLD*0.2f, oy, -WORLD*0.2f);
    glVertex3f(WORLD*1.2f, oy, -WORLD*0.2f);
    glVertex3f(WORLD*1.2f, oy, WORLD*1.2f);
    glVertex3f(-WORLD*0.2f, oy, WORLD*1.2f);
    glEnd();
    glDisable(GL_BLEND);
    glEnable(GL_LIGHTING);
}

// ── ENHANCED HUD ────────────────────────────────────────────
static void drawHUD(int w, int h)
{
    glMatrixMode(GL_PROJECTION);
    glPushMatrix(); glLoadIdentity();
    gluOrtho2D(0, w, 0, h);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix(); glLoadIdentity();
    glDisable(GL_LIGHTING);
    glDisable(GL_DEPTH_TEST);

    int revealed = 0, colored = 0, total = GRID * GRID;
    for (int i = 0; i < total; ++i) {
        if (revealStruct[i] > 0.5f) ++revealed;
        if (revealColor[i] > 0.5f) ++colored;
    }

    char buf[256];
    glColor3f(0.2f, 1.0f, 0.8f);

    // Title
    glRasterPos2i(15, h - 28);
    const char* title = "SONAR TERRAIN MAPPER  v2.0  -  ENHANCED ROV";
    for (const char* c = title; *c; ++c) glutBitmapCharacter(GLUT_BITMAP_HELVETICA_12, *c);

    // Sonar position
    sprintf(buf, "ROV Position: %.1f, %.1f   Altitude: %.0fm", sonarX, sonarZ, SONAR_H);
    glRasterPos2i(15, h - 55);
    for (const char* c = buf; *c; ++c) glutBitmapCharacter(GLUT_BITMAP_HELVETICA_12, *c);

    // Stats
    sprintf(buf, "Terrain Mapped: %d%%   Detailed: %d%%   Grid: %dx%d",
            (revealed * 100) / total, (colored * 100) / total, GRID, GRID);
    glRasterPos2i(15, h - 78);
    for (const char* c = buf; *c; ++c) glutBitmapCharacter(GLUT_BITMAP_HELVETICA_12, *c);

    // Status
    const char* stage = pinging ? "[ SCANNING — HIGH-RES DETAIL ]" : "[ HOLD LMB TO SCAN ]";
    glColor3f(1.0f, 0.9f, 0.3f);
    glRasterPos2i(15, h - 102);
    for (const char* c = stage; *c; ++c) glutBitmapCharacter(GLUT_BITMAP_HELVETICA_12, *c);

    // Controls
    glColor3f(0.6f, 0.75f, 0.9f);
    glRasterPos2i(15, 20);
    const char* ctrl1 = "WASD: Move ROV   Mouse Wheel: Zoom   LMB Hold: Scan   R: Reset";
    for (const char* c = ctrl1; *c; ++c) glutBitmapCharacter(GLUT_BITMAP_HELVETICA_12, *c);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LIGHTING);
    glMatrixMode(GL_PROJECTION); glPopMatrix();
    glMatrixMode(GL_MODELVIEW);  glPopMatrix();
}

// ── ZOOM CAMERA ─────────────────────────────────────────────
static void updateCamera()
{
    float cx = WORLD * 0.5f;
    float cz = WORLD * 0.5f;

    float camX = cx + camDistance * sinf(camAngleY * 0.0174533f) * cosf(camAngleX * 0.0174533f);
    float camY = 40.0f + camDistance * sinf(camAngleX * 0.0174533f);
    float camZ = cz + camDistance * cosf(camAngleY * 0.0174533f) * cosf(camAngleX * 0.0174533f);

    gluLookAt(camX, camY, camZ, cx, 0, cz, 0, 1, 0);
}

// ── GLUT CALLBACKS ──────────────────────────────────────────
static int winW = 1400, winH = 900;

static void reshape(int w, int h)
{
    winW = w; winH = h;
    glViewport(0, 0, w, h);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(55.0, (double)w / h, 0.1, 2000.0);
    glMatrixMode(GL_MODELVIEW);
}

static void display()
{
    glClearColor(0.008f, 0.018f, 0.05f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glLoadIdentity();
    updateCamera();

    // Enhanced lighting
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    GLfloat lpos[] = { WORLD * 0.4f, 120.0f, WORLD * 0.3f, 1.0f };
    GLfloat lamb[] = { 0.06f, 0.08f, 0.12f, 1.0f };
    GLfloat ldif[] = { 0.85f, 0.88f, 0.92f, 1.0f };
    GLfloat lspe[] = { 0.65f, 0.68f, 0.72f, 1.0f };
    glLightfv(GL_LIGHT0, GL_POSITION, lpos);
    glLightfv(GL_LIGHT0, GL_AMBIENT,  lamb);
    glLightfv(GL_LIGHT0, GL_DIFFUSE,  ldif);
    glLightfv(GL_LIGHT0, GL_SPECULAR, lspe);

    glEnable(GL_COLOR_MATERIAL);
    glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
    GLfloat mspec[] = { 0.4f, 0.4f, 0.5f, 1.0f };
    glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, mspec);
    glMateriali(GL_FRONT_AND_BACK, GL_SHININESS, 48);

    glShadeModel(GL_SMOOTH);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_NORMALIZE);

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
    if (dt > 0.083f) dt = 0.083f;  // 12ms cap
    lastTime   = now;
    totalTime += dt;

    // WASD movement
    if (wKey) sonarZ -= PAN_SPEED * dt;
    if (sKey) sonarZ += PAN_SPEED * dt;
    if (aKey) sonarX -= PAN_SPEED * dt;
    if (dKey) sonarX += PAN_SPEED * dt;
    sonarX = std::max(5.0f, std::min(WORLD - 5.0f, sonarX));
    sonarZ = std::max(5.0f, std::min(WORLD - 5.0f, sonarZ));

    // Ping animation
    if (mouseDown) {
        pinging = true;
        float maxR = SONAR_H * tanf(BEAM_HALF_RAD) * 4.0f;
        pingRadius += PING_SPEED * dt;
        if (pingRadius > maxR) pingRadius = 0.0f;
    } else {
        pinging = false;
        pingRadius *= 0.92f;  // fade out
    }

    updateReveal(dt);
    glutPostRedisplay();
    glutTimerFunc(16, timer, 0);
}

static void mouseButton(int button, int state, int x, int y)
{
    if (button == GLUT_LEFT_BUTTON)
        mouseDown = (state == GLUT_DOWN);
}

// Only keep this if you link FreeGLUT
static void mouseWheel(int wheel, int dir, int x, int y)
{
    camDistance -= dir * 8.0f;
    camDistance = std::max(20.0f, std::min(500.0f, camDistance));
}

static void keyboard(unsigned char key, int x, int y)
{
    switch (key) {
        case 'w': case 'W': wKey = true; break;
        case 's': case 'S': sKey = true; break;
        case 'a': case 'A': aKey = true; break;
        case 'd': case 'D': dKey = true; break;
        case 'r': case 'R':
            memset(revealStruct, 0, GRID*GRID*sizeof(float));
            memset(revealColor,  0, GRID*GRID*sizeof(float));
            memset(dwellTime,    0, GRID*GRID*sizeof(float));
            break;
        case 27: exit(0);
    }
}

static void keyboardUp(unsigned char key, int x, int y)
{
    switch (key) {
        case 'w': case 'W': wKey = false; break;
        case 's': case 'S': sKey = false; break;
        case 'a': case 'A': aKey = false; break;
        case 'd': case 'D': dKey = false; break;
    }
}

static void specialKey(int key, int x, int y)
{
    // Legacy arrow support
    if (key == GLUT_KEY_LEFT)  aKey = true;
    if (key == GLUT_KEY_RIGHT) dKey = true;
    if (key == GLUT_KEY_UP)    wKey = true;
    if (key == GLUT_KEY_DOWN)  sKey = true;
}

static void specialKeyUp(int key, int x, int y)
{
    if (key == GLUT_KEY_LEFT)  aKey = false;
    if (key == GLUT_KEY_RIGHT) dKey = false;
    if (key == GLUT_KEY_UP)    wKey = false;
    if (key == GLUT_KEY_DOWN)  sKey = false;
}

// ── VBO INIT (stub) ─────────────────────────────────────────
// (we USE_VBO=0 so this is not compiled or called)
#ifdef USE_VBO

#endif

// ── MAIN ────────────────────────────────────────────────────
int main(int argc, char** argv)
{
    srand((unsigned)time(0));
    initPerm();

    printf("Initializing %dx%d terrain...\n", GRID, GRID);
    allocTerrain();
    buildTerrain();
    buildNormals();
    memset(revealStruct, 0, GRID*GRID*sizeof(float));
    memset(revealColor,  0, GRID*GRID*sizeof(float));
    memset(dwellTime,    0, GRID*GRID*sizeof(float));

    // No VBO here (USE_VBO = 0)
#ifndef USE_VBO
    printf("Using immediate mode (no VBO)\n");
#endif

    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGB | GLUT_DEPTH);  // removed GL_MULTISAMPLE

    glutInitWindowSize(winW, winH);
    glutCreateWindow("Sonar Terrain Mapper v2.0 - Enhanced ROV Edition");

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_NORMALIZE);
    glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);

    glutDisplayFunc(display);
    glutReshapeFunc(reshape);
    glutMouseFunc(mouseButton);

    // Remove this line if you are not using FreeGLUT
    glutMouseWheelFunc(mouseWheel);

    glutKeyboardFunc(keyboard);
    glutKeyboardUpFunc(keyboardUp);
    glutSpecialFunc(specialKey);
    glutSpecialUpFunc(specialKeyUp);
    glutTimerFunc(16, timer, 0);

    lastTime = (float)glutGet(GLUT_ELAPSED_TIME) * 0.001f;

    printf("Controls: WASD/Arrows = Move, Mouse Wheel = Zoom, LMB = Scan, R = Reset\n");
    printf("Grid: %dx%d | World: %.0fx%.0f | Target: 60fps\n", GRID, GRID, WORLD, WORLD);

    glutMainLoop();
    return 0;
}
