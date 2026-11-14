// 4fps - funciones extra (parte 2): Discord Rich Presence, letras de canciones, galería, servidores en vivo, eventos
#include "FourFpsFeatures.h"
#include "FourFpsShell.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QLineEdit>
#include <QSlider>
#include <QMouseEvent>
#include <cmath>
#include <QFileDialog>
#include <QApplication>
#include <QDebug>
#include <QBuffer>
#include <QButtonGroup>
#include <QClipboard>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLocalSocket>
#include <QLocale>
#include <QMessageBox>
#include <QMimeData>
#include <QNetworkReply>
#include <QPainter>
#include <QPainterPath>
#include <QProcess>
#include <QPushButton>
#include <QRegularExpression>
#include <QScreen>
#include <QScrollArea>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QToolButton>
#include <QUrlQuery>
#include <QUuid>
#include <QDate>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QRadialGradient>
#include <QVBoxLayout>

#include "Application.h"
#include "BaseInstance.h"
#include "InstanceList.h"
#include "minecraft/MinecraftInstance.h"

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace fps {

// ================================================================= Discord Rich Presence (IPC por named pipe)
DiscordPresence* DiscordPresence::get()
{
    static DiscordPresence* p = new DiscordPresence(qApp);
    return p;
}

DiscordPresence::DiscordPresence(QObject* parent) : QObject(parent)
{
    m_retry = new QTimer(this);
    m_retry->setInterval(20000);
    connect(m_retry, &QTimer::timeout, this, [this] {
        if (m_hasActivity && !m_ready)
            ensureConnected();
    });
    connect(Bus::get(), &Bus::settingsChanged, this, [this] {
        if (!enabled(cfg::DiscordRpc))
            clear();
        else if (m_hasActivity)
            pushActivity();
    });
}

void DiscordPresence::ensureConnected()
{
    const QString appId = setting(cfg::DiscordAppId).toString().trimmed();
    if (!enabled(cfg::DiscordRpc) || appId.isEmpty())
        return;
    if (m_sock && m_sock->state() == QLocalSocket::ConnectedState)
        return;
    if (!m_sock) {
        m_sock = new QLocalSocket(this);
        connect(m_sock, &QLocalSocket::connected, this, [this, appId] {
            send(0, QJsonDocument(QJsonObject{ { "v", 1 }, { "client_id", appId } }).toJson(QJsonDocument::Compact));
        });
        connect(m_sock, &QLocalSocket::readyRead, this, [this] {
            const QByteArray data = m_sock->readAll();
            if (data.contains("\"READY\"")) {
                m_ready = true;
                pushActivity();
            }
        });
        connect(m_sock, &QLocalSocket::disconnected, this, [this] { m_ready = false; });
        connect(m_sock, &QLocalSocket::errorOccurred, this, [this](QLocalSocket::LocalSocketError) {
            m_ready = false;
            // Discord usa discord-ipc-0..9
            if (m_pipe < 9) {
                m_pipe++;
                m_sock->abort();
                m_sock->connectToServer(QStringLiteral("discord-ipc-%1").arg(m_pipe));
            } else {
                m_pipe = 0;
            }
        });
    }
    m_pipe = 0;
    m_sock->abort();
    m_sock->connectToServer(QStringLiteral("discord-ipc-0"));
    m_retry->start();
}

void DiscordPresence::send(int op, const QByteArray& json)
{
    if (!m_sock)
        return;
    QByteArray frame(8, 0);
    const quint32 o = quint32(op), len = quint32(json.size());
    memcpy(frame.data(), &o, 4);  // little endian en Windows/x86
    memcpy(frame.data() + 4, &len, 4);
    m_sock->write(frame + json);
    m_sock->flush();
}

void DiscordPresence::pushActivity()
{
    if (!m_ready || !enabled(cfg::DiscordRpc))
        return;
    QJsonObject args{ { "pid", int(QCoreApplication::applicationPid()) } };
    if (m_hasActivity) {
        QJsonObject act;
        act["details"] = m_details.left(120);
        if (!m_state.isEmpty())
            act["state"] = m_state.left(120);
        if (m_start.isValid())
            act["timestamps"] = QJsonObject{ { "start", m_start.toSecsSinceEpoch() } };
        act["assets"] = QJsonObject{ { "large_image", "logo" }, { "large_text", "4fps Launcher" } };
        act["buttons"] = QJsonArray{ QJsonObject{ { "label", "Discord de 4fps" }, { "url", "https://discord.gg/YwfbeN2N4" } } };
        args["activity"] = act;
    }
    const QJsonObject msg{ { "cmd", "SET_ACTIVITY" }, { "args", args }, { "nonce", QUuid::createUuid().toString(QUuid::WithoutBraces) } };
    send(1, QJsonDocument(msg).toJson(QJsonDocument::Compact));
}

void DiscordPresence::setPlaying(const QString& details, const QString& state, const QDateTime& start)
{
    m_details = details;
    m_state = state;
    m_start = start;
    m_hasActivity = true;
    if (m_ready)
        pushActivity();
    else
        ensureConnected();
}

void DiscordPresence::setIdle()
{
    setPlaying(QObject::tr("En el launcher"), QString(), QDateTime());
}

void DiscordPresence::clear()
{
    m_hasActivity = false;
    if (m_ready)
        pushActivity();
}

// ================================================================= letras de canciones
namespace {
// helper de PowerShell: lee Windows SMTC (GlobalSystemMediaTransportControlsSessionManager) y escribe JSON por línea
const char* kSmtcScript = R"PS(
$ErrorActionPreference = 'SilentlyContinue'
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
Add-Type -AssemblyName System.Runtime.WindowsRuntime
$asTaskGeneric = ([System.WindowsRuntimeSystemExtensions].GetMethods() | Where-Object { $_.Name -eq 'AsTask' -and $_.GetParameters().Count -eq 1 -and $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncOperation`1' })[0]
function Await($op, $type) { $t = $asTaskGeneric.MakeGenericMethod($type).Invoke($null, @($op)); $t.Wait(-1) | Out-Null; $t.Result }
$null = [Windows.Media.Control.GlobalSystemMediaTransportControlsSessionManager, Windows.Media.Control, ContentType = WindowsRuntime]
$null = [Windows.Storage.Streams.IRandomAccessStreamWithContentType, Windows.Storage.Streams, ContentType = WindowsRuntime]
$mgr = Await ([Windows.Media.Control.GlobalSystemMediaTransportControlsSessionManager]::RequestAsync()) ([Windows.Media.Control.GlobalSystemMediaTransportControlsSessionManager])
$cmdFile = $args[0]; $coverFile = $args[1]
$lastKey = ''
while ($true) {
  if (Test-Path $cmdFile) {
    $cmd = (Get-Content $cmdFile -Raw).Trim(); Remove-Item $cmdFile -Force
    $s = $mgr.GetCurrentSession()
    if ($s) {
      if ($cmd -eq 'playpause') { $null = Await ($s.TryTogglePlayPauseAsync()) ([bool]) }
      elseif ($cmd -eq 'next') { $null = Await ($s.TrySkipNextAsync()) ([bool]) }
      elseif ($cmd -eq 'previous') { $null = Await ($s.TrySkipPreviousAsync()) ([bool]) }
    }
  }
  $s = $mgr.GetCurrentSession()
  if ($s) {
    $p = Await ($s.TryGetMediaPropertiesAsync()) ([Windows.Media.Control.GlobalSystemMediaTransportControlsSessionMediaProperties])
    $t = $s.GetTimelineProperties(); $pb = $s.GetPlaybackInfo()
    $key = "$($p.Title)|$($p.Artist)"
    $cover = 0
    if ($key -ne $lastKey) {
      $lastKey = $key
      if ($p.Thumbnail) {
        try {
          $ras = Await ($p.Thumbnail.OpenReadAsync()) ([Windows.Storage.Streams.IRandomAccessStreamWithContentType])
          $st = [System.IO.WindowsRuntimeStreamExtensions]::AsStreamForRead($ras)
          $fs = [System.IO.File]::Create($coverFile); $st.CopyTo($fs); $fs.Close(); $st.Close(); $cover = 1
        } catch {}
      }
    }
    $o = [ordered]@{ title = $p.Title; artist = $p.Artist; album = $p.AlbumTitle; status = [string]$pb.PlaybackStatus;
      pos = [math]::Round($t.Position.TotalMilliseconds); dur = [math]::Round($t.EndTime.TotalMilliseconds);
      updated = $t.LastUpdatedTime.ToUnixTimeMilliseconds(); app = $s.SourceAppUserModelId; cover = $cover }
    [Console]::Out.WriteLine(($o | ConvertTo-Json -Compress)); [Console]::Out.Flush()
  } else { [Console]::Out.WriteLine('{"status":"None"}'); [Console]::Out.Flush() }
  Start-Sleep -Milliseconds 500
}
)PS";

QString lyricsDir()
{
    const QString d = APPLICATION->dataRoot() + "/letras";
    QDir().mkpath(d);
    return d;
}

QList<QPair<double, QString>> parseLrc(const QString& lrc)
{
    QList<QPair<double, QString>> out;
    static const QRegularExpression tag("\\[(\\d+):(\\d+(?:\\.\\d+)?)\\]");
    for (const auto& line : lrc.split('\n')) {
        auto it = tag.globalMatch(line);
        QList<double> times;
        int last = 0;
        while (it.hasNext()) {
            const auto m = it.next();
            times << (m.captured(1).toDouble() * 60 + m.captured(2).toDouble()) * 1000;
            last = m.capturedEnd();
        }
        const QString text = line.mid(last).trimmed();
        for (double t : times)
            out << qMakePair(t, text);
    }
    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    return out;
}

// ventana flotante "Escuchando ahora" (no recibe clics): portada, canción y artista; la letra debajo si se encontró.
// Posición configurable; por defecto arriba a la izquierda, en chico.
class LyricsOverlay : public QWidget {
   public:
    LyricsOverlay() : QWidget(nullptr, Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool | Qt::WindowTransparentForInput)
    {
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_ShowWithoutActivating);
        place();
    }
    QString title, artist, line;
    QImage cover;

    void place()
    {
        const QString pos = setting(cfg::LyricsPosition).toString();
        const int scale = setting(cfg::LyricsSize).toInt();  // 0 chico, 1 mediano, 2 grande
        m_base = scale == 2 ? 20 : scale == 1 ? 16 : 13;
        resize(m_base * 30, m_base * 5 + 20);
        m_left = pos.endsWith("izquierda");
        m_right = pos.endsWith("derecha");
        if (auto* sc = QGuiApplication::primaryScreen()) {
            const QRect g = sc->geometry();
            const int x = m_left ? g.left() + 12 : m_right ? g.right() - width() - 12 : g.center().x() - width() / 2;
            const int y = pos.startsWith("abajo") ? g.bottom() - height() - 110 : g.top() + 10;
            move(x, y);
        }
    }

   protected:
    void paintEvent(QPaintEvent*) override
    {
        if (title.isEmpty())
            return;
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        QFont ft = font();
        ft.setPixelSize(m_base);
        ft.setBold(true);
        QFont fa = font();
        fa.setPixelSize(qMax(10, m_base - 3));
        QFont fl = font();
        fl.setPixelSize(qMax(10, m_base - 2));
        fl.setItalic(true);
        const int rows = line.isEmpty() ? 2 : 3;
        const int coverSize = m_base * rows + (rows - 1) * 2;
        const int pad = m_base / 2 + 2;
        const QString t = QStringLiteral("♪ ") + title;
        const int textW = qMax(QFontMetrics(ft).horizontalAdvance(t), qMax(QFontMetrics(fa).horizontalAdvance(artist), QFontMetrics(fl).horizontalAdvance(line)));
        const int w = qMin(width() - 4, (cover.isNull() ? 0 : coverSize + pad) + textW + pad * 2);
        const int h = coverSize + pad * 2;
        const double x = m_left ? 0 : m_right ? width() - w : (width() - w) / 2.0;
        QRectF card(x, 2, w, h);
        QPainterPath path;
        path.addRoundedRect(card, 10, 10);
        p.fillPath(path, QColor(10, 10, 12, 175));
        double tx = card.left() + pad;
        if (!cover.isNull()) {
            QPainterPath cp;
            const QRectF cr(tx, card.top() + pad, coverSize, coverSize);
            cp.addRoundedRect(cr, 6, 6);
            p.save();
            p.setClipPath(cp);
            p.drawImage(cr, cover);
            p.restore();
            tx += coverSize + pad;
        }
        const double tw = card.right() - pad - tx;
        p.setFont(ft);
        p.setPen(Qt::white);
        const int lineH = coverSize / rows;
        p.drawText(QRectF(tx, card.top() + pad, tw, lineH), Qt::AlignLeft | Qt::AlignVCenter, p.fontMetrics().elidedText(t, Qt::ElideRight, int(tw)));
        p.setFont(fa);
        p.setPen(QColor(200, 200, 205));
        p.drawText(QRectF(tx, card.top() + pad + lineH, tw, lineH), Qt::AlignLeft | Qt::AlignVCenter, p.fontMetrics().elidedText(artist, Qt::ElideRight, int(tw)));
        if (!line.isEmpty()) {
            p.setFont(fl);
            p.setPen(QColor(235, 235, 240));
            p.drawText(QRectF(tx, card.top() + pad + lineH * 2, tw, lineH), Qt::AlignLeft | Qt::AlignVCenter, p.fontMetrics().elidedText(line, Qt::ElideRight, int(tw)));
        }
    }

   private:
    int m_base = 13;
    bool m_left = true, m_right = false;
};

// limpia títulos de YouTube/navegador: "Artista - Canción (Official Video) [4K]" -> artista, canción
void cleanTrack(QString& title, QString& artist)
{
    static const QRegularExpression junk(
        "\\s*[\\(\\[][^\\)\\]]*(official|oficial|video|v[ií]deo|lyric|letra|audio|visuali[sz]er|remaster|\\bhd\\b|\\b4k\\b|\\bmv\\b|clip|music)[^\\)\\]]*[\\)\\]]",
        QRegularExpression::CaseInsensitiveOption);
    title.remove(junk);
    title.remove(QRegularExpression("\\s*\\|.*$"));
    title.remove(QRegularExpression("\\s+(ft\\.?|feat\\.?|featuring)\\s+.*$", QRegularExpression::CaseInsensitiveOption));
    artist.remove(QRegularExpression("\\s*-\\s*Topic$", QRegularExpression::CaseInsensitiveOption));
    artist.remove(QRegularExpression("VEVO$", QRegularExpression::CaseInsensitiveOption));
    artist.remove(QRegularExpression("\\s*(,|&| x | ft\\.? | feat\\.? ).*$", QRegularExpression::CaseInsensitiveOption));
    title = title.simplified();
    artist = artist.simplified();
}
}  // namespace

LyricsService* LyricsService::get()
{
    static LyricsService* s = new LyricsService(qApp);
    return s;
}

LyricsService::LyricsService(QObject* parent) : QObject(parent)
{
    m_tick = new QTimer(this);
    m_tick->setInterval(150);
    connect(m_tick, &QTimer::timeout, this, &LyricsService::tick);
}

QString LyricsService::overlayUrl() const
{
    return QStringLiteral("http://127.0.0.1:%1/").arg(setting(cfg::LyricsPort).toInt());
}

QString LyricsService::helperScript() const
{
    const QString path = lyricsDir() + "/smtc-4fps.ps1";
    QFile f(path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QByteArray(kSmtcScript));
    return path;
}

void LyricsService::start()
{
    if (m_running)
        return;
    m_running = true;
#ifdef Q_OS_WIN
    m_helper = new QProcess(this);
    m_helper->setProgram("powershell.exe");
    m_helper->setArguments({ "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-WindowStyle", "Hidden", "-File", helperScript(),
                             lyricsDir() + "/cmd.txt", lyricsDir() + "/cover.img" });
    m_helper->setProcessChannelMode(QProcess::SeparateChannels);
    m_helper->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* a) { a->flags |= CREATE_NO_WINDOW; });
    connect(m_helper, &QProcess::readyReadStandardOutput, this, [this] {
        m_buf += m_helper->readAllStandardOutput();
        int nl;
        while ((nl = m_buf.indexOf('\n')) >= 0) {
            onHelperLine(m_buf.left(nl).trimmed());
            m_buf.remove(0, nl + 1);
        }
    });
    m_helper->start();
#endif
    // socket local: HTTP (OBS / estado) y conexiones de juego (JSON por línea)
    m_server = new QTcpServer(this);
    m_server->listen(QHostAddress::LocalHost, quint16(setting(cfg::LyricsPort).toInt()));
    connect(m_server, &QTcpServer::newConnection, this, [this] {
        while (auto* s = m_server->nextPendingConnection()) {
            connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
            connect(s, &QTcpSocket::readyRead, this, [this, s] {
                const QByteArray head = s->peek(16);
                if (head.startsWith("GET ")) {
                    handleHttp(s);
                } else {
                    const QByteArray cmd = s->readAll().trimmed();
                    if (cmd.startsWith("HELLO"))
                        m_streams << s;
                    else if (cmd == "playpause" || cmd == "next" || cmd == "previous")
                        control(QString::fromLatin1(cmd));
                }
            });
        }
    });
    if (enabled(cfg::LyricsOverlay)) {
        auto* ov = new LyricsOverlay();
        m_overlay = ov;
        ov->show();
        connect(Bus::get(), &Bus::settingsChanged, ov, [ov] {
            ov->place();
            ov->update();
        });
    }
    m_tick->start();
    emit updated();
}

void LyricsService::stop()
{
    if (!m_running)
        return;
    m_running = false;
    m_tick->stop();
    if (m_helper) {
        m_helper->kill();
        m_helper->deleteLater();
        m_helper = nullptr;
    }
    if (m_server) {
        m_server->close();
        m_server->deleteLater();
        m_server = nullptr;
    }
    if (m_overlay) {
        m_overlay->deleteLater();
        m_overlay = nullptr;
    }
    m_streams.clear();
    title.clear();
    currentLine.clear();
    nextLine.clear();
    emit updated();
}

void LyricsService::control(const QString& action)
{
    QFile f(lyricsDir() + "/cmd.txt");
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(action.toUtf8());
}

void LyricsService::onHelperLine(const QByteArray& line)
{
    QByteArray clean = line;
    if (clean.startsWith("\xEF\xBB\xBF"))
        clean.remove(0, 3);
    const auto o = QJsonDocument::fromJson(clean).object();
    if (o.isEmpty())
        return;
    status = o.value("status").toString();
    playing = status == "Playing";
    if (status == "None") {
        title.clear();
        return;
    }
    const QString t = o.value("title").toString(), a = o.value("artist").toString();
    const double pos = o.value("pos").toDouble();
    durationMs = o.value("dur").toDouble();
    if (o.value("cover").toInt() == 1)
        cover.load(lyricsDir() + "/cover.img");
    const QString key = t + '|' + a;
    if (key != m_trackKey) {
        m_trackKey = key;
        title = t;
        artist = a;
        album = o.value("album").toString();
        m_lines.clear();
        synced = false;
        currentLine.clear();
        nextLine.clear();
        m_lastReportPos = -1;
        fetchLyrics();
    }
    // interpolación: algunos navegadores reportan la posición a saltos
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (pos != m_lastReportPos) {
        m_lastReportPos = pos;
        const qint64 updatedAt = qint64(o.value("updated").toDouble());
        m_lastReportWall = (updatedAt > 0 && now - updatedAt < 5000 && now >= updatedAt) ? updatedAt : now;
    }
}

void LyricsService::fetchLyrics()
{
    if (title.isEmpty())
        return;
    const QString hash = QString::fromLatin1(QCryptographicHash::hash((title + '|' + artist).toLower().toUtf8(), QCryptographicHash::Sha1).toHex());
    const QString cacheFile = lyricsDir() + "/" + hash + ".json";
    QFile cf(cacheFile);
    if (cf.open(QIODevice::ReadOnly)) {
        const auto o = QJsonDocument::fromJson(cf.readAll()).object();
        m_lines = parseLrc(o.value("syncedLyrics").toString());
        synced = !m_lines.isEmpty();
        if (synced) {  // solo se usan cachés con letra (las vacías de versiones viejas se ignoran)
            emit updated();
            return;
        }
    }
    // intentos: datos exactos, título limpio, título "Artista - Canción" partido, y búsqueda libre
    QString ct = title, ca = artist;
    cleanTrack(ct, ca);
    QList<QUrl> tries;
    auto addGet = [&tries, this](const QString& t, const QString& a) {
        if (t.isEmpty() || a.isEmpty())
            return;
        QUrl u("https://lrclib.net/api/get");
        QUrlQuery q;
        q.addQueryItem("track_name", t);
        q.addQueryItem("artist_name", a);
        if (durationMs > 0)
            q.addQueryItem("duration", QString::number(qRound(durationMs / 1000)));
        u.setQuery(q);
        tries << u;
    };
    auto addSearch = [&tries](const QString& t, const QString& a) {
        if (t.isEmpty())
            return;
        QUrl u("https://lrclib.net/api/search");
        QUrlQuery q;
        if (a.isEmpty()) {
            q.addQueryItem("q", t);
        } else {
            q.addQueryItem("track_name", t);
            q.addQueryItem("artist_name", a);
        }
        u.setQuery(q);
        tries << u;
    };
    addGet(title, artist);
    addGet(ct, ca);
    addSearch(ct, ca);
    if (ct.contains(" - ")) {  // YouTube: "Artista - Canción"
        QString t2 = ct.section(" - ", 1).trimmed(), a2 = ct.section(" - ", 0, 0).trimmed();
        cleanTrack(t2, a2);
        addGet(t2, a2);
        addSearch(t2, a2);
        addSearch(t2 + ' ' + a2, QString());
    }
    addSearch(ct + ' ' + ca, QString());
    addSearch(ct, QString());
    searching = true;
    tryLyrics(tries, m_trackKey, cacheFile);
}

void LyricsService::tryLyrics(QList<QUrl> tries, const QString& key, const QString& cacheFile)
{
    if (key != m_trackKey)
        return;
    if (tries.isEmpty()) {
        m_lines.clear();
        synced = false;
        searching = false;
        emit updated();
        return;
    }
    const QUrl url = tries.takeFirst();
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, "4fps-launcher (https://github.com/nofranzip/4fps-launcher)");
    req.setTransferTimeout(10000);
    auto* reply = APPLICATION->network()->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, key, cacheFile, tries] {
        reply->deleteLater();
        if (key != m_trackKey)
            return;
        const auto doc = QJsonDocument::fromJson(reply->readAll());
        QString lrc;
        if (doc.isObject()) {
            lrc = doc.object().value("syncedLyrics").toString();
        } else if (doc.isArray()) {
            // la que tenga letra sincronizada y la duración más parecida
            double best = 1e18;
            for (const auto& v : doc.array()) {
                const auto o = v.toObject();
                const QString l = o.value("syncedLyrics").toString();
                if (l.isEmpty())
                    continue;
                const double diff = durationMs > 0 ? std::abs(o.value("duration").toDouble() * 1000 - durationMs) : 0;
                if (diff < best && (durationMs <= 0 || diff < 15000)) {
                    best = diff;
                    lrc = l;
                }
            }
        }
        if (lrc.isEmpty()) {
            qDebug() << "4fps letras: sin resultado en" << reply->url().toString();
            tryLyrics(tries, key, cacheFile);
            return;
        }
        QFile f(cacheFile);
        if (f.open(QIODevice::WriteOnly))
            f.write(QJsonDocument(QJsonObject{ { "syncedLyrics", lrc } }).toJson());
        m_lines = parseLrc(lrc);
        synced = !m_lines.isEmpty();
        searching = false;
        emit updated();
    });
}

void LyricsService::tick()
{
    if (title.isEmpty()) {
        if (!currentLine.isEmpty()) {
            currentLine.clear();
            nextLine.clear();
            emit updated();
        }
        return;
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    positionMs = m_lastReportPos + (playing ? double(now - m_lastReportWall) : 0.0);
    if (durationMs > 0)
        positionMs = qMin(positionMs, durationMs);
    const double t = positionMs + setting(cfg::LyricsOffset).toInt();
    QString cur, nxt;
    if (m_lines.isEmpty()) {
        cur = searching ? QObject::tr("Buscando letra...") : QObject::tr("Sin letra sincronizada");
    } else {
        int idx = -1;
        for (int i = 0; i < m_lines.size(); i++)
            if (m_lines[i].first <= t)
                idx = i;
        cur = idx >= 0 ? m_lines[idx].second : QStringLiteral("♪");
        nxt = idx + 1 < m_lines.size() ? m_lines[idx + 1].second : QString();
    }
    const bool changed = cur != currentLine || nxt != nextLine;
    currentLine = cur;
    nextLine = nxt;
    if (auto* ov = static_cast<LyricsOverlay*>(m_overlay)) {
        QString dt = title, da = artist;
        cleanTrack(dt, da);  // sin "(Official Video)", "- Topic", etc.
        const QString newTitle = playing ? dt : QString();
        const QString newLine = playing && synced ? cur : QString();
        if (ov->title != newTitle || ov->line != newLine || ov->artist != da || ov->cover.cacheKey() != cover.cacheKey()) {
            ov->title = newTitle;
            ov->artist = da;
            ov->line = newLine;
            ov->cover = cover;
            ov->update();
        }
    }
    // enviar al juego (clientes que dijeron HELLO)
    const QByteArray js = stateJson() + '\n';
    for (int i = m_streams.size() - 1; i >= 0; i--) {
        if (!m_streams[i] || m_streams[i]->state() != QAbstractSocket::ConnectedState)
            m_streams.removeAt(i);
        else
            m_streams[i]->write(js);
    }
    if (changed)
        emit updated();
}

QByteArray LyricsService::stateJson() const
{
    return QJsonDocument(QJsonObject{ { "title", title },
                                      { "artist", artist },
                                      { "album", album },
                                      { "playing", playing },
                                      { "line", currentLine },
                                      { "next", nextLine },
                                      { "synced", synced },
                                      { "position", positionMs },
                                      { "duration", durationMs },
                                      { "progress", durationMs > 0 ? positionMs / durationMs : 0.0 } })
        .toJson(QJsonDocument::Compact);
}

void LyricsService::handleHttp(QTcpSocket* s)
{
    const QByteArray req = s->readAll();
    const QByteArray path = req.mid(4, req.indexOf(' ', 4) - 4);
    QByteArray body, type = "text/plain";
    if (path.startsWith("/estado")) {
        body = stateJson();
        type = "application/json; charset=utf-8";
    } else if (path.startsWith("/cover")) {
        QBuffer b(&body);
        b.open(QIODevice::WriteOnly);
        cover.save(&b, "PNG");
        type = "image/png";
    } else if (path.startsWith("/control/")) {
        control(QString::fromLatin1(path.mid(9)));
        body = "ok";
    } else {
        // página para OBS (Fuente de navegador) con la letra animada
        body = R"HTML(<!doctype html><meta charset=utf-8><style>
body{margin:0;background:transparent;font-family:Segoe UI,Arial,sans-serif;color:#fff;overflow:hidden}
#w{position:fixed;left:0;right:0;bottom:30px;text-align:center}
#l{display:inline-block;font-size:42px;font-weight:700;padding:8px 22px;border-radius:14px;background:rgba(10,10,12,.65);transition:opacity .25s,transform .25s}
#n{font-size:24px;color:#d4d4d8;margin-top:8px;text-shadow:0 2px 6px #000}
#l.out{opacity:0;transform:translateY(10px)}
</style><div id=w><div id=l></div><div id=n></div></div><script>
let last='';async function t(){try{const r=await fetch('/estado');const s=await r.json();const L=document.getElementById('l');
const line=s.playing?s.line:'';if(line!==last){L.classList.add('out');setTimeout(()=>{L.textContent=line;L.style.display=line?'inline-block':'none';L.classList.remove('out')},180);last=line}
document.getElementById('n').textContent=s.playing?s.next:''}catch(e){}}setInterval(t,200)</script>)HTML";
        type = "text/html; charset=utf-8";
    }
    QByteArray resp = "HTTP/1.1 200 OK\r\nContent-Type: " + type + "\r\nAccess-Control-Allow-Origin: *\r\nCache-Control: no-store\r\nContent-Length: " +
                      QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
    s->write(resp);
    s->disconnectFromHost();
}

// ================================================================= galería de clips y capturas
namespace {
struct MediaItem {
    QString path, instance;
    bool video = false;
    QDateTime date;
};

QList<MediaItem> collectMedia()
{
    QList<MediaItem> out;
    auto* list = APPLICATION->instances();
    static const QStringList imgExt = { "*.png", "*.jpg", "*.jpeg" };
    static const QStringList vidExt = { "*.mp4", "*.webm", "*.mov", "*.mkv", "*.gif", "*.avi" };
    for (int i = 0; i < list->count(); i++) {
        auto inst = list->at(i);
        const QString root = inst->gameRoot();
        for (const auto& fi : QDir(root + "/screenshots").entryInfoList(imgExt, QDir::Files))
            out << MediaItem{ fi.absoluteFilePath(), inst->name(), false, fi.lastModified() };
        // carpetas de exportación de Flashback (y comunes)
        for (const QString sub : { "flashback/exports", "flashback/export", "flashback/videos", "flashback", "exports", "videos" })
            for (const auto& fi : QDir(root + "/" + sub).entryInfoList(vidExt, QDir::Files))
                out << MediaItem{ fi.absoluteFilePath(), inst->name(), true, fi.lastModified() };
    }
    std::sort(out.begin(), out.end(), [](const MediaItem& a, const MediaItem& b) { return a.date > b.date; });
    return out;
}

QPixmap videoThumb(int w, int h)
{
    QPixmap pm(w, h);
    pm.fill(QColor("#1c1c20"));
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(QColor(255, 255, 255, 220));
    p.setPen(Qt::NoPen);
    QPolygonF tri;
    tri << QPointF(w / 2.0 - 14, h / 2.0 - 18) << QPointF(w / 2.0 - 14, h / 2.0 + 18) << QPointF(w / 2.0 + 18, h / 2.0);
    p.drawPolygon(tri);
    return pm;
}

class GalleryPage : public QWidget {
   public:
    QGridLayout* grid = nullptr;
    QWidget* gridHost = nullptr;
    QComboBox* filter = nullptr;
    QLabel* info = nullptr;
    QString selected;
    QList<QPushButton*> actions;
    QHash<QString, QPixmap> thumbs;
};
}  // namespace

QWidget* buildGalleryPage(QWidget* parent)
{
    auto* page = new GalleryPage();
    page->setParent(parent);
    page->setObjectName(QStringLiteral("fpsPage"));
    auto* v = new QVBoxLayout(page);
    v->setContentsMargins(32, 28, 32, 24);
    v->setSpacing(14);
    auto* title = makeLabel(QObject::tr("Galería"), QStringLiteral("fpsTitle"), page);
    v->addWidget(title);
    v->addWidget(makeLabel(QObject::tr("Tus capturas y los clips exportados con Flashback, de todas las instancias."), QStringLiteral("fpsMuted"), page));
    auto* bar = new QHBoxLayout();
    page->filter = new QComboBox(page);
    page->filter->addItems({ QObject::tr("Todo"), QObject::tr("Capturas"), QObject::tr("Clips") });
    page->filter->setToolTip(QObject::tr("Muestra todo, solo capturas o solo clips de Flashback."));
    bar->addWidget(page->filter);
    bar->addStretch();
    const QStringList names = { QObject::tr("Abrir"), QObject::tr("Abrir carpeta"), QObject::tr("Copiar"), QObject::tr("Borrar") };
    const QStringList helps = { QObject::tr("Abre el archivo con el programa de Windows."), QObject::tr("Abre la carpeta donde está guardado."), QObject::tr("Copia la imagen (o el archivo del clip) para pegarlo en Discord, WhatsApp, etc."), QObject::tr("Manda el archivo a la papelera. Te pide confirmación.") };
    for (int i = 0; i < names.size(); i++) {
        const QString& n = names[i];
        auto* b = makeButton(n, QString(), i == 0, page);
        b->setToolTip(QStringLiteral("<b>%1</b><br>%2").arg(n, helps[i]));
        b->setEnabled(false);
        bar->addWidget(b);
        page->actions << b;
    }
    v->addLayout(bar);
    page->info = makeLabel(QString(), QStringLiteral("fpsHint"), page);
    v->addWidget(page->info);
    auto* scroll = new QScrollArea(page);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    page->gridHost = new QWidget(scroll);
    page->gridHost->setObjectName(QStringLiteral("fpsGridHost"));
    page->grid = new QGridLayout(page->gridHost);
    page->grid->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    page->grid->setSpacing(12);
    scroll->setWidget(page->gridHost);
    v->addWidget(scroll, 1);

    QObject::connect(page->filter, &QComboBox::currentIndexChanged, page, [page] { refreshGalleryPage(page); });
    QObject::connect(page->actions[0], &QPushButton::clicked, page, [page] { QDesktopServices::openUrl(QUrl::fromLocalFile(page->selected)); });
    QObject::connect(page->actions[1], &QPushButton::clicked, page, [page] { QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(page->selected).absolutePath())); });
    QObject::connect(page->actions[2], &QPushButton::clicked, page, [page] {
        auto* mime = new QMimeData();
        QImage img;
        if (!page->selected.endsWith(".gif") && img.load(page->selected))
            mime->setImageData(img);
        mime->setUrls({ QUrl::fromLocalFile(page->selected) });
        QApplication::clipboard()->setMimeData(mime);
        page->info->setText(QObject::tr("Copiado al portapapeles."));
    });
    QObject::connect(page->actions[3], &QPushButton::clicked, page, [page] {
        if (QMessageBox::question(page, QObject::tr("Borrar"), QObject::tr("¿Borrar \"%1\"? Se manda a la papelera si se puede.").arg(QFileInfo(page->selected).fileName())) !=
            QMessageBox::Yes)
            return;
        if (!QFile::moveToTrash(page->selected))
            QFile::remove(page->selected);
        page->selected.clear();
        refreshGalleryPage(page);
    });
    return page;
}

void refreshGalleryPage(QWidget* w)
{
    auto* page = static_cast<GalleryPage*>(w);
    while (auto* it = page->grid->takeAt(0)) {
        if (it->widget())
            it->widget()->deleteLater();
        delete it;
    }
    for (auto* b : page->actions)
        b->setEnabled(!page->selected.isEmpty() && QFile::exists(page->selected));
    const int mode = page->filter->currentIndex();
    const auto items = collectMedia();
    int shown = 0, col = 0, row = 0;
    const int cols = qMax(2, (page->gridHost->width() - 10) / 212);
    auto* group = new QButtonGroup(page->gridHost);
    for (const auto& m : items) {
        if ((mode == 1 && m.video) || (mode == 2 && !m.video))
            continue;
        if (shown >= 240)
            break;
        shown++;
        auto* tile = new QToolButton(page->gridHost);
        tile->setObjectName(QStringLiteral("fpsWardrobeTile"));
        tile->setCheckable(true);
        group->addButton(tile);
        tile->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        tile->setIconSize(QSize(192, 108));
        tile->setFixedSize(200, 150);
        QPixmap pm = page->thumbs.value(m.path);
        if (pm.isNull()) {
            if (m.video) {
                pm = videoThumb(192, 108);
            } else {
                QImageReader r(m.path);
                const QSize sz = r.size();
                if (sz.isValid())
                    r.setScaledSize(sz.scaled(192, 108, Qt::KeepAspectRatio));
                pm = QPixmap::fromImage(r.read());
            }
            page->thumbs.insert(m.path, pm);
        }
        tile->setIcon(pm);
        const QString label = (m.video ? QObject::tr("Clip · ") : QString()) + streamSafe(m.instance, QObject::tr("instancia"));
        tile->setText(tile->fontMetrics().elidedText(label, Qt::ElideRight, 186));
        tile->setToolTip(QStringLiteral("%1\n%2").arg(QFileInfo(m.path).fileName(), QLocale().toString(m.date, QLocale::ShortFormat)));
        tile->setChecked(m.path == page->selected);
        const QString path = m.path;
        QObject::connect(tile, &QToolButton::clicked, page, [page, path] {
            page->selected = path;
            for (auto* b : page->actions)
                b->setEnabled(true);
            page->info->setText(QFileInfo(path).fileName());
        });
        QObject::connect(tile, &QToolButton::pressed, page, [] {});
        page->grid->addWidget(tile, row, col);
        if (++col >= cols) {
            col = 0;
            row++;
        }
    }
    if (shown == 0)
        page->grid->addWidget(makeLabel(QObject::tr("No hay capturas ni clips todavía. En el juego, F2 saca una captura; los clips de Flashback aparecen aquí al exportarlos."),
                                        QStringLiteral("fpsHint"), page->gridHost),
                              0, 0, 1, 3);
    if (page->selected.isEmpty())
        page->info->setText(QObject::tr("%n archivo(s)", "", shown));
}

// ================================================================= servidores en vivo (portada)
namespace {
class LiveServers : public QWidget {
   public:
    QHBoxLayout* row = nullptr;
    QTimer* timer = nullptr;
    void rebuild();
};

void LiveServers::rebuild()
{
    while (auto* it = row->takeAt(0)) {
        if (it->widget())
            it->widget()->deleteLater();
        delete it;
    }
    const QStringList list = setting(cfg::LiveServerList).toStringList();
    const bool show = enabled(cfg::LiveServers) && !list.isEmpty();
    setVisible(show);
    if (!show)
        return;
    int idx = 0;
    for (const auto& entry : list) {
        idx++;
        const QString name = entry.contains('|') ? entry.section('|', 0, 0).trimmed() : entry.trimmed();
        const QString addr = entry.contains('|') ? entry.section('|', 1, 1).trimmed() : entry.trimmed();
        const bool eventOnly = entry.section('|', 2, 2).trimmed().compare("evento", Qt::CaseInsensitive) == 0;
        if (addr.isEmpty())
            continue;
        auto* card = makeCard(this);
        card->setFixedHeight(84);
        card->setMinimumWidth(250);
        card->setToolTip(eventOnly ? QObject::tr("<b>Servidor de eventos</b><br>Solo abre en eventos. Se actualiza cada minuto.")
                                   : QObject::tr("<b>Estado del servidor</b><br>Jugadores, ping y versión. Se actualiza cada minuto. Edita la lista en Ajustes → Funciones 4fps."));
        card->setMaximumWidth(380);
        auto* h = new QHBoxLayout(card);
        h->setContentsMargins(12, 10, 12, 10);
        h->setSpacing(10);
        auto* icon = new QLabel(card);
        icon->setFixedSize(44, 44);
        icon->setPixmap(QIcon::fromTheme("server").pixmap(24, 24));
        icon->setAlignment(Qt::AlignCenter);
        h->addWidget(icon);
        auto* tv = new QVBoxLayout();
        tv->setSpacing(1);
        auto* nameL = makeLabel(streamSafe(name, QObject::tr("Servidor %1").arg(idx)), QStringLiteral("fpsRowTitle"), card);
        auto* stat = makeLabel(QObject::tr("<span style='color:#a1a1aa'>●</span> Consultando..."), QStringLiteral("fpsMuted"), card);
        stat->setTextFormat(Qt::RichText);
        auto* motd = makeLabel(streamSafe(addr), QStringLiteral("fpsHint"), card);
        tv->addWidget(nameL);
        tv->addWidget(stat);
        tv->addWidget(motd);
        h->addLayout(tv, 1);
        row->addWidget(card);
        QPointer<QLabel> pIcon(icon), pStat(stat), pMotd(motd);
        pingServer(card, addr, [pIcon, pStat, pMotd, addr, eventOnly](PingResult r) {
            if (!pStat)
                return;
            if (!r.online) {
                pStat->setText(eventOnly ? QObject::tr("<span style='color:#facc15'>●</span> Pendiente de evento")
                                         : QObject::tr("<span style='color:#ef4444'>●</span> Apagado"));
                if (pMotd)
                    pMotd->setText(streamSafe(addr));
                return;
            }
            pStat->setText(QObject::tr("<span style='color:#22c55e'>●</span> En línea · %1 jugadores · %2 ms").arg(r.playersText(), r.pingMs >= 0 ? QString::number(r.pingMs) : "?"));
            if (pMotd) {
                const QString m = (r.version.isEmpty() ? QString() : r.version + " · ") + r.motd;
                pMotd->setText(pMotd->fontMetrics().elidedText(m, Qt::ElideRight, 260));
                pMotd->setToolTip(m);
            }
            if (pIcon && !r.icon.isNull())
                pIcon->setPixmap(r.icon.scaled(44, 44, Qt::KeepAspectRatio, Qt::FastTransformation));
        });
    }
    row->addStretch();
}
}  // namespace

QWidget* buildLiveServersStrip(QWidget* parent)
{
    auto* w = new LiveServers();
    w->setParent(parent);
    w->row = new QHBoxLayout(w);
    w->row->setContentsMargins(0, 0, 0, 0);
    w->row->setSpacing(12);
    w->timer = new QTimer(w);
    w->timer->setInterval(60000);
    QObject::connect(w->timer, &QTimer::timeout, w, [w] { w->rebuild(); });
    QObject::connect(Bus::get(), &Bus::settingsChanged, w, [w] { w->rebuild(); });
    w->timer->start();
    QTimer::singleShot(0, w, [w] { w->rebuild(); });
    return w;
}

// ================================================================= próximo evento (Perfil)
QWidget* buildEventCard(QWidget* parent)
{
    auto* card = makeCard(parent);
    auto* v = new QVBoxLayout(card);
    v->setContentsMargins(18, 18, 18, 18);
    v->setSpacing(6);
    v->addWidget(makeLabel(QObject::tr("PRÓXIMO EVENTO"), QStringLiteral("fpsCaption"), card));
    auto* name = makeLabel(QObject::tr("Cargando..."), QStringLiteral("fpsRowTitle"), card);
    auto* when = makeLabel(QString(), QStringLiteral("fpsMuted"), card);
    auto* desc = makeLabel(QString(), QStringLiteral("fpsHint"), card);
    desc->setWordWrap(true);
    v->addWidget(name);
    v->addWidget(when);
    v->addWidget(desc);
    if (!enabled(cfg::Events)) {
        card->hide();
        return card;
    }
    card->hide();  // aparece solo si hay un evento anunciado
    QPointer<QLabel> pName(name), pWhen(when), pDesc(desc);
    QNetworkRequest req(QUrl(setting(cfg::EventsUrl).toString()));
    req.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    auto* reply = APPLICATION->network()->get(req);
    QObject::connect(reply, &QNetworkReply::finished, card, [reply, pName, pWhen, pDesc] {
        reply->deleteLater();
        if (!pName)
            return;
        const auto doc = QJsonDocument::fromJson(reply->readAll());
        const QJsonArray events = doc.isArray() ? doc.array() : doc.object().value("eventos").toArray();
        QJsonObject next;
        QDateTime nextDate;
        const QDateTime now = QDateTime::currentDateTime();
        for (const auto& e : events) {
            const auto o = e.toObject();
            const QDateTime d = QDateTime::fromString(o.value("fecha").toString(), Qt::ISODate);
            if (d.isValid() && d > now.addSecs(-3 * 3600) && (!nextDate.isValid() || d < nextDate)) {
                next = o;
                nextDate = d;
            }
        }
        if (next.isEmpty()) {
            // sin eventos anunciados: la tarjeta no se muestra
            if (auto* card = pName->parentWidget())
                card->hide();
            pName->setText(QObject::tr("No hay eventos anunciados"));
            pWhen->setText(QString());
            pDesc->setText(QObject::tr("Cuando se anuncie uno, aparece aquí."));
            return;
        }
        if (auto* card = pName->parentWidget())
            card->show();
        pName->setText(next.value("nombre").toString());
        const qint64 secs = now.secsTo(nextDate);
        QString falta;
        if (secs > 86400)
            falta = QObject::tr("faltan %n día(s)", "", int(secs / 86400));
        else if (secs > 3600)
            falta = QObject::tr("faltan %n hora(s)", "", int(secs / 3600));
        else if (secs > 0)
            falta = QObject::tr("empieza pronto");
        else
            falta = QObject::tr("¡en curso!");
        pWhen->setText(QStringLiteral("%1 · %2").arg(QLocale(QLocale::Spanish).toString(nextDate.toLocalTime(), QStringLiteral("dddd d 'de' MMMM, HH:mm")), falta));
        pDesc->setText(next.value("descripcion").toString());
    });
    return card;
}

// ================================================================= aviso de actualización
namespace {
int cmpVer(const QString& a, const QString& b)
{
    const QStringList x = QString(a).remove(QRegularExpression("^[vV]")).section('-', 0, 0).split('.');
    const QStringList y = QString(b).remove(QRegularExpression("^[vV]")).section('-', 0, 0).split('.');
    for (int i = 0; i < qMax(x.size(), y.size()); i++) {
        const int p = i < x.size() ? x[i].toInt() : 0, q = i < y.size() ? y[i].toInt() : 0;
        if (p != q)
            return p < q ? -1 : 1;
    }
    return 0;
}
}  // namespace

QWidget* buildUpdateBanner(QWidget* parent)
{
    auto* bar = new QFrame(parent);
    bar->setObjectName(QStringLiteral("fpsUpdateBanner"));
    bar->setStyleSheet(QStringLiteral("QFrame#fpsUpdateBanner{background:#e4e4e7;}QFrame#fpsUpdateBanner QLabel{color:#09090b;font-weight:600;}"
                                      "QFrame#fpsUpdateBanner QPushButton{background:#09090b;color:#fafafa;border:none;border-radius:6px;padding:6px 14px;font-weight:700;}"
                                      "QFrame#fpsUpdateBanner QPushButton#later{background:transparent;color:#27272a;}"));
    auto* h = new QHBoxLayout(bar);
    h->setContentsMargins(24, 8, 24, 8);
    auto* text = new QLabel(bar);
    h->addWidget(text, 1);
    auto* get = new QPushButton(QObject::tr("Descargar"), bar);
    get->setToolTip(QObject::tr("Abre la página de la versión nueva para bajar el instalador."));
    get->setCursor(Qt::PointingHandCursor);
    auto* later = new QPushButton(QObject::tr("Más tarde"), bar);
    later->setObjectName(QStringLiteral("later"));
    later->setCursor(Qt::PointingHandCursor);
    h->addWidget(get);
    h->addWidget(later);
    bar->hide();
    auto url = std::make_shared<QString>(setting(cfg::UpdateUrl).toString());  // página de descargas (no el GitHub)
    QObject::connect(get, &QPushButton::clicked, bar, [url] { QDesktopServices::openUrl(QUrl(*url)); });
    QObject::connect(later, &QPushButton::clicked, bar, &QWidget::hide);
    auto check = [bar, text, url] {
        QNetworkRequest req(QUrl("https://api.github.com/repos/nofranzip/4fps-launcher/releases/latest"));
        req.setRawHeader("Accept", "application/vnd.github+json");
        req.setHeader(QNetworkRequest::UserAgentHeader, QByteArray("4fps-launcher/") + Version);
        auto* reply = APPLICATION->network()->get(req);
        QObject::connect(reply, &QNetworkReply::finished, bar, [reply, bar, text, url] {
            reply->deleteLater();
            const auto o = QJsonDocument::fromJson(reply->readAll()).object();
            const QString tag = o.value("tag_name").toString();
            if (tag.isEmpty() || cmpVer(tag, QString::fromLatin1(Version)) <= 0)
                return;
            // el botón "Descargar" siempre abre la página de descargas de 4fps, no el GitHub
            text->setText(QObject::tr("Hay una versión nueva de 4fps (%1). Tú tienes la %2. Descárgala desde la página del launcher.").arg(tag, QString::fromLatin1(Version)));
            bar->show();
        });
    };
    QTimer::singleShot(4000, bar, check);
    auto* timer = new QTimer(bar);
    timer->setInterval(6 * 3600 * 1000);
    QObject::connect(timer, &QTimer::timeout, bar, check);
    timer->start();
    return bar;
}

// ================================================================= color de la interfaz
namespace {
QString& baseStyle()
{
    static QString base;
    return base;
}

// círculo cromático: ángulo = tono, distancia al centro = saturación
class ColorWheel : public QWidget {
   public:
    std::function<void(const QColor&)> changed;
    int value = 255;
    QColor color = Qt::white;
    explicit ColorWheel(QWidget* parent) : QWidget(parent)
    {
        setFixedSize(240, 240);
        setCursor(Qt::CrossCursor);
    }
    void setColor(const QColor& c)
    {
        color = c;
        value = qMax(40, c.value());
        update();
    }
    void setValue(int v)
    {
        value = v;
        int h, s, vv;
        color.getHsv(&h, &s, &vv);
        color = QColor::fromHsv(qMax(0, h), s, value);
        update();
        if (changed)
            changed(color);
    }

   protected:
    void paintEvent(QPaintEvent*) override
    {
        const int d = qMin(width(), height());
        if (m_cache.isNull()) {
            m_cache = QImage(d, d, QImage::Format_ARGB32_Premultiplied);
            m_cache.fill(Qt::transparent);
            const double r = d / 2.0;
            for (int y = 0; y < d; y++)
                for (int x = 0; x < d; x++) {
                    const double dx = x - r + 0.5, dy = y - r + 0.5;
                    const double dist = std::sqrt(dx * dx + dy * dy);
                    if (dist > r)
                        continue;
                    double ang = std::atan2(-dy, dx) * 180.0 / M_PI;
                    if (ang < 0)
                        ang += 360;
                    QColor c = QColor::fromHsv(int(ang) % 360, int(qMin(1.0, dist / r) * 255), 255);
                    if (dist > r - 1)
                        c.setAlpha(int((r - dist) * 255));
                    m_cache.setPixelColor(x, y, c);
                }
        }
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.drawImage(0, 0, m_cache);
        // el brillo se aplica oscureciendo encima (la rueda se dibuja una sola vez)
        if (value < 255) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(0, 0, 0, 255 - value));
            p.drawEllipse(QRectF(0, 0, d, d));
        }
        // marcador
        int h, s, v;
        color.getHsv(&h, &s, &v);
        const double r = d / 2.0;
        const double ang = (h < 0 ? 0 : h) * M_PI / 180.0;
        const QPointF pt(r + std::cos(ang) * s / 255.0 * r, r - std::sin(ang) * s / 255.0 * r);
        p.setPen(QPen(Qt::black, 3));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(pt, 8, 8);
        p.setPen(QPen(Qt::white, 2));
        p.drawEllipse(pt, 8, 8);
    }
    void mousePressEvent(QMouseEvent* e) override { pick(e->position()); }
    void mouseReleaseEvent(QMouseEvent*) override
    {
        if (released)
            released();
    }

   public:
    std::function<void()> released;

   protected:
    void mouseMoveEvent(QMouseEvent* e) override
    {
        if (e->buttons() & Qt::LeftButton)
            pick(e->position());
    }

   private:
    QImage m_cache;
    void pick(const QPointF& pos)
    {
        const double r = qMin(width(), height()) / 2.0;
        const double dx = pos.x() - r, dy = pos.y() - r;
        double ang = std::atan2(-dy, dx) * 180.0 / M_PI;
        if (ang < 0)
            ang += 360;
        const double sat = qMin(1.0, std::sqrt(dx * dx + dy * dy) / r);
        color = QColor::fromHsv(int(ang) % 360, int(sat * 255), value);
        update();
        if (changed)
            changed(color);
    }
};
}  // namespace

static bool g_bgActive = false;  // hay imagen/tema detrás: las páginas se vuelven transparentes

void applyAccent(const QColor& preview)
{
    QString& base = baseStyle();
    if (base.isEmpty())
        base = qApp->styleSheet();
    if (base.isEmpty())
        return;
    const QColor a = preview.isValid() ? preview : QColor(setting(cfg::Accent).toString());
    // con imagen de fondo, la parte central se vuelve transparente (las tarjetas siguen sólidas)
    const QString bgRules = !g_bgActive
                                ? QString()
                                : QStringLiteral("\nQStackedWidget#fpsStack, QStackedWidget#fpsStack QWidget#fpsPage, QStackedWidget#fpsStack QWidget#fpsGridHost,"
                                                 " QStackedWidget#fpsStack QScrollArea, QStackedWidget#fpsStack QScrollArea > QWidget > QWidget"
                                                 " { background: transparent; }\n");
    if (!a.isValid() || a == QColor(Qt::white)) {
        if (qApp->styleSheet() != base + bgRules)
            qApp->setStyleSheet(base + bgRules);
        return;
    }
    // texto sobre el color: negro si el color es claro, blanco si es oscuro
    const double lum = 0.2126 * a.redF() + 0.7152 * a.greenF() + 0.0722 * a.blueF();
    const QString acc = a.name(), on = lum > 0.55 ? QStringLiteral("#0d0d0e") : QStringLiteral("#ffffff");
    const QString hover = (lum > 0.5 ? a.darker(112) : a.lighter(118)).name();
    const QString press = (lum > 0.5 ? a.darker(125) : a.lighter(135)).name();
    QString q = base;
    q.replace(QRegularExpression("((?:selection-)?background(?:-color)?:\\s*)#ffffff", QRegularExpression::CaseInsensitiveOption), "\\1" + acc);
    q.replace(QRegularExpression("(border[\\w-]*:[^;{}]*?)#ffffff", QRegularExpression::CaseInsensitiveOption), "\\1" + acc);
    q.replace(QRegularExpression("(background(?:-color)?:\\s*)#e4e4e7", QRegularExpression::CaseInsensitiveOption), "\\1" + hover);
    q.replace(QRegularExpression("(border-color:\\s*)#e4e4e7", QRegularExpression::CaseInsensitiveOption), "\\1" + hover);
    q.replace(QRegularExpression("(background(?:-color)?:\\s*)#d4d4d8", QRegularExpression::CaseInsensitiveOption), "\\1" + press);
    q.replace(QRegularExpression("((?<![\\w-])color:\\s*|selection-color:\\s*)#0d0d0e", QRegularExpression::CaseInsensitiveOption), "\\1" + on);
    if (qApp->styleSheet() != q + bgRules)  // no repinta todo si no cambió nada
        qApp->setStyleSheet(q + bgRules);
}

void openAccentPicker(QWidget* parent)
{
    const QColor original(setting(cfg::Accent).toString());
    QDialog dlg(parent);
    dlg.setWindowTitle(QObject::tr("Color y fondo del launcher"));
    auto* v = new QVBoxLayout(&dlg);
    v->setSpacing(12);
    auto* intro = new QLabel(QObject::tr("Elige el color de los botones, la selección y los bordes. El fondo sigue negro."), &dlg);
    intro->setWordWrap(true);
    v->addWidget(intro);
    auto* row = new QHBoxLayout();
    auto* wheel = new ColorWheel(&dlg);
    wheel->setColor(original.isValid() ? original : QColor(Qt::white));
    row->addWidget(wheel);
    auto* side = new QVBoxLayout();
    auto* brightLabel = new QLabel(QObject::tr("Brillo"), &dlg);
    side->addWidget(brightLabel);
    auto* bright = new QSlider(Qt::Horizontal, &dlg);
    bright->setRange(40, 255);
    bright->setValue(wheel->value);
    side->addWidget(bright);
    auto* swatch = new QLabel(&dlg);
    swatch->setFixedSize(120, 44);
    side->addWidget(swatch);
    auto* hex = new QLineEdit(&dlg);
    hex->setPlaceholderText(QStringLiteral("#ff69b4"));
    hex->setMaxLength(7);
    side->addWidget(hex);
    side->addWidget(new QLabel(QObject::tr("Rápidos"), &dlg));
    auto* grid = new QGridLayout();
    grid->setSpacing(6);
    const QList<QPair<QString, QString>> presets = { { "#ffffff", QObject::tr("Blanco") }, { "#ff69b4", QObject::tr("Rosa") },   { "#ef4444", QObject::tr("Rojo") },
                                                    { "#f97316", QObject::tr("Naranja") }, { "#facc15", QObject::tr("Amarillo") }, { "#22c55e", QObject::tr("Verde") },
                                                    { "#22d3ee", QObject::tr("Cian") },    { "#3b82f6", QObject::tr("Azul") },    { "#a855f7", QObject::tr("Morado") } };
    int i = 0;
    QList<QPushButton*> presetButtons;
    for (const auto& pr : presets) {
        auto* b = new QPushButton(&dlg);
        b->setFixedSize(28, 28);
        b->setToolTip(pr.second);
        b->setCursor(Qt::PointingHandCursor);
        b->setStyleSheet(QStringLiteral("QPushButton{background:%1;border:2px solid #3a3a40;border-radius:14px;}QPushButton:hover{border-color:#f4f4f5;}").arg(pr.first));
        b->setProperty("hex", pr.first);
        grid->addWidget(b, i / 3, i % 3);
        presetButtons << b;
        i++;
    }
    side->addLayout(grid);
    side->addStretch();
    row->addLayout(side);
    v->addLayout(row);
    // imagen de fondo de la parte central
    auto* bgBox = new QFrame(&dlg);
    auto* bgv = new QVBoxLayout(bgBox);
    bgv->setContentsMargins(0, 8, 0, 0);
    bgv->addWidget(new QLabel(QObject::tr("<b>Imagen de fondo</b> (solo la parte central; el menú y la barra de abajo no cambian)"), bgBox));
    auto* bgRow = new QHBoxLayout();
    auto* bgName = new QLabel(bgBox);
    auto* bgPick = new QPushButton(QObject::tr("Elegir imagen..."), bgBox);
    auto* bgClear = new QPushButton(QObject::tr("Quitar"), bgBox);
    bgRow->addWidget(bgName, 1);
    bgRow->addWidget(bgPick);
    bgRow->addWidget(bgClear);
    bgv->addLayout(bgRow);
    auto* dimRow = new QHBoxLayout();
    dimRow->addWidget(new QLabel(QObject::tr("Oscurecer"), bgBox));
    auto* dim = new QSlider(Qt::Horizontal, bgBox);
    dim->setRange(0, 90);
    dim->setValue(setting(cfg::BgDim).toInt());
    dimRow->addWidget(dim, 1);
    bgv->addLayout(dimRow);
    v->addWidget(bgBox);
    if (liteMode()) {
        // PC de 8 GB o menos: solo los colores predeterminados (la rueda y la imagen de fondo gastan más)
        wheel->hide();
        brightLabel->hide();
        bright->hide();
        hex->hide();
        bgBox->hide();
        intro->setText(QObject::tr("Modo ligero: tu PC tiene 8 GB de RAM o menos, así que solo puedes usar los colores predeterminados. "
                                   "(Se quita en Ajustes → Funciones 4fps → \"Ajustar el launcher a mi PC\").")
                       );
    }
    const QString oldBg = setting(cfg::BgImage).toString();
    const int oldDim = setting(cfg::BgDim).toInt();
    auto bgPath = std::make_shared<QString>(oldBg);
    auto refreshBgName = [bgName, bgPath, bgClear] {
        bgName->setText(bgPath->isEmpty() ? QObject::tr("Sin imagen") : QObject::tr("Imagen elegida"));
        bgClear->setEnabled(!bgPath->isEmpty());
    };
    refreshBgName();
    QObject::connect(bgPick, &QPushButton::clicked, &dlg, [&dlg, bgPath, refreshBgName] {
        const QString f = QFileDialog::getOpenFileName(&dlg, QObject::tr("Imagen de fondo"), QStandardPaths::writableLocation(QStandardPaths::PicturesLocation),
                                                       QObject::tr("Imágenes (*.png *.jpg *.jpeg *.webp *.bmp *.gif)"));
        QImage img(f);
        if (img.isNull())
            return;
        const QString target = APPLICATION->dataRoot() + "/fondo-launcher.png";
        img.scaled(QSize(2560, 1440), Qt::KeepAspectRatio, Qt::SmoothTransformation).save(target, "PNG");
        *bgPath = target;
        setSettingQuiet(cfg::BgImage, QString());  // fuerza recarga
        emit Bus::get()->backgroundChanged();
        setSettingQuiet(cfg::BgImage, target);  // vista previa en vivo
        emit Bus::get()->backgroundChanged();
        refreshBgName();
    });
    QObject::connect(bgClear, &QPushButton::clicked, &dlg, [bgPath, refreshBgName] {
        bgPath->clear();
        setSettingQuiet(cfg::BgImage, QString());
        emit Bus::get()->backgroundChanged();
        refreshBgName();
    });
    QObject::connect(dim, &QSlider::valueChanged, &dlg, [](int val) {
        setSettingQuiet(cfg::BgDim, val);
        emit Bus::get()->backgroundChanged();
    });

    auto* bb = new QDialogButtonBox(&dlg);
    auto* reset = bb->addButton(QObject::tr("Volver al blanco"), QDialogButtonBox::ResetRole);
    bb->addButton(QObject::tr("Guardar"), QDialogButtonBox::AcceptRole);
    bb->addButton(QObject::tr("Cancelar"), QDialogButtonBox::RejectRole);
    v->addWidget(bb);

    auto current = std::make_shared<QColor>(wheel->color);
    // la vista previa en todo el launcher es pesada: se aplica cuando dejas de mover la rueda (antes se aplicaba
    // en cada pixel que movías el mouse y se trababa todo)
    auto* previewTimer = new QTimer(&dlg);
    previewTimer->setSingleShot(true);
    previewTimer->setInterval(180);
    QObject::connect(previewTimer, &QTimer::timeout, &dlg, [current] { applyAccent(*current); });
    wheel->released = [previewTimer, current] {
        previewTimer->stop();
        applyAccent(*current);
    };
    auto show = [swatch, hex, current, previewTimer](const QColor& c, bool updateHex) {
        *current = c;
        swatch->setStyleSheet(QStringLiteral("background:%1;border-radius:8px;").arg(c.name()));
        if (updateHex)
            hex->setText(c.name());
        previewTimer->start();  // vista previa en vivo (con pausa)
    };
    wheel->changed = [show](const QColor& c) { show(c, true); };
    QObject::connect(bright, &QSlider::valueChanged, &dlg, [wheel](int val) { wheel->setValue(val); });
    QObject::connect(hex, &QLineEdit::textEdited, &dlg, [wheel, bright, show](const QString& t) {
        const QColor c(t.startsWith('#') ? t : '#' + t);
        if (c.isValid() && t.size() >= 6) {
            wheel->setColor(c);
            bright->blockSignals(true);
            bright->setValue(wheel->value);
            bright->blockSignals(false);
            show(c, false);
        }
    });
    for (auto* b : presetButtons)
        QObject::connect(b, &QPushButton::clicked, &dlg, [b, wheel, bright, show] {
            const QColor c(b->property("hex").toString());
            wheel->setColor(c);
            bright->blockSignals(true);
            bright->setValue(wheel->value);
            bright->blockSignals(false);
            show(c, true);
        });
    QObject::connect(reset, &QPushButton::clicked, &dlg, [wheel, bright, show] {
        wheel->setColor(Qt::white);
        bright->blockSignals(true);
        bright->setValue(255);
        bright->blockSignals(false);
        show(Qt::white, true);
    });
    QObject::connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    show(*current, true);
    const int result = dlg.exec();
    previewTimer->stop();
    if (result == QDialog::Accepted) {
        setSettingQuiet(cfg::Accent, current->name());
        setSettingQuiet(cfg::BgImage, *bgPath);
    } else {
        // cancelar: vuelve el color y el fondo de antes
        setSettingQuiet(cfg::BgImage, oldBg);
        setSettingQuiet(cfg::BgDim, oldDim);
    }
    emit Bus::get()->backgroundChanged();
    applyAccent();
}

// ================================================================= fondo de la parte central
namespace {

struct EventBg {
    QString name, image, color;
    int dim = 55;
};

QString seasonsCache()
{
    return APPLICATION->dataRoot() + "/fondos.json";
}

// evento activo hoy según fondos.json (bajado del GitHub)
EventBg activeEvent()
{
    QFile f(seasonsCache());
    if (!f.open(QIODevice::ReadOnly))
        return {};
    const auto doc = QJsonDocument::fromJson(f.readAll());
    const QDate today = QDate::currentDate();
    for (const auto& v : doc.object().value("eventos").toArray()) {
        const auto o = v.toObject();
        const QDate from = QDate::fromString(o.value("desde").toString(), Qt::ISODate);
        const QDate to = QDate::fromString(o.value("hasta").toString(), Qt::ISODate);
        if (!from.isValid() || !to.isValid() || today < from || today > to)
            continue;
        EventBg e;
        e.name = o.value("nombre").toString();
        e.color = o.value("color").toString("#a1a1aa");
        e.dim = qBound(0, o.value("oscurecer").toInt(60), 90);
        const QString url = o.value("imagen").toString();
        if (!url.isEmpty()) {
            const QString file = APPLICATION->dataRoot() + "/fondos/" + QCryptographicHash::hash(url.toUtf8(), QCryptographicHash::Sha1).toHex() + ".png";
            if (QFile::exists(file))
                e.image = file;
        }
        return e;
    }
    return {};
}

QString seasonByDate(const QDate& d)
{
    const int m = d.month(), day = d.day();
    if ((m == 12 && day >= 27) || (m == 1 && day <= 3))
        return "anonuevo";
    if (m == 12)
        return "navidad";
    if (m == 10)
        return "halloween";
    if (m == 9 && day >= 10 && day <= 20)
        return "fiestaspatrias";
    if (m == 1 || m == 2 || (m == 3 && day <= 20))
        return "verano";  // verano en Chile
    return "normal";
}

QString seasonLabel(const QString& s)
{
    if (s == "halloween")
        return QObject::tr("Halloween");
    if (s == "navidad")
        return QObject::tr("Navidad");
    if (s == "anonuevo")
        return QObject::tr("Año Nuevo");
    if (s == "verano")
        return QObject::tr("Verano");
    if (s == "fiestaspatrias")
        return QObject::tr("Fiestas Patrias");
    return {};
}

// dibuja el tema de temporada (suave, para que el texto se siga leyendo bien)
void paintSeason(QPainter& p, const QSize& sz, const QString& season, const QColor& eventColor)
{
    const int w = sz.width(), h = sz.height();
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(0, 0, w, h, QColor("#0d0d0e"));
    QRandomGenerator rnd(4);
    auto glow = [&](QPointF c, qreal r, QColor col) {
        QRadialGradient g(c, r);
        g.setColorAt(0, col);
        col.setAlpha(0);
        g.setColorAt(1, col);
        p.fillRect(0, 0, w, h, g);
    };
    if (season == "halloween") {
        glow(QPointF(w * 0.85, h * 1.05), h * 0.9, QColor(249, 115, 22, 70));
        glow(QPointF(w * 0.1, -h * 0.1), h * 0.8, QColor(126, 34, 206, 60));
        // luna
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(254, 243, 199, 22));
        p.drawEllipse(QPointF(w * 0.62, h * 0.13), h * 0.06, h * 0.06);
        // murciélagos
        p.setPen(QPen(QColor(10, 10, 10, 170), 3, Qt::SolidLine, Qt::RoundCap));
        for (int i = 0; i < 9; i++) {
            const qreal x = rnd.bounded(w), y = rnd.bounded(h / 2), s = 8 + rnd.bounded(14);
            QPainterPath bat;
            bat.moveTo(x - s, y - s * 0.3);
            bat.quadTo(x - s * 0.5, y - s * 0.6, x, y);
            bat.quadTo(x + s * 0.5, y - s * 0.6, x + s, y - s * 0.3);
            p.drawPath(bat);
        }
        // calabazas abajo
        for (int i = 0; i < 4; i++) {
            const qreal r = 22 + rnd.bounded(18), x = w * (0.08 + 0.26 * i) + rnd.bounded(40), y = h - r * 0.6;
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(234, 88, 12, 55));
            p.drawEllipse(QPointF(x, y), r * 1.2, r);
            p.setBrush(QColor(13, 13, 14, 120));
            p.drawEllipse(QPointF(x - r * 0.4, y - r * 0.2), r * 0.15, r * 0.15);
            p.drawEllipse(QPointF(x + r * 0.4, y - r * 0.2), r * 0.15, r * 0.15);
        }
    } else if (season == "navidad") {
        glow(QPointF(w * 0.5, -h * 0.2), h, QColor(22, 101, 52, 70));
        glow(QPointF(w * 0.9, h * 1.1), h * 0.7, QColor(185, 28, 28, 55));
        p.setPen(Qt::NoPen);
        for (int i = 0; i < 140; i++) {
            const qreal r = 1 + rnd.bounded(3.0);
            p.setBrush(QColor(255, 255, 255, 40 + rnd.bounded(80)));
            p.drawEllipse(QPointF(rnd.bounded(w), rnd.bounded(h)), r, r);
        }
        // nieve acumulada
        QPainterPath snow;
        snow.moveTo(0, h);
        for (int x = 0; x <= w; x += 40)
            snow.lineTo(x, h - 10 - rnd.bounded(14));
        snow.lineTo(w, h);
        p.setBrush(QColor(244, 244, 245, 35));
        p.drawPath(snow);
    } else if (season == "anonuevo") {
        glow(QPointF(w * 0.5, h * 0.4), h, QColor(30, 58, 138, 60));
        const QList<QColor> cols = { QColor("#facc15"), QColor("#f472b6"), QColor("#22d3ee"), QColor("#a855f7"), QColor("#f4f4f5") };
        p.setPen(Qt::NoPen);
        for (int i = 0; i < 110; i++) {
            QColor c = cols[rnd.bounded(cols.size())];
            c.setAlpha(60 + rnd.bounded(70));
            p.save();
            p.translate(rnd.bounded(w), rnd.bounded(h));
            p.rotate(rnd.bounded(180));
            p.fillRect(QRectF(-4, -2, 8, 4), c);
            p.restore();
        }
        // fuegos artificiales
        for (int k = 0; k < 3; k++) {
            const QPointF c(w * (0.2 + 0.3 * k), h * (0.2 + 0.1 * (k % 2)));
            QColor col = cols[k];
            col.setAlpha(70);
            p.setPen(QPen(col, 2, Qt::SolidLine, Qt::RoundCap));
            for (int a = 0; a < 16; a++) {
                const qreal ang = a * M_PI / 8, r1 = 18, r2 = 55;
                p.drawLine(c + QPointF(std::cos(ang) * r1, std::sin(ang) * r1), c + QPointF(std::cos(ang) * r2, std::sin(ang) * r2));
            }
        }
    } else if (season == "verano") {
        QLinearGradient g(0, 0, 0, h);
        g.setColorAt(0, QColor(249, 115, 22, 55));
        g.setColorAt(0.45, QColor(219, 39, 119, 25));
        g.setColorAt(1, QColor(13, 13, 14, 0));
        p.fillRect(0, 0, w, h, g);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(253, 224, 71, 45));
        p.drawEllipse(QPointF(w * 0.78, h * 0.3), h * 0.12, h * 0.12);
        // olas
        p.setPen(QPen(QColor(34, 211, 238, 40), 3));
        for (int k = 0; k < 4; k++) {
            QPainterPath wave;
            const qreal y = h * (0.72 + 0.07 * k);
            wave.moveTo(0, y);
            for (int x = 0; x <= w; x += 60)
                wave.quadTo(x + 15, y - 8, x + 30, y), wave.quadTo(x + 45, y + 8, x + 60, y);
            p.drawPath(wave);
        }
    } else if (season == "fiestaspatrias") {
        // bandera chilena muy suave
        p.fillRect(QRectF(0, 0, w, h / 2.0), QColor(244, 244, 245, 14));
        p.fillRect(QRectF(0, h / 2.0, w, h / 2.0), QColor(213, 43, 30, 30));
        p.fillRect(QRectF(0, 0, h / 2.0, h / 2.0), QColor(0, 57, 166, 45));
        QPainterPath star;
        const QPointF c(h / 4.0, h / 4.0);
        for (int i = 0; i < 10; i++) {
            const qreal r = i % 2 ? h * 0.035 : h * 0.09, a = -M_PI / 2 + i * M_PI / 5;
            const QPointF pt(c.x() + std::cos(a) * r, c.y() + std::sin(a) * r);
            i == 0 ? star.moveTo(pt) : star.lineTo(pt);
        }
        star.closeSubpath();
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(255, 255, 255, 60));
        p.drawPath(star);
    } else if (season == "evento") {
        QColor c = eventColor.isValid() ? eventColor : QColor("#a1a1aa");
        c.setAlpha(60);
        glow(QPointF(w * 0.5, h * 0.2), h, c);
        c.setAlpha(35);
        glow(QPointF(w * 0.9, h), h * 0.7, c);
    }
}

class BackgroundHost : public QWidget {
   public:
    QPixmap image;
    QString loaded, theme;
    EventBg event;
    bool initialized = false;
    QPixmap scaled;
    QSize scaledFor;
    QString scaledTheme;
    void reload()
    {
        // prioridad: tu imagen > evento (fondos.json) > temporada > gris normal
        const QString user = setting(cfg::BgImage).toString();
        event = EventBg();
        QString t, path = user;
        if (user.isEmpty()) {
            t = currentSeason();
            if (t == "evento") {
                event = activeEvent();
                path = event.image;
            }
        }
        const bool wasActive = g_bgActive;
        g_bgActive = !path.isEmpty() || (!t.isEmpty() && t != "normal");
        if (path != loaded || !initialized) {
            loaded = path;
            image = path.isEmpty() ? QPixmap() : QPixmap(path);
            scaled = QPixmap();
        }
        if (t != theme)
            scaled = QPixmap();
        theme = t;
        if (!initialized || wasActive != g_bgActive)
            applyAccent();  // activa/desactiva la transparencia de las páginas
        initialized = true;
        update();
    }

   protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.fillRect(rect(), QColor("#0d0d0e"));
        if (!image.isNull()) {
            if (scaled.isNull() || scaledFor != size()) {
                scaled = image.scaled(size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
                scaledFor = size();
            }
            p.drawPixmap((width() - scaled.width()) / 2, (height() - scaled.height()) / 2, scaled);
            const int dim = loaded == setting(cfg::BgImage).toString() ? qBound(0, setting(cfg::BgDim).toInt(), 90) : event.dim;
            p.fillRect(rect(), QColor(13, 13, 14, dim * 255 / 100));
        } else if (!theme.isEmpty() && theme != "normal") {
            if (scaled.isNull() || scaledFor != size() || scaledTheme != theme) {
                scaled = QPixmap(size());
                QPainter sp(&scaled);
                paintSeason(sp, size(), theme, QColor(event.color));
                scaledFor = size();
                scaledTheme = theme;
            }
            p.drawPixmap(0, 0, scaled);
        } else {
            return;
        }
        // etiqueta chiquita abajo a la derecha
        const QString label = theme == "evento" ? QObject::tr("Evento: %1").arg(event.name) : seasonLabel(theme);
        if (!label.isEmpty()) {
            QFont f = font();
            f.setPointSizeF(f.pointSizeF() * 0.85);
            p.setFont(f);
            p.setPen(QColor(244, 244, 245, 90));
            p.drawText(rect().adjusted(0, 0, -12, -8), Qt::AlignRight | Qt::AlignBottom, label);
        }
    }
};

// baja fondos.json y las imágenes de eventos (solo desde GitHub)
void syncSeasons(QPointer<BackgroundHost> host)
{
    const QUrl url(setting(cfg::SeasonUrl).toString());
    if (!url.isValid() || url.scheme() != "https")
        return;
    QNetworkRequest req(url);
    req.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    req.setTransferTimeout(15000);
    auto* reply = APPLICATION->network()->get(req);
    QObject::connect(reply, &QNetworkReply::finished, reply, [reply, host] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError)
            return;
        const QByteArray data = reply->readAll();
        const auto doc = QJsonDocument::fromJson(data);
        if (!doc.isObject())
            return;
        QFile f(seasonsCache());
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            f.write(data);
        f.close();
        QDir().mkpath(APPLICATION->dataRoot() + "/fondos");
        const QDate today = QDate::currentDate();
        for (const auto& v : doc.object().value("eventos").toArray()) {
            const auto o = v.toObject();
            const QDate to = QDate::fromString(o.value("hasta").toString(), Qt::ISODate);
            const QUrl u(o.value("imagen").toString());
            const QString hostName = u.host().toLower();
            if (!to.isValid() || to < today || u.scheme() != "https" || !(hostName == "raw.githubusercontent.com" || hostName == "github.com"))
                continue;
            const QString file = APPLICATION->dataRoot() + "/fondos/" + QCryptographicHash::hash(u.toString().toUtf8(), QCryptographicHash::Sha1).toHex() + ".png";
            if (QFile::exists(file))
                continue;
            QNetworkRequest ir(u);
            ir.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
            ir.setTransferTimeout(30000);
            auto* rep = APPLICATION->network()->get(ir);
            QObject::connect(rep, &QNetworkReply::finished, rep, [rep, file, host] {
                rep->deleteLater();
                QImage img;
                if (rep->error() == QNetworkReply::NoError && img.loadFromData(rep->readAll()))
                    img.scaled(QSize(2560, 1440), Qt::KeepAspectRatio, Qt::SmoothTransformation).save(file, "PNG");
                if (host)
                    host->reload();
            });
        }
        if (host)
            host->reload();
    });
}
}  // namespace

QString currentSeason()
{
    if (!enabled(cfg::SeasonalBg))
        return QStringLiteral("normal");
    const QString o = setting(cfg::SeasonOverride).toString();
    if (!o.isEmpty() && o != "auto")
        return o;
    if (!activeEvent().name.isEmpty())
        return QStringLiteral("evento");
    return seasonByDate(QDate::currentDate());
}

QWidget* buildBackgroundHost(QWidget* parent, QWidget* content)
{
    auto* host = new BackgroundHost();
    host->setParent(parent);
    host->setObjectName(QStringLiteral("fpsBgHost"));
    auto* l = new QVBoxLayout(host);
    l->setContentsMargins(0, 0, 0, 0);
    l->addWidget(content);
    QObject::connect(Bus::get(), &Bus::settingsChanged, host, [host] { host->reload(); });
    QObject::connect(Bus::get(), &Bus::backgroundChanged, host, [host] { host->reload(); });
    QTimer::singleShot(0, host, [host] { host->reload(); });
    QTimer::singleShot(3000, host, [host] { syncSeasons(host); });
    // revisa la fecha cada hora (por si el launcher queda abierto al cambiar de mes)
    auto* t = new QTimer(host);
    t->setInterval(60 * 60 * 1000);
    QObject::connect(t, &QTimer::timeout, host, [host] { host->reload(); });
    t->start();
    return host;
}

}  // namespace fps
