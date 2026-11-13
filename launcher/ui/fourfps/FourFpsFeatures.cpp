// 4fps - funciones extra (parte 1): ajustes, ping, conflictos de mods, crashes, hardware/modo PC lento, grupos
#include "FourFpsFeatures.h"
#include "FourFpsShell.h"


#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QClipboard>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDirIterator>
#include <QDnsLookup>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHash>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSettings>
#include <QSpinBox>
#include <QTcpSocket>
#include <QTextBrowser>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <memory>

#include "Application.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/PackProfile.h"
#include "settings/SettingsObject.h"

#include <archive.h>
#include <archive_entry.h>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace fps {

// ================================================================= ajustes
void registerSettings()
{
    auto* s = APPLICATION->settings();
    auto reg = [s](const char* k, const QVariant& def) {
        if (!s->contains(k))
            s->registerSetting(k, def);
    };
    reg(cfg::ConflictCheck, true);
    reg(cfg::CrashExplainer, true);
    reg(cfg::ModGroups, true);
    reg(cfg::LiveServers, true);
    reg(cfg::LiveServerList, QStringList{ QStringLiteral("Permaquistán|permaquistan.serverprism.online|evento") });
    reg(cfg::Events, true);
    reg(cfg::EventsUrl, QStringLiteral("https://raw.githubusercontent.com/nofranzip/4fps-launcher/main/eventos.json"));
    reg(cfg::Gallery, true);
    reg(cfg::StreamMode, false);
    reg(cfg::SlowPc, true);
    reg(cfg::DiscordRpc, true);
    reg(cfg::DiscordAppId, QString());
    reg(cfg::Lyrics, false);
    reg(cfg::LyricsOffset, 0);
    reg(cfg::LyricsOverlay, true);
    reg(cfg::LyricsPort, 47323);
    reg(cfg::Accent, QStringLiteral("#ffffff"));
    reg(cfg::LyricsPosition, QStringLiteral("arriba-izquierda"));
    reg(cfg::LyricsSize, 0);
    reg(cfg::BgImage, QString());
    reg(cfg::BgDim, 55);
}

QVariant setting(const char* key)
{
    registerSettings();
    return APPLICATION->settings()->get(key);
}
bool enabled(const char* key)
{
    return setting(key).toBool();
}
void setSetting(const char* key, const QVariant& v)
{
    registerSettings();
    APPLICATION->settings()->set(key, v);
    emit Bus::get()->settingsChanged();
}

Bus* Bus::get()
{
    static Bus* b = new Bus();
    return b;
}

bool streamMode()
{
    return enabled(cfg::StreamMode);
}
QString streamSafe(const QString& text, const QString& replacement)
{
    return streamMode() ? replacement : text;
}

QIcon SettingsPage::icon() const
{
    return QIcon(QStringLiteral(":/org.prismlauncher.PrismLauncher.svg"));
}

SettingsPage::SettingsPage(QWidget* parent) : QWidget(parent)
{
    registerSettings();
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* host = new QWidget(scroll);
    scroll->setWidget(host);
    outer->addWidget(scroll);
    auto* v = new QVBoxLayout(host);

    auto* g1 = new QGroupBox(tr("Funciones del launcher"), host);
    auto* g1v = new QVBoxLayout(g1);
    const QList<QPair<const char*, QString>> items = {
        { cfg::ConflictCheck, tr("Revisar conflictos de mods antes de jugar") },
        { cfg::CrashExplainer, tr("Explicar los crashes en español") },
        { cfg::ModGroups, tr("Grupos de mods (actualizar juntos)") },
        { cfg::LiveServers, tr("Estado de mis servidores en la portada") },
        { cfg::Events, tr("Próximo evento en el Perfil") },
        { cfg::Gallery, tr("Galería de clips y capturas") },
        { cfg::StreamMode, tr("Modo stream (ocultar IPs, cuenta y datos privados)") },
        { cfg::SlowPc, tr("Botón \"Modo potato\" (rendimiento) en las instancias") },
        { cfg::DiscordRpc, tr("Mostrar \"Jugando en 4fps\" en Discord") },
        { cfg::Lyrics, tr("Modo música (canción que suena y letra si existe)") },
        { cfg::LyricsOverlay, tr("Música: mostrar la canción sobre el juego") },
    };
    for (const auto& it : items) {
        auto* c = new QCheckBox(it.second, g1);
        c->setChecked(setting(it.first).toBool());
        g1v->addWidget(c);
        m_checks << qMakePair(it.first, c);
    }
    v->addWidget(g1);

    auto* gc = new QGroupBox(tr("Color del launcher"), host);
    auto* gcv = new QHBoxLayout(gc);
    auto* colorHint = new QLabel(tr("Cambia el color de los botones y pon una imagen de fondo en la parte central. El menú y la barra de abajo no cambian."), gc);
    colorHint->setWordWrap(true);
    gcv->addWidget(colorHint, 1);
    auto* pick = new QPushButton(tr("Color y fondo..."), gc);
    connect(pick, &QPushButton::clicked, this, [this] { openAccentPicker(this); });
    gcv->addWidget(pick);
    v->addWidget(gc);

    auto* g2 = new QGroupBox(tr("Mis servidores (portada)"), host);
    auto* g2v = new QVBoxLayout(g2);
    auto* hint = new QLabel(tr("Uno por línea, como  Nombre|direccion  . Agrega |evento al final si el server solo abre en eventos (ej: Permaquistán|permaquistan.serverprism.online|evento)"), g2);
    hint->setWordWrap(true);
    g2v->addWidget(hint);
    m_servers = new QPlainTextEdit(g2);
    m_servers->setPlainText(setting(cfg::LiveServerList).toStringList().join('\n'));
    m_servers->setFixedHeight(110);
    g2v->addWidget(m_servers);
    v->addWidget(g2);

    auto* g3 = new QGroupBox(tr("Discord, eventos y letras"), host);
    auto* form = new QFormLayout(g3);
    m_discordId = new QLineEdit(setting(cfg::DiscordAppId).toString(), g3);
    m_discordId->setPlaceholderText(tr("ID de la aplicación de Discord (Developer Portal)"));
    form->addRow(tr("Discord App ID"), m_discordId);
    m_eventsUrl = new QLineEdit(setting(cfg::EventsUrl).toString(), g3);
    form->addRow(tr("JSON de eventos"), m_eventsUrl);
    m_offset = new QSpinBox(g3);
    m_offset->setRange(-10000, 10000);
    m_offset->setSingleStep(100);
    m_offset->setSuffix(QStringLiteral(" ms"));
    m_offset->setValue(setting(cfg::LyricsOffset).toInt());
    form->addRow(tr("Desfase de las letras"), m_offset);
    m_lyricsPos = new QComboBox(g3);
    const QList<QPair<QString, QString>> positions = { { "arriba-izquierda", tr("Arriba a la izquierda") }, { "arriba-centro", tr("Arriba al centro") },
                                                       { "arriba-derecha", tr("Arriba a la derecha") },   { "abajo-centro", tr("Abajo al centro") },
                                                       { "abajo-izquierda", tr("Abajo a la izquierda") } };
    for (const auto& p : positions)
        m_lyricsPos->addItem(p.second, p.first);
    m_lyricsPos->setCurrentIndex(qMax(0, m_lyricsPos->findData(setting(cfg::LyricsPosition).toString())));
    form->addRow(tr("Posición de la música"), m_lyricsPos);
    m_lyricsSize = new QComboBox(g3);
    m_lyricsSize->addItems({ tr("Chica"), tr("Mediana"), tr("Grande") });
    m_lyricsSize->setCurrentIndex(qBound(0, setting(cfg::LyricsSize).toInt(), 2));
    form->addRow(tr("Tamaño de la música"), m_lyricsSize);
    v->addWidget(g3);
    v->addStretch();
}

bool SettingsPage::apply()
{
    auto* s = APPLICATION->settings();
    for (const auto& c : m_checks)
        s->set(c.first, c.second->isChecked());
    QStringList servers;
    for (const auto& l : m_servers->toPlainText().split('\n'))
        if (!l.trimmed().isEmpty())
            servers << l.trimmed();
    s->set(cfg::LiveServerList, servers);
    s->set(cfg::DiscordAppId, m_discordId->text().trimmed());
    s->set(cfg::EventsUrl, m_eventsUrl->text().trimmed());
    s->set(cfg::LyricsOffset, m_offset->value());
    s->set(cfg::LyricsPosition, m_lyricsPos->currentData().toString());
    s->set(cfg::LyricsSize, m_lyricsSize->currentIndex());
    emit Bus::get()->settingsChanged();
    return true;
}

