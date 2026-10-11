// =============================================================================
//  editor.cpp  -  In-game map editor
//
//  Edit any of the six maps cell by cell: walls, doors, secret walls, ledge
//  heights, pits, hazards, spawns, items and mission markers. The bucket tool
//  is a 4-connected seed fill (stack-based flood fill). Validation flood-fills
//  from the player start to find markers nobody can reach. Saving writes
//  maps/<NAME>.txt next to the game, which replaces the built-in layout the
//  next time the game starts; F5 play-tests the edited map straight away.
// =============================================================================
#include "mainwindow.h"
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QCoreApplication>
#include <cmath>
#include <algorithm>

using namespace rs;

static const uint32_t ED_AMBER = 0xFFFFB43A, ED_BG = 0xFF0E1216;
static inline QColor qcE(uint32_t c, int a = 255) { return QColor(R(c), G(c), B(c), a); }

struct Brush { char c; const char *name; };
static const Brush BRUSHES[] = {
    {'.', "GROUND"}, {'#', "STONE WALL"}, {'B', "BRICK WALL"}, {'P', "PILLAR / TREE"}, {'D', "DOOR"}, {'S', "SECRET WALL"},
    {'-', "STEP +0.25"}, {'^', "LEDGE +0.5"}, {'=', "HIGH +1.0"}, {'v', "PIT -0.6"}, {'~', "HAZARD POOL"},
    {'@', "PLAYER START"}, {'e', "ENEMY"}, {'x', "ELITE"}, {'k', "MEDKIT"}, {'a', "AMMO"}, {'r', "RIFLE"},
    {'s', "SHOTGUN"}, {'t', "INTEL"}, {'m', "MENU CAMERA"}, {'b', "BOSS SPOT"}, {'1', "MISSION POINT 1"},
};
static const int BRUSH_COUNT = int(sizeof(BRUSHES) / sizeof(BRUSHES[0]));

static QString mapFilePath(int map)
{
    return QCoreApplication::applicationDirPath() + "/maps/" + QString(MAP_NAMES[map]).replace(' ', '_') + ".txt";
}

// The built-in rows of a map, or the saved override if one exists
void MainWindow::loadLayoutRows(bool overrides)
{
    for (int m = 0; m < MAP_COUNT; m++) {
        m_layoutRows[m].clear();
        for (int y = 0; y < LAYOUT_ROWS[m]; y++) m_layoutRows[m].push_back(ALL_LAYOUTS[m][y]);
        if (!overrides || m_testMode) continue;              // tests always use the built-in maps
        QFile f(mapFilePath(m));
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
        std::vector<std::string> rows;
        QTextStream in(&f);
        while (!in.atEnd()) { QString line = in.readLine().trimmed(); if (!line.isEmpty()) rows.push_back(line.toStdString()); }
        if (validateRows(rows).isEmpty()) { m_layoutRows[m] = rows; applyLayoutRows(m); }
    }
}

// Re-parse a map from its rows (after an edit or loading an override)
void MainWindow::applyLayoutRows(int map)
{
    std::vector<const char *> ptrs;
    for (const std::string &r : m_layoutRows[map]) ptrs.push_back(r.c_str());
    parseLayout(ptrs.data(), int(ptrs.size()), m_parsed[map]);
    m_sunVisCache[map].clear(); m_lightCache[map].clear(); m_sunCache[map].clear();
}

