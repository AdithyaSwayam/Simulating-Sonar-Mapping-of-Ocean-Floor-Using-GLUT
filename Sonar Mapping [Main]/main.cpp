#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <GL/freeglut.h>
#include <GL/glu.h>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <ctime>
#include <algorithm>

static const int   GRID  = 1024;
static const float WORLD = 700.0f;
static const float CELL  = WORLD / GRID;

static const int   NEAR_STEP  = 1;
static const int   MID_STEP   = 2;
static const int   FAR_STEP   = 4;

static const float SONAR_H          = 25.0f;
static const float BEAM_HALF_DEG    = 35.0f;
static const float BEAM_HALF_RAD    = BEAM_HALF_DEG * 3.14159265f / 180.0f;
static const float PING_SPEED       = 80.0f;
static const float PAN_SPEED        = 30.0f;

static const float T_STRUCTURE      = 2.5f;
static const float T_DETAIL         = 8.0f;
static const float T_DETAIL_FULL    = 14.0f;

static float* height = 0;
static float colorLowCut  = -0.8f;
static float colorHighCut =  0.9f;
static float* geo = 0;
static float* revealStruct = 0;
static float* revealColor = 0;
static float* dwellTime = 0;
static float* nx = 0;
static float* ny = 0;
static float* nz = 0;

static float sonarX = WORLD * 0.5f;
static float sonarZ = WORLD * 0.5f;
static bool  mouseDown = false;
static bool  wKey = false, aKey = false, sKey = false, dKey = false;
static float pingRadius = 0.0f;
static bool  pinging = false;
static float lastTime = 0.0f;
static float totalTime = 0.0f;
static int   mappedCells = 0;
static int   detailedCells = 0;

static float scanSpeedMult = 1.0f;
static const float SCAN_SPEED_MIN = 0.1f;
static const float SCAN_SPEED_MAX = 10.0f;
static const float SCAN_SPEED_STEP = 0.25f;

static bool fullReveal = false;

static float camDistance = 120.0f;
static float camAngleX   = 45.0f;
static float camAngleY   = -135.0f;
static bool  rightMouseDown = false;
static int   lastMouseX = 0, lastMouseY = 0;

static unsigned char perm[512];
static inline int idx(int z, int x) { return z * GRID + x; }
static inline float deg2rad(float d) { return d * 0.01745329252f; }

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
    return lerp(lerp(grad(perm[A], x, y), grad(perm[B], x - 1, y), u),
                lerp(grad(perm[A + 1], x, y - 1), grad(perm[B + 1], x - 1, y - 1), u), v);
}

static float fbm(float x, float y, int oct, float lac, float gain)
{
    float val = 0.0f, amp = 0.5f, freq = 1.0f;
    for (int i = 0; i < oct; ++i) {
        val += amp * noise2(x * freq, y * freq);
        amp *= gain;
        freq *= lac;
    }
    return val;
}

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