// ================================================================= ping (Server List Ping)
QString PingResult::playersText() const
{
    return players < 0 ? QString() : QStringLiteral("%1/%2").arg(players).arg(maxPlayers);
}

namespace {
QByteArray varInt(int v)
{
    QByteArray out;
    quint32 u = static_cast<quint32>(v);
    do {
        quint8 b = u & 0x7f;
        u >>= 7;
        if (u)
            b |= 0x80;
        out.append(char(b));
    } while (u);
    return out;
}
QByteArray packet(const QByteArray& body)
{
    return varInt(body.size()) + body;
}
QString chatToText(const QJsonValue& v)
{
    if (v.isString())
        return v.toString();
    if (!v.isObject())
        return {};
    const auto o = v.toObject();
    QString out = o.value("text").toString();
    for (const auto& e : o.value("extra").toArray())
        out += chatToText(e);
    return out;
}
}  // namespace

void pingServer(QObject* ctx, const QString& address, std::function<void(PingResult)> done)
{
    QString host = address.trimmed();
    int port = 25565;
    bool explicitPort = false;
    if (!host.startsWith('[') && host.count(':') == 1) {
        port = host.section(':', 1).toInt();
        host = host.section(':', 0, 0);
        explicitPort = true;
    }
    auto finished = std::make_shared<bool>(false);
    auto finish = [finished, done](PingResult r) {
        if (*finished)
            return;
        *finished = true;
        done(r);
    };
    auto connectTo = [ctx, finish](const QString& target, int targetPort, const QString& handshakeHost) {
        auto* sock = new QTcpSocket(ctx);
        auto buf = std::make_shared<QByteArray>();
        auto timer = std::make_shared<QElapsedTimer>();
        auto result = std::make_shared<PingResult>();
        auto stage = std::make_shared<int>(0);
        QTimer::singleShot(6000, sock, [sock, finish, result] {
            finish(*result);
            sock->deleteLater();
        });
        QObject::connect(sock, &QTcpSocket::connected, sock, [sock, handshakeHost, targetPort] {
            QByteArray hs;
            hs += varInt(0x00);
            hs += varInt(-1);
            const QByteArray h = handshakeHost.toUtf8();
            hs += varInt(h.size()) + h;
            hs += char((targetPort >> 8) & 0xff);
            hs += char(targetPort & 0xff);
            hs += varInt(1);
            sock->write(packet(hs) + packet(varInt(0x00)));
        });
        QObject::connect(sock, &QTcpSocket::readyRead, sock, [sock, buf, finish, timer, result, stage] {
            buf->append(sock->readAll());
            auto readVar = [&](int& pos, bool& ok) {
                quint32 val = 0;
                for (int i = 0; i < 5; i++) {
                    if (pos >= buf->size()) {
                        ok = false;
                        return 0;
                    }
                    const quint8 b = quint8(buf->at(pos++));
                    val |= quint32(b & 0x7f) << (7 * i);
                    if (!(b & 0x80))
                        return int(val);
                }
                ok = false;
                return 0;
            };
            int pos = 0;
            bool ok = true;
            const int len = readVar(pos, ok);
            if (!ok || buf->size() < pos + len)
                return;
            if (*stage == 1) {  // respuesta al ping: listo
                result->pingMs = int(timer->elapsed());
                finish(*result);
                sock->deleteLater();
                return;
            }
            readVar(pos, ok);
            const int jlen = readVar(pos, ok);
            if (!ok || buf->size() < pos + jlen)
                return;
            const auto obj = QJsonDocument::fromJson(buf->mid(pos, jlen)).object();
            result->online = true;
            const QString fav = obj.value("favicon").toString();
            const int comma = fav.indexOf(',');
            if (comma > 0)
                result->icon.loadFromData(QByteArray::fromBase64(fav.mid(comma + 1).toLatin1()), "PNG");
            const auto pl = obj.value("players").toObject();
            if (!pl.isEmpty()) {
                result->players = pl.value("online").toInt();
                result->maxPlayers = pl.value("max").toInt();
            }
            result->version = obj.value("version").toObject().value("name").toString();
            result->motd = chatToText(obj.value("description")).remove(QRegularExpression("§.")).simplified();
            // medir ping
            buf->clear();
            *stage = 1;
            timer->start();
            QByteArray p = varInt(0x01);
            for (int i = 0; i < 8; i++)
                p.append(char(0));
            sock->write(packet(p));
        });
        QObject::connect(sock, &QTcpSocket::errorOccurred, sock, [sock, finish, result](QAbstractSocket::SocketError) {
            finish(*result);
            sock->deleteLater();
        });
        sock->connectToHost(target, targetPort);
    };
    if (explicitPort) {
        connectTo(host, port, host);
        return;
    }
    auto* dns = new QDnsLookup(QDnsLookup::SRV, "_minecraft._tcp." + host, ctx);
    QObject::connect(dns, &QDnsLookup::finished, dns, [dns, connectTo, host] {
        const auto recs = dns->serviceRecords();
        if (dns->error() == QDnsLookup::NoError && !recs.isEmpty())
            connectTo(recs.first().target(), recs.first().port(), host);
        else
            connectTo(host, 25565, host);
        dns->deleteLater();
    });
    dns->lookup();
}

// ================================================================= lectura de jars
namespace {
struct JarInfo {
    QString topFile;   // archivo .jar en la carpeta mods
    QString jarName;   // nombre del jar (anidado o el mismo)
    QString modId, modName, version;
    QJsonObject depends, breaks;
    QStringList provides;
    QSet<QString> packages;  // paquetes de 3 niveles con clases
    bool nested = false;
};

void scanJarBytes(const QByteArray& data, const QString& topFile, const QString& jarName, bool nested, int depth, QList<JarInfo>& out)
{
    struct archive* a = archive_read_new();
    archive_read_support_format_zip(a);
    if (archive_read_open_memory(a, data.constData(), size_t(data.size())) != ARCHIVE_OK) {
        archive_read_free(a);
        return;
    }
    JarInfo info;
    info.topFile = topFile;
    info.jarName = jarName;
    info.nested = nested;
    QList<QPair<QString, QByteArray>> inner;
    struct archive_entry* e;
    while (archive_read_next_header(a, &e) == ARCHIVE_OK) {
        const QString name = QString::fromUtf8(archive_entry_pathname(e));
        if (name == "fabric.mod.json") {
            QByteArray buf(int(archive_entry_size(e)), 0);
            if (archive_entry_size(e) > 0)
                archive_read_data(a, buf.data(), size_t(buf.size()));
            const auto o = QJsonDocument::fromJson(buf).object();
            info.modId = o.value("id").toString();
            info.modName = o.value("name").toString();
            info.version = o.value("version").toString();
            info.depends = o.value("depends").toObject();
            info.breaks = o.value("breaks").toObject();
            for (const auto& p : o.value("provides").toArray())
                info.provides << p.toString();
        } else if (depth < 3 && name.startsWith("META-INF/jars/") && name.endsWith(".jar")) {
            const qint64 sz = archive_entry_size(e);
            if (sz > 0 && sz < 300 * 1024 * 1024) {
                QByteArray buf(int(sz), 0);
                la_ssize_t got = 0, n;
                while (got < sz && (n = archive_read_data(a, buf.data() + got, size_t(sz - got))) > 0)
                    got += n;
                inner << qMakePair(QFileInfo(name).fileName(), buf);
            }
        } else if (name.endsWith(".class") && !name.startsWith("META-INF/")) {
            const QStringList parts = name.split('/');
            if (parts.size() >= 4)  // paquete de al menos 3 niveles
                info.packages.insert(parts.mid(0, 3).join('/'));
        }
    }
    archive_read_free(a);
    out << info;
    for (const auto& in : inner)
        scanJarBytes(in.second, topFile, in.first, true, depth + 1, out);
}

struct ScanCache {
    QString key;
    QList<JarInfo> jars;
};
QHash<QString, ScanCache>& scanCache()
{
    static QHash<QString, ScanCache> c;
    return c;
}

QList<JarInfo> scanMods(const QString& modsDir)
{
    QDir dir(modsDir);
    const auto files = dir.entryInfoList({ "*.jar" }, QDir::Files, QDir::Name);
    QString key;
    for (const auto& f : files)
        key += f.fileName() + QString::number(f.size()) + QString::number(f.lastModified().toMSecsSinceEpoch()) + ';';
    auto& cache = scanCache()[modsDir];
    if (cache.key == key)
        return cache.jars;
    QList<JarInfo> out;
    for (const auto& f : files) {
        QFile file(f.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly))
            continue;
        scanJarBytes(file.readAll(), f.fileName(), f.fileName(), false, 0, out);
    }
    cache.key = key;
    cache.jars = out;
    return out;
}

