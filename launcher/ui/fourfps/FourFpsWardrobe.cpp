// 4fps - Ropa y accesorios: fusiona prendas (PNG 64x64 en formato skin) con la skin del jugador.
// La prenda pasa a ser la capa exterior predominante: en cada parte del cuerpo que toca la prenda,
// se borra la segunda capa de la skin original y se pinta la prenda.
#include "FourFpsShell.h"
#include "SkinRender.h"

#include <QDesktopServices>
#include <QMenu>
#include <QCursor>
#include <QUrl>
#include <QButtonGroup>
#include <QCoreApplication>
#include <QCheckBox>
#include <QDir>
#include <QDateTime>
#include <QHash>
#include <QPixmap>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QToolButton>
#include <QRegularExpression>
#include <QVBoxLayout>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "Application.h"
#include "DesktopServices.h"
#include "minecraft/auth/AccountList.h"
#include "minecraft/auth/MinecraftAccount.h"
#include "minecraft/skins/SkinRequests.h"
#include "net/NetJob.h"
#include "ui/dialogs/ProgressDialog.h"

namespace {
constexpr qint64 kMaxCapeBytes = 400 * 1024;
QHash<QString, QImage> s_capeTexCache;
QHash<QString, QPixmap> s_capeThumbCache;

// deja la capa liviana para la galeria: primer cuadro si es animada y maximo 256 px de ancho
QImage lightCape(QImage t)
{
    if (t.isNull() || t.width() < 64 || t.height() < t.width() / 2)
        return {};
    if (t.height() > t.width() / 2 * 1.5)
        t = t.copy(0, 0, t.width(), t.width() / 2);
    if (t.width() > 256)
        t = t.scaled(256, 128, Qt::IgnoreAspectRatio, Qt::FastTransformation);
    return t.convertToFormat(QImage::Format_ARGB32);
}
}  // namespace


using namespace fps;

namespace {
struct ArmBox {
    int u, v;
};
// cajas de brazo (capa base y exterior, derecho e izquierdo)
const ArmBox kArms[] = { { 40, 16 }, { 40, 32 }, { 32, 48 }, { 48, 48 } };

// rectangulos de una caja de brazo para un ancho w (3 = slim, 4 = clasico)
QList<QRect> armRects(const ArmBox& b, int w)
{
    return { QRect(b.u + 4, b.v, w, 4),       QRect(b.u + 4 + w, b.v, w, 4),     QRect(b.u, b.v + 4, 4, 12),
             QRect(b.u + 4, b.v + 4, w, 12),  QRect(b.u + 4 + w, b.v + 4, 4, 12), QRect(b.u + 8 + w, b.v + 4, w, 12) };
}

// convierte los brazos de una textura entre slim (3px) y clasico (4px)
QImage convertArms(const QImage& src, bool toSlim)
{
    QImage out = src.copy();
    const int from = toSlim ? 4 : 3, to = toSlim ? 3 : 4;
    for (const auto& b : kArms) {
        const auto a = armRects(b, from), c = armRects(b, to);
        QRect all(b.u, b.v, 16, 16);
        QImage region = src.copy(all);
        QPainter p(&out);
        p.setCompositionMode(QPainter::CompositionMode_Source);
        p.fillRect(all, Qt::transparent);
        for (int i = 0; i < a.size(); i++)
            p.drawImage(c[i], region.copy(a[i].translated(-b.u, -b.v)).scaled(c[i].size(), Qt::IgnoreAspectRatio, Qt::FastTransformation));
    }
    return out;
}

QImage normalizeSkin(const QImage& in)
{
    QImage s = in.convertToFormat(QImage::Format_ARGB32);
    if (s.width() == 64 && s.height() == 32) {
        // skin antigua: copia brazo y pierna derechos al lado izquierdo
        QImage n(64, 64, QImage::Format_ARGB32);
        n.fill(Qt::transparent);
        QPainter p(&n);
        p.drawImage(0, 0, s);
        p.drawImage(QPoint(16, 48), s.copy(0, 16, 16, 16));
        p.drawImage(QPoint(32, 48), s.copy(40, 16, 16, 16));
        return n;
    }
    return s;
}

struct Part {
    QRect base, outer;
};
const Part kParts[] = {
    { QRect(0, 0, 32, 16), QRect(32, 0, 32, 16) },    // cabeza
    { QRect(16, 16, 24, 16), QRect(16, 32, 24, 16) },  // torso
    { QRect(40, 16, 16, 16), QRect(40, 32, 16, 16) },  // brazo derecho
    { QRect(32, 48, 16, 16), QRect(48, 48, 16, 16) },  // brazo izquierdo
    { QRect(0, 16, 16, 16), QRect(0, 32, 16, 16) },    // pierna derecha
    { QRect(16, 48, 16, 16), QRect(0, 48, 16, 16) },   // pierna izquierda
};

bool hasPixels(const QImage& img, const QRect& r)
{
    for (int y = r.top(); y <= r.bottom(); y++)
        for (int x = r.left(); x <= r.right(); x++)
            if (qAlpha(img.pixel(x, y)) > 0)
                return true;
    return false;
}

QImage loadCosmetic(const QString& path, bool skinSlim)
{
    QImage c(path);
    if (c.width() != 64 || c.height() != 64)
        return {};
    c = c.convertToFormat(QImage::Format_ARGB32);
    const bool cosmeticSlim = QFileInfo(path).completeBaseName().endsWith("-slim", Qt::CaseInsensitive);
    if (cosmeticSlim != skinSlim)
        c = convertArms(c, skinSlim);
    return c;
}
}  // namespace

