// 4fps - funciones extra: ajustes, conflictos de mods, crashes, servidores en vivo, galería,
// modo stream, modo PC lento, Discord Rich Presence y letras de canciones.
#pragma once

#include <QDateTime>
#include <QImage>
#include <QObject>
#include <QPixmap>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QWidget>
#include <functional>

#include "ui/pages/BasePage.h"

class MinecraftInstance;
class BaseInstance;
class QLocalSocket;
class QTcpServer;
class QTcpSocket;
class QProcess;
class QTimer;
class QLabel;
class QCheckBox;
class QPlainTextEdit;
class QLineEdit;
class QSpinBox;

namespace fps {

// versión de 4fps (se compara con la última release de GitHub)
inline const char* Version = "1.1.1";

// ---------------------------------------------------------------- ajustes
namespace cfg {
inline const char* ConflictCheck = "FourFps_ConflictCheck";
inline const char* CrashExplainer = "FourFps_CrashExplainer";
inline const char* ModGroups = "FourFps_ModGroups";
inline const char* LiveServers = "FourFps_LiveServers";
inline const char* LiveServerList = "FourFps_LiveServerList";
inline const char* Events = "FourFps_Events";
inline const char* EventsUrl = "FourFps_EventsUrl";
inline const char* Gallery = "FourFps_Gallery";
inline const char* StreamMode = "FourFps_StreamMode";
inline const char* SlowPc = "FourFps_SlowPc";
inline const char* DiscordRpc = "FourFps_DiscordRpc";
inline const char* DiscordAppId = "FourFps_DiscordAppId";
inline const char* Lyrics = "FourFps_Lyrics";
inline const char* LyricsOffset = "FourFps_LyricsOffset";
inline const char* LyricsOverlay = "FourFps_LyricsOverlay";
inline const char* LyricsPort = "FourFps_LyricsPort";
}  // namespace cfg

void registerSettings();
bool enabled(const char* key);
QVariant setting(const char* key);
void setSetting(const char* key, const QVariant& v);

// Notifica cambios de ajustes 4fps (modo stream, servidores...) a quien esté escuchando
class Bus : public QObject {
    Q_OBJECT
   public:
    static Bus* get();
   signals:
    void settingsChanged();
};

// texto que se oculta en modo stream
QString streamSafe(const QString& text, const QString& replacement = QStringLiteral("••••••"));
bool streamMode();

// página "4fps" en Ajustes
class SettingsPage : public QWidget, public BasePage {
    Q_OBJECT
   public:
    explicit SettingsPage(QWidget* parent = nullptr);
    QString displayName() const override { return tr("Funciones 4fps"); }
    QIcon icon() const override;
    QString id() const override { return "fourfps-features"; }
    QString helpPage() const override { return QString(); }
    bool apply() override;