// ---- versiones al estilo SemVer de Fabric
int compareVersions(const QString& aIn, const QString& bIn)
{
    auto split = [](QString v, QStringList& core, QStringList& pre) {
        v = v.section('+', 0, 0);
        const int dash = v.indexOf('-');
        if (dash >= 0) {
            pre = v.mid(dash + 1).split('.');
            v = v.left(dash);
        }
        core = v.split('.');
    };
    QStringList ac, ap, bc, bp;
    split(aIn, ac, ap);
    split(bIn, bc, bp);
    auto cmpPart = [](const QString& x, const QString& y) {
        bool okx, oky;
        const qlonglong nx = x.toLongLong(&okx), ny = y.toLongLong(&oky);
        if (okx && oky)
            return nx < ny ? -1 : nx > ny ? 1 : 0;
        if (okx != oky)
            return okx ? -1 : 1;
        return QString::compare(x, y);
    };
    for (int i = 0; i < qMax(ac.size(), bc.size()); i++) {
        const int c = cmpPart(i < ac.size() ? ac[i] : "0", i < bc.size() ? bc[i] : "0");
        if (c)
            return c;
    }
    if (ap.isEmpty() != bp.isEmpty())
        return ap.isEmpty() ? 1 : -1;  // la versión sin "-beta" es mayor
    for (int i = 0; i < qMax(ap.size(), bp.size()); i++) {
        if (i >= ap.size())
            return -1;
        if (i >= bp.size())
            return 1;
        const int c = cmpPart(ap[i], bp[i]);
        if (c)
            return c;
    }
    return 0;
}

bool matchesSingle(const QString& version, QString pred)
{
    pred = pred.trimmed();
    if (pred.isEmpty() || pred == "*")
        return true;
    auto starts = [&](const char* p) { return pred.startsWith(QLatin1String(p)); };
    if (starts(">="))
        return compareVersions(version, pred.mid(2)) >= 0;
    if (starts("<="))
        return compareVersions(version, pred.mid(2)) <= 0;
    if (starts(">"))
        return compareVersions(version, pred.mid(1)) > 0;
    if (starts("<"))
        return compareVersions(version, pred.mid(1)) < 0;
    if (starts("="))
        return compareVersions(version, pred.mid(1)) == 0;
    if (starts("~")) {  // misma menor
        const QString base = pred.mid(1);
        const QStringList p = base.split('.');
        return compareVersions(version, base) >= 0 && version.section('.', 0, 1) == QStringList(p.mid(0, 2)).join('.');
    }
    if (starts("^")) {  // misma mayor
        const QString base = pred.mid(1);
        return compareVersions(version, base) >= 0 && version.section('.', 0, 0) == base.section('.', 0, 0);
    }
    if (pred.contains('x') || pred.contains('X')) {  // 1.21.x
        QString prefix = pred;
        prefix.remove(QRegularExpression("[.]?[xX*].*$"));
        return version == prefix || version.startsWith(prefix + '.');
    }
    return compareVersions(version, pred) == 0;
}

// "*", ">=1.0 <2.0", o arreglo (OR)
bool matches(const QString& version, const QJsonValue& predicate)
{
    if (predicate.isArray()) {
        for (const auto& p : predicate.toArray())
            if (matches(version, p))
                return true;
        return predicate.toArray().isEmpty();
    }
    for (const auto& part : predicate.toString().split(' ', Qt::SkipEmptyParts))
        if (!matchesSingle(version, part))
            return false;
    return true;
}

QString predicateText(const QJsonValue& p)
{
    if (p.isArray()) {
        QStringList l;
        for (const auto& x : p.toArray())
            l << predicateText(x);
        return l.join(QObject::tr(" o "));
    }
    QStringList parts;
    for (QString t : p.toString().split(' ', Qt::SkipEmptyParts)) {
        if (t == "*")
            parts << QObject::tr("cualquier versión");
        else if (t.startsWith(">="))
            parts << QObject::tr("%1 o más nueva").arg(t.mid(2));
        else if (t.startsWith("<="))
            parts << QObject::tr("%1 o más vieja").arg(t.mid(2));
        else if (t.startsWith('>'))
            parts << QObject::tr("más nueva que %1").arg(t.mid(1));
        else if (t.startsWith('<'))
            parts << QObject::tr("más vieja que %1").arg(t.mid(1));
        else if (t.startsWith('~') || t.startsWith('^'))
            parts << QObject::tr("%1 (compatible)").arg(t.mid(1));
        else
            parts << t.remove('=');
    }
    return parts.isEmpty() ? QObject::tr("cualquier versión") : parts.join(QObject::tr(" y "));
}

QString niceMod(const JarInfo& j)
{
    QString n = j.modName.isEmpty() ? (j.modId.isEmpty() ? j.jarName : j.modId) : j.modName;
    if (!j.version.isEmpty())
        n += ' ' + j.version;
    return n;
}

// "javacv-1.5.14.jar" -> ("javacv", "1.5.14")
QPair<QString, QString> splitJarName(QString n)
{
    n.remove(QRegularExpression("\\.jar$"));
    const auto m = QRegularExpression("^(.*?)[-_]v?(\\d[\\w.+-]*)$").match(n);
    if (m.hasMatch())
        return { m.captured(1), m.captured(2) };
    return { n, QString() };
}
}  // namespace

