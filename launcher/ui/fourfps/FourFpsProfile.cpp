// 4fps - Perfil tipo Discord, medallas (por server o por código) y notificaciones (campanita)
#include "FourFpsShell.h"
#include "FourFpsFeatures.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include "Application.h"
#include "minecraft/auth/AccountData.h"
#include "minecraft/auth/AccountList.h"
#include "minecraft/auth/MinecraftAccount.h"

using namespace fps;

namespace {
// ------------------------------------------------------------------ catálogo de medallas
// Las direcciones y los códigos se guardan como SHA-256 para que no se puedan leer dentro del launcher
// (sirve para eventos con IP privada). Para agregar una medalla: añade una entrada aquí.
struct BadgeDef {
    const char* id;
    const char* name;
    const char* desc;     // lo que dice el perfil
    const char* howTo;    // cómo se consigue (se muestra si está bloqueada)
    const char* c1;
    const char* c2;
    const char* glyph;
    QList<QByteArray> hostHashes;  // sha256(host en minúsculas, sin puerto)
    QList<QByteArray> codeHashes;  // sha256(código en mayúsculas, sin espacios)
};

QByteArray sha(const QString& s)
{
    return QCryptographicHash::hash(s.toUtf8(), QCryptographicHash::Sha256).toHex();
}

const QList<BadgeDef>& badges()
{
    static const QList<BadgeDef> list = {
        { "equipo", "Equipo 4fps", "Forma parte del equipo de 4fps (launcher y servidores)", "Exclusiva para el equipo de 4fps",
          "#fde68a", "#b45309", "4", {}, {} },
        { "bienvenida", "Jugador 4fps", "Usa el launcher 4fps", "Se consigue al abrir 4fps por primera vez", "#e4e4e7", "#71717a", "4",
          {}, {} },
        { "permaquistan", "Permaquistán", "Jugó en el servidor Permaquistán", "Añade el server Permaquistán en Servers o canjea su código", "#f59e0b", "#b45309", "P",
          {}, {} },
        { "panaquistan", "Panaquistán", "Participó en el evento Panaquistán", "Evento: canjea el código de Panaquistán",
          "#22c55e", "#15803d", "P1", {}, {} },
        { "panaquistan2", "Panaquistán 2", "Participó en el evento Panaquistán 2", "Evento privado: únete con la IP del evento o canjea su código",
          "#a855f7", "#6d28d9", "P2", {}, {} },
    };
    static bool init = false;
    if (!init) {
        init = true;
        auto& l = const_cast<QList<BadgeDef>&>(list);
        // permaquistan.serverprism.online (se guarda solo el hash)
        // Equipo 4fps: solo por código (el código no queda escrito en el launcher)
        l[0].codeHashes = { QByteArray("b64fbb23ab3a2241668f1d82d5c336f94084f7f7cb41c59715decaa243a9ea07") /* sha256 */ };
        l[2].hostHashes = { QByteArray("5265804969b20364a73a05dad850e512950532d282d56f25b8e57a35e9f6d441") /* sha256 */ };
        // Permaquistán: también por código
        l[2].codeHashes = { QByteArray("af27ac40c7bf9bcdd3675116e1c060978bba508e68154f48fe9b0b7c071ae08c") /* sha256 */ };
        // Panaquistán (1): por código
        l[3].codeHashes = { QByteArray("66cff8e31251615253332b2df45d7dc1d6629f75f33cefd3aa4f7a6ada63dc9b") /* sha256 */ };
        // Panaquistán 2: código de canje (la IP privada del evento se agrega cuando la tengas)
        l[4].codeHashes = { QByteArray("7114fe68d75c3c133c9dc8f461238d335beadd39d76ce85e7b0c6a7e01076a68") /* sha256 */ };
    }
    return list;
}

const BadgeDef* badgeById(const QString& id)
{
    for (const auto& b : badges())
        if (QString::fromUtf8(b.id) == id)
            return &b;
    return nullptr;
}

QString profileFile()
{
    return APPLICATION->dataRoot() + "/perfil.json";
}

QJsonObject loadProfile()
{
    QFile f(profileFile());
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}

void saveProfile(const QJsonObject& o)
{
    QFile f(profileFile());
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(o).toJson());
}