// Problems that would make a map unplayable (empty = fine)
QString MainWindow::validateRows(const std::vector<std::string> &rows) const
{
    if (rows.size() < 8) return "MAP TOO SMALL";
    size_t w = rows[0].size();
    for (const std::string &r : rows) if (r.size() != w) return "ROWS HAVE DIFFERENT LENGTHS";
    int h = int(rows.size());
    auto wall = [](char c) { return c == '#' || c == 'B' || c == 'P'; };
    for (int x = 0; x < int(w); x++) if (!wall(rows[0][size_t(x)]) || !wall(rows[size_t(h - 1)][size_t(x)])) return "THE BORDER MUST BE WALL";
    for (int y = 0; y < h; y++) if (!wall(rows[size_t(y)][0]) || !wall(rows[size_t(y)][w - 1])) return "THE BORDER MUST BE WALL";
    int starts = 0, sx = 0, sy = 0;
    for (int y = 0; y < h; y++) for (int x = 0; x < int(w); x++) if (rows[size_t(y)][size_t(x)] == '@') { starts++; sx = x; sy = y; }
    if (starts != 1) return starts ? "MORE THAN ONE PLAYER START" : "NO PLAYER START (@)";
    // flood fill from the start: every marker must be reachable (doors and secret walls open)
    std::vector<uint8_t> seen(size_t(w) * h, 0);
    std::vector<int> st{sy * int(w) + sx};
    while (!st.empty()) {
        int id = st.back(); st.pop_back();
        if (seen[size_t(id)]) continue;
        int x = id % int(w), y = id / int(w);
        if (wall(rows[size_t(y)][size_t(x)])) continue;
        seen[size_t(id)] = 1;
        if (x > 0) st.push_back(id - 1);
        if (x < int(w) - 1) st.push_back(id + 1);
        if (y > 0) st.push_back(id - int(w));
        if (y < h - 1) st.push_back(id + int(w));
    }
    int lost = 0;
    for (int y = 0; y < h; y++) for (int x = 0; x < int(w); x++) {
        char c = rows[size_t(y)][size_t(x)];
        if (std::string("exkartsm123456789bcnzyfg").find(c) != std::string::npos && !seen[size_t(y) * w + size_t(x)]) lost++;
    }
    if (lost) return QString("%1 MARKER(S) CAN'T BE REACHED").arg(lost);
    return QString();
}

void MainWindow::openEditor(int map)
{
    m_edMap = clampi(map, 0, MAP_COUNT - 1);
    m_edRows = m_layoutRows[m_edMap];
    int mw = int(m_edRows[0].size()), mh = int(m_edRows.size());
    m_edZoom = std::max(4, std::min(560 / mw, 500 / mh));
    m_edPanX = m_edPanY = 0;
    m_edMsg = validateRows(m_edRows); m_edMsgT = 3.f;
    if (m_edMsg.isEmpty()) m_edMsg = "MAP IS VALID";
    m_screen = SCR_EDITOR;
}

// Grid area on screen
static const int ED_X = 12, ED_Y = 64, ED_W = 560, ED_H = 500;

bool MainWindow::editorCellAt(float fx, float fy, int &cx, int &cy) const
{
    if (fx < ED_X || fy < ED_Y || fx >= ED_X + ED_W || fy >= ED_Y + ED_H) return false;
    cx = int((fx - ED_X) / m_edZoom) + m_edPanX;
    cy = int((fy - ED_Y) / m_edZoom) + m_edPanY;
    return cy >= 0 && cy < int(m_edRows.size()) && cx >= 0 && cx < int(m_edRows[0].size());
}

void MainWindow::editorPaint(int cx, int cy, bool erase)
{
    int mw = int(m_edRows[0].size()), mh = int(m_edRows.size());
    char c = erase ? '.' : BRUSHES[m_edBrush].c;
    bool border = cx == 0 || cy == 0 || cx == mw - 1 || cy == mh - 1;
    if (border && c != '#' && c != 'B' && c != 'P') return;   // keep the outer wall
    if (m_edFill && !erase) {
        // seed fill: replace the 4-connected region of the clicked character
        char target = m_edRows[size_t(cy)][size_t(cx)];
        if (target == c) return;
        std::vector<int> st{cy * mw + cx};
        int n = 0;
        while (!st.empty() && n < 20000) {
            int id = st.back(); st.pop_back();
            int x = id % mw, y = id / mw;
            if (m_edRows[size_t(y)][size_t(x)] != target) continue;
            if (x == 0 || y == 0 || x == mw - 1 || y == mh - 1) continue;
            m_edRows[size_t(y)][size_t(x)] = c; n++;
            st.push_back(id - 1); st.push_back(id + 1); st.push_back(id - mw); st.push_back(id + mw);
        }
    } else {
        if (c == '@')                                           // only one start
            for (std::string &r : m_edRows) for (char &ch : r) if (ch == '@') ch = '.';
        m_edRows[size_t(cy)][size_t(cx)] = c;
    }
    m_edDirty = true;
}