// ================================================================= detector de conflictos
QList<ConflictIssue> scanModConflicts(MinecraftInstance* inst)
{
    QList<ConflictIssue> issues;
    if (!inst)
        return issues;
    const auto jars = scanMods(inst->modsRoot());
    auto* pp = inst->getPackProfile();
    const QString mc = pp ? pp->getComponentVersion("net.minecraft") : QString();
    const QString loader = pp ? pp->getComponentVersion("net.fabricmc.fabric-loader") : QString();

    // mods presentes (top-level y jar-in-jar con id; Fabric usa la versión más nueva de cada id)
    QHash<QString, JarInfo> present;
    for (const auto& j : jars) {
        if (j.modId.isEmpty())
            continue;
        const QStringList ids = QStringList{ j.modId } + j.provides;
        for (const auto& id : ids) {
            if (!present.contains(id) || compareVersions(j.version, present[id].version) > 0)
                present[id] = j;
        }
    }
    auto versionOf = [&](const QString& id, bool& found) -> QString {
        found = true;
        if (id == "minecraft") {
            found = !mc.isEmpty();
            return mc;
        }
        if (id == "fabricloader" || id == "fabric-loader") {
            found = !loader.isEmpty();
            return loader;
        }
        if (id == "java")
            return QString();
        if (present.contains(id))
            return present[id].version;
        found = false;
        return QString();
    };

    // b) depends / breaks (solo de mods top-level, que es lo que el usuario puede tocar)
    for (const auto& j : jars) {
        if (j.nested || j.modId.isEmpty())
            continue;
        for (auto it = j.depends.begin(); it != j.depends.end(); ++it) {
            const QString dep = it.key();
            if (dep == "java")
                continue;
            bool found;
            const QString v = versionOf(dep, found);
            if (!found) {
                if (dep == "fabricloader" || dep == "minecraft")
                    continue;  // instancia sin Fabric: lo maneja Prism
                issues << ConflictIssue{ QObject::tr("A %1 le falta un mod").arg(niceMod(j)),
                                         QObject::tr("Necesita \"%1\" (%2) y no está instalado.").arg(dep, predicateText(it.value())),
                                         QObject::tr("Instala %1 desde la pestaña Mods o desactiva %2.").arg(dep, j.modName.isEmpty() ? j.modId : j.modName),
                                         { j.topFile } };
            } else if (!v.isEmpty() && !matches(v, it.value())) {
                const QString what = dep == "fabricloader" ? QStringLiteral("Fabric Loader") : dep == "minecraft" ? QStringLiteral("Minecraft") : present.value(dep).modName.isEmpty() ? dep : present.value(dep).modName;
                issues << ConflictIssue{ QObject::tr("%1 no funciona con tu versión de %2").arg(niceMod(j), what),
                                         QObject::tr("Pide %1 %2, pero tienes la %3.").arg(what, predicateText(it.value()), v),
                                         dep == "fabricloader" ? QObject::tr("Actualiza Fabric Loader en la instancia (Editar → Versión) o usa otra versión de %1.").arg(j.modName)
                                                               : QObject::tr("Cambia la versión de %1 o de %2, o desactiva uno de los dos.").arg(j.modName.isEmpty() ? j.modId : j.modName, what),
                                         { j.topFile, present.value(dep).topFile } };
            }
        }
        for (auto it = j.breaks.begin(); it != j.breaks.end(); ++it) {
            bool found;
            const QString v = versionOf(it.key(), found);
            if (found && !v.isEmpty() && matches(v, it.value())) {
                const auto other = present.value(it.key());
                issues << ConflictIssue{ QObject::tr("%1 choca con %2").arg(niceMod(j), niceMod(other)),
                                         QObject::tr("%1 declara que rompe %2 %3.").arg(j.modName.isEmpty() ? j.modId : j.modName, other.modName.isEmpty() ? it.key() : other.modName, predicateText(it.value())),
                                         QObject::tr("Actualiza %1 a una versión más nueva o desactiva uno de los dos.").arg(other.modName.isEmpty() ? it.key() : other.modName),
                                         { j.topFile, other.topFile } };
            }
        }
    }

    // c) mods pesados en PCs débiles
    if (enabled(cfg::SlowPc)) {
        const Hardware hw = detectHardware();
        if (hw.weak()) {
            for (const auto& j : jars) {
                if (j.nested)
                    continue;
                const QString r = heavyModReason(j.modId.isEmpty() ? j.topFile : j.modId);
                if (!r.isEmpty())
                    issues << ConflictIssue{ QObject::tr("%1 puede no correrte bien").arg(niceMod(j)),
                                             QObject::tr("%1 %2. Tu PC tiene %3 GB de RAM%4.")
                                                 .arg(j.modName.isEmpty() ? j.modId : j.modName, r)
                                                 .arg(QString::number(hw.ramMb / 1024.0, 'f', 0))
                                                 .arg(hw.gpu.isEmpty() ? QString() : QObject::tr(" y %1").arg(hw.gpu)),
                                             QObject::tr("Desactívalo o usa el \"Modo potato\" de la instancia, que lo apaga por ti."),
                                             { j.topFile } };
            }
        }
    }

    // a) librerías duplicadas sin relocalizar (mismo paquete en jars de mods distintos)
    struct Owner {
        QString top, jar, id, version;
    };
    QHash<QString, QList<Owner>> byPackage;
    static const QStringList ignore = { "net/fabricmc", "net/minecraft", "com/mojang", "kotlin", "kotlinx", "org/spongepowered", "com/llamalad7" };
    for (const auto& j : jars) {
        for (const auto& p : j.packages) {
            bool skip = false;
            for (const auto& ig : ignore)
                if (p == ig || p.startsWith(ig + '/'))
                    skip = true;
            if (!skip)
                byPackage[p] << Owner{ j.topFile, j.jarName, j.modId, j.version.isEmpty() ? splitJarName(j.jarName).second : j.version };
        }
    }
    QSet<QString> reported;
    for (auto it = byPackage.begin(); it != byPackage.end(); ++it) {
        const auto& owners = it.value();
        for (int a = 0; a < owners.size(); a++) {
            for (int b = a + 1; b < owners.size(); b++) {
                const auto& x = owners[a];
                const auto& y = owners[b];
                if (x.top == y.top)
                    continue;
                if (!x.id.isEmpty() && x.id == y.id)
                    continue;  // mismo mod anidado en dos lados: Fabric elige uno
                if (x.version == y.version && splitJarName(x.jar).first == splitJarName(y.jar).first)
                    continue;  // la misma librería y versión: no hay choque
                const QString key = QStringList({ x.top, y.top }).join('|') + it.key().section('/', 0, 1);
                if (reported.contains(key))
                    continue;
                reported.insert(key);
                const QString pkg = QString(it.key()).replace('/', '.');
                auto describe = [](const Owner& o) {
                    QString s = o.top;
                    if (o.jar != o.top)
                        s += QObject::tr(" (dentro trae %1)").arg(o.jar);
                    return s;
                };
                issues << ConflictIssue{ QObject::tr("Librería repetida: %1").arg(pkg),
                                         QObject::tr("%1 y %2 traen su propia copia de %3 en versiones distintas (%4 y %5). Java solo carga una, y el otro mod puede crashear con NoSuchMethodError o ClassNotFound.")
                                             .arg(describe(x), describe(y), pkg, x.version.isEmpty() ? "?" : x.version, y.version.isEmpty() ? "?" : y.version),
                                         QObject::tr("Desactiva uno de los dos mods mientras uses el otro, o busca versiones que usen la misma versión de la librería."),
                                         { x.top, y.top } };
            }
        }
    }
    return issues;
}

namespace {
QString issuesHtml(const QList<ConflictIssue>& issues, bool full)
{
    QString html;
    for (const auto& i : issues) {
        html += QStringLiteral("<p><b>%1</b><br>%2").arg(i.title.toHtmlEscaped(), i.detail.toHtmlEscaped());
        html += QStringLiteral("<br><span style='color:#a1a1aa'>%1 %2</span>").arg(QObject::tr("Qué hacer:"), i.fix.toHtmlEscaped());
        if (full && !i.mods.isEmpty())
            html += QStringLiteral("<br><span style='color:#71717a'>%1 %2</span>").arg(QObject::tr("Archivos:"), i.mods.join(", ").toHtmlEscaped());
        html += "</p>";
    }
    return html;
}
}  // namespace

bool confirmLaunch(QWidget* parent, MinecraftInstance* inst)
{
    if (!inst || !enabled(cfg::ConflictCheck))
        return true;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const auto issues = scanModConflicts(inst);
    QApplication::restoreOverrideCursor();
    if (issues.isEmpty())
        return true;
    QDialog dlg(parent);
    dlg.setWindowTitle(QObject::tr("Posibles conflictos de mods"));
    dlg.setMinimumSize(560, 380);
    auto* v = new QVBoxLayout(&dlg);
    auto* head = new QLabel(QObject::tr("<b>Encontré %n problema(s) en los mods de \"%1\".</b><br>Si juegas así, es probable que el juego crashee o que algo no funcione.", "", issues.size())
                                .arg(inst->name().toHtmlEscaped()),
                            &dlg);
    head->setWordWrap(true);
    v->addWidget(head);
    auto* text = new QTextBrowser(&dlg);
    text->setHtml(issuesHtml(issues, false));
    v->addWidget(text, 1);
    auto* bb = new QDialogButtonBox(&dlg);
    auto* launch = bb->addButton(QObject::tr("Lanzar igual"), QDialogButtonBox::AcceptRole);
    auto* details = bb->addButton(QObject::tr("Ver detalles"), QDialogButtonBox::ActionRole);
    auto* folder = bb->addButton(QObject::tr("Abrir carpeta de mods"), QDialogButtonBox::ActionRole);
    bb->addButton(QObject::tr("Cancelar"), QDialogButtonBox::RejectRole);
    v->addWidget(bb);
    QObject::connect(details, &QPushButton::clicked, &dlg, [text, issues] { text->setHtml(issuesHtml(issues, true)); });
    QObject::connect(folder, &QPushButton::clicked, &dlg, [inst] { QDesktopServices::openUrl(QUrl::fromLocalFile(inst->modsRoot())); });
    QObject::connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    Q_UNUSED(launch);
    return dlg.exec() == QDialog::Accepted;
}

// ================================================================= explicador de crashes
namespace {
QString readTail(const QString& path, qint64 maxBytes)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    if (f.size() > maxBytes)
        f.seek(f.size() - maxBytes);
    return QString::fromUtf8(f.readAll());
}

// busca a qué mod pertenece un paquete o clase (a.b.c.D)
QString modForClass(const QList<JarInfo>& jars, const QString& cls, QString* file = nullptr)
{
    const QString path = QString(cls).replace('.', '/');
    const QStringList parts = path.split('/');
    if (parts.size() < 4)
        return {};
    const QString pkg = parts.mid(0, 3).join('/');
    for (const auto& j : jars) {
        if (j.packages.contains(pkg)) {
            if (file)
                *file = j.topFile;
            // devolver el mod top-level dueño
            for (const auto& t : jars)
                if (!t.nested && t.topFile == j.topFile)
                    return t.modId.isEmpty() ? t.topFile : t.modId;
        }
    }
    return {};
}

