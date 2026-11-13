// 4fps - funciones extra (parte 2): Discord Rich Presence, letras de canciones, galería, servidores en vivo, eventos
#include "FourFpsFeatures.h"
#include "FourFpsShell.h"

#include <QApplication>
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

// ventana flotante con la letra (no recibe clics)
class LyricsOverlay : public QWidget {
   public:
    LyricsOverlay() : QWidget(nullptr, Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool | Qt::WindowTransparentForInput)
    {
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_ShowWithoutActivating);
        resize(900, 110);
        if (auto* sc = QGuiApplication::primaryScreen()) {
            const QRect g = sc->availableGeometry();
            move(g.center().x() - width() / 2, g.bottom() - height() - 90);
        }
    }
    QString line, next;
    double fade = 1.0;

   protected:
    void paintEvent(QPaintEvent*) override
    {
        if (line.isEmpty())
            return;
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        QFont f = font();
        f.setPixelSize(30);
        f.setBold(true);
        p.setFont(f);
        const int w = qMin(width() - 20, p.fontMetrics().horizontalAdvance(line) + 40);
        QRectF bg((width() - w) / 2.0, 6, w, 48);
        QPainterPath path;
        path.addRoundedRect(bg, 12, 12);
        p.fillPath(path, QColor(10, 10, 12, int(170 * fade)));
        p.setPen(QColor(255, 255, 255, int(255 * fade)));
        p.drawText(bg, Qt::AlignCenter, line);
        if (!next.isEmpty()) {
            f.setPixelSize(17);
            f.setBold(false);
            p.setFont(f);
            p.setPen(QColor(200, 200, 205, int(190 * fade)));
            p.drawText(QRectF(0, 60, width(), 30), Qt::AlignCenter, next);
        }
    }
};
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
        emit updated();
        return;
    }
    QUrl url("https://lrclib.net/api/get");
    QUrlQuery q;
    q.addQueryItem("track_name", title);
    q.addQueryItem("artist_name", artist);
    if (!album.isEmpty())
        q.addQueryItem("album_name", album);
    if (durationMs > 0)
        q.addQueryItem("duration", QString::number(qRound(durationMs / 1000)));
    url.setQuery(q);
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, "4fps-launcher (https://github.com/nofranzip/4fps-launcher)");
    const QString key = m_trackKey;
    auto* reply = APPLICATION->network()->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, key, cacheFile] {
        reply->deleteLater();
        if (key != m_trackKey)
            return;
        QJsonObject o = QJsonDocument::fromJson(reply->readAll()).object();
        if (o.value("syncedLyrics").toString().isEmpty()) {
            // segundo intento: búsqueda sin álbum/duración
            QUrl s("https://lrclib.net/api/search");
            QUrlQuery q;
            q.addQueryItem("track_name", title);
            q.addQueryItem("artist_name", artist);
            s.setQuery(q);
            QNetworkRequest req(s);
            req.setHeader(QNetworkRequest::UserAgentHeader, "4fps-launcher (https://github.com/nofranzip/4fps-launcher)");
            auto* r2 = APPLICATION->network()->get(req);
            connect(r2, &QNetworkReply::finished, this, [this, r2, key, cacheFile] {
                r2->deleteLater();
                if (key != m_trackKey)
                    return;
                QJsonObject best;
                for (const auto& v : QJsonDocument::fromJson(r2->readAll()).array())
                    if (!v.toObject().value("syncedLyrics").toString().isEmpty()) {
                        best = v.toObject();
                        break;
                    }
                QFile f(cacheFile);
                if (f.open(QIODevice::WriteOnly))
                    f.write(QJsonDocument(QJsonObject{ { "syncedLyrics", best.value("syncedLyrics") } }).toJson());
                m_lines = parseLrc(best.value("syncedLyrics").toString());
                synced = !m_lines.isEmpty();
                emit updated();
            });
            return;
        }
        QFile f(cacheFile);
        if (f.open(QIODevice::WriteOnly))
            f.write(QJsonDocument(QJsonObject{ { "syncedLyrics", o.value("syncedLyrics") } }).toJson());
        m_lines = parseLrc(o.value("syncedLyrics").toString());
        synced = !m_lines.isEmpty();
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
        cur = QObject::tr("Sin letra sincronizada");
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
        ov->line = playing ? cur : QString();
        ov->next = nxt;
        ov->update();
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
    setVisible(enabled(cfg::LiveServers));
    if (!enabled(cfg::LiveServers))
        return;
    const QStringList list = setting(cfg::LiveServerList).toStringList();
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
            pName->setText(QObject::tr("No hay eventos anunciados"));
            pWhen->setText(QString());
            pDesc->setText(QObject::tr("Cuando se anuncie uno, aparece aquí."));
            return;
        }
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
    auto url = std::make_shared<QString>(QStringLiteral("https://github.com/nofranzip/4fps-launcher/releases/latest"));
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
            if (!o.value("html_url").toString().isEmpty())
                *url = o.value("html_url").toString();
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

}  // namespace fps