static void buildTerrain()
{
    for (int gz = 0; gz < GRID; ++gz) {
        for (int gx = 0; gx < GRID; ++gx) {
            float fx = gx / (float)GRID;
            float fz = gz / (float)GRID;

            float base = fbm(fx * 0.9f + 13.0f, fz * 0.9f + 29.0f, 4, 2.0f, 0.50f) * 0.45f;

            float r1 = fabsf(fbm(fx * 1.5f + 23.0f, fz * 1.4f + 47.0f, 6, 2.15f, 0.48f));
            float ridge1 = 1.0f - std::min(1.0f, r1 * 3.6f);
            ridge1 = ridge1 * ridge1 * ridge1;

            float r2 = fabsf(fbm(fx * 3.6f + 79.0f, fz * 3.1f + 11.0f, 4, 2.25f, 0.46f));
            float ridge2 = 1.0f - std::min(1.0f, r2 * 2.9f);
            ridge2 = ridge2 * ridge2;

            float hill = std::max(0.0f, fbm(fx * 2.4f + 33.0f, fz * 2.1f + 19.0f, 6, 2.1f, 0.52f)) * 2.6f;

            float crest = std::max(0.0f, fbm(fx * 8.0f + 71.0f, fz * 7.5f + 9.0f, 3, 2.35f, 0.44f));
            crest = crest * crest * 1.5f;

            float cut = fabsf(fbm(fx * 5.2f + 141.0f, fz * 4.7f + 57.0f, 4, 2.2f, 0.50f));
            cut = powf(std::min(1.0f, cut), 1.7f);

            float bumps = fbm(fx * 14.0f + 5.0f, fz * 14.0f + 17.0f, 4, 2.15f, 0.54f) * 0.18f;

            float micro = fbm(fx * 30.0f + 7.0f, fz * 30.0f + 15.0f, 3, 2.2f, 0.52f) * 0.08f;
            float ripple = sinf((fx * 22.0f + fz * 11.0f) * 3.14159f) * 0.045f
                         + sinf((fx * 17.0f - fz * 19.0f) * 3.14159f) * 0.040f;

            float cx = fx - 0.5f;
            float cz = fz - 0.5f;
            float rSq = cx * cx + cz * cz;

            float borderFade = sinf(fx * 3.14159f) * sinf(fz * 3.14159f);
            borderFade = borderFade * borderFade;
            float centreFade = 1.0f - expf(-rSq * 20.0f);
            float peakMask = borderFade * centreFade;

            float trenchBias = 1.0f - peakMask * 0.6f;

            float h =
                base +
                ridge1 * hill * 1.30f * peakMask +
                ridge2 * crest * 1.15f * peakMask +
                -cut * 1.25f * trenchBias +
                bumps + micro + ripple;

            h -= rSq * 0.03f;
            height[idx(gz, gx)] = h;

            float steepness = std::min(1.0f, ridge1 * 0.85f + ridge2 * 0.75f + crest * 0.65f);
            float rockiness = std::min(1.0f, steepness + fabsf(h) * 0.25f);

            geo[idx(gz,gx)*3 + 0] = lerp(0.57f, 0.18f, rockiness);
            geo[idx(gz,gx)*3 + 1] = lerp(0.48f, 0.16f, rockiness);
            geo[idx(gz,gx)*3 + 2] = lerp(0.34f, 0.13f, rockiness);
        }
    }

    float hmin = 1e9f, hmax = -1e9f;
    for (int i = 0; i < GRID * GRID; ++i) {
        if (height[i] < hmin) hmin = height[i];
        if (height[i] > hmax) hmax = height[i];
    }

    float hrng = hmax - hmin;
    for (int i = 0; i < GRID * GRID; ++i) {
        height[i] = ((height[i] - hmin) / hrng) * 6.0f - 3.0f;
    }

    const int BINS = 512;
    int hist[BINS];
    memset(hist, 0, sizeof(hist));

    for (int i = 0; i < GRID * GRID; ++i) {
        float t = (height[i] + 3.0f) / 6.0f;
        int b = (int)(t * (BINS - 1));
        if (b < 0) b = 0;
        if (b >= BINS) b = BINS - 1;
        hist[b]++;
    }

    int total = GRID * GRID;
    int acc = 0;
    int lowBin = 0, highBin = BINS - 1;
    int lowTarget = (int)(0.15f * total);
    int highTarget = (int)(0.75f * total);

    for (int b = 0; b < BINS; ++b) {
        acc += hist[b];
        if (acc >= lowTarget) { lowBin = b; break; }
    }

    acc = 0;
    for (int b = 0; b < BINS; ++b) {
        acc += hist[b];
        if (acc >= highTarget) { highBin = b; break; }
    }

    colorLowCut  = -3.0f + 6.0f * (lowBin  / (float)(BINS - 1));
    colorHighCut = -3.0f + 6.0f * (highBin / (float)(BINS - 1));
}

static void buildNormals()
{
    for (int i = 0; i < GRID * GRID; ++i) {
        nx[i] = 0.0f;
        ny[i] = 1.0f;
        nz[i] = 0.0f;
    }

    for (int gz = 1; gz < GRID - 1; ++gz) {
        for (int gx = 1; gx < GRID - 1; ++gx) {
            int i = idx(gz, gx);
            float dhdx = (height[idx(gz, gx + 1)] - height[idx(gz, gx - 1)]) / (2.0f * CELL);
            float dhdz = (height[idx(gz + 1, gx)] - height[idx(gz - 1, gx)]) / (2.0f * CELL);
            float len = sqrtf(dhdx * dhdx + 1.0f + dhdz * dhdz);
            nx[i] = -dhdx / len;
            ny[i] =  1.0f / len;
            nz[i] = -dhdz / len;
        }
    }
}