QString translateFabricSolution(QString l)
{
    l = l.trimmed();
    l.remove(QRegularExpression("^[-\\s]+"));
    static const QList<QPair<QString, QString>> rep = {
        { "Install ", "Instala " }, { "Replace ", "Reemplaza " }, { "Remove ", "Quita " }, { "Upgrade ", "Actualiza " },
        { " with ", " por " }, { "any version", "cualquier versión" }, { " or later", " o más nueva" }, { " or earlier", " o más vieja" },
        { "version ", "versión " }, { "requires ", "necesita " }, { "is incompatible with", "es incompatible con" },
        { "which is missing!", "que no está instalado" }, { "but only the wrong version is present:", "pero tienes otra versión:" },
        { "Mod ", "El mod " }, { "between ", "entre " }, { " and ", " y " }, { "(inclusive)", "(incluida)" }, { "(exclusive)", "(sin incluir)" },
    };
    for (const auto& r : rep)
        l.replace(r.first, r.second);
    return l;
}
}  // namespace

CrashExplanation explainCrash(MinecraftInstance* inst, const QDateTime& since)
{
    CrashExplanation ex;
    if (!inst)
        return ex;
    const QString root = inst->gameRoot();
    // reporte de crash más nuevo desde que se lanzó
    QString report;
    QFileInfo newest;
    for (const auto& fi : QDir(root + "/crash-reports").entryInfoList({ "crash-*.txt" }, QDir::Files, QDir::Time)) {
        if (fi.lastModified() >= since.addSecs(-5)) {
            newest = fi;
            break;
        }
    }
    const QString log = readTail(root + "/logs/latest.log", 400 * 1024);
    if (newest.exists()) {
        report = readTail(newest.absoluteFilePath(), 600 * 1024);
        ex.reportPath = newest.absoluteFilePath();
    } else {
        ex.reportPath = root + "/logs/latest.log";
    }
    const QString text = report.isEmpty() ? log : report + "\n" + log;
    const bool crashed = !report.isEmpty() || log.contains("Incompatible mods found") || log.contains("OutOfMemoryError") ||
                         log.contains("Exception in thread \"main\"") || log.contains("has crashed") || log.contains("MixinTransformerError") ||
                         log.contains("UnsupportedClassVersionError");
    if (!crashed)
        return ex;
    ex.found = true;
    const auto jars = scanMods(inst->modsRoot());

    // líneas útiles (sin los avisos genéricos de "fue hecho para Fabric Loader X")
    QStringList lines;
    for (const auto& l : text.split('\n'))
        if (!l.contains("made for Fabric Loader", Qt::CaseInsensitive) && !l.contains("CrashAssistant", Qt::CaseInsensitive))
            lines << l;
    const QString clean = lines.join('\n');

    QRegularExpressionMatch m;
    if (clean.contains("OutOfMemoryError")) {
        ex.title = QObject::tr("Minecraft se quedó sin memoria RAM");
        ex.cause = QObject::tr("La instancia necesitó más RAM de la que tiene asignada (OutOfMemoryError).");
        ex.fix = QObject::tr("Sube la RAM de la instancia en Instancias → panel derecho (RAM), o usa el \"Modo potato\" para quitar mods pesados y bajar la distancia de render.");
    } else if ((m = QRegularExpression("UnsupportedClassVersionError.*?class file version (\\d+)").match(clean)).hasMatch()) {
        const int java = m.captured(1).toInt() - 44;
        ex.title = QObject::tr("Java incorrecto");
        ex.cause = QObject::tr("Un mod o el juego necesita Java %1, y la instancia está usando uno más viejo.").arg(java);
        ex.fix = QObject::tr("En Ajustes → Java (o en la instancia) elige Java %1 o activa la descarga automática de Java.").arg(java);
    } else if (clean.contains("Incompatible mods found") || clean.contains("Incompatible mod set")) {
        ex.title = QObject::tr("Fabric encontró mods incompatibles");
        QStringList sol;
        const int idx = clean.indexOf("A potential solution has been determined");
        const QString part = idx >= 0 ? clean.mid(idx, 3000) : clean.mid(clean.indexOf("Incompatible mod"), 3000);
        for (const auto& l : part.split('\n').mid(1, 12)) {
            const QString t = l.trimmed();
            if (t.startsWith('-') || t.startsWith("Install") || t.startsWith("Replace") || t.startsWith("Remove") || t.startsWith("Mod "))
                sol << QStringLiteral("• ") + translateFabricSolution(t);
            if (t.startsWith("Unmet") || t.startsWith("More details") || t.startsWith("at "))
                break;
        }
        ex.cause = QObject::tr("Algún mod necesita otro mod u otra versión que no tienes.");
        ex.fix = sol.isEmpty() ? QObject::tr("Revisa los mods que nombra el reporte y cambia sus versiones.") : QObject::tr("Fabric propone:\n%1").arg(sol.join('\n'));
        const auto mm = QRegularExpression("Mod '([^']+)' \\(([\\w.-]+)\\)").match(part);
        if (mm.hasMatch())
            ex.culpritModId = mm.captured(2);
    } else if ((m = QRegularExpression("(?:Mixin apply for mod |from mod )([\\w.-]+)").match(clean)).hasMatch() || clean.contains("MixinTransformerError") ||
               clean.contains("InjectionError")) {
        QString mod = m.hasMatch() ? m.captured(1) : QString();
        if (mod.isEmpty()) {
            const auto mc = QRegularExpression("([\\w.-]+)\\.mixins?\\.json").match(clean);
            if (mc.hasMatch())
                mod = mc.captured(1).section('.', 0, 0);
        }
        QString target;
        const auto tm = QRegularExpression("(?:into|->|target[: ]+)\\s*L?([a-z][\\w]*(?:[./][\\w$]+){2,})").match(clean);
        if (tm.hasMatch())
            target = modForClass(jars, QString(tm.captured(1)).replace('/', '.'));
        ex.title = QObject::tr("Un mod no pudo modificar el juego (Mixin)");
        ex.culpritModId = mod;
        auto nameOf = [&jars](const QString& id) {
            for (const auto& j : jars)
                if (!j.nested && j.modId == id && !j.modName.isEmpty())
                    return j.modName;
            return id;
        };
        mod = nameOf(mod);
        target = nameOf(target);
        ex.cause = target.isEmpty() || target == mod
                       ? QObject::tr("El mod \"%1\" falló al inyectar su código (MixinTransformerError/InjectionError). Normalmente es una versión que no es compatible con la de Minecraft o con otro mod.")
                             .arg(mod.isEmpty() ? QObject::tr("desconocido") : mod)
                       : QObject::tr("El mod \"%1\" falló al inyectar código en \"%2\". %1 no es compatible con la versión de %2 que tienes.").arg(mod, target);
        ex.fix = target.isEmpty() || target == mod ? QObject::tr("Actualiza o desactiva \"%1\".").arg(mod)
                                                   : QObject::tr("Cambia la versión de %1 o de %2 para que coincidan, o desactiva %1.").arg(mod, target);
    } else if ((m = QRegularExpression("(NoSuchMethodError|NoSuchFieldError|NoClassDefFoundError|ClassNotFoundException)[: ]+'?([\\w$./]+)").match(clean)).hasMatch()) {
        QString cls = m.captured(2);
        cls.replace('/', '.');
        cls.remove(QRegularExpression("^L"));
        // clase dueña de un método "a.b.C.metodo(...)": quitar el método
        const auto own = QRegularExpression("^([\\w.$]+)\\.[a-z]\\w*$").match(cls);
        const QString clsOwner = own.hasMatch() ? own.captured(1) : cls;
        QStringList owners;
        const QString pkg = clsOwner.split('.').mid(0, 3).join('/');
        for (const auto& j : jars)
            if (j.packages.contains(pkg)) {
                QString t = j.topFile;
                if (j.nested)
                    t += QObject::tr(" (dentro: %1)").arg(j.jarName);
                if (!owners.contains(t))
                    owners << t;
            }
        // quién llamó: primer "at" de un mod
        QString caller;
        for (const auto& l : clean.split('\n')) {
            const auto at = QRegularExpression("^\\s*at ([\\w.$]+)\\.").match(l);
            if (at.hasMatch()) {
                caller = modForClass(jars, at.captured(1), &ex.culpritFile);
                if (!caller.isEmpty())
                    break;
            }
        }
        ex.culpritModId = caller;
        ex.title = QObject::tr("Falta una clase o método (%1)").arg(m.captured(1));
        if (owners.size() >= 2) {
            ex.cause = QObject::tr("La librería %1 viene repetida en varios mods con versiones distintas:\n%2\nJava cargó una versión y %3 esperaba otra.")
                           .arg(QString(pkg).replace('/', '.'), QStringLiteral("• ") + owners.join(QStringLiteral("\n• ")), caller.isEmpty() ? QObject::tr("un mod") : caller);
            ex.fix = QObject::tr("Desactiva uno de esos mods mientras usas el otro (por ejemplo, el que no necesites en esta partida).");
        } else {
            ex.cause = QObject::tr("%1 buscó \"%2\" y no existe en la versión instalada. Pasa cuando un mod es para otra versión de Minecraft o de una librería.")
                           .arg(caller.isEmpty() ? QObject::tr("Un mod") : caller, cls);
            ex.fix = caller.isEmpty() ? QObject::tr("Revisa que todos los mods sean para tu versión de Minecraft.") : QObject::tr("Actualiza o desactiva %1.").arg(caller);
        }
    } else {
        // genérico: excepción principal y el primer mod en el stack
        const auto em = QRegularExpression("(?:Description: .*\\n\\s*\\n)?([\\w.$]+(?:Exception|Error))(?::\\s*(.*))?").match(clean);
        QString caller;
        for (const auto& l : clean.split('\n')) {
            const auto at = QRegularExpression("^\\s*at ([\\w.$]+)\\.").match(l);
            if (at.hasMatch()) {
                caller = modForClass(jars, at.captured(1), &ex.culpritFile);
                if (!caller.isEmpty())
                    break;
            }
        }
        ex.culpritModId = caller;
        ex.title = QObject::tr("El juego se cerró con un error");
        ex.cause = (em.hasMatch() ? QObject::tr("Error: %1").arg(em.captured(1).section('.', -1)) : QObject::tr("Hubo un error inesperado.")) +
                   (caller.isEmpty() ? QString() : QObject::tr("\nEl error pasó dentro del mod \"%1\".").arg(caller));
        ex.fix = caller.isEmpty() ? QObject::tr("Copia el reporte y revísalo, o prueba desactivando los últimos mods que agregaste.")
                                  : QObject::tr("Prueba actualizando o desactivando \"%1\".").arg(caller);
    }
    // archivo del culpable
    if (!ex.culpritModId.isEmpty() && ex.culpritFile.isEmpty())
        for (const auto& j : jars)
            if (!j.nested && (j.modId == ex.culpritModId || j.topFile.startsWith(ex.culpritModId, Qt::CaseInsensitive)))
                ex.culpritFile = j.topFile;
    ex.rawExcerpt = clean.right(6000);
    return ex;
}