QString normalizeHost(QString ip)
{
    ip = ip.trimmed().toLower();
    if (ip.startsWith('[')) {  // IPv6 [::1]:25565
        const int e = ip.indexOf(']');
        return e > 0 ? ip.mid(1, e - 1) : ip;
    }
    if (ip.count(':') == 1)
        ip = ip.left(ip.indexOf(':'));
    while (ip.endsWith('.'))
        ip.chop(1);
    return ip;
}
}  // namespace

// ------------------------------------------------------------------ icono de medalla (dibujo propio)
QPixmap FourFpsShell::badgeIcon(const QString& id, int size, bool locked)
{
    const BadgeDef* b = badgeById(id);
    QPixmap pm(size * 2, size * 2);
    pm.setDevicePixelRatio(2);
    pm.fill(Qt::transparent);
    if (!b)
        return pm;
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    QColor c1(b->c1), c2(b->c2);
    if (locked) {
        c1 = QColor("#3a3a40");
        c2 = QColor("#26262b");
    }
    // escudo
    QPainterPath path;
    const double s = size;
    path.moveTo(s * 0.5, s * 0.04);
    path.lineTo(s * 0.92, s * 0.2);
    path.lineTo(s * 0.86, s * 0.62);
    path.quadTo(s * 0.78, s * 0.84, s * 0.5, s * 0.97);
    path.quadTo(s * 0.22, s * 0.84, s * 0.14, s * 0.62);
    path.lineTo(s * 0.08, s * 0.2);
    path.closeSubpath();
    QLinearGradient g(0, 0, 0, s);
    g.setColorAt(0, c1);
    g.setColorAt(1, c2);
    p.fillPath(path, g);
    p.setPen(QPen(locked ? QColor("#52525b") : c1.lighter(130), qMax(1.0, s / 18)));
    p.drawPath(path);
    QFont f;
    f.setBold(true);
    f.setPixelSize(int(s * (QString::fromUtf8(b->glyph).size() > 1 ? 0.34 : 0.46)));
    p.setFont(f);
    p.setPen(locked ? QColor("#71717a") : QColor(255, 255, 255, 235));
    p.drawText(QRectF(0, s * 0.06, s, s * 0.82), Qt::AlignCenter, QString::fromUtf8(b->glyph));
    return pm;
}

QStringList FourFpsShell::ownedBadges()
{
    QStringList out;
    for (const auto& v : loadProfile().value("badges").toArray())
        out << v.toObject().value("id").toString();
    return out;
}

bool FourFpsShell::grantBadge(const QString& id, const QString& reason)
{
    const BadgeDef* b = badgeById(id);
    if (!b || ownedBadges().contains(id))
        return false;
    QJsonObject o = loadProfile();
    QJsonArray arr = o.value("badges").toArray();
    const QString now = QDateTime::currentDateTime().toString(Qt::ISODate);
    arr.append(QJsonObject{ { "id", id }, { "date", now } });
    o["badges"] = arr;
    QJsonArray notes = o.value("notifications").toArray();
    notes.prepend(QJsonObject{ { "badge", id }, { "text", reason }, { "date", now }, { "read", false } });
    while (notes.size() > 50)
        notes.removeLast();
    o["notifications"] = notes;
    saveProfile(o);
    updateBell();
    if (m_profilePage && m_stack && m_stack->currentWidget() == m_profilePage)
        refreshProfile();
    return true;
}

