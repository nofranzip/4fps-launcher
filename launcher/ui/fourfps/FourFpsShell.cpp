// 4fps - interfaz principal personalizada (basada en Prism Launcher, GPL-3.0)
#include "FourFpsShell.h"

#include <QApplication>
#include <QProgressDialog>
#include <QButtonGroup>
#include <QCheckBox>
#include <QCursor>
#include <QFile>
#include <QFileDialog>
#include <QMenu>
#include <QStandardPaths>
#include <QComboBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QStackedWidget>
#include <QStyle>
#include <QConicalGradient>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include "Application.h"
#include "FourFpsFeatures.h"
#include "BaseInstance.h"
#include "launch/LaunchTask.h"
#include "InstanceList.h"
#include "icons/IconList.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/PackProfile.h"
#include "minecraft/auth/AccountList.h"
#include "minecraft/auth/MinecraftAccount.h"
#include "ui/dialogs/CustomMessageBox.h"
#include "ui/dialogs/MSALoginDialog.h"

namespace fps {
QPushButton* makeButton(const QString& text, const QString& icon, bool primary, QWidget* parent)
{
    auto* b = new QPushButton((icon.isEmpty() || primary) ? QIcon() : QIcon::fromTheme(icon), text, parent);
    b->setObjectName(primary ? QStringLiteral("fpsPrimary") : QStringLiteral("fpsOutline"));
    b->setCursor(Qt::PointingHandCursor);
    return b;
}
QLabel* makeLabel(const QString& text, const QString& name, QWidget* parent)
{
    auto* l = new QLabel(text, parent);
    l->setObjectName(name);
    return l;
}
QFrame* makeCard(QWidget* parent, const QString& name)
{
    auto* f = new QFrame(parent);
    f->setObjectName(name);
    return f;
}
void repolish(QWidget* w)
{
    w->style()->unpolish(w);
    w->style()->polish(w);
}
}  // namespace fps
using namespace fps;

// ---------------------------------------------------------------- PixelBanner
// Paisajes pixelados originales de 4fps inspirados en cada actualizacion (no usan arte de Mojang).
namespace {
enum Theme {
    ThClassic, ThBeta, ThEnd, ThAquatic, ThVillage, ThBees, ThNether, ThCaves, ThCliffs, ThWild, ThCherry, ThTrial, ThPale, ThSpring, ThSkies, ThModern
};
struct ThemeInfo {
    const char* label;
};
const ThemeInfo kThemes[] = {
    { "Clásico" },         { "Beta" },          { "Combat · The End" }, { "Update Aquatic" }, { "Village & Pillage" },
    { "Buzzy Bees" },      { "Nether Update" }, { "Caves & Cliffs I" }, { "Caves & Cliffs II" }, { "The Wild Update" },
    { "Trails & Tales" },  { "Tricky Trials" }, { "The Garden Awakens" }, { "Spring to Life" }, { "Chase the Skies" },
    { "Minecraft" },
};

int themeFor(const QString& vIn)
{
    const QString v = vIn.trimmed().toLower();
    if (v.isEmpty())
        return -1;
    if (v.startsWith("b1") || v.startsWith("a1") || v.startsWith("inf-") || v.startsWith("in-"))
        return ThBeta;
    if (v.startsWith("rd-") || v.startsWith("c0") || v.startsWith("c1"))
        return ThClassic;
    QRegularExpressionMatch m = QRegularExpression("^(\\d\\d)w(\\d\\d)").match(v);
    if (m.hasMatch()) {  // snapshot: se asocia a la actualizacion de ese año
        const int y = m.captured(1).toInt(), w = m.captured(2).toInt();
        if (y >= 26) return ThModern;
        if (y == 25) return w < 15 ? ThSpring : ThSkies;
        if (y == 24) return w < 33 ? ThTrial : ThPale;
        if (y == 23) return w < 40 ? ThCherry : ThTrial;
        if (y == 22) return w < 40 ? ThWild : ThCherry;
        if (y == 21) return w < 30 ? ThCaves : ThCliffs;
        if (y == 20) return w < 30 ? ThNether : ThCaves;
        if (y == 19) return w < 30 ? ThVillage : ThBees;
        if (y == 18) return w < 40 ? ThAquatic : ThVillage;
        if (y >= 15) return ThEnd;
        return ThClassic;
    }
    m = QRegularExpression("^(\\d+)\\.(\\d+)(?:\\.(\\d+))?").match(v);
    if (!m.hasMatch())
        return ThModern;
    const int major = m.captured(1).toInt(), minor = m.captured(2).toInt(), patch = m.captured(3).toInt();
    if (major >= 26)  // numeracion nueva (26.1, 26.2...)
        return ThModern;
    if (major != 1)
        return ThModern;
    if (minor >= 22) return ThModern;
    if (minor == 21) {
        if (patch >= 6) return ThSkies;
        if (patch == 5) return ThSpring;
        if (patch == 4) return ThPale;
        return ThTrial;
    }
    if (minor == 20) return ThCherry;
    if (minor == 19) return ThWild;
    if (minor == 18) return ThCliffs;
    if (minor == 17) return ThCaves;
    if (minor == 16) return ThNether;
    if (minor == 15) return ThBees;
    if (minor == 14) return ThVillage;
    if (minor == 13) return ThAquatic;
    if (minor >= 9) return ThEnd;
    return ThClassic;
}

QColor mix(const QColor& a, const QColor& b, double t)
{
    return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * t, a.greenF() + (b.greenF() - a.greenF()) * t,
                            a.blueF() + (b.blueF() - a.blueF()) * t);
}

struct Scene {
    QColor skyTop, skyBottom;
    QColor top, top2, fill, fill2, deep;  // terreno
    QColor accent, accent2;              // detalles (arboles, cristales, lava...)
    QColor celestial;
    bool water = false, lavaFalls = false, stars = false, trees = false, islands = false, crystals = false, ghast = false;
    double height = 0.55, rough = 1.0;
};

Scene sceneFor(int t)
{
    Scene s;
    s.skyTop = QColor("#6fa8ff"); s.skyBottom = QColor("#bfe0ff");
    s.top = QColor("#5daa3a"); s.top2 = QColor("#4c9330"); s.fill = QColor("#8a5a35"); s.fill2 = QColor("#76492a"); s.deep = QColor("#7d7d7d");
    s.accent = QColor("#3f7f2a"); s.accent2 = QColor("#6b4a28"); s.celestial = QColor("#fff3a0");
    switch (t) {
        case ThBeta:
            s.skyTop = QColor("#7fa7ff"); s.skyBottom = QColor("#cfe0ff"); s.top = QColor("#79c05a"); s.top2 = QColor("#6aad4b");
            s.trees = true; s.height = 0.58; break;
        case ThClassic:
            s.trees = true; break;
        case ThEnd:
            s.skyTop = QColor("#0c0814"); s.skyBottom = QColor("#2a1d3a"); s.top = QColor("#e6e3a6"); s.top2 = QColor("#d5d18f");
            s.fill = QColor("#cfcb8a"); s.fill2 = QColor("#bdb977"); s.deep = QColor("#a8a464"); s.accent = QColor("#9b6cc9"); s.accent2 = QColor("#c9a8e6");
            s.stars = true; s.islands = true; s.celestial = QColor(0, 0, 0, 0); s.height = 0.62; break;
        case ThAquatic:
            s.skyTop = QColor("#3d8fe0"); s.skyBottom = QColor("#9fd6ff"); s.top = QColor("#e8dca0"); s.top2 = QColor("#d8cb8b");
            s.fill = QColor("#d1c48a"); s.fill2 = QColor("#c0b37a"); s.accent = QColor("#ff6f91"); s.accent2 = QColor("#ffb347");
            s.water = true; s.height = 0.8; s.rough = 0.6; break;
        case ThVillage:
            s.skyTop = QColor("#ff9e5e"); s.skyBottom = QColor("#ffd59e"); s.accent = QColor("#9c6b3c"); s.accent2 = QColor("#b84a3a");
            s.celestial = QColor("#ffe27a"); s.trees = true; break;
        case ThBees:
            s.skyTop = QColor("#7cc4ff"); s.skyBottom = QColor("#d8f0ff"); s.top = QColor("#6cc04a"); s.accent = QColor("#f2c12e"); s.accent2 = QColor("#ffe27a");
            s.trees = true; break;
        case ThNether:
            s.skyTop = QColor("#2b0606"); s.skyBottom = QColor("#6b1a12"); s.top = QColor("#b3262d"); s.top2 = QColor("#1f8a83");
            s.fill = QColor("#6e2323"); s.fill2 = QColor("#5a1b1b"); s.deep = QColor("#3e1212"); s.accent = QColor("#ff8a1f"); s.accent2 = QColor("#ffcf3f");
            s.celestial = QColor(0, 0, 0, 0); s.lavaFalls = true; s.rough = 1.6; s.height = 0.6; break;
        case ThCaves:
            s.skyTop = QColor("#141a1f"); s.skyBottom = QColor("#2c3a33"); s.top = QColor("#5fae3c"); s.top2 = QColor("#f2a7d9");
            s.fill = QColor("#5b5f63"); s.fill2 = QColor("#4b4f53"); s.deep = QColor("#36383b"); s.accent = QColor("#a46be0"); s.accent2 = QColor("#d6b3ff");
            s.celestial = QColor(0, 0, 0, 0); s.crystals = true; s.rough = 1.3; break;
        case ThCliffs:
            s.skyTop = QColor("#6aa9ff"); s.skyBottom = QColor("#d2e8ff"); s.top = QColor("#f4f8ff"); s.top2 = QColor("#dde6f2");
            s.fill = QColor("#8a8f96"); s.fill2 = QColor("#767b82"); s.deep = QColor("#5f6369"); s.rough = 3.2; s.height = 0.42; break;
        case ThWild:
            s.skyTop = QColor("#05090c"); s.skyBottom = QColor("#0f2a2e"); s.top = QColor("#0e3b45"); s.top2 = QColor("#145763");
            s.fill = QColor("#1c1f26"); s.fill2 = QColor("#15171d"); s.deep = QColor("#0e0f13"); s.accent = QColor("#2fd3e0"); s.accent2 = QColor("#7af2ff");
            s.celestial = QColor(0, 0, 0, 0); s.crystals = true; s.stars = true; s.rough = 1.2; break;
        case ThCherry:
            s.skyTop = QColor("#8fc4ff"); s.skyBottom = QColor("#ffe2f0"); s.top = QColor("#7cc457"); s.accent = QColor("#ffb7d9"); s.accent2 = QColor("#ff8fc4");
            s.trees = true; s.rough = 1.4; break;
        case ThTrial:
            s.skyTop = QColor("#2a1a10"); s.skyBottom = QColor("#5e3a20"); s.top = QColor("#c46a3a"); s.top2 = QColor("#4fa38c");
            s.fill = QColor("#a35a33"); s.fill2 = QColor("#8a4b2a"); s.deep = QColor("#5b5b66"); s.accent = QColor("#5fd1b5"); s.accent2 = QColor("#ffb04a");
            s.celestial = QColor(0, 0, 0, 0); s.crystals = true; s.rough = 0.4; break;
        case ThPale:
            s.skyTop = QColor("#8e9496"); s.skyBottom = QColor("#c9cdce"); s.top = QColor("#b9c0b5"); s.top2 = QColor("#a5ad9f");
            s.fill = QColor("#6d6a63"); s.fill2 = QColor("#5e5b55"); s.accent = QColor("#7d877c"); s.accent2 = QColor("#ff8a2a");
            s.celestial = QColor(0, 0, 0, 0); s.trees = true; break;
        case ThSpring:
            s.skyTop = QColor("#7cc8ff"); s.skyBottom = QColor("#e4f7ff"); s.top = QColor("#79d14f"); s.accent = QColor("#ffe066"); s.accent2 = QColor("#ff7aa8");
            s.trees = true; s.rough = 0.8; break;
        case ThSkies:
            s.skyTop = QColor("#4aa3ff"); s.skyBottom = QColor("#d6efff"); s.ghast = true; s.height = 0.72; s.rough = 0.7; s.trees = true; break;
        default:
            s.trees = true; break;
    }
    return s;
}
}  // namespace