void showCrashDialog(QWidget* parent, MinecraftInstance* inst, const CrashExplanation& e)
{
    auto* dlg = new QDialog(parent);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setWindowTitle(QObject::tr("El juego crasheó"));
    dlg->setMinimumSize(560, 360);
    auto* v = new QVBoxLayout(dlg);
    auto* t = new QLabel(QStringLiteral("<h3>%1</h3>").arg(e.title.toHtmlEscaped()), dlg);
    v->addWidget(t);
    auto* cause = new QLabel(QStringLiteral("<b>%1</b><br>%2").arg(QObject::tr("Qué pasó"), e.cause.toHtmlEscaped().replace('\n', "<br>")), dlg);
    cause->setWordWrap(true);
    v->addWidget(cause);
    auto* fix = new QLabel(QStringLiteral("<b>%1</b><br>%2").arg(QObject::tr("Qué hacer"), e.fix.toHtmlEscaped().replace('\n', "<br>")), dlg);
    fix->setWordWrap(true);
    v->addWidget(fix);
    if (!e.culpritFile.isEmpty())
        v->addWidget(new QLabel(QObject::tr("Mod culpable: %1").arg(e.culpritFile), dlg));
    v->addStretch();
    auto* bb = new QDialogButtonBox(dlg);
    auto* openFolder = bb->addButton(QObject::tr("Abrir carpeta del mod"), QDialogButtonBox::ActionRole);
    auto* disable = bb->addButton(QObject::tr("Desactivar mod culpable"), QDialogButtonBox::ActionRole);
    auto* copy = bb->addButton(QObject::tr("Copiar reporte"), QDialogButtonBox::ActionRole);
    bb->addButton(QObject::tr("Cerrar"), QDialogButtonBox::RejectRole);
    disable->setEnabled(!e.culpritFile.isEmpty());
    v->addWidget(bb);
    const QString modsDir = inst->modsRoot();
    QObject::connect(openFolder, &QPushButton::clicked, dlg, [modsDir] { QDesktopServices::openUrl(QUrl::fromLocalFile(modsDir)); });
    QObject::connect(disable, &QPushButton::clicked, dlg, [dlg, modsDir, e, disable] {
        if (QMessageBox::question(dlg, QObject::tr("Desactivar mod"), QObject::tr("¿Desactivar \"%1\"? Puedes volver a activarlo en la pestaña Mods.").arg(e.culpritFile)) !=
            QMessageBox::Yes)
            return;
        if (QFile::rename(modsDir + "/" + e.culpritFile, modsDir + "/" + e.culpritFile + ".disabled")) {
            disable->setEnabled(false);
            disable->setText(QObject::tr("Desactivado"));
        }
    });
    QObject::connect(copy, &QPushButton::clicked, dlg, [e] {
        QApplication::clipboard()->setText(QStringLiteral("%1\n\n%2\n\n%3\n\n--- %4 ---\n%5").arg(e.title, e.cause, e.fix, e.reportPath, e.rawExcerpt));
    });
    QObject::connect(bb, &QDialogButtonBox::rejected, dlg, &QDialog::close);
    dlg->show();
}

// ================================================================= hardware y modo PC lento
bool Hardware::weak() const
{
    return (ramMb > 0 && ramMb <= 8 * 1024 + 512) || (cores > 0 && cores <= 4) ||
           gpu.contains(QRegularExpression("Intel\\(R\\) (U?HD|Iris)|Radeon\\(TM\\) (R[2-5]|Vega [3-8]) Graphics", QRegularExpression::CaseInsensitiveOption));
}

Hardware detectHardware()
{
    static Hardware cached;
    static bool done = false;
    if (done)
        return cached;
    done = true;
    Hardware h;
    h.cores = QThread::idealThreadCount();
#ifdef Q_OS_WIN
    MEMORYSTATUSEX ms;
    ms.dwLength = sizeof(ms);
    if (GlobalMemoryStatusEx(&ms))
        h.ramMb = qint64(ms.ullTotalPhys / (1024 * 1024));
    QSettings cpu("HKEY_LOCAL_MACHINE\\HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", QSettings::NativeFormat);
    h.cpu = cpu.value("ProcessorNameString").toString().simplified();
    for (int i = 0; i < 4 && h.gpu.isEmpty(); i++) {
        QSettings gpu(QStringLiteral("HKEY_LOCAL_MACHINE\\SYSTEM\\CurrentControlSet\\Control\\Class\\{4d36e968-e325-11ce-bfc1-08002be10318}\\%1").arg(i, 4, 10, QChar('0')),
                      QSettings::NativeFormat);
        const QString d = gpu.value("DriverDesc").toString();
        if (!d.isEmpty() && !d.contains("Basic", Qt::CaseInsensitive) && !d.contains("Virtual", Qt::CaseInsensitive))
            h.gpu = d;
    }
#else
    QFile mi("/proc/meminfo");
    if (mi.open(QIODevice::ReadOnly)) {
        const auto m = QRegularExpression("MemTotal:\\s+(\\d+)").match(QString::fromLatin1(mi.readAll()));
        if (m.hasMatch())
            h.ramMb = m.captured(1).toLongLong() / 1024;
    }
#endif
    cached = h;
    return h;
}