QImage FourFpsShell::composeSkin(const QImage& baseIn, bool slim, const QStringList& cosmetics, bool replaceOuter)
{
    QImage skin = normalizeSkin(baseIn.isNull() ? SkinRenderer::defaultSkin() : baseIn);
    for (const auto& path : cosmetics) {
        QImage c = loadCosmetic(path, slim);
        if (c.isNull())
            continue;
        QPainter p(&skin);
        for (const auto& part : kParts) {
            const bool touches = hasPixels(c, part.base) || hasPixels(c, part.outer);
            if (!touches)
                continue;
            if (replaceOuter) {
                // la prenda es la capa predominante: se quita la segunda capa original de esa parte
                p.setCompositionMode(QPainter::CompositionMode_Source);
                p.fillRect(part.outer, Qt::transparent);
            }
            p.setCompositionMode(QPainter::CompositionMode_SourceOver);
            p.drawImage(part.base, c, part.base);
            p.drawImage(part.outer, c, part.outer);
        }
    }
    return skin;
}

// Pack original de 4fps (viene en el setup): se copia una vez a las carpetas del usuario.
// Si el usuario borra una prenda o capa del pack, no se vuelve a copiar.
void FourFpsShell::seedStarterPack()
{
    const QString packRoot = QCoreApplication::applicationDirPath() + "/pack4fps";
    if (!QDir(packRoot).exists())
        return;
    const QString marker = APPLICATION->dataRoot() + "/pack4fps-copiado.txt";
    QStringList done;
    {
        QFile f(marker);
        if (f.open(QIODevice::ReadOnly | QIODevice::Text))
            done = QString::fromUtf8(f.readAll()).split('\n', Qt::SkipEmptyParts);
    }
    const QList<QPair<QString, QString>> dirs = { { "ropa", cosmeticsDir() }, { "capas", localCapesDir() } };
    bool changed = false;
    for (const auto& d : dirs) {
        for (const auto& fi : QDir(packRoot + "/" + d.first).entryInfoList({ "*.png" }, QDir::Files)) {
            const QString key = d.first + "/" + fi.fileName();
            if (done.contains(key))
                continue;
            const QString target = d.second + "/" + fi.fileName();
            if (!QFile::exists(target))
                QFile::copy(fi.absoluteFilePath(), target);
            done << key;
            changed = true;
        }
    }
    // la capa dorada pasó a ser exclusiva del equipo: se quita de "Mis capas" si la copió una versión anterior
    QFile::remove(localCapesDir() + "/4fps dorada.png");
    if (changed) {
        QFile f(marker);
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
            f.write(done.join('\n').toUtf8());
    }
}

QString FourFpsShell::cosmeticsDir()
{
    const QString d = APPLICATION->dataRoot() + "/cosmeticos";
    QDir().mkpath(d);
    return d;
}