void FourFpsShell::awardForServer(const QString& address)
{
    const QByteArray h = sha(normalizeHost(address));
    for (const auto& b : badges())
        if (b.hostHashes.contains(h))
            grantBadge(QString::fromUtf8(b.id), tr("Te ha llegado tu emblema por participar en %1").arg(QString::fromUtf8(b.name)));
}

void FourFpsShell::redeemCode(const QString& codeIn)
{
    QString code = codeIn.trimmed().toUpper();
    code.remove(' ');
    if (code.isEmpty())
        return;
    const QByteArray h = sha(code);
    for (const auto& b : badges()) {
        if (!b.codeHashes.contains(h))
            continue;
        const QString id = QString::fromUtf8(b.id);
        if (ownedBadges().contains(id)) {
            QMessageBox::information(this, tr("Canjear código"), tr("Ya tienes la medalla \"%1\".").arg(QString::fromUtf8(b.name)));
            return;
        }
        grantBadge(id, tr("Canjeaste un código: emblema de %1").arg(QString::fromUtf8(b.name)));
        QMessageBox::information(this, tr("Canjear código"), tr("¡Listo! Ganaste la medalla \"%1\".").arg(QString::fromUtf8(b.name)));
        return;
    }
    QMessageBox::warning(this, tr("Canjear código"), tr("Ese código no existe o está mal escrito."));
}

// ------------------------------------------------------------------ campanita
QIcon FourFpsShell::bellIcon(int unread)
{
    QPixmap pm(64, 64);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.scale(2, 2);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor("#d4d4d8"));
    QPainterPath bell;
    bell.moveTo(16, 6);
    bell.cubicTo(10, 6, 8, 11, 8, 15);
    bell.lineTo(8, 20);
    bell.lineTo(5.5, 23.5);
    bell.lineTo(26.5, 23.5);
    bell.lineTo(24, 20);
    bell.lineTo(24, 15);
    bell.cubicTo(24, 11, 22, 6, 16, 6);
    p.drawPath(bell);
    p.drawEllipse(QPointF(16, 26.5), 3, 3);
    p.drawEllipse(QPointF(16, 5), 1.8, 1.8);
    if (unread > 0) {
        p.setBrush(QColor("#ef4444"));
        p.drawEllipse(QPointF(24, 8), 7, 7);
        QFont f;
        f.setBold(true);
        f.setPixelSize(9);
        p.setFont(f);
        p.setPen(Qt::white);
        p.drawText(QRectF(17, 1, 14, 14), Qt::AlignCenter, unread > 9 ? "9+" : QString::number(unread));
    }
    return QIcon(pm);
}

void FourFpsShell::updateBell()
{
    if (!m_bell)
        return;
    int unread = 0;
    for (const auto& v : loadProfile().value("notifications").toArray())
        if (!v.toObject().value("read").toBool())
            unread++;
    m_bell->setIcon(bellIcon(unread));
    m_bell->setToolTip(QStringLiteral("<b>%1</b><br>%2").arg(tr("Notificaciones"), unread ? tr("Tienes %n nueva(s): medallas y avisos.", "", unread) : tr("Aquí llegan tus medallas y avisos.")));
}