namespace {
QString slowStateFile(MinecraftInstance* inst)
{
    return inst->instanceRoot() + "/4fps-pclento.json";
}
const QStringList& heavyKeys()
{
    static const QStringList k = { "voxy", "distanthorizons", "distant-horizons", "iris", "oculus", "entity_model_features", "entity-model-features",
                                   "emf", "lyrica", "physicsmod", "particlerain", "fresh-animations", "freshanimations" };
    return k;
}
// cambia valores en options.txt; devuelve los anteriores
QHash<QString, QString> patchOptions(const QString& path, const QHash<QString, QString>& values)
{
    QHash<QString, QString> old;
    QStringList lines;
    QFile f(path);
    if (f.open(QIODevice::ReadOnly | QIODevice::Text))
        lines = QString::fromUtf8(f.readAll()).split('\n');
    f.close();
    QSet<QString> done;
    for (auto& l : lines) {
        const int c = l.indexOf(':');
        if (c <= 0)
            continue;
        const QString k = l.left(c);
        if (values.contains(k)) {
            old[k] = l.mid(c + 1);
            l = k + ':' + values[k];
            done.insert(k);
        }
    }
    for (auto it = values.begin(); it != values.end(); ++it)
        if (!done.contains(it.key())) {
            old[it.key()] = QString();  // no existía
            lines.insert(qMax(0, lines.size() - 1), it.key() + ':' + it.value());
        }
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        f.write(lines.join('\n').toUtf8());
    return old;
}
}  // namespace

QString heavyModReason(const QString& in)
{
    const QString n = QString(in).toLower().remove(' ').remove('-').remove('_');
    static const QList<QPair<QStringList, QString>> table = {
        { { "voxy" }, QObject::tr("dibuja terreno muy lejano: usa mucha RAM, CPU y GPU") },
        { { "distanthorizons" }, QObject::tr("dibuja terreno muy lejano: usa mucha RAM, CPU y GPU") },
        { { "iris", "oculus" }, QObject::tr("es para shaders: sin una tarjeta de video dedicada se pone muy lento") },
        { { "entitymodelfeatures", "entity_model_features", "emf" }, QObject::tr("agrega animaciones y modelos extra que gastan CPU") },
        { { "freshanimations" }, QObject::tr("agrega animaciones extra que gastan CPU") },
        { { "lyrica" }, QObject::tr("trae librerías de video grandes (FFmpeg/JavaCV) y gasta RAM") },
        { { "physicsmod" }, QObject::tr("simula física de bloques y entidades: muy pesado para la CPU") },
        { { "particlerain" }, QObject::tr("agrega muchas partículas de clima") },
        { { "bobby" }, QObject::tr("guarda y dibuja chunks extra: más RAM y disco") },
        { { "c2me" }, QObject::tr("usa muchos hilos de la CPU para generar chunks: en CPUs de 4 núcleos puede dar tirones") },
    };
    for (const auto& t : table)
        for (const auto& k : t.first)
            if (n.startsWith(k) || (k.size() > 4 && n.contains(k)))
                return t.second;
    return {};
}

bool confirmHeavyMods(QWidget* parent, MinecraftInstance* inst, const QStringList& names)
{
    if (!enabled(cfg::SlowPc))
        return true;
    const Hardware hw = detectHardware();
    if (!hw.weak() && !slowModeActive(inst))
        return true;
    QStringList lines;
    for (const auto& n : names) {
        const QString r = heavyModReason(n);
        if (!r.isEmpty())
            lines << QStringLiteral("• <b>%1</b>: %2").arg(n.toHtmlEscaped(), r.toHtmlEscaped());
    }
    if (lines.isEmpty())
        return true;
    QMessageBox box(parent);
    box.setWindowTitle(QObject::tr("Mods pesados para tu PC"));
    box.setTextFormat(Qt::RichText);
    box.setText(QObject::tr("Tu PC (%1, %2 GB de RAM) puede tener problemas con estos mods:<br><br>%3<br><br>Puede que te vaya lento, con tirones o que crashee. ¿Instalarlos igual?")
                    .arg(hw.cpu.isEmpty() ? QObject::tr("CPU") : hw.cpu.toHtmlEscaped())
                    .arg(QString::number(hw.ramMb / 1024.0, 'f', 0))
                    .arg(lines.join("<br>")));
    auto* yes = box.addButton(QObject::tr("Instalar igual"), QMessageBox::AcceptRole);
    box.addButton(QObject::tr("Cancelar"), QMessageBox::RejectRole);
    box.exec();
    return box.clickedButton() == yes;
}

bool slowModeActive(MinecraftInstance* inst)
{
    return inst && QFile::exists(slowStateFile(inst));
}

QString modKey(const QString& fileNameIn)
{
    QString n = fileNameIn.toLower();
    n.remove(QRegularExpression("\\.disabled$"));
    n.remove(QRegularExpression("\\.jar$"));
    n.remove(QRegularExpression("[-_+]?(mc|fabric|forge|neoforge|quilt)?[-_]?v?\\d[\\w.+-]*$"));
    n.remove(QRegularExpression("[-_](fabric|forge|neoforge|quilt|mc)$"));
    return n;
}

QString enableSlowMode(MinecraftInstance* inst)
{
    if (!inst || slowModeActive(inst))
        return {};
    const Hardware hw = detectHardware();
    QJsonObject state;
    auto* s = inst->settings();
    state["OverrideMemory"] = s->get("OverrideMemory").toBool();
    state["MaxMemAlloc"] = s->get("MaxMemAlloc").toInt();
    state["MinMemAlloc"] = s->get("MinMemAlloc").toInt();
    int ram = 4096;
    if (hw.ramMb > 0) {
        if (hw.ramMb <= 4300)
            ram = 1536;
        else if (hw.ramMb <= 8700)
            ram = 2560;
        else if (hw.ramMb <= 12500)
            ram = 3584;
    }
    state["OverrideJavaArgs"] = s->get("OverrideJavaArgs").toBool();
    state["JvmArgs"] = s->get("JvmArgs").toString();
    s->set("OverrideJavaArgs", true);
    s->set("JvmArgs", QStringLiteral("-XX:+UseG1GC -XX:MaxGCPauseMillis=50 -XX:G1HeapRegionSize=16M -XX:+ParallelRefProcEnabled -XX:+DisableExplicitGC"));
    s->set("OverrideMemory", true);
    s->set("MaxMemAlloc", ram);
    s->set("MinMemAlloc", qMin(512, ram));
    QStringList summary{ QObject::tr("RAM de la instancia: %1 MB").arg(ram), QObject::tr("Java optimizado (G1GC con pausas cortas)") };

    // mods pesados -> .disabled
    QJsonArray disabled;
    const auto jars = scanMods(inst->modsRoot());
    QDir mods(inst->modsRoot());
    for (const auto& fi : mods.entryInfoList({ "*.jar" }, QDir::Files)) {
        QString id;
        for (const auto& j : jars)
            if (!j.nested && j.topFile == fi.fileName())
                id = j.modId;
        const QString key = modKey(fi.fileName());
        bool heavy = false;
        for (const auto& h : heavyKeys())
            if (id == h || key == h || key.startsWith(h + '-') || id.replace('_', '-') == h)
                heavy = true;
        if (heavy && QFile::rename(fi.absoluteFilePath(), fi.absoluteFilePath() + ".disabled")) {
            disabled.append(fi.fileName());
            summary << QObject::tr("Desactivado: %1").arg(fi.fileName());
        }
    }
    state["disabled"] = disabled;
    // options.txt
    const auto old = patchOptions(inst->gameRoot() + "/options.txt", { { "renderDistance", "6" },
                                                                       { "simulationDistance", "5" },
                                                                       { "particles", "2" },
                                                                       { "entityDistanceScaling", "0.75" },
                                                                       { "graphicsMode", "0" } });
    QJsonObject oldOpts;
    for (auto it = old.begin(); it != old.end(); ++it)
        oldOpts[it.key()] = it.value();
    state["options"] = oldOpts;
    summary << QObject::tr("Distancia de render 6, simulación 5, partículas mínimas, gráficos rápidos");
    QFile f(slowStateFile(inst));
    if (f.open(QIODevice::WriteOnly))
        f.write(QJsonDocument(state).toJson());
    return summary.join('\n');
}