static void depthColor(float h, float &r, float &g, float &b)
{
    if (h <= colorLowCut) {
        float t = (h + 3.0f) / (colorLowCut + 3.0f + 1e-6f);
        t = std::max(0.0f, std::min(1.0f, t));
        float hue = 270.0f - t * 60.0f;
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
    else if (h >= colorHighCut) {
        float t = (h - colorHighCut) / (3.0f - colorHighCut + 1e-6f);
        t = std::max(0.0f, std::min(1.0f, t));
        float hue = 120.0f - t * 120.0f;
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
    else {
        float t = (h - colorLowCut) / (colorHighCut - colorLowCut + 1e-6f);
        t = std::max(0.0f, std::min(1.0f, t));
        float hue = 180.0f - t * 60.0f;
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
}

static void updateReveal(float dt)
{
    if (!pinging) return;

    float maxR = SONAR_H * tanf(BEAM_HALF_RAD) * 4.0f;
    int minX = std::max(0, (int)((sonarX - maxR) / CELL));
    int maxX = std::min(GRID - 1, (int)((sonarX + maxR) / CELL));
    int minZ = std::max(0, (int)((sonarZ - maxR) / CELL));
    int maxZ = std::min(GRID - 1, (int)((sonarZ + maxR) / CELL));
    float maxRSq = maxR * maxR;

    float effDt = dt * scanSpeedMult;

    for (int gz = minZ; gz <= maxZ; ++gz) {
        float wz = gz * CELL;
        float dz = wz - sonarZ;

        for (int gx = minX; gx <= maxX; ++gx) {
            float wx = gx * CELL;
            float dx = wx - sonarX;
            float distSq = dx * dx + dz * dz;
            if (distSq > maxRSq) continue;

            float dist = sqrtf(distSq);
            float factor = 1.0f - dist / maxR;
            int i = idx(gz, gx);

            float prevStruct = revealStruct[i];
            float prevColor  = revealColor[i];

            dwellTime[i] += effDt * (0.4f + 0.6f * factor);

            float s1 = std::min(1.0f, dwellTime[i] / T_STRUCTURE);
            if (s1 > revealStruct[i]) revealStruct[i] = s1;

            if (dwellTime[i] > T_DETAIL) {
                float s2 = (dwellTime[i] - T_DETAIL) / (T_DETAIL_FULL - T_DETAIL);
                s2 = std::min(1.0f, s2);
                if (s2 > revealColor[i]) revealColor[i] = s2;
            }

            if (prevStruct <= 0.5f && revealStruct[i] > 0.5f) ++mappedCells;
            if (prevColor  <= 0.5f && revealColor[i]  > 0.5f) ++detailedCells;
        }
    }
}

static void emitVertex(int gx, int gz, float heightScale)
{
    int i = idx(gz, gx);
    float rs = revealStruct[i];
    float rc = revealColor[i];

    if (rs < 0.01f) {
        glColor3f(0.0f, 0.0f, 0.0f);
        glNormal3f(0.0f, 1.0f, 0.0f);
        glVertex3f(gx * CELL, -50.0f, gz * CELL);
        return;
    }

    float sr = 0.12f + rs * 0.25f;
    float sg = 0.15f + rs * 0.28f;
    float sb = 0.20f + rs * 0.32f;

    if (rc > 0.0f) {
        float dr, dg, db;
        depthColor(height[i], dr, dg, db);

        float gr = geo[i*3 + 0];
        float gg = geo[i*3 + 1];
        float gb = geo[i*3 + 2];

        float mr = gr * 0.15f + dr * 0.85f;
        float mg = gg * 0.15f + dg * 0.85f;
        float mb = gb * 0.15f + db * 0.85f;

        sr = lerp(sr, mr, rc);
        sg = lerp(sg, mg, rc);
        sb = lerp(sb, mb, rc);
    }

    sr *= rs * 0.95f;
    sg *= rs * 0.95f;
    sb *= rs * 0.95f;

    glColor3f(sr, sg, sb);
    glNormal3f(nx[i], ny[i], nz[i]);
    glVertex3f(gx * CELL, height[i] * heightScale, gz * CELL);
}

static void drawTerrainBand(int z0, int z1, int step)
{
    float heightScale = 13.0f;

    for (int gz = z0; gz < z1; gz += step) {
        int gzNext = std::min(gz + step, GRID - 1);
        glBegin(GL_TRIANGLE_STRIP);

        for (int gx = 0; gx < GRID; gx += step) {
            emitVertex(gx, gz, heightScale);
            emitVertex(gx, gzNext, heightScale);
        }

        if (((GRID - 1) % step) != 0) {
            emitVertex(GRID - 1, gz, heightScale);
            emitVertex(GRID - 1, gzNext, heightScale);
        }

        glEnd();
    }
}

static void drawTerrain()
{
    glEnable(GL_DEPTH_TEST);

    int nearRows = (int)(45.0f / CELL);
    int midRows  = (int)(110.0f / CELL);
    int sonarRow = std::max(0, std::min(GRID - 1, (int)(sonarZ / CELL)));

    int zNear0 = std::max(0, sonarRow - nearRows);
    int zNear1 = std::min(GRID - 1, sonarRow + nearRows);
    int zMid0  = std::max(0, sonarRow - midRows);
    int zMid1  = std::min(GRID - 1, sonarRow + midRows);

    drawTerrainBand(0, zMid0, FAR_STEP);
    drawTerrainBand(zMid0, zNear0, MID_STEP);
    drawTerrainBand(zNear0, zNear1, NEAR_STEP);
    drawTerrainBand(zNear1, zMid1, MID_STEP);
    drawTerrainBand(zMid1, GRID - 1, FAR_STEP);
}

static void drawPingRings()
{
    if (!pinging && pingRadius <= 0.01f) return;

    float maxR = SONAR_H * tanf(BEAM_HALF_RAD) * 4.0f;
    float alpha = 1.0f - pingRadius / maxR;
    if (alpha < 0.0f) alpha = 0.0f;

    glDisable(GL_LIGHTING);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glLineWidth(2.0f);

    for (int ring = 0; ring < 3; ++ring) {
        float r = pingRadius - ring * 7.0f;
        if (r < 0.0f) continue;

        float a = alpha * (1.0f - ring * 0.30f);
        glColor4f(0.3f, 0.95f, 1.0f, a * 0.8f);
        glBegin(GL_LINE_LOOP);

        const int segs = 48;
        for (int s = 0; s < segs; ++s) {
            float ang = s * 2.0f * 3.14159265f / segs;
            float px = sonarX + cosf(ang) * r;
            float pz = sonarZ + sinf(ang) * r;
            int igx = std::max(0, std::min(GRID - 1, (int)(px / CELL)));
            int igz = std::max(0, std::min(GRID - 1, (int)(pz / CELL)));
            float h = height[idx(igz, igx)] * 8.0f;
            glVertex3f(px, h + 0.3f, pz);
        }

        glEnd();
    }

    glDisable(GL_BLEND);
    glEnable(GL_LIGHTING);
}

static void drawSonarDevice()
{
    glDisable(GL_LIGHTING);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    float devY = SONAR_H;
    float maxR = SONAR_H * tanf(BEAM_HALF_RAD);

    glColor4f(0.85f, 0.88f, 0.92f, 0.95f);
    glPushMatrix();
    glTranslatef(sonarX, devY + 1.2f, sonarZ);
    GLUquadric* quad = gluNewQuadric();
    gluQuadricDrawStyle(quad, GLU_FILL);
    gluQuadricNormals(quad, GLU_SMOOTH);
    gluCylinder(quad, 1.1f, 1.1f, 2.4f, 12, 1);
    gluSphere(quad, 1.1f, 12, 8);
    gluDeleteQuadric(quad);
    glPopMatrix();

    glColor4f(0.95f, 0.95f, 1.0f, 1.0f);
    glPushMatrix();
    glTranslatef(sonarX, devY + 0.8f, sonarZ);
    glutSolidSphere(0.85f, 10, 8);
    glPopMatrix();

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
        glutSolidSphere(0.35f, 8, 6);
        glPopMatrix();
    }

    glColor4f(0.85f, 0.88f, 0.95f, 0.14f);
    glBegin(GL_TRIANGLE_FAN);
    glVertex3f(sonarX, devY, sonarZ);
    const int segs = 36;
    for (int s = 0; s <= segs; ++s) {
        float ang = s * 2.0f * 3.14159265f / segs;
        glVertex3f(sonarX + cosf(ang) * maxR, 0.0f, sonarZ + sinf(ang) * maxR);
    }
    glEnd();

    glColor4f(0.95f, 0.97f, 1.0f, 0.45f);
    glBegin(GL_LINE_LOOP);
    for (int s = 0; s < segs; ++s) {
        float ang = s * 2.0f * 3.14159265f / segs;
        glVertex3f(sonarX + cosf(ang) * maxR, 0.0f, sonarZ + sinf(ang) * maxR);
    }
    glEnd();

    glDisable(GL_BLEND);
    glEnable(GL_LIGHTING);
}

static void drawOceanSurface()
{
    glDisable(GL_LIGHTING);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(0.015f, 0.045f, 0.16f, 0.55f);

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

static void drawText2D(int x, int y, const char* txt)
{
    glRasterPos2i(x, y);
    for (const char* c = txt; *c; ++c) glutBitmapCharacter(GLUT_BITMAP_HELVETICA_12, *c);
}

static void drawHUD(int w, int h)
{
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    gluOrtho2D(0, w, 0, h);

    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    glDisable(GL_LIGHTING);
    glDisable(GL_DEPTH_TEST);

    char buf[256];

    glColor3f(0.2f, 1.0f, 0.8f);
    drawText2D(15, h - 28, "SONAR TERRAIN MAPPER - OPTIMIZED");

    sprintf(buf, "ROV Position: %.1f, %.1f  Altitude: %.0fm", sonarX, sonarZ, SONAR_H);
    drawText2D(15, h - 55, buf);

    int total = GRID * GRID;
    sprintf(buf, "Terrain Mapped: %d%%  Detailed: %d%%  Grid: %dx%d",
            (mappedCells * 100) / total, (detailedCells * 100) / total, GRID, GRID);
    drawText2D(15, h - 78, buf);

    glColor3f(1.0f, 0.9f, 0.3f);
    drawText2D(15, h - 102, pinging ? "[ SCANNING ]" : "[ HOLD LMB TO SCAN ]");

    glColor3f(0.8f, 0.7f, 1.0f);
    sprintf(buf, "Scan Speed: %.2fx  (+/- to adjust)", scanSpeedMult);
    drawText2D(15, h - 126, buf);

    if (fullReveal) {
        glColor3f(1.0f, 0.5f, 0.2f);
        drawText2D(15, h - 150, "[ FULL REVEAL ACTIVE — press F to hide ]");
    }

    glColor3f(0.6f, 0.75f, 0.9f);
    drawText2D(15, 20, "WASD: Move  Wheel: Zoom  LMB: Scan  RMB Drag: Rotate  F: Full Reveal  +/-: Scan Speed  R: Reset");

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LIGHTING);

    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
}

static int winW = 1400;
static int winH = 900;

static void updateCamera()
{
    float ax = deg2rad(camAngleX);
    float ay = deg2rad(camAngleY);

    float camX = sonarX + camDistance * sinf(ay) * cosf(ax);
    float camZ = sonarZ + camDistance * cosf(ay) * cosf(ax);
    float camY = SONAR_H + camDistance * 0.28f * sinf(ax);

    float lookY = -camDistance * 0.08f;

    gluLookAt(camX, camY, camZ, sonarX, lookY, sonarZ, 0.0f, 1.0f, 0.0f);
}

static void reshape(int w, int h)
{
    winW = w;
    winH = (h <= 0 ? 1 : h);
    glViewport(0, 0, winW, winH);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(55.0, (double)winW / winH, 0.1, 2000.0);
    glMatrixMode(GL_MODELVIEW);
}

static void display()
{
    glClearColor(0.008f, 0.018f, 0.05f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glLoadIdentity();
    updateCamera();

    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);

    GLfloat lpos[] = { WORLD * 0.4f, 120.0f, WORLD * 0.3f, 1.0f };
    GLfloat lamb[] = { 0.06f, 0.08f, 0.12f, 1.0f };
    GLfloat ldif[] = { 0.85f, 0.88f, 0.92f, 1.0f };
    GLfloat lspe[] = { 0.65f, 0.68f, 0.72f, 1.0f };

    glLightfv(GL_LIGHT0, GL_POSITION, lpos);
    glLightfv(GL_LIGHT0, GL_AMBIENT, lamb);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, ldif);
    glLightfv(GL_LIGHT0, GL_SPECULAR, lspe);

    glEnable(GL_COLOR_MATERIAL);
    glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);

    GLfloat mspec[] = { 0.4f, 0.4f, 0.5f, 1.0f };
    glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, mspec);
    glMateriali(GL_FRONT_AND_BACK, GL_SHININESS, 32);

    glShadeModel(GL_SMOOTH);
    glEnable(GL_DEPTH_TEST);

    drawTerrain();
    drawPingRings();
    drawSonarDevice();
    drawOceanSurface();
    drawHUD(winW, winH);

    glutSwapBuffers();
}