void FourFpsShell::showNotifications()
{
    QJsonObject o = loadProfile();
    QJsonArray notes = o.value("notifications").toArray();

    auto* pop = new QFrame(this, Qt::Popup);
    pop->setAttribute(Qt::WA_DeleteOnClose);
    pop->setObjectName(QStringLiteral("fpsNotifPopup"));
    pop->setStyleSheet(QStringLiteral("QFrame#fpsNotifPopup{background:#161618;border:1px solid #2e2e33;border-radius:10px;}"
                                      "QFrame#fpsNotifItem{background:#1e1e21;border-radius:8px;}"
                                      "QFrame#fpsNotifItem[unread=\"true\"]{background:#26262b;border:1px solid #3f3f46;}"));
    pop->setFixedWidth(340);
    auto* v = new QVBoxLayout(pop);
    v->setContentsMargins(14, 14, 14, 14);
    v->setSpacing(8);
    v->addWidget(makeLabel(tr("NOTIFICACIONES"), QStringLiteral("fpsCaption"), pop));
    if (notes.isEmpty())
        v->addWidget(makeLabel(tr("No tienes notificaciones todavía."), QStringLiteral("fpsMuted"), pop));
    const QLocale es(QLocale::Spanish);
    int shown = 0;
    for (int i = 0; i < notes.size() && shown < 8; i++, shown++) {
        const auto n = notes[i].toObject();
        auto* item = new QFrame(pop);
        item->setObjectName(QStringLiteral("fpsNotifItem"));
        item->setProperty("unread", !n.value("read").toBool());
        auto* h = new QHBoxLayout(item);
        h->setContentsMargins(10, 8, 10, 8);
        h->setSpacing(10);
        auto* ic = new QLabel(item);
        ic->setPixmap(badgeIcon(n.value("badge").toString(), 34));
        ic->setFixedSize(34, 34);
        h->addWidget(ic, 0, Qt::AlignTop);
        auto* tv = new QVBoxLayout();
        tv->setSpacing(2);
        auto* t = makeLabel(n.value("text").toString(), QStringLiteral("fpsRowTitle"), item);
        t->setWordWrap(true);
        tv->addWidget(t);
        tv->addWidget(makeLabel(es.toString(QDateTime::fromString(n.value("date").toString(), Qt::ISODate), QLocale::ShortFormat),
                                QStringLiteral("fpsHint"), item));
        h->addLayout(tv, 1);
        v->addWidget(item);
        auto obj = n;
        obj["read"] = true;
        notes[i] = obj;
    }
    auto* goProfile = makeButton(tr("Ver mi perfil"), QString(), true, pop);
    connect(goProfile, &QPushButton::clicked, this, [this, pop] {
        pop->close();
        showPage(8);
    });
    v->addWidget(goProfile);
    o["notifications"] = notes;
    saveProfile(o);
    QTimer::singleShot(0, this, &FourFpsShell::updateBell);

    pop->adjustSize();
    pop->move(m_bell->mapToGlobal(QPoint(0, m_bell->height() + 6)));
    pop->show();
}

// ------------------------------------------------------------------ página de perfil
namespace {
QPixmap headFromSkin(const QImage& skin, int size)
{
    QImage head(8, 8, QImage::Format_ARGB32);
    head.fill(QColor("#3a3a40"));
    if (!skin.isNull() && skin.width() >= 64) {
        const int k = skin.width() / 64;
        QImage base = skin.copy(8 * k, 8 * k, 8 * k, 8 * k).scaled(8, 8);
        QImage hat = skin.copy(40 * k, 8 * k, 8 * k, 8 * k).scaled(8, 8);
        QPainter p(&head);
        p.drawImage(0, 0, base);
        p.drawImage(0, 0, hat);
    }
    QPixmap out(size, size);
    out.fill(Qt::transparent);
    QPainter p(&out);
    p.setRenderHint(QPainter::Antialiasing);
    QPainterPath clip;
    clip.addEllipse(QRectF(0, 0, size, size));
    p.setClipPath(clip);
    p.fillRect(out.rect(), QColor("#2c2c31"));
    const int inner = int(size * 0.72);
    p.drawImage(QRect((size - inner) / 2, (size - inner) / 2, inner, inner), head.scaled(inner, inner, Qt::IgnoreAspectRatio, Qt::FastTransformation));
    return out;
}

class ProfileBanner : public QWidget {
   public:
    QPixmap image;
    QPixmap avatar;
    explicit ProfileBanner(QWidget* parent) : QWidget(parent) { setFixedHeight(170); }

   protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        const QRect band(0, 0, width(), 118);
        QPainterPath clip;
        clip.addRoundedRect(QRectF(band).adjusted(0, 0, 0, 12), 12, 12);
        p.save();
        p.setClipPath(clip);
        p.setClipRect(band, Qt::IntersectClip);
        if (!image.isNull()) {
            const QSize sz = image.size().scaled(band.size(), Qt::KeepAspectRatioByExpanding);
            p.drawPixmap(QRect(QPoint((band.width() - sz.width()) / 2, (band.height() - sz.height()) / 2), sz), image);
        } else {
            QLinearGradient g(0, 0, width(), band.height());
            g.setColorAt(0, QColor("#3f3f46"));
            g.setColorAt(1, QColor("#18181b"));
            p.fillRect(band, g);
            const int cell = 10;  // pixeles decorativos
            uint s = 7;
            for (int x = 0; x < width(); x += cell)
                for (int y = band.height() - 30; y < band.height(); y += cell) {
                    s = s * 1103515245u + 12345u;
                    if ((s >> 16) % 3 == 0)
                        p.fillRect(x, y, cell, cell, QColor(255, 255, 255, 14 + int((s >> 20) % 20)));
                }
        }
        p.restore();
        // avatar con aro del color del fondo
        const int d = 104;
        const QRect ar(22, band.height() - d / 2 - 6, d, d);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor("#121214"));
        p.drawEllipse(ar.adjusted(-6, -6, 6, 6));
        if (!avatar.isNull())
            p.drawPixmap(ar, avatar);
    }
};
}  // namespace

QWidget* FourFpsShell::buildProfilePage()
{
    auto* page = new QWidget(this);
    page->setObjectName(QStringLiteral("fpsPage"));
    m_profilePage = page;
    auto* outer = new QVBoxLayout(page);
    outer->setContentsMargins(32, 28, 32, 24);
    outer->setSpacing(18);
    outer->addWidget(header(tr("Perfil"), tr("Tu tarjeta de jugador y tus medallas."), {}));

    auto* scroll = new QScrollArea(page);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* host = new QWidget(scroll);
    host->setObjectName(QStringLiteral("fpsGridHost"));
    scroll->setWidget(host);
    m_profileLayout = new QVBoxLayout(host);
    m_profileLayout->setContentsMargins(0, 0, 0, 0);
    m_profileLayout->setSpacing(18);
    m_profileLayout->setAlignment(Qt::AlignTop);
    outer->addWidget(scroll, 1);
    return page;
}