QWidget* FourFpsShell::buildWardrobe(QWidget* parent)
{
    auto* page = new QWidget(parent);
    auto* cols = new QHBoxLayout(page);
    cols->setContentsMargins(0, 0, 0, 0);
    cols->setSpacing(20);

    // ---- izquierda: biblioteca de prendas
    auto* left = new QVBoxLayout();
    left->setSpacing(12);
    auto* tools = new QHBoxLayout();
    tools->setSpacing(8);
    m_wardrobeSearch = new QLineEdit(page);
    m_wardrobeSearch->setObjectName(QStringLiteral("fpsSearch"));
    m_wardrobeSearch->setPlaceholderText(tr("Buscar prenda..."));
    connect(m_wardrobeSearch, &QLineEdit::textChanged, this, &FourFpsShell::refreshWardrobe);
    tools->addWidget(m_wardrobeSearch, 1);
    auto* importBtn = makeButton(tr("Importar"), "export", false, page);
    connect(importBtn, &QPushButton::clicked, this, [this] {
        const QStringList files = QFileDialog::getOpenFileNames(this, tr("Importar prendas (PNG 64×64 en formato skin)"), QString(), tr("PNG (*.png)"));
        int ok = 0, bad = 0;
        for (const auto& f : files) {
            QImage img(f);
            if (img.width() == 64 && img.height() == 64 && QFile::copy(f, cosmeticsDir() + "/" + QFileInfo(f).fileName()))
                ok++;
            else
                bad++;
        }
        if (bad)
            QMessageBox::information(this, tr("Importar"),
                                     tr("Se importaron %1 prendas. %2 archivos no son prendas de 64×64 (por ejemplo capas o bandanas) y se omitieron.").arg(ok).arg(bad));
        refreshWardrobe();
    });
    tools->addWidget(importBtn);
    auto* folderBtn = makeButton(tr("Carpeta"), "viewfolder", false, page);
    connect(folderBtn, &QPushButton::clicked, this, [] { DesktopServices::openPath(cosmeticsDir(), true); });
    tools->addWidget(folderBtn);
    left->addLayout(tools);

    QWidget* host;
    auto* scroll = new QScrollArea(page);
    scroll->setObjectName(QStringLiteral("fpsScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    host = new QWidget(scroll);
    host->setObjectName(QStringLiteral("fpsGridHost"));
    scroll->setWidget(host);
    m_wardrobeGrid = new QGridLayout(host);
    m_wardrobeGrid->setContentsMargins(0, 0, 0, 0);
    m_wardrobeGrid->setSpacing(10);
    m_wardrobeGrid->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    left->addWidget(scroll, 1);
    cols->addLayout(left, 1);

    // ---- derecha: vista previa y aplicar
    auto* panel = makeCard(page);
    panel->setFixedWidth(330);
    auto* pv = new QVBoxLayout(panel);
    pv->setContentsMargins(16, 16, 16, 16);
    pv->setSpacing(8);
    pv->addWidget(makeLabel(tr("VISTA PREVIA"), QStringLiteral("fpsCaption"), panel));
    m_wardrobePreview = new SkinRenderView(panel);
    m_wardrobePreview->setMinimumSize(290, 300);
    m_wardrobePreview->options.background = 1;
    SkinRenderer::applyPreset(0, m_wardrobePreview->options);
    pv->addWidget(m_wardrobePreview, 1);
    m_wardrobeCount = makeLabel(QString(), QStringLiteral("fpsMuted"), panel);
    pv->addWidget(m_wardrobeCount);
    m_wardrobeReplace = new QCheckBox(tr("La prenda reemplaza la 2ª capa de la skin"), panel);
    m_wardrobeReplace->setChecked(true);
    m_wardrobeReplace->setToolTip(tr("En cada parte del cuerpo que cubre la prenda, se quita la capa exterior de tu skin y la prenda queda encima."));
    connect(m_wardrobeReplace, &QCheckBox::toggled, this, &FourFpsShell::updateWardrobePreview);
    pv->addWidget(m_wardrobeReplace);
    auto* clear = makeButton(tr("Quitar todo"), QString(), false, panel);
    connect(clear, &QPushButton::clicked, this, [this] {
        m_wardrobeSelected.clear();
        refreshWardrobe();
    });
    pv->addWidget(clear);
    auto* apply = makeButton(tr("Aplicar a mi skin"), QString(), true, panel);
    apply->setMinimumHeight(42);
    connect(apply, &QPushButton::clicked, this, &FourFpsShell::applyWardrobe);
    pv->addWidget(apply);
    auto* savePng = makeButton(tr("Guardar skin como PNG"), "export", false, panel);
    connect(savePng, &QPushButton::clicked, this, [this] {
        const QString f = QFileDialog::getSaveFileName(this, tr("Guardar skin"), "skin_4fps.png", tr("PNG (*.png)"));
        if (!f.isEmpty())
            m_wardrobePreview->skin.save(f, "PNG");
    });
    pv->addWidget(savePng);
    auto* restore = makeButton(tr("Volver a mi skin original"), QString(), false, panel);
    connect(restore, &QPushButton::clicked, this, [this] {
        m_wardrobeSelected.clear();
        refreshWardrobe();
        applyWardrobe();
    });
    pv->addWidget(restore);
    auto* note = makeLabel(tr("Se sube a tu cuenta de Minecraft: la ven todos los jugadores, tengan o no 4fps."), QStringLiteral("fpsHint"), panel);
    note->setWordWrap(true);
    pv->addWidget(note);
    cols->addWidget(panel);
    return page;
}

// skin original de la cuenta (guardada la primera vez, para no acumular prendas)
static QImage originalSkinFor(const MinecraftAccountPtr& account, bool* slim)
{
    QImage skin;
    *slim = false;
    if (!account)
        return skin;
    auto* data = account->accountData();
    *slim = data->minecraftProfile.skin.variant.compare("slim", Qt::CaseInsensitive) == 0;
    const QString dir = APPLICATION->dataRoot() + "/skins";
    QDir().mkpath(dir);
    const QString file = dir + "/original_" + data->minecraftProfile.id + ".png";
    if (QFile::exists(file) && skin.load(file))
        return skin;
    skin.loadFromData(data->minecraftProfile.skin.data);
    if (!skin.isNull())
        skin.save(file, "PNG");
    return skin;
}

void FourFpsShell::refreshWardrobe()
{
    if (!m_wardrobeGrid)
        return;
    while (auto* item = m_wardrobeGrid->takeAt(0)) {
        if (auto* w = item->widget())
            w->deleteLater();
        delete item;
    }
    const QString q = m_wardrobeSearch->text().trimmed();
    const auto files = QDir(cosmeticsDir()).entryInfoList({ "*.png" }, QDir::Files, QDir::Name);
    int n = 0;
    for (const auto& fi : files) {
        if (!q.isEmpty() && !fi.completeBaseName().contains(q, Qt::CaseInsensitive))
            continue;
        const QString path = fi.absoluteFilePath();
        if (!m_wardrobeThumbs.contains(path)) {
            QImage c = loadCosmetic(path, true);
            if (c.isNull())
                continue;  // capas, bandanas u otros formatos
            QImage mannequin(64, 64, QImage::Format_ARGB32);
            mannequin.fill(Qt::transparent);
            {
                QPainter p(&mannequin);
                p.drawImage(0, 0, SkinRenderer::defaultSkin());
                p.setCompositionMode(QPainter::CompositionMode_Source);
                for (const auto& part : kParts)
                    p.fillRect(part.outer, Qt::transparent);
            }
            SkinRenderOptions o;
            o.background = 0;
            o.shadow = false;
            o.slim = true;
            o.yaw = 25;
            o.zoom = 1.45;
            SkinRenderer::applyPreset(0, o);
            m_wardrobeThumbs.insert(path, QPixmap::fromImage(SkinRenderer::renderImage(96, composeSkin(mannequin, true, { path }, true), QImage(), o)));
        }
        auto* tile = new QToolButton(m_wardrobeGrid->parentWidget());
        tile->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        tile->setObjectName(QStringLiteral("fpsWardrobeTile"));
        tile->setCheckable(true);
        tile->setChecked(m_wardrobeSelected.contains(path));
        tile->setFixedSize(112, 132);
        tile->setCursor(Qt::PointingHandCursor);
        tile->setIcon(m_wardrobeThumbs.value(path));
        tile->setIconSize(QSize(96, 96));
        QString name = fi.completeBaseName();
        name.remove(QRegularExpression("-slim$"));
        name.replace('-', ' ');
        tile->setText(tile->fontMetrics().elidedText(name, Qt::ElideRight, 100));
        tile->setToolTip(name);
        connect(tile, &QToolButton::toggled, this, [this, path](bool on) {
            if (on && !m_wardrobeSelected.contains(path))
                m_wardrobeSelected << path;
            if (!on)
                m_wardrobeSelected.removeAll(path);
            updateWardrobePreview();
        });
        m_wardrobeGrid->addWidget(tile, n / 5, n % 5);
        n++;
    }
    if (n == 0) {
        auto* l = makeLabel(tr("No hay prendas todavía. Usa \"Importar\" y elige PNG de 64×64 en formato skin (ropa, gorros, accesorios)."),
                            QStringLiteral("fpsMuted"), m_wardrobeGrid->parentWidget());
        l->setWordWrap(true);
        m_wardrobeGrid->addWidget(l, 0, 0, 1, 5);
    }
    updateWardrobePreview();
}

void FourFpsShell::updateWardrobePreview()
{
    if (!m_wardrobePreview)
        return;
    auto account = APPLICATION->accounts()->defaultAccount();
    bool slim = false;
    QImage base = originalSkinFor(account, &slim);
    m_wardrobePreview->options.slim = slim;
    m_wardrobePreview->setSkin(composeSkin(base, slim, m_wardrobeSelected, m_wardrobeReplace->isChecked()), QImage());
    m_wardrobeCount->setText(m_wardrobeSelected.isEmpty() ? tr("Elige una o varias prendas.") : tr("%n prenda(s) puesta(s)", "", m_wardrobeSelected.size()));
}

void FourFpsShell::applyWardrobe()
{
    auto account = APPLICATION->accounts()->defaultAccount();
    if (!account || account->accountType() != AccountType::MSA) {
        QMessageBox::information(this, tr("Ropa y accesorios"),
                                 tr("Para subir la skin a tu cuenta necesitas una cuenta premium activa. Igual puedes usar \"Guardar skin como PNG\"."));
        return;
    }
    bool slim = false;
    QImage base = originalSkinFor(account, &slim);
    QImage result = composeSkin(base, slim, m_wardrobeSelected, m_wardrobeReplace->isChecked());
    const QString file = APPLICATION->dataRoot() + "/skins/4fps_aplicada.png";
    if (!result.save(file, "PNG"))
        return;
    ProgressDialog prog(this);
    NetJob::Ptr job{ new NetJob(tr("Subiendo skin"), APPLICATION->network(), 1) };
    job->addNetAction(makeSkinUploadRequest(account->accessToken(), file, slim ? "SLIM" : "CLASSIC"));
    job->addTask(account->refresh().staticCast<Task>());
    if (prog.execWithTask(job.get()) != QDialog::Accepted) {
        QMessageBox::warning(this, tr("Ropa y accesorios"), tr("No se pudo subir la skin. Revisa tu conexión e inténtalo de nuevo."));
        return;
    }
    QMessageBox::information(this, tr("Ropa y accesorios"),
                             m_wardrobeSelected.isEmpty() ? tr("Listo: volviste a tu skin original.")
                                                          : tr("Listo: tu skin ahora tiene las prendas. Se verá en el juego la próxima vez que entres."));
}

// ======================================================================= Capas
QWidget* FourFpsShell::buildCapesPanel(QWidget* parent)
{
    auto* page = new QWidget(parent);
    auto* cols = new QHBoxLayout(page);
    cols->setContentsMargins(0, 0, 0, 0);
    cols->setSpacing(20);

    auto* scroll = new QScrollArea(page);
    scroll->setObjectName(QStringLiteral("fpsScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* host = new QWidget(scroll);
    host->setObjectName(QStringLiteral("fpsGridHost"));
    scroll->setWidget(host);
    m_capesGrid = new QGridLayout(host);
    m_capesGrid->setContentsMargins(0, 0, 0, 0);
    m_capesGrid->setSpacing(12);
    m_capesGrid->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    cols->addWidget(scroll, 1);

    auto* panel = makeCard(page);
    panel->setFixedWidth(330);
    auto* pv = new QVBoxLayout(panel);
    pv->setContentsMargins(16, 16, 16, 16);
    pv->setSpacing(8);
    pv->addWidget(makeLabel(tr("VISTA PREVIA"), QStringLiteral("fpsCaption"), panel));
    m_capePreview = new SkinRenderView(panel);
    m_capePreview->setMinimumSize(290, 320);
    m_capePreview->options.yaw = 160;
    m_capePreview->options.pitch = 8;
    SkinRenderer::applyPreset(1, m_capePreview->options);
    pv->addWidget(m_capePreview, 1);
    m_capeName = makeLabel(QString(), QStringLiteral("fpsRowTitle"), panel);
    pv->addWidget(m_capeName);
    auto* apply = makeButton(tr("Usar esta capa"), QString(), true, panel);
    apply->setMinimumHeight(42);
    m_capeApply = apply;
    connect(apply, &QPushButton::clicked, this, [this] {
        if (!m_selectedLocalCape.isEmpty()) {
            // capa personal: solo para la vista previa y el Render
            if (m_render) {
                QImage capeImg = m_selectedLocalCape.startsWith("official:") ? m_capeTextures.value(m_selectedLocalCape.mid(9))
                                                                            : QImage(m_selectedLocalCape);
                m_render->setSkin(m_render->skin, capeImg);
                m_render->options.showCape = true;
            }
            showPage(7);
            return;
        }
        auto account = APPLICATION->accounts()->defaultAccount();
        if (!account || account->accountType() != AccountType::MSA)
            return;
        ProgressDialog prog(this);
        NetJob::Ptr job{ new NetJob(tr("Cambiando capa"), APPLICATION->network(), 1) };
        job->addNetAction(makeCapeChangeRequest(account->accessToken(), m_selectedCape));
        job->addTask(account->refresh().staticCast<Task>());
        if (prog.execWithTask(job.get()) != QDialog::Accepted) {
            QMessageBox::warning(this, tr("Capas"), tr("No se pudo cambiar la capa. Revisa tu conexión e inténtalo de nuevo."));
            return;
        }
        refreshCapes();
    });
    pv->addWidget(apply);
    m_capeGame = makeButton(tr("Usar en el juego (solo 4fps)"), QString(), true, panel);
    m_capeGame->setMinimumHeight(42);
    connect(m_capeGame, &QPushButton::clicked, this, [this] {
        if (m_selectedLocalCape.isEmpty())
            return;
        const QImage capeImg = m_selectedLocalCape.startsWith("official:") ? m_capeTextures.value(m_selectedLocalCape.mid(9))
                                                                          : QImage(m_selectedLocalCape);
        useCapeInGame(capeImg, m_capeName ? m_capeName->text() : QString());
    });
    pv->addWidget(m_capeGame);
    auto* importCapes = makeButton(tr("Importar capas (PNG)"), "export", false, panel);
    connect(importCapes, &QPushButton::clicked, this, [this] {
        const QStringList files = QFileDialog::getOpenFileNames(this, tr("Importar capas"), QString(), tr("PNG (*.png)"));
        for (const auto& f : files) {
            QImage img(f);
            if (QFileInfo(f).size() > kMaxCapeBytes)
                continue;  // muy pesada
            if (!img.isNull() && img.width() >= 64 && img.width() % 64 == 0 && img.height() >= img.width() / 2)
                QFile::copy(f, localCapesDir() + "/" + QFileInfo(f).fileName());
        }
        refreshCapes();
    });
    pv->addWidget(importCapes);
    auto* more = makeButton(tr("Conseguir más capas"), "centralmods", false, panel);
    connect(more, &QPushButton::clicked, this, [this] {
        QMenu menu(this);
        auto* a1 = menu.addAction(QStringLiteral("MinecraftCapes"));
        auto* a2 = menu.addAction(QStringLiteral("Cosmetica"));
        menu.addSeparator();
        auto* a3 = menu.addAction(tr("Abrir mi carpeta de capas"));
        auto* chosen = menu.exec(QCursor::pos());
        if (chosen == a1)
            QDesktopServices::openUrl(QUrl("https://minecraftcapes.net"));
        else if (chosen == a2)
            QDesktopServices::openUrl(QUrl("https://cosmetica.cc"));
        else if (chosen == a3)
            QDesktopServices::openUrl(QUrl::fromLocalFile(localCapesDir()));
    });
    pv->addWidget(more);
    auto* moreHint = makeLabel(tr("Descarga el PNG de la capa en esas páginas y súbelo con \"Importar capas\"."), QStringLiteral("fpsHint"), panel);
    moreHint->setWordWrap(true);
    pv->addWidget(moreHint);
    m_capeNote = makeLabel(QString(), QStringLiteral("fpsHint"), panel);
    auto* note = m_capeNote;
    note->setWordWrap(true);
    pv->addWidget(note);
    cols->addWidget(panel);
    return page;
}

QString FourFpsShell::localCapesDir()
{
    const QString d = APPLICATION->dataRoot() + "/capas";
    QDir().mkpath(d);
    return d;
}

void FourFpsShell::updateCapeButton()
{
    if (!m_capeApply)
        return;
    const bool local = !m_selectedLocalCape.isEmpty();
    if (m_capeGame)
        m_capeGame->setVisible(local);
    m_capeApply->setText(local ? tr("Usar en el Render") : tr("Usar esta capa"));
    if (m_selectedLocalCape.startsWith("official:")) {
        m_capeApply->setText(tr("Usar en el Render"));
        m_capeNote->setText(tr("Capa del catálogo: con \"Usar en el juego (solo 4fps)\" la llevas puesta en la instancia elegida. Solo la ves tú en 4fps; los demás no."));
        return;
    }
    m_capeNote->setText(local ? tr("Capa personal: con \"Usar en el juego (solo 4fps)\" la llevas puesta en la instancia elegida (Fabric). Solo la ves tú en 4fps; los demás no.")
                              : tr("Capa oficial de tu cuenta de Minecraft. La que elijas la ven todos los jugadores."));
}

void FourFpsShell::refreshCapes()
{
    if (!m_capesGrid)
        return;
    while (auto* item = m_capesGrid->takeAt(0)) {
        if (auto* w = item->widget())
            w->deleteLater();
        delete item;
    }
    auto account = APPLICATION->accounts()->defaultAccount();
    const bool premium = account && account->accountType() == AccountType::MSA;
    QImage skin;
    bool slim = false;
    QString current;
    struct Entry {
        QString id, name;
        QImage tex;
        QString localPath;
        bool locked = false;
    };
    QList<Entry> entries;
    int row = 0;
    auto section = [this, &row](const QString& text) {
        auto* l = makeLabel(text, QStringLiteral("fpsCaption"), m_capesGrid->parentWidget());
        l->setWordWrap(true);
        m_capesGrid->addWidget(l, row++, 0, 1, 4);
    };
    if (premium) {
        auto* data = account->accountData();
        skin.loadFromData(data->minecraftProfile.skin.data);
        slim = data->minecraftProfile.skin.variant.compare("slim", Qt::CaseInsensitive) == 0;
        current = data->minecraftProfile.currentCape;
        if (m_selectedCape.isNull() && m_selectedLocalCape.isEmpty())
            m_selectedCape = current;
        entries << Entry{ QString(""), tr("Sin capa"), QImage(), QString() };
        for (const auto& cape : data->minecraftProfile.capes) {
            QImage t;
            t.loadFromData(cape.data);
            if (t.isNull())
                t = m_capeTextures.value(cape.id);
            if (t.isNull() && !cape.url.isEmpty() && !m_capeDownloads.contains(cape.id) && !m_capeFailed.contains(cape.id)) {
                // Prism no guarda la imagen de cada capa: se descarga una vez
                m_capeDownloads.insert(cape.id);
                QNetworkRequest req{ QUrl(cape.url) };
                req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
                auto* reply = APPLICATION->network()->get(req);
                const QString id = cape.id;
                connect(reply, &QNetworkReply::finished, this, [this, reply, id] {
                    reply->deleteLater();
                    QImage img;
                    if (reply->error() == QNetworkReply::NoError && img.loadFromData(reply->readAll()))
                        m_capeTextures.insert(id, img);
                    else
                        m_capeFailed.insert(id);  // no reintenta en bucle
                    m_capeDownloads.remove(id);
                    if (m_capeDownloads.isEmpty())
                        QTimer::singleShot(0, this, &FourFpsShell::refreshCapes);
                });
            }
            entries << Entry{ cape.id, cape.alias.isEmpty() ? tr("Capa") : cape.alias, t, QString() };
        }
    }
    const int officialCount = entries.size();
    // capas exclusivas: solo para quien tiene la medalla (pack4fps/exclusivas/<medalla>/*.png)
    const QStringList owned = ownedBadges();
    const QString exclRoot = QCoreApplication::applicationDirPath() + "/pack4fps/exclusivas";
    for (const auto& badgeDir : QDir(exclRoot).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        for (const auto& fi : QDir(badgeDir.absoluteFilePath()).entryInfoList({ "*.png" }, QDir::Files, QDir::Name)) {
            const QImage t = lightCape(QImage(fi.absoluteFilePath()));
            if (t.isNull())
                continue;
            Entry e{ QString(), fi.completeBaseName(), t, fi.absoluteFilePath() };
            e.locked = !owned.contains(badgeDir.fileName());
            entries << e;
        }
    }
    const int exclusiveEnd = entries.size();
    // capas muy pesadas (animadas largas / 4K): se apartan a capas/pesadas para que la galeria no se trabe
    {
        const QString heavyDir = localCapesDir() + "/pesadas";
        for (const auto& fi : QDir(localCapesDir()).entryInfoList({ "*.png" }, QDir::Files)) {
            if (fi.size() > kMaxCapeBytes) {
                QDir().mkpath(heavyDir);
                QFile::rename(fi.absoluteFilePath(), heavyDir + "/" + fi.fileName());
            }
        }
    }
    for (const auto& fi : QDir(localCapesDir()).entryInfoList({ "*.png" }, QDir::Files, QDir::Name)) {
        const QString key = fi.absoluteFilePath() + "|" + QString::number(fi.lastModified().toMSecsSinceEpoch());
        QImage t = s_capeTexCache.value(key);
        if (t.isNull()) {
            t = lightCape(QImage(fi.absoluteFilePath()));
            if (t.isNull())
                continue;
            s_capeTexCache.insert(key, t);
        }
        QString name = fi.completeBaseName();
        name.remove(QRegularExpression(" - Cosmetica texture$"));
        name.remove(QRegularExpression("_[0-9a-f]{32}( \\(\\d+\\))?$"));
        name.remove(QRegularExpression("^cape_"));
        name.replace('_', ' ');
        entries << Entry{ QString(), name, t, fi.absoluteFilePath() };
    }

    // catalogo oficial: lista de MineSkin, imagenes desde los servidores de Mojang (no se incluyen en el launcher)
    const int localEnd = entries.size();
    if (!m_officialLoaded && !m_officialLoading) {
        m_officialLoading = true;
        QNetworkRequest req{ QUrl(QStringLiteral("https://api.mineskin.org/v2/capes")) };
        req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("4fps-launcher/1.0"));
        req.setTransferTimeout(20000);
        auto* reply = APPLICATION->network()->get(req);
        connect(reply, &QNetworkReply::finished, this, [this, reply] {
            reply->deleteLater();
            m_officialLoading = false;
            const auto doc = QJsonDocument::fromJson(reply->readAll());
            const auto arr = doc.isArray() ? doc.array() : doc.object().value("capes").toArray();
            for (const auto& v : arr) {
                const auto o = v.toObject();
                QString url = o.value("url").toString();
                if (url.startsWith("http://"))
                    url.replace(0, 7, "https://");
                if (!url.isEmpty())
                    m_officialCapes << qMakePair(o.value("alias").toString(), url);
            }
            m_officialLoaded = true;
            m_officialError = m_officialCapes.isEmpty();
            refreshCapes();
        });
    }
    for (const auto& oc : m_officialCapes) {
        const QString url = oc.second;
        QImage t = m_capeTextures.value(url);
        if (t.isNull() && !m_capeDownloads.contains(url) && !m_capeFailed.contains(url)) {
            m_capeDownloads.insert(url);
            QNetworkRequest req{ QUrl(url) };
            req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
            auto* reply = APPLICATION->network()->get(req);
            connect(reply, &QNetworkReply::finished, this, [this, reply, url] {
                reply->deleteLater();
                QImage img;
                if (reply->error() == QNetworkReply::NoError && img.loadFromData(reply->readAll()))
                    m_capeTextures.insert(url, lightCape(img));
                else
                    m_capeFailed.insert(url);
                m_capeDownloads.remove(url);
                if (m_capeDownloads.isEmpty())
                    QTimer::singleShot(0, this, &FourFpsShell::refreshCapes);
            });
        }
        entries << Entry{ QString(), oc.first.isEmpty() ? tr("Capa oficial") : oc.first, t, "official:" + url };
    }

    auto* group = new QButtonGroup(m_capesGrid->parentWidget());
    if (!premium)
        section(tr("CAPAS DE TU CUENTA: inicia sesión con una cuenta premium para verlas y cambiarlas."));
    int col = 0;
    for (int i = 0; i < entries.size(); i++) {
        const auto& e = entries[i];
        if (i == 0 && premium) {
            section(tr("CAPAS DE TU CUENTA"));
            col = 0;
        }
        if (i == officialCount && exclusiveEnd > officialCount) {
            if (col != 0)
                row++;
            section(tr("EXCLUSIVAS · solo para el Equipo 4fps"));
            col = 0;
        }
        if (i == exclusiveEnd) {
            if (col != 0)
                row++;
            section(tr("MIS CAPAS · render y juego (solo 4fps)"));
            col = 0;
        }
        if (i == localEnd) {
            if (col != 0)
                row++;
            section(tr("CATÁLOGO OFICIAL DE MINECRAFT · render y juego (solo 4fps)"));
            col = 0;
        }
        const bool isLocal = !e.localPath.isEmpty();
        SkinRenderOptions o;
        o.background = 0;
        o.shadow = false;
        o.slim = slim;
        o.yaw = 155;
        o.pitch = 6;
        o.zoom = 1.3;
        SkinRenderer::applyPreset(0, o);
        auto* tile = new QToolButton(m_capesGrid->parentWidget());
        tile->setObjectName(QStringLiteral("fpsWardrobeTile"));
        tile->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        tile->setCheckable(true);
        tile->setChecked(isLocal ? e.localPath == m_selectedLocalCape : (m_selectedLocalCape.isEmpty() && e.id == m_selectedCape));
        tile->setFixedSize(150, 186);
        tile->setCursor(Qt::PointingHandCursor);
        {
            const QString tkey = QString::number(e.tex.cacheKey()) + "|" + QString::number(skin.cacheKey()) + (slim ? "s" : "c") +
                                 (isLocal ? e.localPath : e.id);
            QPixmap pm = s_capeThumbCache.value(tkey);
            if (pm.isNull()) {
                pm = QPixmap::fromImage(SkinRenderer::renderImage(140, skin, e.tex, o));
                if (s_capeThumbCache.size() > 600)
                    s_capeThumbCache.clear();
                s_capeThumbCache.insert(tkey, pm);
            }
            tile->setIcon(pm);
        }
        tile->setIconSize(QSize(140, 140));
        QString label = (!isLocal && e.id == current && premium) ? e.name + "  ·  " + tr("En uso") : e.name;
        if (e.locked) {
            label = tr("Bloqueada");
            tile->setEnabled(false);
            QPixmap pm = tile->icon().pixmap(140, 140);
            QPainter lp(&pm);
            lp.fillRect(pm.rect(), QColor(13, 13, 14, 150));
            lp.end();
            tile->setIcon(pm);
        }
        tile->setText(tile->fontMetrics().elidedText(label, Qt::ElideRight, 140));
        tile->setToolTip(e.locked ? tr("%1 · exclusiva del Equipo 4fps (canjea el código del equipo en Perfil)").arg(e.name) : e.name);
        group->addButton(tile);
        const QString id = e.id, name = e.name, path = e.localPath;
        const QImage tex = e.tex;
        connect(tile, &QToolButton::clicked, this, [this, id, name, tex, path] {
            m_selectedLocalCape = path;
            if (path.isEmpty())
                m_selectedCape = id;
            m_capePreview->setSkin(m_capePreview->skin, tex);
            m_capeName->setText(name);
            updateCapeButton();
        });
        if (tile->isChecked()) {
            m_capePreview->options.slim = slim;
            m_capePreview->setSkin(skin, e.tex);
            m_capeName->setText(e.name);
        }
        m_capesGrid->addWidget(tile, row, col);
        if (++col == 4) {
            col = 0;
            row++;
        }
    }
    if (col != 0)
        row++;
    if (localEnd == exclusiveEnd)
        section(tr("No tienes capas personales todavía. Usa \"Importar capas (PNG)\"."));
    if (entries.size() == localEnd) {
        if (col != 0)
            row++;
        section(m_officialLoading ? tr("CATÁLOGO OFICIAL DE MINECRAFT · cargando...")
                                  : tr("CATÁLOGO OFICIAL DE MINECRAFT · no se pudo cargar (revisa tu conexión)."));
    }
    if (m_capePreview->skin.isNull())
        m_capePreview->setSkin(skin, m_capePreview->cape);
    updateCapeButton();
}