static void timer(int)
{
    float now = (float)glutGet(GLUT_ELAPSED_TIME) * 0.001f;
    float dt = now - lastTime;
    if (dt > 0.05f) dt = 0.05f;
    if (dt < 0.0f) dt = 0.0f;

    lastTime = now;
    totalTime += dt;

    float ax = deg2rad(camAngleX);
    float ay = deg2rad(camAngleY);
    float cosX = cosf(ax);
    float sinX = sinf(ax);
    float cosY = cosf(ay);
    float sinY = sinf(ay);


    float fwX = -sinY;
    float fwZ = -cosY;

    float rtX =  cosY;
    float rtZ = -sinY;

    float speed = PAN_SPEED * dt;

    float dx = 0.0f, dz = 0.0f;

    if (wKey) { dx += fwX; dz += fwZ; }
    if (sKey) { dx -= fwX; dz -= fwZ; }
    if (dKey) { dx += rtX; dz += rtZ; }
    if (aKey) { dx -= rtX; dz -= rtZ; }

    float len = sqrtf(dx * dx + dz * dz);
    if (len > 1e-5f) {
        dx /= len;
        dz /= len;
    }

    sonarX += dx * speed;
    sonarZ += dz * speed;

    sonarX = std::max(5.0f, std::min(WORLD - 5.0f, sonarX));
    sonarZ = std::max(5.0f, std::min(WORLD - 5.0f, sonarZ));

    if (mouseDown) {
        pinging = true;
        float maxR = SONAR_H * tanf(BEAM_HALF_RAD) * 4.0f;
        pingRadius += PING_SPEED * dt;
        if (pingRadius > maxR) pingRadius = 0.0f;
    } else {
        pinging = false;
        pingRadius *= 0.88f;
        if (pingRadius < 0.01f) pingRadius = 0.0f;
    }

    updateReveal(dt);
    glutPostRedisplay();
    glutTimerFunc(16, timer, 0);
}
static void mouseButton(int button, int state, int x, int y)
{
    if (button == GLUT_LEFT_BUTTON) {
        mouseDown = (state == GLUT_DOWN);
    } else if (button == GLUT_RIGHT_BUTTON) {
        rightMouseDown = (state == GLUT_DOWN);
        lastMouseX = x;
        lastMouseY = y;
    }
}