QString disableSlowMode(MinecraftInstance* inst)
{
    if (!inst || !slowModeActive(inst))
        return {};
    QFile f(slowStateFile(inst));
    if (!f.open(QIODevice::ReadOnly))
        return {};
    const auto state = QJsonDocument::fromJson(f.readAll()).object();
    f.close();
    auto* s = inst->settings();
    s->set("OverrideMemory", state.value("OverrideMemory").toBool());
    s->set("MaxMemAlloc", state.value("MaxMemAlloc").toInt());
    s->set("MinMemAlloc", state.value("MinMemAlloc").toInt());
    if (state.contains("OverrideJavaArgs")) {
        s->set("OverrideJavaArgs", state.value("OverrideJavaArgs").toBool());
        s->set("JvmArgs", state.value("JvmArgs").toString());
    }
    QStringList summary{ QObject::tr("RAM y Java restaurados"), QObject::tr("Mods de rendimiento de 4fps quitados") };
    for (const auto& d : state.value("disabled").toArray()) {
        const QString p = inst->modsRoot() + "/" + d.toString();
        if (QFile::exists(p + ".disabled") && QFile::rename(p + ".disabled", p))
            summary << QObject::tr("Reactivado: %1").arg(d.toString());
    }
    QHash<QString, QString> restore;
    QSet<QString> drop;  // opciones que no existían antes: se quitan
    const auto opts = state.value("options").toObject();
    for (auto it = opts.begin(); it != opts.end(); ++it) {
        if (it.value().toString().isEmpty())
            drop.insert(it.key());
        else
            restore[it.key()] = it.value().toString();
    }
    if (!restore.isEmpty())
        patchOptions(inst->gameRoot() + "/options.txt", restore);
    if (!drop.isEmpty()) {
        QFile of(inst->gameRoot() + "/options.txt");
        QStringList lines;
        if (of.open(QIODevice::ReadOnly | QIODevice::Text))
            lines = QString::fromUtf8(of.readAll()).split('\n');
        of.close();
        QStringList kept;
        for (const auto& l : lines)
            if (!drop.contains(l.section(':', 0, 0)))
                kept << l;
        if (of.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
            of.write(kept.join('\n').toUtf8());
    }
    summary << QObject::tr("Opciones de video restauradas");
    QFile::remove(slowStateFile(inst));
    return summary.join('\n');
}

// ================================================================= grupos de mods
namespace {
QString groupsFile(MinecraftInstance* inst)
{
    return inst->instanceRoot() + "/4fps-grupos.json";
}
}  // namespace

QList<ModGroup> modGroups(MinecraftInstance* inst)
{
    QList<ModGroup> out;
    if (!inst)
        return out;
    QFile f(groupsFile(inst));
    if (!f.open(QIODevice::ReadOnly))
        return out;
    for (const auto& v : QJsonDocument::fromJson(f.readAll()).array()) {
        const auto o = v.toObject();
        ModGroup g;
        g.name = o.value("name").toString();
        g.locked = o.value("locked").toBool();
        for (const auto& m : o.value("members").toArray())
            g.members << m.toString();
        out << g;
    }
    return out;
}

void saveModGroups(MinecraftInstance* inst, const QList<ModGroup>& groups)
{
    QJsonArray arr;
    for (const auto& g : groups)
        arr.append(QJsonObject{ { "name", g.name }, { "locked", g.locked }, { "members", QJsonArray::fromStringList(g.members) } });
    QFile f(groupsFile(inst));
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(arr).toJson());
}

void editModGroups(QWidget* parent, MinecraftInstance* inst)
{
    if (!inst)
        return;
    auto groups = modGroups(inst);
    QDialog dlg(parent);
    dlg.setWindowTitle(QObject::tr("Grupos de mods · %1").arg(inst->name()));
    dlg.setMinimumSize(620, 420);
    auto* h = new QHBoxLayout(&dlg);
    auto* left = new QVBoxLayout();
    left->addWidget(new QLabel(QObject::tr("<b>Grupos</b>"), &dlg));
    auto* list = new QListWidget(&dlg);
    left->addWidget(list, 1);
    auto* add = new QPushButton(QObject::tr("Nuevo grupo"), &dlg);
    auto* del = new QPushButton(QObject::tr("Borrar grupo"), &dlg);
    auto* lock = new QCheckBox(QObject::tr("Bloqueado (no actualizar)"), &dlg);
    left->addWidget(add);
    left->addWidget(del);
    left->addWidget(lock);
    h->addLayout(left, 1);
    auto* right = new QVBoxLayout();
    right->addWidget(new QLabel(QObject::tr("<b>Mods del grupo</b> (se actualizan juntos)"), &dlg));
    auto* mods = new QListWidget(&dlg);
    right->addWidget(mods, 1);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dlg);
    right->addWidget(bb);
    h->addLayout(right, 2);

    QStringList keys;
    for (const auto& fi : QDir(inst->modsRoot()).entryInfoList({ "*.jar", "*.jar.disabled" }, QDir::Files, QDir::Name)) {
        const QString k = modKey(fi.fileName());
        if (!keys.contains(k))
            keys << k;
    }
    auto refreshGroups = [&] {
        list->clear();
        for (const auto& g : groups)
            list->addItem((g.locked ? QObject::tr("[bloqueado] ") : QString()) + g.name + QStringLiteral("  (%1)").arg(g.members.size()));
    };
    bool loading = false;
    auto loadMods = [&] {
        loading = true;
        mods->clear();
        const int gi = list->currentRow();
        mods->setEnabled(gi >= 0);
        lock->setEnabled(gi >= 0);
        lock->setChecked(gi >= 0 && groups[gi].locked);
        for (const auto& k : keys) {
            auto* it = new QListWidgetItem(k, mods);
            it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
            it->setCheckState(gi >= 0 && groups[gi].members.contains(k) ? Qt::Checked : Qt::Unchecked);
        }
        loading = false;
    };
    QObject::connect(list, &QListWidget::currentRowChanged, &dlg, [&](int) { loadMods(); });
    QObject::connect(mods, &QListWidget::itemChanged, &dlg, [&](QListWidgetItem* it) {
        const int gi = list->currentRow();
        if (loading || gi < 0)
            return;
        groups[gi].members.removeAll(it->text());
        if (it->checkState() == Qt::Checked)
            groups[gi].members << it->text();
        list->item(gi)->setText(groups[gi].name + QStringLiteral("  (%1)").arg(groups[gi].members.size()));
    });
    QObject::connect(lock, &QCheckBox::toggled, &dlg, [&](bool on) {
        const int gi = list->currentRow();
        if (!loading && gi >= 0)
            groups[gi].locked = on;
    });
    QObject::connect(add, &QPushButton::clicked, &dlg, [&] {
        const QString name = QInputDialog::getText(&dlg, QObject::tr("Nuevo grupo"), QObject::tr("Nombre del grupo (ej: Renderizado):"));
        if (name.trimmed().isEmpty())
            return;
        groups << ModGroup{ name.trimmed(), {}, false };
        refreshGroups();
        list->setCurrentRow(groups.size() - 1);
    });
    QObject::connect(del, &QPushButton::clicked, &dlg, [&] {
        const int gi = list->currentRow();
        if (gi < 0)
            return;
        groups.removeAt(gi);
        refreshGroups();
        loadMods();
    });
    QObject::connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    refreshGroups();
    if (!groups.isEmpty())
        list->setCurrentRow(0);
    else
        loadMods();
    if (dlg.exec() == QDialog::Accepted)
        saveModGroups(inst, groups);
}

QStringList applyGroupsToUpdate(QWidget* parent, MinecraftInstance* inst, const QStringList& wantUpdate, const QStringList& canUpdate)
{
    if (!inst || !enabled(cfg::ModGroups))
        return wantUpdate;
    QStringList result = wantUpdate;
    const auto groups = modGroups(inst);
    for (const auto& g : groups) {
        QStringList inWant, inCan;
        for (const auto& f : wantUpdate)
            if (g.members.contains(modKey(f)))
                inWant << f;
        for (const auto& f : canUpdate)
            if (g.members.contains(modKey(f)))
                inCan << f;
        if (inWant.isEmpty())
            continue;
        if (g.locked) {
            for (const auto& f : inWant)
                result.removeAll(f);
            continue;
        }
        if (inWant.size() >= inCan.size())
            continue;  // ya va el grupo completo
        QMessageBox box(parent);
        box.setWindowTitle(QObject::tr("Grupo \"%1\"").arg(g.name));
        box.setText(QObject::tr("Vas a actualizar solo una parte del grupo \"%1\" (%2 de %3 mods con actualización).\nLos mods de este grupo deberían actualizarse juntos para que sigan siendo compatibles.")
                        .arg(g.name)
                        .arg(inWant.size())
                        .arg(inCan.size()));
        auto* all = box.addButton(QObject::tr("Actualizar todo el grupo"), QMessageBox::AcceptRole);
        auto* skip = box.addButton(QObject::tr("Saltarse el grupo"), QMessageBox::RejectRole);
        box.addButton(QObject::tr("Seguir igual"), QMessageBox::DestructiveRole);
        box.exec();
        if (box.clickedButton() == all) {
            for (const auto& f : inCan)
                if (!result.contains(f))
                    result << f;
        } else if (box.clickedButton() == skip) {
            for (const auto& f : inWant)
                result.removeAll(f);
        }
    }
    return result;
}

}  // namespace fps