void MainWindow::editorSave()
{
    QString err = validateRows(m_edRows);
    if (!err.isEmpty()) { m_edMsg = "NOT SAVED: " + err; m_edMsgT = 4.f; sfx(SND_UI_BACK, 0.6f); return; }
    m_layoutRows[m_edMap] = m_edRows;
    applyLayoutRows(m_edMap);
    if (!m_testMode) {
        QDir().mkpath(QCoreApplication::applicationDirPath() + "/maps");
        QFile f(mapFilePath(m_edMap));
        if (f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
            QTextStream out(&f);
            for (const std::string &r : m_edRows) out << QString::fromStdString(r) << "\n";
        }
    }
    m_edDirty = false;
    m_edMsg = "SAVED - " + QString(MAP_NAMES[m_edMap]) + " NOW USES THIS LAYOUT"; m_edMsgT = 4.f;
    sfx(SND_UPGRADE, 0.6f);
}

void MainWindow::editorPlaytest()
{
    QString err = validateRows(m_edRows);
    if (!err.isEmpty()) { m_edMsg = "CAN'T PLAY: " + err; m_edMsgT = 4.f; sfx(SND_UI_BACK, 0.6f); return; }
    m_layoutRows[m_edMap] = m_edRows;
    applyLayoutRows(m_edMap);
    m_mapChoice = m_edMap;
    m_edPlaytest = true;
    initGame();
    m_screen = SCR_PLAYING;
    pushFeed("PLAYTEST - F5 RETURNS TO THE EDITOR", 5.f, true);
}

void MainWindow::editorKey(QKeyEvent *e)
{
    int k = e->key();
    int mw = int(m_edRows[0].size()), mh = int(m_edRows.size());
    int vis = ED_W / m_edZoom;
    if (k == Qt::Key_Escape) { m_screen = SCR_MENU; m_menuCursor = 4; sfx(SND_UI_BACK, 0.6f); return; }
    if (k == Qt::Key_F5) { editorPlaytest(); return; }
    if (k == Qt::Key_S && (e->modifiers() & Qt::ControlModifier)) { editorSave(); return; }
    if (k == Qt::Key_F) { m_edFill = !m_edFill; sfx(SND_UI_MOVE, 0.5f); return; }
    if (k == Qt::Key_PageUp || k == Qt::Key_PageDown) {
        if (m_edDirty) { m_edMsg = "UNSAVED CHANGES - CTRL+S TO SAVE, OR SWITCH AGAIN TO DISCARD"; m_edMsgT = 3.f; m_edDirty = false; return; }
        openEditor((m_edMap + (k == Qt::Key_PageDown ? 1 : MAP_COUNT - 1)) % MAP_COUNT); sfx(SND_UI_MOVE, 0.5f); return;
    }
    if (k == Qt::Key_R && (e->modifiers() & Qt::ControlModifier)) {         // revert to the built-in layout
        m_edRows.clear();
        for (int y = 0; y < LAYOUT_ROWS[m_edMap]; y++) m_edRows.push_back(ALL_LAYOUTS[m_edMap][y]);
        m_edDirty = true; m_edMsg = "REVERTED TO THE BUILT-IN LAYOUT (CTRL+S TO KEEP)"; m_edMsgT = 3.f; return;
    }
    if (k == Qt::Key_Plus || k == Qt::Key_Equal) m_edZoom = std::min(28, m_edZoom + 2);
    if (k == Qt::Key_Minus) m_edZoom = std::max(4, m_edZoom - 2);
    if (k == Qt::Key_Left)  m_edPanX = std::max(0, m_edPanX - 4);
    if (k == Qt::Key_Right) m_edPanX = std::min(std::max(0, mw - vis), m_edPanX + 4);
    if (k == Qt::Key_Up)    m_edPanY = std::max(0, m_edPanY - 4);
    if (k == Qt::Key_Down)  m_edPanY = std::min(std::max(0, mh - ED_H / m_edZoom), m_edPanY + 4);
    m_edPanX = std::min(m_edPanX, std::max(0, mw - vis));
    m_edPanY = std::min(m_edPanY, std::max(0, mh - ED_H / m_edZoom));
    // type a cell character to pick that brush
    QString t = e->text();
    if (t.size() == 1 && !(e->modifiers() & Qt::ControlModifier)) {
        char c = char(t[0].toLatin1());
        for (int i = 0; i < BRUSH_COUNT; i++) if (BRUSHES[i].c == c) { m_edBrush = i; sfx(SND_UI_MOVE, 0.4f); }
        if (c >= '2' && c <= '9') { m_edBrush = BRUSH_COUNT - 1; m_edDigit = c; }
    }
}