static void mouseMotion(int x, int y)
{
    if (rightMouseDown) {
        int dx = x - lastMouseX;
        int dy = y - lastMouseY;
        lastMouseX = x;
        lastMouseY = y;

        camAngleY += dx * 0.25f;
        camAngleX += dy * 0.25f;

        if (camAngleX < 10.0f) camAngleX = 10.0f;
        if (camAngleX > 80.0f) camAngleX = 80.0f;

        glutPostRedisplay();
    }
}

static void mouseWheel(int, int dir, int, int)
{
    camDistance -= dir * 8.0f;
    camDistance = std::max(20.0f, std::min(500.0f, camDistance));
}

static void applyFullReveal(bool reveal)
{
    int total = GRID * GRID;
    if (reveal) {
        for (int i = 0; i < total; ++i) {
            revealStruct[i] = 1.0f;
            revealColor[i]  = 1.0f;
            dwellTime[i]    = T_DETAIL_FULL + 1.0f;
        }
        mappedCells   = total;
        detailedCells = total;
    } else {
        memset(revealStruct, 0, total * sizeof(float));
        memset(revealColor,  0, total * sizeof(float));
        memset(dwellTime,    0, total * sizeof(float));
        mappedCells   = 0;
        detailedCells = 0;
    }
}

static void keyboard(unsigned char key, int, int)
{
    switch (key) {
        case 'w': case 'W': wKey = true; break;
        case 's': case 'S': sKey = true; break;
        case 'a': case 'A': aKey = true; break;
        case 'd': case 'D': dKey = true; break;

        case 'f': case 'F':
            fullReveal = !fullReveal;
            applyFullReveal(fullReveal);
            break;

        case '+': case '=':
            scanSpeedMult += SCAN_SPEED_STEP;
            if (scanSpeedMult > SCAN_SPEED_MAX) scanSpeedMult = SCAN_SPEED_MAX;
            printf("Scan speed: %.2fx\n", scanSpeedMult);
            break;

        case '-': case '_':
            scanSpeedMult -= SCAN_SPEED_STEP;
            if (scanSpeedMult < SCAN_SPEED_MIN) scanSpeedMult = SCAN_SPEED_MIN;
            printf("Scan speed: %.2fx\n", scanSpeedMult);
            break;

        case 'r': case 'R':
            fullReveal = false;
            applyFullReveal(false);
            break;

        case 27:
            exit(0);
    }
}