PixelBanner::PixelBanner(const QString& seed, QWidget* parent) : QWidget(parent), m_seed(qHash(seed))
{
    setFixedHeight(96);
}

void PixelBanner::setVersion(const QString& mcVersion)
{
    m_theme = themeFor(mcVersion);
    m_label = m_theme >= 0 ? QString::fromUtf8(kThemes[m_theme].label) : QString();
    update();
}

QString PixelBanner::themeName(const QString& mcVersion)
{
    const int t = themeFor(mcVersion);
    return t >= 0 ? QString::fromUtf8(kThemes[t].label) : QString();
}

void PixelBanner::setImagePath(const QString& path)
{
    m_imagePath = path;
    m_image = QPixmap();
    if (!path.isEmpty() && QFile::exists(path))
        m_image.load(path);
    update();
}

void PixelBanner::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    const QRect r = rect();
    if (!m_image.isNull()) {
        // imagen propia: recorte tipo "cover"
        const QSize sz = m_image.size().scaled(r.size(), Qt::KeepAspectRatioByExpanding);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.drawPixmap(QRect(QPoint((r.width() - sz.width()) / 2, (r.height() - sz.height()) / 2), sz), m_image);
        return;
    }
    uint s = m_seed;
    auto rnd = [&s]() {
        s = s * 1103515245u + 12345u;
        return static_cast<int>((s >> 16) & 0x7fff);
    };
    if (m_theme < 0) {
        p.fillRect(r, QColor("#232327"));
        const int cell = 12, cols = width() / cell + 1, rows = height() / cell + 1;
        const QColor tones[] = { QColor("#3a3a40"), QColor("#52525b"), QColor("#71717a"), QColor("#a1a1aa"), QColor("#d4d4d8") };
        const int base = rows / 2 + 1;
        for (int c = 0; c < cols; c++) {
            const int h = base + rnd() % 3 - 1;
            for (int y = h; y < rows; y++)
                p.fillRect(c * cell, y * cell, cell, cell, tones[y == h ? 3 + rnd() % 2 : rnd() % 5]);
        }
        return;
    }
    const Scene sc = sceneFor(m_theme);
    const int cell = 6;
    const int cols = width() / cell + 1, rows = height() / cell + 1;
    // cielo en bandas pixeladas
    for (int y = 0; y < rows; y++)
        p.fillRect(0, y * cell, width(), cell, mix(sc.skyTop, sc.skyBottom, double(y) / rows));
    if (sc.stars)
        for (int i = 0; i < 26; i++)
            p.fillRect((rnd() % cols) * cell, (rnd() % (rows / 2)) * cell, 2, 2, QColor(255, 255, 255, 120 + rnd() % 120));
    if (sc.celestial.alpha() > 0)
        p.fillRect((cols - 8) * cell, 2 * cell, cell * 3, cell * 3, sc.celestial);
    // nubes
    if (sc.celestial.alpha() > 0 || sc.ghast)
        for (int i = 0; i < 3; i++) {
            const int cx = rnd() % cols, cy = 1 + rnd() % 4, w = 4 + rnd() % 5;
            p.fillRect(cx * cell, cy * cell, w * cell, cell, QColor(255, 255, 255, 200));
            p.fillRect((cx + 1) * cell, (cy - 1) * cell, (w - 2) * cell, cell, QColor(255, 255, 255, 170));
        }
    // islas flotantes del End
    if (sc.islands)
        for (int i = 0; i < 2; i++) {
            const int cx = rnd() % cols, cy = 3 + rnd() % 3, w = 4 + rnd() % 4;
            p.fillRect(cx * cell, cy * cell, w * cell, cell, sc.top);
            p.fillRect((cx + 1) * cell, (cy + 1) * cell, (w - 2) * cell, cell, sc.fill2);
            p.fillRect((cx + w / 2) * cell, (cy - 3) * cell, cell, cell * 3, QColor("#1b1124"));  // pilar de obsidiana
        }
    // ghast feliz (globo blanco) para Chase the Skies
    if (sc.ghast) {
        const int gx = cols / 3 + rnd() % (cols / 3), gy = 3;
        p.fillRect(gx * cell, gy * cell, cell * 4, cell * 4, QColor("#f4f4f4"));
        p.fillRect((gx + 1) * cell, (gy + 1) * cell, cell, cell, QColor("#3a3a3a"));
        p.fillRect((gx + 2) * cell, (gy + 1) * cell, cell, cell, QColor("#3a3a3a"));
        for (int k = 0; k < 4; k++)
            p.fillRect((gx + k) * cell + 2, (gy + 4) * cell, 2, cell * (1 + k % 2), QColor("#e2e2e2"));
    }
    // terreno
    QVector<int> hs(cols);
    double h = rows * sc.height;
    for (int c = 0; c < cols; c++) {
        h += ((rnd() % 3) - 1) * sc.rough;
        h = qBound(rows * 0.30, h, rows * 0.85);
        hs[c] = int(h);
    }
    for (int c = 0; c < cols; c++) {
        const int top = hs[c];
        for (int y = top; y < rows; y++) {
            QColor col = y == top ? (rnd() % 4 ? sc.top : sc.top2) : y - top < 3 ? (rnd() % 3 ? sc.fill : sc.fill2) : (rnd() % 3 ? sc.deep : sc.fill2);
            p.fillRect(c * cell, y * cell, cell, cell, col);
        }
    }
    // agua (Aquatic)
    if (sc.water) {
        const int wl = int(rows * 0.5);
        for (int c = 0; c < cols; c++)
            for (int y = wl; y < hs[c]; y++)
                p.fillRect(c * cell, y * cell, cell, cell, y == wl ? QColor(120, 200, 255, 220) : QColor(40, 110, 200, 200));
        for (int i = 0; i < 5; i++) {  // corales
            const int c = rnd() % cols;
            p.fillRect(c * cell, (hs[c] - 2) * cell, cell, cell * 2, i % 2 ? sc.accent : sc.accent2);
        }
    }
    // cascadas de lava (Nether)
    if (sc.lavaFalls) {
        for (int i = 0; i < 2; i++) {
            const int c = rnd() % cols;
            p.fillRect(c * cell, 0, cell, hs[c] * cell, sc.accent);
            p.fillRect(c * cell + 2, 0, 2, hs[c] * cell, sc.accent2);
        }
        for (int c = 0; c < cols; c++)
            if (hs[c] >= int(rows * 0.7))
                p.fillRect(c * cell, hs[c] * cell, cell, cell, sc.accent2);
    }
    // cristales / sculk / cobre
    if (sc.crystals)
        for (int i = 0; i < 7; i++) {
            const int c = rnd() % cols;
            const int y = hs[c] + 2 + rnd() % qMax(1, rows - hs[c] - 2);
            p.fillRect(c * cell, y * cell, cell, cell, i % 2 ? sc.accent : sc.accent2);
        }
    // arboles
    if (sc.trees)
        for (int i = 0; i < 4; i++) {
            const int c = 1 + rnd() % qMax(1, cols - 3);
            const int base = hs[c];
            const int th = 2 + rnd() % 2;
            p.fillRect(c * cell, (base - th) * cell, cell, th * cell, m_theme == ThPale ? QColor("#d9d6cf") : QColor("#6b4a28"));
            p.fillRect((c - 1) * cell, (base - th - 2) * cell, cell * 3, cell * 2, sc.accent);
            p.fillRect(c * cell, (base - th - 3) * cell, cell, cell, sc.accent2.isValid() && m_theme != ThClassic && m_theme != ThBeta ? sc.accent2 : sc.accent);
        }
    // etiqueta de la actualizacion
    if (!m_label.isEmpty()) {
        QFont f = font();
        f.setPixelSize(10);
        f.setBold(true);
        p.setFont(f);
        const int tw = p.fontMetrics().horizontalAdvance(m_label) + 12;
        p.fillRect(8, 8, tw, 18, QColor(0, 0, 0, 150));
        p.setPen(Qt::white);
        p.drawText(QRect(8, 8, tw, 18), Qt::AlignCenter, m_label);
    }
}