   private:
    QList<QPair<const char*, QCheckBox*>> m_checks;
    QPlainTextEdit* m_servers = nullptr;
    QLineEdit* m_discordId = nullptr;
    QLineEdit* m_eventsUrl = nullptr;
    QSpinBox* m_offset = nullptr;
};

// ---------------------------------------------------------------- ping de servidores (Server List Ping)
struct PingResult {
    bool online = false;
    QPixmap icon;
    int players = -1, maxPlayers = -1, pingMs = -1;
    QString version, motd;
    QString playersText() const;
};
void pingServer(QObject* ctx, const QString& address, std::function<void(PingResult)> done);

// ---------------------------------------------------------------- detector de conflictos
struct ConflictIssue {
    QString title;    // qué pasa
    QString detail;   // por qué
    QString fix;      // qué hacer
    QStringList mods; // archivos involucrados
};
QList<ConflictIssue> scanModConflicts(MinecraftInstance* inst);
// muestra el aviso si hay conflictos; devuelve true si se puede lanzar
bool confirmLaunch(QWidget* parent, MinecraftInstance* inst);

// ---------------------------------------------------------------- explicador de crashes
struct CrashExplanation {
    bool found = false;
    QString title, cause, fix;
    QString culpritModId, culpritFile;
    QString reportPath, rawExcerpt;
};
CrashExplanation explainCrash(MinecraftInstance* inst, const QDateTime& since);
void showCrashDialog(QWidget* parent, MinecraftInstance* inst, const CrashExplanation& e);

// ---------------------------------------------------------------- hardware y modo PC lento
struct Hardware {
    QString cpu, gpu;
    qint64 ramMb = 0;
    int cores = 0;
    bool weak() const;
};
Hardware detectHardware();
bool slowModeActive(MinecraftInstance* inst);
// mods que pueden no correr bien en PCs débiles: devuelve el motivo o vacío
QString heavyModReason(const QString& nameOrFile);
// antes de instalar: avisa si alguno es pesado para este PC. true = seguir
bool confirmHeavyMods(QWidget* parent, MinecraftInstance* inst, const QStringList& names);
QString enableSlowMode(MinecraftInstance* inst);   // devuelve resumen de cambios
QString disableSlowMode(MinecraftInstance* inst);

// ---------------------------------------------------------------- grupos de mods
struct ModGroup {
    QString name;
    QStringList members;  // ids o nombres de archivo base
    bool locked = false;
};
QList<ModGroup> modGroups(MinecraftInstance* inst);
void saveModGroups(MinecraftInstance* inst, const QList<ModGroup>& groups);
QString modKey(const QString& fileName);  // "sodium-fabric-0.6.jar" -> "sodium"

// ---------------------------------------------------------------- Discord Rich Presence
class DiscordPresence : public QObject {
    Q_OBJECT
   public:
    static DiscordPresence* get();
    void setPlaying(const QString& details, const QString& state, const QDateTime& start);
    void setIdle();
    void clear();

   private:
    explicit DiscordPresence(QObject* parent = nullptr);
    void ensureConnected();
    void send(int op, const QByteArray& json);
    void pushActivity();
    QLocalSocket* m_sock = nullptr;
    bool m_ready = false;
    int m_pipe = 0;
    QString m_details, m_state;
    QDateTime m_start;
    bool m_hasActivity = false;
    QTimer* m_retry = nullptr;
};

// ---------------------------------------------------------------- letras de canciones
class LyricsService : public QObject {
    Q_OBJECT
   public:
    static LyricsService* get();
    void start();
    void stop();
    bool running() const { return m_running; }
    QString overlayUrl() const;
    void control(const QString& action);  // playpause / next / previous

   signals:
    void updated();

   public:
    QString title, artist, album, status, currentLine, nextLine;
    QImage cover;
    double positionMs = 0, durationMs = 0;
    bool synced = false, playing = false;

   private:
    explicit LyricsService(QObject* parent = nullptr);
    void onHelperLine(const QByteArray& line);
    void fetchLyrics();
    void tick();
    QByteArray stateJson() const;
    void handleHttp(QTcpSocket* s);
    QString helperScript() const;

    bool m_running = false;
    QProcess* m_helper = nullptr;
    QTcpServer* m_server = nullptr;
    QList<QPointer<QTcpSocket>> m_streams;  // clientes de juego (JSON por línea)
    QTimer* m_tick = nullptr;
    QList<QPair<double, QString>> m_lines;
    QString m_trackKey;
    qint64 m_lastReportWall = 0;
    double m_lastReportPos = 0;
    QByteArray m_buf;
    QWidget* m_overlay = nullptr;
};

// ---------------------------------------------------------------- páginas / widgets
QWidget* buildGalleryPage(QWidget* parent);
void refreshGalleryPage(QWidget* page);
QWidget* buildLiveServersStrip(QWidget* parent);
QWidget* buildEventCard(QWidget* parent);
// aviso de versión nueva (consulta la última release de GitHub)
QWidget* buildUpdateBanner(QWidget* parent);
void editModGroups(QWidget* parent, MinecraftInstance* inst);
// antes de actualizar mods: filtra según grupos (devuelve nombres de archivo a actualizar)
QStringList applyGroupsToUpdate(QWidget* parent, MinecraftInstance* inst, const QStringList& wantUpdate, const QStringList& canUpdate);

}  // namespace fps