static void keyboardUp(unsigned char key, int, int)
{
    switch (key) {
        case 'w': case 'W': wKey = false; break;
        case 's': case 'S': sKey = false; break;
        case 'a': case 'A': aKey = false; break;
        case 'd': case 'D': dKey = false; break;
    }
}

static void specialKey(int key, int, int)
{
    if (key == GLUT_KEY_LEFT)  aKey = true;
    if (key == GLUT_KEY_RIGHT) dKey = true;
    if (key == GLUT_KEY_UP)    wKey = true;
    if (key == GLUT_KEY_DOWN)  sKey = true;
}

static void specialKeyUp(int key, int, int)
{
    if (key == GLUT_KEY_LEFT)  aKey = false;
    if (key == GLUT_KEY_RIGHT) dKey = false;
    if (key == GLUT_KEY_UP)    wKey = false;
    if (key == GLUT_KEY_DOWN)  sKey = false;
}

int main(int argc, char** argv)
{
    srand((unsigned)time(0));
    initPerm();

    printf("Initializing %dx%d terrain...\n", GRID, GRID);
    allocTerrain();
    buildTerrain();
    buildNormals();

    memset(revealStruct, 0, GRID * GRID * sizeof(float));
    memset(revealColor, 0, GRID * GRID * sizeof(float));
    memset(dwellTime, 0, GRID * GRID * sizeof(float));

    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGB | GLUT_DEPTH);
    glutInitWindowSize(winW, winH);
    glutCreateWindow("Sonar Terrain Mapper - Optimized");

    glDisable(GL_NORMALIZE);
    glDisable(GL_LINE_SMOOTH);
    glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_FASTEST);
    glHint(GL_LINE_SMOOTH_HINT, GL_FASTEST);

    glutDisplayFunc(display);
    glutReshapeFunc(reshape);
    glutMouseFunc(mouseButton);
    glutMotionFunc(mouseMotion);
    glutMouseWheelFunc(mouseWheel);
    glutKeyboardFunc(keyboard);
    glutKeyboardUpFunc(keyboardUp);
    glutSpecialFunc(specialKey);
    glutSpecialUpFunc(specialKeyUp);
    glutTimerFunc(16, timer, 0);

    lastTime = (float)glutGet(GLUT_ELAPSED_TIME) * 0.001f;

    printf("Controls: WASD/Arrows=Move  Wheel=Zoom  LMB=Scan  RMB Drag=Rotate around sonar\n");
    printf("          F=Full Reveal Toggle  +/-=Scan Speed  R=Reset\n");
    printf("Optimized grid: %dx%d | World: %.0fx%.0f\n", GRID, GRID, WORLD, WORLD);

    glutMainLoop();
    return 0;
}