// --------------------------------------------------------------- InstanceCard
InstanceCard::InstanceCard(MinecraftInstance* inst, QWidget* parent) : QFrame(parent), m_inst(inst)
{
    setObjectName(QStringLiteral("fpsCard"));
    setCursor(Qt::PointingHandCursor);
    setFixedWidth(204);
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 12);
    v->setSpacing(6);
    auto* banner = new PixelBanner(inst->id(), this);
    m_banner = banner;
    if (auto* pp = inst->getPackProfile())
        banner->setVersion(pp->getComponentVersion("net.minecraft"));
    banner->setImagePath(inst->instanceRoot() + "/4fps-banner.png");
    v->addWidget(banner);

    auto* body = new QVBoxLayout();
    body->setContentsMargins(12, 4, 12, 0);
    body->setSpacing(4);
    auto* titleRow = new QHBoxLayout();
    auto* name = makeLabel(inst->name(), QStringLiteral("fpsCardTitle"), this);
    name->setMaximumWidth(78);
    name->setText(name->fontMetrics().elidedText(inst->name(), Qt::ElideRight, 78));
    titleRow->addWidget(name);
    titleRow->addStretch();
    m_badge = makeLabel(QObject::tr("Seleccionada"), QStringLiteral("fpsBadge"), this);
    m_badge->setVisible(false);
    titleRow->addWidget(m_badge);
    body->addLayout(titleRow);

    QString loader;
    if (auto* pp = inst->getPackProfile()) {
        const QString mc = pp->getComponentVersion("net.minecraft");
        if (!pp->getComponentVersion("net.fabricmc.fabric-loader").isEmpty())
            loader = "Fabric";
        else if (!pp->getComponentVersion("net.neoforged").isEmpty())
            loader = "NeoForge";
        else if (!pp->getComponentVersion("net.minecraftforge").isEmpty())
            loader = "Forge";
        else if (!pp->getComponentVersion("org.quiltmc.quilt-loader").isEmpty())
            loader = "Quilt";
        else
            loader = "Vanilla";
        body->addWidget(makeLabel(QStringLiteral("%1 · %2").arg(loader, mc), QStringLiteral("fpsMuted"), this));
    }

    auto* btnRow = new QHBoxLayout();
    btnRow->setSpacing(8);
    auto* play = makeButton(QObject::tr("Jugar"), QString(), true, this);
    play->setText(QStringLiteral("▶  ") + QObject::tr("Jugar"));
    play->setObjectName(QStringLiteral("fpsCardPlay"));
    auto* edit = makeButton(QString(), QStringLiteral("rename"), false, this);
    edit->setObjectName(QStringLiteral("fpsIconButton"));
    edit->setFixedSize(32, 32);
    auto* img = makeButton(QString(), QStringLiteral("screenshots"), false, this);
    img->setObjectName(QStringLiteral("fpsIconButton"));
    img->setFixedSize(32, 32);
    img->setToolTip(QObject::tr("Cambiar imagen de la instancia"));
    connect(img, &QPushButton::clicked, this, [this] { chooseImage(); });
    btnRow->addWidget(play, 1);
    btnRow->addWidget(img);
    btnRow->addWidget(edit);
    body->addSpacing(4);
    body->addLayout(btnRow);
    v->addLayout(body);

    connect(play, &QPushButton::clicked, this, [this] { emit this->play(m_inst); });
    connect(edit, &QPushButton::clicked, this, [this] { emit this->edit(m_inst); });
}

void InstanceCard::chooseImage()
{
    const QString target = m_inst->instanceRoot() + "/4fps-banner.png";
    QMenu menu(this);
    auto* pick = menu.addAction(QObject::tr("Elegir imagen de mi PC..."));
    QAction* remove = nullptr;
    if (QFile::exists(target))
        remove = menu.addAction(QObject::tr("Quitar imagen (usar la de la versión)"));
    auto* chosen = menu.exec(QCursor::pos());
    if (!chosen)
        return;
    if (chosen == remove) {
        QFile::remove(target);
        m_banner->setImagePath(QString());
        return;
    }
    if (chosen != pick)
        return;
    const QString file = QFileDialog::getOpenFileName(this, QObject::tr("Imagen de la instancia"),
                                                      QStandardPaths::writableLocation(QStandardPaths::PicturesLocation),
                                                      QObject::tr("Imágenes (*.png *.jpg *.jpeg *.webp *.bmp *.gif)"));
    if (file.isEmpty())
        return;
    QImage img(file);
    if (img.isNull()) {
        QMessageBox::warning(this, QObject::tr("Imagen"), QObject::tr("No se pudo abrir esa imagen."));
        return;
    }
    // se guarda una copia liviana dentro de la instancia
    img = img.scaled(QSize(816, 384), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    img.save(target, "PNG");
    m_banner->setImagePath(target);
}

void InstanceCard::setSelected(bool s)
{
    setProperty("selected", s);
    m_badge->setVisible(s);
    repolish(this);
}

void InstanceCard::mousePressEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton)
        emit clicked(m_inst);
    QFrame::mousePressEvent(e);
}

// --------------------------------------------------------------- FourFpsShell
namespace {
// tooltip con título en negrita y una explicación corta
QString tip(const QString& title, const QString& text)
{
    return QStringLiteral("<div style='font-size:10pt'><b>%1</b><br>%2</div>").arg(title.toHtmlEscaped(), text.toHtmlEscaped().replace('\n', QStringLiteral("<br>")));
}
}  // namespace

FourFpsShell::FourFpsShell(FourFpsHooks hooks, QWidget* parent) : QWidget(parent), m_hooks(std::move(hooks))
{
    setObjectName(QStringLiteral("fpsRoot"));
    fps::registerSettings();
    seedStarterPack();
    setAttribute(Qt::WA_StyledBackground, true);
    auto* h = new QHBoxLayout(this);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(0);
    h->addWidget(buildSidebar());

    auto* right = new QWidget(this);
    auto* rv = new QVBoxLayout(right);
    rv->setContentsMargins(0, 0, 0, 0);
    rv->setSpacing(0);
    m_stack = new QStackedWidget(right);
    m_stack->setObjectName(QStringLiteral("fpsStack"));
    m_stack->addWidget(buildAccountPage());    // 0
    m_stack->addWidget(buildInstancesPage());  // 1
    m_stack->addWidget(buildVersionsPage());   // 2
    m_stack->addWidget(buildModsPage());       // 3
    m_stack->addWidget(buildFilesPage());      // 4
    m_stack->addWidget(buildServersPage());    // 5
    m_stack->addWidget(buildSkinsPage());      // 6
    m_stack->addWidget(buildRenderPage());     // 7
    m_stack->addWidget(buildProfilePage());    // 8
    m_galleryPage = fps::buildGalleryPage(this);
    m_stack->addWidget(m_galleryPage);         // 9
    rv->addWidget(fps::buildUpdateBanner(right));
    rv->addWidget(fps::buildBackgroundHost(right, m_stack), 1);
    rv->addWidget(buildBottomBar());
    h->addWidget(right, 1);

    auto* instances = APPLICATION->instances();
    connect(instances, &QAbstractItemModel::rowsInserted, this, &FourFpsShell::scheduleRebuild);
    connect(instances, &QAbstractItemModel::rowsRemoved, this, &FourFpsShell::scheduleRebuild);
    connect(instances, &QAbstractItemModel::modelReset, this, &FourFpsShell::scheduleRebuild);
    connect(instances, &QAbstractItemModel::dataChanged, this, &FourFpsShell::scheduleRebuild);

    auto* accounts = APPLICATION->accounts();
    connect(accounts, &AccountList::listChanged, this, &FourFpsShell::rebuildAccounts);
    connect(accounts, &AccountList::defaultAccountChanged, this, &FourFpsShell::rebuildAccounts);

    rebuildInstances();
    rebuildAccounts();
    showPage(1);
    // ajustes 4fps: modo stream, galería, letras
    connect(fps::Bus::get(), &fps::Bus::settingsChanged, this, &FourFpsShell::applyFeatureSettings);
    applyFeatureSettings();
    fps::DiscordPresence::get()->setIdle();
    QTimer::singleShot(0, this, [] { fps::applyAccent(); });
    // prueba interna: FOURFPS_TEST=<id de instancia> muestra el detector y el explicador al abrir
    const QString testId = qEnvironmentVariable("FOURFPS_TEST");
    if (!testId.isEmpty())
        QTimer::singleShot(2500, this, [this, testId] {
            if (auto inst = APPLICATION->instances()->getInstanceById(testId)) {
                auto* mi = inst;
                fps::confirmLaunch(this, mi);
                const auto ex = fps::explainCrash(mi, QDateTime::currentDateTime().addDays(-1));
                if (ex.found)
                    fps::showCrashDialog(this, mi, ex);
            }
        });
    // medalla de bienvenida y medallas por servers que ya tenías guardados
    QTimer::singleShot(1500, this, [this] {
        grantBadge(QStringLiteral("bienvenida"), tr("¡Bienvenido a 4fps! Ganaste tu primera medalla"));
        scanServerBadges();
        updateBell();
        checkRewards(false);
        syncRewards();  // baja la lista de premios (premios.json) del GitHub
    });
}

void FourFpsShell::scheduleRebuild()
{
    if (m_rebuildQueued)
        return;
    m_rebuildQueued = true;
    QTimer::singleShot(0, this, [this] {
        m_rebuildQueued = false;
        rebuildInstances();
    });
}