void MainWindow::editorMouse(const QPointF &f, Qt::MouseButtons buttons)
{
    int cx, cy;
    // palette clicks
    for (int i = 0; i < BRUSH_COUNT; i++) {
        int px = 590 + (i % 2) * 102, py = 116 + (i / 2) * 22;
        if (f.x() >= px && f.x() < px + 98 && f.y() >= py && f.y() < py + 20 && (buttons & Qt::LeftButton)) { m_edBrush = i; m_edDigit = '1'; return; }
    }
    if (!editorCellAt(float(f.x()), float(f.y()), cx, cy)) return;
    if (buttons & Qt::LeftButton) {
        char saved = BRUSHES[m_edBrush].c;
        if (saved == '1' && m_edDigit != '1') {               // mission point 2..9 typed on the keyboard
            m_edRows[size_t(cy)][size_t(cx)] = m_edDigit; m_edDirty = true; return;
        }
        editorPaint(cx, cy, false);
    } else if (buttons & Qt::RightButton) editorPaint(cx, cy, true);
}

void MainWindow::renderEditor()
{
    m_cv.fillRect(0, 0, W, H, 0xFF0A0D10);
    menuHeader(QString("MAP EDITOR   -   %1   (%2 x %3)").arg(MAP_NAMES[m_edMap]).arg(m_edRows[0].size()).arg(m_edRows.size()), "");
    const int th = MAP_THEME[m_edMap];
    const Theme &T = THEMES[th];
    uint32_t floorC = m_floorTex[th].lv[MIP_LEVELS - 1][0];
    int mw = int(m_edRows[0].size()), mh = int(m_edRows.size());
    int z = m_edZoom;
    m_cv.fillRect(ED_X - 2, ED_Y - 2, ED_W + 4, ED_H + 4, 0xFF1C232B);
    for (int y = 0; y < ED_H / z && y + m_edPanY < mh; y++) for (int x = 0; x < ED_W / z && x + m_edPanX < mw; x++) {
        char c = m_edRows[size_t(y + m_edPanY)][size_t(x + m_edPanX)];
        uint32_t col;
        switch (c) {
        case '#': col = m_wallTex[th][1].lv[MIP_LEVELS - 1][0]; break;
        case 'B': col = m_wallTex[th][2].lv[MIP_LEVELS - 1][0]; break;
        case 'P': col = th == BIO_SWAMP || th == BIO_SNOW ? rgb(50, 110, 50) : rgb(130, 130, 136); break;
        case 'D': col = rgb(170, 110, 50); break;
        case 'S': col = rgb(150, 120, 200); break;
        case '-': col = scaleCol(floorC, 1.2f); break;
        case '^': col = scaleCol(floorC, 1.45f); break;
        case '=': col = scaleCol(floorC, 1.75f); break;
        case 'v': col = scaleCol(floorC, 0.5f); break;
        case '~': col = T.hazLight; break;
        default: col = floorC; break;
        }
        int px = ED_X + x * z, py = ED_Y + y * z;
        m_cv.fillRect(px, py, z, z, col);
        if (z >= 8) m_cv.blendRect(px, py, z, 1, withAlpha(0, 60)), m_cv.blendRect(px, py, 1, z, withAlpha(0, 60));
        if (std::string("@exkartsmbcnzyfg123456789").find(c) != std::string::npos) {
            uint32_t mc = c == '@' ? ED_AMBER : c == 'e' || c == 'x' || c == 'b' || c == 'c' ? rgb(240, 70, 60)
                        : (c >= '1' && c <= '9') ? rgb(90, 200, 255) : rgb(120, 230, 140);
            m_cv.fillCircle(px + z / 2, py + z / 2, std::max(1, z / 3), mc);
            if (z >= 12) text(px, py, z, z, Qt::AlignCenter, QString(QChar(c)), QColor(10, 10, 10), 10, 900, false);
        }
    }
    // hover outline
    QPointF f = widgetToFrame(m_mouseWidget);
    int hx, hy;
    if (editorCellAt(float(f.x()), float(f.y()), hx, hy))
        m_cv.rectOutline(ED_X + (hx - m_edPanX) * z, ED_Y + (hy - m_edPanY) * z, z, z, 0xFFFFFFFF);

    // palette
    text(590, 88, 200, 20, Qt::AlignLeft, m_edFill ? "TOOL: BUCKET FILL  [F]" : "TOOL: PENCIL  [F]", qcE(ED_AMBER), 12, 800, false);
    for (int i = 0; i < BRUSH_COUNT; i++) {
        int px = 590 + (i % 2) * 102, py = 116 + (i / 2) * 22;
        bool sel = i == m_edBrush;
        m_cv.blendRect(px, py, 98, 20, withAlpha(sel ? ED_AMBER : ED_BG, sel ? 90 : 200));
        if (sel) m_cv.rectOutline(px, py, 98, 20, ED_AMBER);
        char shown = BRUSHES[i].c == '1' ? m_edDigit : BRUSHES[i].c;
        text(px + 4, py, 16, 20, Qt::AlignLeft | Qt::AlignVCenter, QString(QChar(shown)), QColor(255, 255, 255), 10, 900, false);
        text(px + 18, py, 80, 20, Qt::AlignLeft | Qt::AlignVCenter, BRUSHES[i].name, sel ? QColor(255, 255, 255) : QColor(170, 175, 180), 7, 700, false);
    }
    const char *help[] = { "LMB paint   RMB erase", "type a cell char to pick it", "+ / - zoom   ARROWS pan", "PGUP / PGDN other map",
                           "CTRL+S save   CTRL+R revert", "F5 play-test   ESC back" };
    for (int i = 0; i < 6; i++) text(590, 376 + i * 16, 200, 14, Qt::AlignLeft, help[i], QColor(150, 158, 166), 9, 600, false);
    QString v = validateRows(m_edRows);
    m_cv.blendRect(590, 478, 200, 40, withAlpha(v.isEmpty() ? 0xFF13341F : 0xFF3A1414, 230));
    text(596, 480, 190, 36, Qt::AlignLeft | Qt::TextWordWrap, v.isEmpty() ? "VALID: one start, every marker reachable" : v,
         v.isEmpty() ? QColor(120, 240, 150) : QColor(255, 130, 110), 9, 800, false);
    if (m_edDirty) text(590, 524, 200, 14, Qt::AlignLeft, "UNSAVED CHANGES", qcE(ED_AMBER), 10, 800, false);
    if (m_edMsgT > 0.f) text(12, 570, 560, 16, Qt::AlignLeft, m_edMsg, QColor(220, 225, 230, int(255 * std::min(1.f, m_edMsgT))), 11, 800, false);
}