void FourFpsShell::refreshProfile()
{
    if (!m_profileLayout)
        return;
    while (auto* item = m_profileLayout->takeAt(0)) {
        if (auto* w = item->widget())
            w->deleteLater();
        delete item;
    }
    QWidget* host = m_profileLayout->parentWidget();
    const QJsonObject o = loadProfile();
    auto account = APPLICATION->accounts()->defaultAccount();
    const QString user = account ? fps::streamSafe(account->profileName(), tr("Jugador")) : tr("Sin cuenta");
    const QString display = o.value("displayName").toString().isEmpty() ? user : o.value("displayName").toString();
    QImage skin;
    if (account && account->accountType() == AccountType::MSA)
        skin.loadFromData(account->accountData()->minecraftProfile.skin.data);
    const QStringList owned = ownedBadges();

    auto* row = new QHBoxLayout();
    row->setSpacing(20);
    // ---- tarjeta tipo Discord
    auto* card = makeCard(host);
    card->setFixedWidth(400);
    auto* cv = new QVBoxLayout(card);
    cv->setContentsMargins(0, 0, 0, 18);
    cv->setSpacing(6);
    auto* banner = new ProfileBanner(card);
    const QString bannerPath = o.value("banner").toString();
    if (!bannerPath.isEmpty())
        banner->image.load(bannerPath);
    banner->avatar = headFromSkin(skin, 104);
    cv->addWidget(banner);
    auto* body = new QVBoxLayout();
    body->setContentsMargins(20, 0, 20, 0);
    body->setSpacing(6);
    auto* nameL = makeLabel(display, QStringLiteral("fpsTitle"), card);
    body->addWidget(nameL);
    body->addWidget(makeLabel(user + (account && account->accountType() == AccountType::MSA ? QStringLiteral("  •  Premium") : QStringLiteral("  •  No premium")),
                              QStringLiteral("fpsMuted"), card));
    // fila de medallas
    auto* badgeRow = new QHBoxLayout();
    badgeRow->setSpacing(4);
    auto* badgeBox = new QFrame(card);
    badgeBox->setStyleSheet(QStringLiteral("QFrame{background:#0d0d0e;border-radius:8px;}"));
    badgeBox->setLayout(badgeRow);
    badgeRow->setContentsMargins(8, 5, 8, 5);
    for (const auto& id : owned) {
        const BadgeDef* b = badgeById(id);
        if (!b)
            continue;
        auto* l = new QLabel(badgeBox);
        l->setPixmap(badgeIcon(id, 24));
        l->setToolTip(QStringLiteral("<b>%1</b><br>%2").arg(QString::fromUtf8(b->name), QString::fromUtf8(b->desc)));
        badgeRow->addWidget(l);
    }
    if (owned.isEmpty())
        badgeRow->addWidget(makeLabel(tr("Sin medallas todavía"), QStringLiteral("fpsHint"), badgeBox));
    auto* br = new QHBoxLayout();
    br->addWidget(badgeBox);
    br->addStretch();
    body->addLayout(br);
    body->addSpacing(6);
    body->addWidget(makeLabel(tr("SOBRE MÍ"), QStringLiteral("fpsCaption"), card));
    QString bio = o.value("bio").toString();
    auto* bioL = makeLabel(bio.isEmpty() ? tr("Todavía no escribiste nada sobre ti.") : bio, QStringLiteral("fpsRowTitle"), card);
    bioL->setWordWrap(true);
    bioL->setTextInteractionFlags(Qt::TextSelectableByMouse);
    body->addWidget(bioL);
    body->addSpacing(8);
    auto* edit = makeButton(tr("Editar perfil"), QStringLiteral("rename"), false, card);
    connect(edit, &QPushButton::clicked, this, [this] {
        QJsonObject o = loadProfile();
        QDialog dlg(this);
        dlg.setWindowTitle(tr("Editar perfil"));
        dlg.setMinimumWidth(420);
        auto* form = new QFormLayout(&dlg);
        auto* name = new QLineEdit(o.value("displayName").toString(), &dlg);
        name->setPlaceholderText(tr("Tu nombre de Minecraft"));
        name->setMaxLength(32);
        auto* bio = new QPlainTextEdit(o.value("bio").toString(), &dlg);
        bio->setPlaceholderText(tr("Cuéntale a la gente quién eres..."));
        bio->setFixedHeight(110);
        QString bannerPath = o.value("banner").toString();
        auto* bannerBtn = new QPushButton(bannerPath.isEmpty() ? tr("Elegir imagen...") : tr("Cambiar imagen..."), &dlg);
        auto* bannerClear = new QPushButton(tr("Quitar"), &dlg);
        auto* bh = new QHBoxLayout();
        bh->addWidget(bannerBtn, 1);
        bh->addWidget(bannerClear);
        const QString target = APPLICATION->dataRoot() + "/perfil-banner.png";
        connect(bannerBtn, &QPushButton::clicked, &dlg, [&] {
            const QString f = QFileDialog::getOpenFileName(&dlg, tr("Banner del perfil"), QStandardPaths::writableLocation(QStandardPaths::PicturesLocation),
                                                           tr("Imágenes (*.png *.jpg *.jpeg *.webp *.bmp *.gif)"));
            QImage img(f);
            if (img.isNull())
                return;
            img.scaled(QSize(1000, 300), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation).save(target, "PNG");
            bannerPath = target;
            bannerBtn->setText(tr("Imagen elegida ✓"));
        });
        connect(bannerClear, &QPushButton::clicked, &dlg, [&] {
            bannerPath.clear();
            bannerBtn->setText(tr("Elegir imagen..."));
        });
        form->addRow(tr("Nombre visible"), name);
        form->addRow(tr("Banner"), bh);
        form->addRow(tr("Sobre mí"), bio);
        auto* bb = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dlg);
        form->addRow(bb);
        connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
        connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
        if (dlg.exec() != QDialog::Accepted)
            return;
        o["displayName"] = name->text().trimmed();
        o["bio"] = bio->toPlainText().trimmed().left(400);
        o["banner"] = bannerPath;
        saveProfile(o);
        refreshProfile();
    });
    auto* er = new QHBoxLayout();
    er->addWidget(edit);
    er->addStretch();
    body->addLayout(er);
    cv->addLayout(body);
    row->addWidget(card, 0, Qt::AlignTop);

    // ---- medallas y canje
    auto* side = new QVBoxLayout();
    side->setSpacing(18);
    side->addWidget(fps::buildEventCard(host));
    auto* medals = makeCard(host);
    auto* mv = new QVBoxLayout(medals);
    mv->setContentsMargins(18, 18, 18, 18);
    mv->setSpacing(10);
    mv->addWidget(makeLabel(tr("MEDALLAS · %1 de %2").arg(owned.size()).arg(badges().size()), QStringLiteral("fpsCaption"), medals));
    const QLocale es(QLocale::Spanish);
    QHash<QString, QString> dates;
    for (const auto& v : o.value("badges").toArray())
        dates.insert(v.toObject().value("id").toString(), v.toObject().value("date").toString());
    for (const auto& b : badges()) {
        const QString id = QString::fromUtf8(b.id);
        const bool has = owned.contains(id);
        auto* it = new QHBoxLayout();
        it->setSpacing(12);
        auto* ic = new QLabel(medals);
        ic->setPixmap(badgeIcon(id, 40, !has));
        ic->setFixedSize(40, 40);
        it->addWidget(ic);
        auto* tv = new QVBoxLayout();
        tv->setSpacing(1);
        tv->addWidget(makeLabel(QString::fromUtf8(b.name), QStringLiteral("fpsRowTitle"), medals));
        tv->addWidget(makeLabel(has ? tr("Conseguida el %1").arg(es.toString(QDateTime::fromString(dates.value(id), Qt::ISODate).date(), QLocale::ShortFormat))
                                    : QString::fromUtf8(b.howTo),
                                QStringLiteral("fpsHint"), medals));
        it->addLayout(tv, 1);
        mv->addLayout(it);
    }
    side->addWidget(medals);

    auto* redeem = makeCard(host);
    auto* rv = new QVBoxLayout(redeem);
    rv->setContentsMargins(18, 18, 18, 18);
    rv->setSpacing(10);
    rv->addWidget(makeLabel(tr("CANJEAR CÓDIGO"), QStringLiteral("fpsCaption"), redeem));
    rv->addWidget(makeLabel(tr("¿Te dieron un código en un evento? Escríbelo aquí para ganar su medalla."), QStringLiteral("fpsHint"), redeem));
    auto* rh = new QHBoxLayout();
    auto* codeEdit = new QLineEdit(redeem);
    codeEdit->setObjectName(QStringLiteral("fpsInput"));
    codeEdit->setPlaceholderText(QStringLiteral("XXXX-XXXX"));
    auto* go = makeButton(tr("Canjear"), QString(), true, redeem);
    connect(go, &QPushButton::clicked, this, [this, codeEdit] { redeemCode(codeEdit->text()); });
    connect(codeEdit, &QLineEdit::returnPressed, go, &QPushButton::click);
    rh->addWidget(codeEdit, 1);
    rh->addWidget(go);
    rv->addLayout(rh);
    side->addWidget(redeem);
    side->addStretch();
    row->addLayout(side, 1);
    m_profileLayout->addLayout(row);
}