QWidget* FourFpsShell::buildSidebar()
{
    auto* side = new QFrame(this);
    side->setObjectName(QStringLiteral("fpsSide"));
    side->setFixedWidth(210);
    auto* v = new QVBoxLayout(side);
    v->setContentsMargins(14, 18, 14, 18);
    v->setSpacing(4);

    auto* logoRow = new QHBoxLayout();
    logoRow->setContentsMargins(0, 0, 0, 22);
    auto* logo = new QLabel(side);
    logo->setPixmap(QIcon(QStringLiteral(":/org.prismlauncher.PrismLauncher.logo.svg")).pixmap(QSize(106, 40)));
    logo->setContentsMargins(4, 0, 0, 0);
    logoRow->addWidget(logo);
    logoRow->addStretch();
    m_bell = new QToolButton(side);
    m_bell->setObjectName(QStringLiteral("fpsBell"));
    m_bell->setAutoRaise(true);
    m_bell->setIconSize(QSize(24, 24));
    m_bell->setFixedSize(30, 36);
    m_bell->setCursor(Qt::PointingHandCursor);
    m_bell->setStyleSheet(QStringLiteral("QToolButton#fpsBell{border:none;border-radius:8px;background:transparent;padding:0px;}"
                                         "QToolButton#fpsBell:hover{background:#26262b;}"));
    connect(m_bell, &QToolButton::clicked, this, &FourFpsShell::showNotifications);
    // botón de color: un círculo cromático chico
    auto* colorBtn = new QToolButton(side);
    colorBtn->setObjectName(QStringLiteral("fpsBell"));
    colorBtn->setAutoRaise(true);
    colorBtn->setFixedSize(30, 36);
    colorBtn->setIconSize(QSize(18, 18));
    colorBtn->setCursor(Qt::PointingHandCursor);
    {
        QPixmap pm(40, 40);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        QConicalGradient g(20, 20, 0);
        for (int i = 0; i <= 6; i++)
            g.setColorAt(i / 6.0, QColor::fromHsv((i * 60) % 360, 230, 255));
        p.setBrush(g);
        p.setPen(Qt::NoPen);
        p.drawEllipse(QRectF(3, 3, 34, 34));
        p.setBrush(QColor("#141416"));
        p.drawEllipse(QRectF(13, 13, 14, 14));
        colorBtn->setIcon(QIcon(pm));
    }
    colorBtn->setStyleSheet(m_bell->styleSheet());
    colorBtn->setToolTip(tip(tr("Color y fondo"), tr("Elige el color de los botones con un círculo cromático (rosa, azul, verde...) y pon una imagen de fondo en la parte central.")));
    connect(colorBtn, &QToolButton::clicked, this, [this] { fps::openAccentPicker(this); });
    logoRow->setSpacing(0);
    logoRow->addWidget(colorBtn, 0, Qt::AlignVCenter);
    logoRow->addWidget(m_bell, 0, Qt::AlignVCenter);
    v->addLayout(logoRow);

    m_nav = new QButtonGroup(this);
    m_nav->setExclusive(true);
    auto add = [this, side, v](const QString& icon, const QString& text, std::function<void()> fn) {
        auto* b = new QPushButton(QIcon::fromTheme(icon), QStringLiteral("   ") + text, side);
        b->setObjectName(QStringLiteral("navButton"));
        b->setIconSize(QSize(17, 17));
        b->setCheckable(true);
        b->setCursor(Qt::PointingHandCursor);
        m_nav->addButton(b);
        m_navButtons << b;
        connect(b, &QPushButton::clicked, this, fn);
        v->addWidget(b);
        return b;
    };
    // las secciones que todavia abren la ventana de Prism no quedan marcadas
    {
        auto* nb = add("accounts", tr("Cuenta"), [this] { showPage(0); });
        nb->setToolTip(tip(tr("Cuenta"), tr("Inicia sesión con Microsoft o agrega cuentas sin premium.")));
    }
    {
        auto* nb = add("instance-settings", tr("Instancias"), [this] { showPage(1); });
        nb->setToolTip(tip(tr("Instancias"), tr("Tus versiones de Minecraft instaladas, el estado de tus servidores y el botón Jugar.")));
    }
    {
        auto* nb = add("new", tr("Versiones"), [this] { showPage(2); });
        nb->setToolTip(tip(tr("Versiones"), tr("Crea una instancia nueva eligiendo la versión y el loader (Fabric, Forge...).")));
    }
    {
        auto* nb = add("loadermods", tr("Mods"), [this] { showPage(3); });
        nb->setToolTip(tip(tr("Mods"), tr("Mods de la instancia elegida: instala, activa, desactiva y agrúpalos.")));
    }
    {
        auto* nb = add("screenshots", tr("Archivos"), [this] { showPage(4); });
        nb->setToolTip(tip(tr("Archivos"), tr("Resource packs, shaders, mundos y otros archivos de la instancia.")));
    }
    {
        auto* nb = add("server", tr("Servers"), [this] { showPage(5); });
        nb->setToolTip(tip(tr("Servers"), tr("Tus servidores guardados: logo, jugadores en línea y Unirse directo.")));
    }
    {
        auto* nb = add("appearance", tr("Skins"), [this] { showPage(6); });
        nb->setToolTip(tip(tr("Skins"), tr("Ropa y accesorios sobre tu skin, capas y tu skin de Minecraft.")));
    }
    {
        auto* nb = add("star", tr("Perfil"), [this] { showPage(8); });
        nb->setToolTip(tip(tr("Perfil"), tr("Tu tarjeta de jugador, medallas, próximo evento y canje de códigos.")));
    }
    m_galleryNav = add("screenshots", tr("Galería"), [this] { showPage(9); });
    m_galleryNav->setToolTip(tip(tr("Galería"), tr("Tus capturas (F2) y los clips que exportas con Flashback.")));
    v->addStretch(1);
    auto* settings = new QPushButton(QIcon::fromTheme("settings"), QStringLiteral("   ") + tr("Ajustes"), side);
    settings->setObjectName(QStringLiteral("navButton"));
    settings->setIconSize(QSize(17, 17));
    settings->setCursor(Qt::PointingHandCursor);
    settings->setToolTip(tip(tr("Ajustes"), tr("Ajustes del launcher. En \"Funciones 4fps\" prendes o apagas cada función nueva.")));
    connect(settings, &QPushButton::clicked, this, [this] { m_hooks.settings(); });
    v->addWidget(settings);
    return side;
}

void FourFpsShell::showPage(int i)
{
    m_stack->setCurrentIndex(i);
    const int nav = i == 7 ? 6 : i == 8 ? 7 : i == 9 ? 8 : i;
    if (nav >= 0 && nav < m_navButtons.size())
        m_navButtons[nav]->setChecked(true);
    onPageShown(i);
}

QWidget* FourFpsShell::header(const QString& title, const QString& subtitle, const QList<QWidget*>& right)
{
    auto* w = new QWidget(this);
    auto* h = new QHBoxLayout(w);
    h->setContentsMargins(0, 0, 0, 0);
    auto* v = new QVBoxLayout();
    v->setSpacing(2);
    v->addWidget(makeLabel(title, QStringLiteral("fpsTitle"), w));
    v->addWidget(makeLabel(subtitle, QStringLiteral("fpsSubtitle"), w));
    h->addLayout(v);
    h->addStretch();
    for (auto* r : right) {
        r->setParent(w);
        h->addWidget(r, 0, Qt::AlignTop);
    }
    return w;
}

// ---------------------------------------------------------------- Instancias
QWidget* FourFpsShell::buildInstancesPage()
{
    auto* page = new QWidget(this);
    page->setObjectName(QStringLiteral("fpsPage"));
    auto* v = new QVBoxLayout(page);
    v->setContentsMargins(32, 28, 32, 24);
    v->setSpacing(18);

    auto* imp = makeButton(tr("Importar modpack"), "export", false, page);
    auto* nueva = makeButton(tr("Nueva instancia"), "new", true, page);
    connect(imp, &QPushButton::clicked, this, [this] { m_hooks.importPack(); });
    connect(nueva, &QPushButton::clicked, this, [this] { m_hooks.newInstance(); });
    v->addWidget(header(tr("Instancias"), tr("Ten varias versiones instaladas y cambia entre ellas con un clic."), { imp, nueva }));
    v->addWidget(fps::buildLiveServersStrip(page));

    auto* filterRow = new QHBoxLayout();
    filterRow->setSpacing(8);
    m_search = new QLineEdit(page);
    m_search->setObjectName(QStringLiteral("fpsSearch"));
    m_search->setPlaceholderText(tr("Buscar instancia..."));
    m_search->addAction(QIcon::fromTheme("viewfolder"), QLineEdit::LeadingPosition)->setVisible(false);
    connect(m_search, &QLineEdit::textChanged, this, &FourFpsShell::rebuildInstances);
    filterRow->addWidget(m_search, 1);
    auto* chips = new QButtonGroup(this);
    for (const QString& f : { QStringLiteral("Todas"), QStringLiteral("Vanilla"), QStringLiteral("Fabric"), QStringLiteral("Forge") }) {
        auto* c = new QPushButton(f, page);
        c->setObjectName(QStringLiteral("fpsChip"));
        c->setCheckable(true);
        c->setChecked(f == m_filter);
        c->setCursor(Qt::PointingHandCursor);
        chips->addButton(c);
        connect(c, &QPushButton::clicked, this, [this, f] {
            m_filter = f;
            rebuildInstances();
        });
        filterRow->addWidget(c);
    }
    v->addLayout(filterRow);

    auto* body = new QHBoxLayout();
    body->setSpacing(20);
    auto* scroll = new QScrollArea(page);
    scroll->setObjectName(QStringLiteral("fpsScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* gridHost = new QWidget(scroll);
    gridHost->setObjectName(QStringLiteral("fpsGridHost"));
    m_grid = new QGridLayout(gridHost);
    m_grid->setContentsMargins(0, 0, 0, 0);
    m_grid->setSpacing(14);
    m_grid->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    scroll->setWidget(gridHost);
    body->addWidget(scroll, 1);

    // panel de detalle
    auto* detail = makeCard(page);
    detail->setFixedWidth(256);
    auto* dv = new QVBoxLayout(detail);
    dv->setContentsMargins(18, 18, 18, 18);
    dv->setSpacing(8);
    m_detailTitle = makeLabel(tr("Elige una instancia"), QStringLiteral("fpsDetailTitle"), detail);
    m_detailSub = makeLabel(QString(), QStringLiteral("fpsMuted"), detail);
    m_detailSub->setWordWrap(true);
    dv->addWidget(m_detailTitle);
    dv->addWidget(m_detailSub);
    m_detailBody = new QWidget(detail);
    auto* bv = new QVBoxLayout(m_detailBody);
    bv->setContentsMargins(0, 8, 0, 0);
    bv->setSpacing(8);
    auto* stats = new QHBoxLayout();
    stats->setSpacing(8);
    auto statBox = [this](const QString& cap, QLabel*& value) {
        auto* f = makeCard(m_detailBody, QStringLiteral("fpsStat"));
        auto* l = new QVBoxLayout(f);
        l->setContentsMargins(10, 6, 10, 6);
        l->setSpacing(0);
        l->addWidget(makeLabel(cap, QStringLiteral("fpsCaption"), f));
        value = makeLabel("-", QStringLiteral("fpsStatValue"), f);
        l->addWidget(value);
        return f;
    };
    stats->addWidget(statBox(tr("RAM"), m_detailRam));
    stats->addWidget(statBox(tr("JAVA"), m_detailJava));
    bv->addLayout(stats);
    auto row = [this, bv](const QString& text, const QString& icon, std::function<void()> fn, const QString& help = QString()) {
        auto* b = makeButton(text, icon, false, m_detailBody);
        b->setObjectName(QStringLiteral("fpsRowButton"));
        if (!help.isEmpty())
            b->setToolTip(tip(text, help));
        connect(b, &QPushButton::clicked, this, fn);
        bv->addWidget(b);
    };
    m_autoRamToggle = new QCheckBox(tr("RAM automática"), m_detailBody);
    m_autoRamToggle->setObjectName(QStringLiteral("fpsToggle"));
    m_autoRamToggle->setToolTip(tip(tr("RAM automática"),
                                    tr("4fps elige cuánta RAM darle al juego según la RAM de tu PC y cuántos mods tiene la instancia (siempre deja RAM libre para Windows). "
                                       "Si lo apagas, la RAM la eliges tú en \"Configuración de la instancia\" → Java.")));
    connect(m_autoRamToggle, &QCheckBox::toggled, this, [this](bool on) {
        if (auto* inst = m_hooks.selected()) {
            fps::setAutoRamEnabled(inst, on);
            if (!on)
                QMessageBox::information(this, tr("RAM automática"),
                                         tr("RAM automática apagada. Ahora la RAM la eliges tú: abre \"Configuración de la instancia\" → Java → Memoria. "
                                            "Se queda con %1 MB hasta que la cambies.").arg(inst->settings()->get("MaxMemAlloc").toInt()));
            updateDetail();
        }
    });
    bv->addWidget(m_autoRamToggle);
    m_autoRamHint = makeLabel(QString(), QStringLiteral("fpsHint"), m_detailBody);
    m_autoRamHint->setWordWrap(true);
    bv->addWidget(m_autoRamHint);
    m_pvpToggle = new QCheckBox(tr("Cliente PvP 4fps"), m_detailBody);
    m_pvpToggle->setObjectName(QStringLiteral("fpsToggle"));
    m_pvpToggle->setToolTip(tip(tr("Cliente PvP 4fps"), tr("Al jugar instala OneConfig de Polyfrost: menú de todos tus mods (Shift derecho), editor de HUD, hitboxes y crosshair. Solo en instancias Fabric.")));
    connect(m_pvpToggle, &QCheckBox::toggled, this, [this](bool on) {
        if (auto* inst = m_hooks.selected())
            setPvpEnabled(inst, on);
    });
    bv->addWidget(m_pvpToggle);
    m_sharedCapesToggle = new QCheckBox(tr("Ver capas de otros jugadores"), m_detailBody);
    m_sharedCapesToggle->setObjectName(QStringLiteral("fpsToggle"));
    m_sharedCapesToggle->setToolTip(tip(tr("Ver capas de otros jugadores"),
                                        tr("Al jugar instala el mod MinecraftCapes para ver las capas de otros jugadores que también usen 4fps (y que las vean de ti). "
                                           "Aparece en la lista de Mods. Funciona en instancias Fabric, Forge y NeoForge.")));
    connect(m_sharedCapesToggle, &QCheckBox::toggled, this, [this](bool on) {
        if (auto* inst = m_hooks.selected())
            setSharedCapesEnabled(inst, on);
    });
    bv->addWidget(m_sharedCapesToggle);
    m_pvpHint = makeLabel(QString(), QStringLiteral("fpsHint"), m_detailBody);
    m_pvpHint->setWordWrap(true);
    bv->addWidget(m_pvpHint);
    m_slowButton = makeButton(tr("Modo potato"), QString(), false, m_detailBody);
    m_slowButton->setObjectName(QStringLiteral("fpsRowButton"));
    connect(m_slowButton, &QPushButton::clicked, this, [this] {
        auto* inst = m_hooks.selected();
        if (!inst)
            return;
        if (inst->isRunning()) {
            QMessageBox::information(this, tr("Modo potato"), tr("Cierra el juego antes de cambiar el modo."));
            return;
        }
        const bool on = fps::slowModeActive(inst);
        if (on)
            removePotatoMods(inst);
        const QString msg = on ? fps::disableSlowMode(inst) : fps::enableSlowMode(inst);
        QMessageBox::information(this, on ? tr("Modo normal") : tr("Modo potato"),
                                 (on ? tr("Volví la instancia a como estaba:\n\n") : tr("Listo, la instancia quedó más liviana:\n\n")) + msg);
        if (!on && pvpSupported(inst))
            installPotatoMods(inst);
        updateDetail();
    });
    bv->addWidget(m_slowButton);
    m_slowHint = makeLabel(QString(), QStringLiteral("fpsHint"), m_detailBody);
    m_slowHint->setWordWrap(true);
    bv->addWidget(m_slowHint);
    row(tr("Mods de la instancia"), "loadermods", [this] { m_hooks.openPage("mods"); }, tr("Abre la lista completa de mods de esta instancia (también para buscar actualizaciones)."));
    row(tr("Grupos de mods"), "tag", [this] { fps::editModGroups(this, m_hooks.selected()); }, tr("Junta mods que deben actualizarse a la vez (ej: Sodium + Iris + Voxy) o bloquéalos para que no se actualicen."));
    row(tr("Abrir carpeta"), "viewfolder", [this] { m_hooks.openFolder(); }, tr("Abre la carpeta de la instancia en el explorador de Windows."));
    row(tr("Duplicar"), "copy", [this] { m_hooks.copyInstance(); }, tr("Crea una copia exacta de la instancia, con sus mods y mundos."));
    row(tr("Eliminar"), "delete", [this] { m_hooks.deleteInstance(); }, tr("Borra la instancia y todos sus archivos. Te pide confirmación."));
    bv->addStretch(1);
    auto* jugar = makeButton(QStringLiteral("▶  ") + tr("Jugar"), QString(), true, m_detailBody);
    jugar->setMinimumHeight(42);
    connect(jugar, &QPushButton::clicked, this, &FourFpsShell::launchSelected);
    bv->addWidget(jugar);
    dv->addWidget(m_detailBody, 1);
    body->addWidget(detail);
    v->addLayout(body, 1);
    return page;
}

QString FourFpsShell::loaderOf(MinecraftInstance* inst)
{
    auto* pp = inst ? inst->getPackProfile() : nullptr;
    if (!pp)
        return {};
    if (!pp->getComponentVersion("net.fabricmc.fabric-loader").isEmpty())
        return "Fabric";
    if (!pp->getComponentVersion("net.neoforged").isEmpty())
        return "NeoForge";
    if (!pp->getComponentVersion("net.minecraftforge").isEmpty())
        return "Forge";
    if (!pp->getComponentVersion("org.quiltmc.quilt-loader").isEmpty())
        return "Quilt";
    return "Vanilla";
}

QString FourFpsShell::mcVersionOf(MinecraftInstance* inst)
{
    auto* pp = inst ? inst->getPackProfile() : nullptr;
    return pp ? pp->getComponentVersion("net.minecraft") : QString();
}

void FourFpsShell::rebuildInstances()
{
    if (!m_grid)
        return;
    while (auto* item = m_grid->takeAt(0)) {
        if (auto* w = item->widget())
            w->deleteLater();
        delete item;
    }
    m_cards.clear();

    auto* list = APPLICATION->instances();
    const QString q = m_search ? m_search->text().trimmed() : QString();
    const int cols = 3;
    int n = 0;
    for (int i = 0; i < list->count(); i++) {
        auto* inst = list->at(i);
        if (!q.isEmpty() && !inst->name().contains(q, Qt::CaseInsensitive))
            continue;
        if (m_filter == "Fabric" && loaderOf(inst) != "Fabric")
            continue;
        if (m_filter == "Forge" && loaderOf(inst) != "Forge" && loaderOf(inst) != "NeoForge")
            continue;
        if (m_filter == "Vanilla" && loaderOf(inst) != "Vanilla")
            continue;
        auto* card = new InstanceCard(inst, m_grid->parentWidget());
        connect(card, &InstanceCard::clicked, this, [this](MinecraftInstance* i) { m_hooks.select(i); });
        connect(card, &InstanceCard::play, this, [this](MinecraftInstance* i) {
            m_hooks.select(i);
            launchSelected();
        });
        connect(card, &InstanceCard::edit, this, [this](MinecraftInstance* i) {
            m_hooks.select(i);
            m_hooks.openPage(QString());
        });
        m_grid->addWidget(card, n / cols, n % cols);
        m_cards << card;
        n++;
    }
    auto* add = new QPushButton(QStringLiteral("+\n\n") + tr("Nueva instancia"), m_grid->parentWidget());
    add->setObjectName(QStringLiteral("fpsAddCard"));
    add->setFixedSize(204, 200);
    add->setCursor(Qt::PointingHandCursor);
    connect(add, &QPushButton::clicked, this, [this] { m_hooks.newInstance(); });
    m_grid->addWidget(add, n / cols, n % cols);

    if (m_combo) {
        m_combo->blockSignals(true);
        m_combo->clear();
        for (int i = 0; i < list->count(); i++) {
            auto* inst = list->at(i);
            m_combo->addItem(QIcon::fromTheme("resourcepacks"), inst->name(), inst->id());
        }
        m_combo->blockSignals(false);
    }
    selectionChanged();
}

void FourFpsShell::selectionChanged()
{
    auto* sel = m_hooks.selected ? m_hooks.selected() : nullptr;
    for (auto* c : m_cards)
        c->setSelected(c->instance() == sel);
    updateDetail();
    updateBottom();
    if (m_stack)
        onPageShown(m_stack->currentIndex());
}

void FourFpsShell::updateDetail()
{
    auto* sel = m_hooks.selected ? m_hooks.selected() : nullptr;
    if (!m_detailTitle)
        return;
    if (!sel) {
        m_detailTitle->setText(tr("Elige una instancia"));
        m_detailSub->setText(tr("Haz clic en una tarjeta para ver sus datos."));
        m_detailBody->setVisible(false);
        return;
    }
    m_detailBody->setVisible(true);
    m_detailTitle->setText(m_detailTitle->fontMetrics().elidedText(sel->name(), Qt::ElideRight, 210));
    auto* pp = sel->getPackProfile();
    QString sub = QStringLiteral("Minecraft %1").arg(mcVersionOf(sel));
    const QString loader = loaderOf(sel);
    if (pp && loader == "Fabric")
        sub += QStringLiteral(" · Fabric %1").arg(pp->getComponentVersion("net.fabricmc.fabric-loader"));
    else if (pp && loader == "Forge")
        sub += QStringLiteral(" · Forge %1").arg(pp->getComponentVersion("net.minecraftforge"));
    else if (pp && loader == "NeoForge")
        sub += QStringLiteral(" · NeoForge %1").arg(pp->getComponentVersion("net.neoforged"));
    else if (pp && loader == "Quilt")
        sub += QStringLiteral(" · Quilt %1").arg(pp->getComponentVersion("org.quiltmc.quilt-loader"));
    m_detailSub->setText(sub);
    {
        const QString why = fps::applyAutoRam(sel);
        const bool autoOn = fps::autoRamEnabled(sel);
        m_autoRamToggle->blockSignals(true);
        m_autoRamToggle->setChecked(autoOn);
        m_autoRamToggle->blockSignals(false);
        m_autoRamHint->setText(autoOn ? why : tr("RAM manual: cámbiala en Configuración de la instancia → Java."));
    }
    const int mb = sel->settings()->get("MaxMemAlloc").toInt();
    m_detailRam->setText(mb >= 1024 ? QStringLiteral("%1 GB").arg(QString::number(mb / 1024.0, 'g', 2)) : QStringLiteral("%1 MB").arg(mb));
    QString java = sel->settings()->get("JavaVersion").toString();
    java = java.section('.', 0, 0) == "1" ? java.section('.', 1, 1) : java.section('.', 0, 0);
    m_detailJava->setText(java.isEmpty() ? tr("Auto") : java);
    const bool supported = pvpSupported(sel);
    m_pvpToggle->blockSignals(true);
    m_pvpToggle->setChecked(supported && pvpEnabled(sel));
    m_pvpToggle->setEnabled(supported);
    m_pvpToggle->blockSignals(false);
    if (m_sharedCapesToggle) {
        const bool capesSupported = sharedCapesSupported(sel);
        m_sharedCapesToggle->blockSignals(true);
        m_sharedCapesToggle->setChecked(capesSupported && sharedCapesEnabled(sel));
        m_sharedCapesToggle->setEnabled(capesSupported);
        m_sharedCapesToggle->blockSignals(false);
    }
    if (m_slowButton) {
        const bool slow = fps::slowModeActive(sel);
        m_slowButton->setVisible(fps::enabled(fps::cfg::SlowPc));
        m_slowHint->setVisible(fps::enabled(fps::cfg::SlowPc));
        m_slowButton->setText(slow ? tr("Volver a modo normal") : tr("Modo potato (rendimiento)"));
        const auto hw = fps::detectHardware();
        QString hwText = QStringLiteral("%1 · %2 GB RAM").arg(hw.cpu.isEmpty() ? tr("CPU") : hw.cpu).arg(QString::number(hw.ramMb / 1024.0, 'f', 0));
        if (!hw.gpu.isEmpty())
            hwText += QStringLiteral(" · ") + hw.gpu;
        m_slowHint->setText(slow ? tr("Modo potato activo: mods de rendimiento, sin mods pesados y video bajo.")
                                 : (hw.weak() ? tr("Recomendado para tu PC (%1 GB RAM).").arg(QString::number(hw.ramMb / 1024.0, 'f', 0)) : QString()));
        m_slowHint->setVisible(fps::enabled(fps::cfg::SlowPc) && !m_slowHint->text().isEmpty());
        m_slowButton->setToolTip(tip(slow ? tr("Modo normal") : tr("Modo potato"),
                                     (slow ? tr("Deshace el Modo potato: vuelve la RAM y Java, reactiva tus mods, quita los mods de rendimiento que puso 4fps y restaura el video.")
                                           : tr("Para PCs de bajos recursos: ajusta la RAM y Java, desactiva mods pesados (Voxy, Distant Horizons, Iris/shaders, EMF, Lyrica), instala mods de rendimiento (Sodium, Lithium, FerriteCore, EntityCulling, ImmediatelyFast, ModernFix) y baja el video. Se deshace con un clic.")) +
                                         QStringLiteral("\n\n") + tr("Tu PC: %1").arg(hwText)));
    }
    m_pvpHint->setText(supported ? tr("OneConfig de Polyfrost: menú de mods, editor de HUD, hitboxes y crosshair. Se instala solo al jugar.")
                                 : tr("Disponible en instancias Fabric o Quilt."));
}

// --------------------------------------------------------------- barra inferior
QWidget* FourFpsShell::buildBottomBar()
{
    auto* bar = new QFrame(this);
    bar->setObjectName(QStringLiteral("fpsBottom"));
    bar->setFixedHeight(76);
    auto* h = new QHBoxLayout(bar);
    h->setContentsMargins(32, 0, 32, 0);
    h->setSpacing(12);
    m_bottomFace = new QLabel(bar);
    m_bottomFace->setFixedSize(36, 36);
    h->addWidget(m_bottomFace);
    m_bottomName = new QLabel(bar);
    m_bottomName->setObjectName(QStringLiteral("fpsBottomName"));
    h->addWidget(m_bottomName);
    m_music = new QLabel(bar);
    m_music->setObjectName(QStringLiteral("fpsNowPlaying"));
    m_music->setMaximumWidth(340);
    m_music->setTextFormat(Qt::RichText);
    m_music->hide();
    h->addSpacing(16);
    h->addWidget(m_music);
    for (const auto& c : { qMakePair(QStringLiteral("◀◀"), QStringLiteral("previous")), qMakePair(QStringLiteral("▶‖"), QStringLiteral("playpause")),
                           qMakePair(QStringLiteral("▶▶"), QStringLiteral("next")) }) {
        auto* b = new QPushButton(c.first, bar);
        b->setObjectName(QStringLiteral("fpsIconButtonFlat"));
        b->setFixedSize(30, 30);
        b->setCursor(Qt::PointingHandCursor);
        b->hide();
        const QString action = c.second;
        connect(b, &QPushButton::clicked, this, [action] { fps::LyricsService::get()->control(action); });
        h->addWidget(b);
        m_musicControls << b;
    }
    h->addStretch(1);
    m_musicToggle = new QPushButton(tr("Música"), bar);
    m_musicToggle->setObjectName(QStringLiteral("fpsChip"));
    m_musicToggle->setCheckable(true);
    m_musicToggle->setCursor(Qt::PointingHandCursor);
    m_musicToggle->setToolTip(tip(tr("Modo música"), tr("Muestra la canción que suena en tu PC (Spotify, YouTube en el navegador, etc.) arriba a la izquierda sobre el juego y aquí abajo. Si encuentra la letra, la agrega. Clic para prender o apagar.")));
    connect(m_musicToggle, &QPushButton::toggled, this, [](bool on) { fps::setSetting(fps::cfg::Lyrics, on); });
    h->addWidget(m_musicToggle);
    m_streamToggle = new QPushButton(tr("Modo stream"), bar);
    m_streamToggle->setObjectName(QStringLiteral("fpsChip"));
    m_streamToggle->setCheckable(true);
    m_streamToggle->setCursor(Qt::PointingHandCursor);
    m_streamToggle->setToolTip(tip(tr("Modo stream"), tr("Para transmitir sin mostrar datos privados: oculta IPs, nombres de servidores, tu nombre de cuenta y el servidor en Discord. Clic para prender o apagar.")));
    connect(m_streamToggle, &QPushButton::toggled, this, [](bool on) { fps::setSetting(fps::cfg::StreamMode, on); });
    h->addWidget(m_streamToggle);
    connect(fps::LyricsService::get(), &fps::LyricsService::updated, this, &FourFpsShell::updateNowPlaying);
    m_combo = new QComboBox(bar);
    m_combo->setObjectName(QStringLiteral("fpsCombo"));
    m_combo->setMinimumSize(160, 44);
    m_combo->setToolTip(tip(tr("Instancia"), tr("Elige con qué instancia jugar.")));
    connect(m_combo, &QComboBox::activated, this, [this](int idx) {
        const QString id = m_combo->itemData(idx).toString();
        if (auto* inst = APPLICATION->instances()->getInstanceById(id))
            m_hooks.select(inst);
    });
    h->addWidget(m_combo);
    m_play = new QPushButton(QStringLiteral("▶    J U G A R"), bar);
    m_play->setObjectName(QStringLiteral("playButton"));
    m_play->setMinimumSize(180, 48);
    m_play->setCursor(Qt::PointingHandCursor);
    m_play->setToolTip(tip(tr("Jugar"), tr("Abre Minecraft con la instancia elegida. Antes revisa que no haya mods en conflicto.")));
    connect(m_play, &QPushButton::clicked, this, &FourFpsShell::launchSelected);
    m_launchStatus = new QLabel(bar);
    m_launchStatus->setObjectName(QStringLiteral("fpsLaunchStatus"));
    m_launchStatus->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_launchStatus->setMinimumWidth(220);
    m_launchStatus->setMaximumWidth(330);
    m_launchStatus->setWordWrap(true);
    m_launchStatus->hide();
    h->insertWidget(h->indexOf(m_combo), m_launchStatus);
    h->addWidget(m_play);
    m_playTimer = new QTimer(this);
    m_playTimer->setInterval(1000);
    connect(m_playTimer, &QTimer::timeout, this, [this] {
        const qint64 secs = m_playStart.secsTo(QDateTime::currentDateTime());
        setLaunchState(2, QStringLiteral("%1:%2").arg(secs / 60, 2, 10, QChar('0')).arg(secs % 60, 2, 10, QChar('0')));
    });
    return bar;
}

void FourFpsShell::setLaunchState(int state, const QString& detail)
{
    if (!m_play || !m_launchStatus)
        return;
    const QString dot = QStringLiteral("<span style='color:%1;font-size:14pt'>●</span> ");
    switch (state) {
        case 1:
            m_play->setText(QStringLiteral("I N I C I A N D O ..."));
            m_play->setEnabled(false);
            m_launchStatus->setText(dot.arg("#facc15") + QStringLiteral("<b>%1</b><br><span style='color:#a1a1aa'>%2</span>")
                                                             .arg(tr("Iniciando Minecraft..."), detail.toHtmlEscaped()));
            m_launchStatus->show();
            break;
        case 2:
            m_play->setText(QStringLiteral("● J U G A N D O"));
            m_play->setEnabled(false);
            m_launchStatus->setText(dot.arg("#22c55e") + QStringLiteral("<b>%1</b><br><span style='color:#a1a1aa'>%2</span>")
                                                             .arg(tr("Minecraft se está ejecutando"), tr("Tiempo jugando: %1").arg(detail)));
            m_launchStatus->show();
            break;
        case 3:
            m_play->setText(QStringLiteral("▶    J U G A R"));
            m_play->setEnabled(true);
            m_launchStatus->setText(dot.arg("#ef4444") + QStringLiteral("<b>%1</b><br><span style='color:#a1a1aa'>%2</span>")
                                                             .arg(tr("No se pudo iniciar"), detail.toHtmlEscaped()));
            m_launchStatus->show();
            QTimer::singleShot(15000, this, [this] {
                if (!m_starting && !(m_watched && m_watched->isRunning()))
                    m_launchStatus->hide();
            });
            break;
        default:
            m_play->setText(QStringLiteral("▶    J U G A R"));
            m_play->setEnabled(m_hooks.selected && m_hooks.selected());
            if (!detail.isEmpty()) {
                m_launchStatus->setText(dot.arg("#71717a") + QStringLiteral("<b>%1</b>").arg(detail.toHtmlEscaped()));
                m_launchStatus->show();
                QTimer::singleShot(8000, this, [this] {
                    if (!m_starting && !(m_watched && m_watched->isRunning()))
                        m_launchStatus->hide();
                });
            } else {
                m_launchStatus->hide();
            }
            break;
    }
}

void FourFpsShell::watchLaunch(BaseInstance* inst)
{
    if (!inst)
        return;
    for (const auto& c : m_watchConns)
        disconnect(c);
    m_watchConns.clear();
    m_watched = inst;
    m_starting = true;
    setLaunchState(1, tr("Preparando..."));
    m_watchConns << connect(inst, &BaseInstance::launchTaskChanged, this, [this](LaunchTask* task) {
        if (!task)
            return;
        m_watchConns << connect(task, &Task::status, this, [this](const QString& st) {
            if (m_starting && !st.isEmpty())
                setLaunchState(1, st);
        });
        m_watchConns << connect(task, &Task::progress, this, [this, task](qint64 cur, qint64 total) {
            if (m_starting && total > 0)
                setLaunchState(1, QStringLiteral("%1 · %2%").arg(task->getStatus()).arg(cur * 100 / total));
        });
        m_watchConns << connect(task, &Task::failed, this, [this](const QString& reason) {
            m_starting = false;
            m_playTimer->stop();
            setLaunchState(3, reason.left(140));
        });
        m_watchConns << connect(task, &Task::aborted, this, [this] {
            m_starting = false;
            m_playTimer->stop();
            setLaunchState(0, tr("Inicio cancelado"));
        });
    });
    m_watchConns << connect(inst, &BaseInstance::runningStatusChanged, this, [this](bool running) {
        if (running) {
            m_starting = false;
            m_playStart = QDateTime::currentDateTime();
            if (auto* mi = dynamic_cast<MinecraftInstance*>(m_watched.data())) {
                QString server = m_currentServer.isEmpty() ? QString() : (fps::streamMode() ? tr("En un servidor") : tr("En %1").arg(m_currentServer));
                fps::DiscordPresence::get()->setPlaying(QStringLiteral("%1 · Minecraft %2").arg(fps::streamSafe(mi->name(), tr("Instancia")), mcVersionOf(mi)), server, m_playStart);
            }
            setLaunchState(2, QStringLiteral("00:00"));
            m_playTimer->start();
        } else {
            m_playTimer->stop();
            fps::DiscordPresence::get()->setIdle();
            if (!m_starting)
                setLaunchState(0, tr("El juego se cerró"));
            if (fps::enabled(fps::cfg::CrashExplainer)) {
                if (auto* mi = dynamic_cast<MinecraftInstance*>(m_watched.data())) {
                    const QDateTime since = m_playStart.isValid() ? m_playStart : QDateTime::currentDateTime().addSecs(-600);
                    QPointer<MinecraftInstance> guard(mi);
                    QTimer::singleShot(1200, this, [this, guard, since] {
                        if (!guard)
                            return;
                        const auto ex = fps::explainCrash(guard, since);
                        if (ex.found)
                            fps::showCrashDialog(this, guard, ex);
                    });
                }
            }
        }
    });
    // si el inicio se corta antes de crear la tarea (cuenta cancelada, java...), se vuelve al botón normal
    checkLaunchStuck(inst, 0);
}

void FourFpsShell::checkLaunchStuck(BaseInstance* inst, int tries)
{
    QPointer<BaseInstance> guard(inst);
    QTimer::singleShot(3000, this, [this, guard, tries] {
        if (!guard || !m_starting || m_watched != guard)
            return;
        if (guard->isRunning() || guard->getLaunchTask())
            return;  // ya está iniciando/jugando
        // hay una ventana abierta (descarga de mods de 4fps, elegir cuenta...): se espera
        if ((QApplication::activeModalWidget() || !findChildren<QProgressDialog*>().isEmpty()) && tries < 200) {
            checkLaunchStuck(guard, tries + 1);
            return;
        }
        m_starting = false;
        setLaunchState(0, tr("No se inició el juego"));
    });
}

void FourFpsShell::updateBottom()
{
    auto account = APPLICATION->accounts()->defaultAccount();
    if (account) {
        m_bottomFace->setPixmap(account->getFace(36, 36));
        const QString type = account->accountType() == AccountType::MSA ? tr("Cuenta Microsoft") : tr("Offline · No premium");
        m_bottomName->setText(QStringLiteral("<b>%1</b><br><span style='color:#a1a1aa'>%2</span>").arg(fps::streamSafe(account->profileName(), tr("Jugador")).toHtmlEscaped(), type));
    } else {
        m_bottomFace->setPixmap(QIcon::fromTheme("noaccount").pixmap(36, 36));
        m_bottomName->setText(QStringLiteral("<b>%1</b><br><span style='color:#a1a1aa'>%2</span>").arg(tr("Sin cuenta"), tr("Inicia sesión en Cuenta")));
    }
    auto* sel = m_hooks.selected ? m_hooks.selected() : nullptr;
    if (!m_starting && !(m_watched && m_watched->isRunning()))
        m_play->setEnabled(sel != nullptr);
    if (m_combo) {
        int idx = sel ? m_combo->findData(sel->id()) : -1;
        m_combo->blockSignals(true);
        m_combo->setCurrentIndex(idx);
        if (idx < 0)
            m_combo->setPlaceholderText(tr("Elige instancia"));
        m_combo->blockSignals(false);
    }
}

// --------------------------------------------------------------------- Cuenta
QWidget* FourFpsShell::buildAccountPage()
{
    auto* page = new QWidget(this);
    page->setObjectName(QStringLiteral("fpsPage"));
    auto* v = new QVBoxLayout(page);
    v->setContentsMargins(32, 28, 32, 24);
    v->setSpacing(22);
    v->addWidget(header(tr("Cuenta"), tr("Inicia sesión con tu cuenta premium o juega con un nombre offline."), {}));

    auto* cols = new QHBoxLayout();
    cols->setSpacing(20);

    // izquierda: premium + offline
    auto* left = makeCard(page);
    left->setMaximumWidth(460);
    auto* lv = new QVBoxLayout(left);
    lv->setContentsMargins(24, 22, 24, 22);
    lv->setSpacing(10);
    lv->addWidget(makeLabel(tr("Premium"), QStringLiteral("fpsSectionTitle"), left));
    auto* info = makeLabel(tr("Se abre la ventana de Microsoft en tu navegador. Tu contraseña nunca pasa por el launcher."),
                           QStringLiteral("fpsMuted"), left);
    info->setWordWrap(true);
    lv->addWidget(info);
    auto* ms = makeButton(tr("Iniciar sesión con Microsoft"), "accounts", true, left);
    ms->setMinimumHeight(44);
    connect(ms, &QPushButton::clicked, this, [this] {
        auto account = MSALoginDialog::newAccount(this);
        if (account) {
            auto accounts = APPLICATION->accounts();
            accounts->addAccount(account);
            if (accounts->count() == 1)
                accounts->setDefaultAccount(account);
        }
    });
    lv->addWidget(ms);
    auto* sep = new QFrame(left);
    sep->setObjectName(QStringLiteral("fpsSeparator"));
    sep->setFixedHeight(1);
    lv->addSpacing(8);
    lv->addWidget(sep);
    lv->addSpacing(8);
    lv->addWidget(makeLabel(tr("No premium (offline)"), QStringLiteral("fpsSectionTitle"), left));
    lv->addWidget(makeLabel(tr("NOMBRE DE USUARIO"), QStringLiteral("fpsCaption"), left));
    m_offlineName = new QLineEdit(left);
    m_offlineName->setObjectName(QStringLiteral("fpsInput"));
    m_offlineName->setPlaceholderText(tr("Tu nombre"));
    m_offlineName->setMaxLength(16);
    lv->addWidget(m_offlineName);
    lv->addWidget(makeLabel(tr("3 a 16 caracteres: letras, números y guion bajo."), QStringLiteral("fpsHint"), left));
    auto* addOff = makeButton(QStringLiteral("+  ") + tr("Añadir cuenta offline"), QString(), false, left);
    addOff->setMinimumHeight(40);
    connect(addOff, &QPushButton::clicked, this, [this] {
        auto accounts = APPLICATION->accounts();
        if (!accounts->anyAccountIsValid()) {
            QMessageBox::warning(this, tr("Cuenta premium necesaria"),
                                 tr("Para usar cuentas offline primero tienes que iniciar sesión una vez con una cuenta de Microsoft que tenga Minecraft."));
            return;
        }
        const QString name = m_offlineName->text().trimmed();
        if (!QRegularExpression(QStringLiteral("^[A-Za-z0-9_]{3,16}$")).match(name).hasMatch()) {
            QMessageBox::warning(this, tr("Nombre no válido"), tr("Usa de 3 a 16 caracteres: letras, números y guion bajo."));
            return;
        }
        if (auto account = MinecraftAccount::createOffline(name)) {
            account->login()->start();
            accounts->addAccount(account);
            if (accounts->count() == 1)
                accounts->setDefaultAccount(account);
            m_offlineName->clear();
        }
    });
    lv->addWidget(addOff);
    lv->addStretch(1);
    cols->addWidget(left, 1, Qt::AlignTop);

    // derecha: cuentas guardadas + skin actual
    auto* right = new QWidget(page);
    auto* rv = new QVBoxLayout(right);
    rv->setContentsMargins(0, 0, 0, 0);
    rv->setSpacing(10);
    rv->addWidget(makeLabel(tr("CUENTAS GUARDADAS"), QStringLiteral("fpsCaption"), right));
    auto* listHost = new QWidget(right);
    m_accountsList = new QVBoxLayout(listHost);
    m_accountsList->setContentsMargins(0, 0, 0, 0);
    m_accountsList->setSpacing(10);
    rv->addWidget(listHost);
    rv->addSpacing(10);

    auto* skin = makeCard(right);
    auto* sv = new QVBoxLayout(skin);
    sv->setContentsMargins(18, 16, 18, 16);
    sv->addWidget(makeLabel(tr("SKIN ACTUAL"), QStringLiteral("fpsCaption"), skin));
    auto* sh = new QHBoxLayout();
    auto* shirt = new QLabel(skin);
    shirt->setObjectName(QStringLiteral("fpsAvatar"));
    shirt->setFixedSize(56, 56);
    shirt->setAlignment(Qt::AlignCenter);
    shirt->setPixmap(QIcon::fromTheme("appearance").pixmap(24, 24));
    sh->addWidget(shirt);
    auto* st = new QVBoxLayout();
    st->setSpacing(0);
    st->addWidget(makeLabel(tr("Modelo clásico"), QStringLiteral("fpsRowTitle"), skin));
    st->addWidget(makeLabel(tr("Se cambia en la sección Skins"), QStringLiteral("fpsMuted"), skin));
    sh->addLayout(st);
    sh->addStretch();
    auto* goSkins = makeButton(tr("Ir a Skins"), QString(), false, skin);
    connect(goSkins, &QPushButton::clicked, this, [this] { showPage(6); });
    sh->addWidget(goSkins);
    sv->addLayout(sh);
    rv->addWidget(skin);
    rv->addStretch(1);
    cols->addWidget(right, 1);
    v->addLayout(cols, 1);
    return page;
}

void FourFpsShell::rebuildAccounts()
{
    if (!m_accountsList)
        return;
    while (auto* item = m_accountsList->takeAt(0)) {
        if (auto* w = item->widget())
            w->deleteLater();
        delete item;
    }
    auto accounts = APPLICATION->accounts();
    auto def = accounts->defaultAccount();
    if (accounts->count() == 0) {
        auto* empty = makeLabel(tr("Todavía no tienes cuentas guardadas."), QStringLiteral("fpsMuted"), m_accountsList->parentWidget());
        m_accountsList->addWidget(empty);
    }
    for (int i = 0; i < accounts->count(); i++) {
        auto acc = accounts->at(i);
        auto* row = makeCard(m_accountsList->parentWidget());
        auto* h = new QHBoxLayout(row);
        h->setContentsMargins(12, 10, 14, 10);
        h->setSpacing(12);
        auto* face = new QLabel(row);
        face->setObjectName(QStringLiteral("fpsAvatar"));
        face->setFixedSize(40, 40);
        face->setPixmap(acc->getFace(40, 40));
        h->addWidget(face);
        auto* t = new QVBoxLayout();
        t->setSpacing(0);
        t->addWidget(makeLabel(fps::streamSafe(acc->profileName(), tr("Cuenta %1").arg(i + 1)), QStringLiteral("fpsRowTitle"), row));
        const bool premium = acc->accountType() == AccountType::MSA;
        t->addWidget(makeLabel(premium ? tr("Microsoft · Premium") : tr("Offline · No premium"), QStringLiteral("fpsMuted"), row));
        h->addLayout(t);
        h->addStretch();
        if (acc == def) {
            h->addWidget(makeLabel(tr("Activa"), QStringLiteral("fpsActivePill"), row));
        } else {
            auto* use = makeButton(tr("Usar"), QString(), false, row);
            connect(use, &QPushButton::clicked, this, [acc] { APPLICATION->accounts()->setDefaultAccount(acc); });
            h->addWidget(use);
        }
        auto* del = makeButton(QString(), "delete", false, row);
        del->setObjectName(QStringLiteral("fpsIconButtonFlat"));
        del->setFixedSize(32, 32);
        connect(del, &QPushButton::clicked, this, [this, acc] {
            if (QMessageBox::question(this, tr("Eliminar cuenta"), tr("¿Eliminar la cuenta %1?").arg(acc->profileName())) != QMessageBox::Yes)
                return;
            auto list = APPLICATION->accounts();
            for (int j = 0; j < list->count(); j++)
                if (list->at(j) == acc) {
                    list->removeAccount(list->index(j));
                    break;
                }
        });
        h->addWidget(del);
        m_accountsList->addWidget(row);
    }
    updateBottom();
}

// ---------------------------------------------------------------- funciones 4fps (ajustes en vivo)
void FourFpsShell::applyFeatureSettings()
{
    if (m_streamToggle) {
        m_streamToggle->blockSignals(true);
        m_streamToggle->setChecked(fps::streamMode());
        m_streamToggle->blockSignals(false);
    }
    if (fps::liteMode() && fps::enabled(fps::cfg::Lyrics))
        fps::setSetting(fps::cfg::Lyrics, false);  // PC de 8 GB o menos: sin modo música
    if (m_musicToggle) {
        m_musicToggle->setEnabled(!fps::liteMode());
        m_musicToggle->setToolTip(fps::liteMode() ? tip(tr("Modo música (bloqueado)"), fps::liteReason())
                                                  : tip(tr("Modo música"), tr("Muestra la canción que suena en tu PC (Spotify, YouTube en el navegador, etc.) arriba a la izquierda sobre el juego y aquí abajo. Si encuentra la letra, la agrega. Clic para prender o apagar.")));
    }
    const bool lyrics = fps::enabled(fps::cfg::Lyrics);
    if (m_musicToggle) {
        m_musicToggle->blockSignals(true);
        m_musicToggle->setChecked(lyrics);
        m_musicToggle->blockSignals(false);
    }
    auto* ls = fps::LyricsService::get();
    if (lyrics && !ls->running())
        ls->start();
    else if (!lyrics && ls->running())
        ls->stop();
    if (m_galleryNav)
        m_galleryNav->setVisible(fps::enabled(fps::cfg::Gallery));
    rebuildAccounts();
    updateDetail();
    updateNowPlaying();
    if (m_stack)
        onPageShown(m_stack->currentIndex());
}

void FourFpsShell::updateNowPlaying()
{
    if (!m_music)
        return;
    auto* ls = fps::LyricsService::get();
    for (auto* b : m_musicControls)
        b->setVisible(ls->running() && !ls->title.isEmpty());
    if (!ls->running()) {
        m_music->hide();
        return;
    }
    m_music->show();
    if (ls->title.isEmpty()) {
        m_music->setText(QStringLiteral("<span style='color:#a1a1aa'>%1</span>").arg(tr("Música: no suena nada")));
        return;
    }
    const QString head = QStringLiteral("%1 — %2").arg(ls->title, ls->artist);
    const QString line = ls->playing ? ls->currentLine : tr("En pausa");
    m_music->setText(QStringLiteral("<b>%1</b><br><span style='color:#a1a1aa'>%2</span>")
                         .arg(m_music->fontMetrics().elidedText(head, Qt::ElideRight, 320).toHtmlEscaped(),
                              m_music->fontMetrics().elidedText(line, Qt::ElideRight, 320).toHtmlEscaped()));
}
