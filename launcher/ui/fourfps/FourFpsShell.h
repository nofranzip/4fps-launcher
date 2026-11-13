// 4fps - interfaz principal personalizada (basada en Prism Launcher, GPL-3.0)
#pragma once

#include <QDateTime>
#include <QFrame>
#include <QPointer>
#include <QWidget>
#include <functional>
#include <QHash>
#include <QSet>
#include <QImage>
#include <QPixmap>

class QStackedWidget;
class QLabel;
class QPushButton;
class QComboBox;
class QLineEdit;
class QGridLayout;
class QVBoxLayout;
class QButtonGroup;
class MinecraftInstance;
class InstanceTask;
class QTreeWidget;
class QSlider;
class QCheckBox;
class SkinRenderView;
class QFrame;

class QToolButton;
class BaseInstance;
class QTimer;
class QVBoxLayout;
namespace fps {
QPushButton* makeButton(const QString& text, const QString& icon, bool primary, QWidget* parent);
QLabel* makeLabel(const QString& text, const QString& name, QWidget* parent);
QFrame* makeCard(QWidget* parent, const QString& name = QStringLiteral("fpsPanel"));
void repolish(QWidget* w);
}  // namespace fps

struct FourFpsHooks {
    std::function<void(MinecraftInstance*)> select;
    std::function<MinecraftInstance*()> selected;
    std::function<void()> launch;
    std::function<void()> newInstance;
    std::function<void()> importPack;
    std::function<void()> openFolder;
    std::function<void()> copyInstance;
    std::function<void()> deleteInstance;
    std::function<void()> settings;
    std::function<void()> skins;
    std::function<void(const QString&)> openPage;
    std::function<void(InstanceTask*)> createInstance;
};

class PixelBanner : public QWidget {
    Q_OBJECT
   public:
    explicit PixelBanner(const QString& seed, QWidget* parent = nullptr);
    // tema segun la version de Minecraft (1.16 nether, 1.20 cerezos, etc.) e imagen propia opcional
    void setVersion(const QString& mcVersion);
    void setImagePath(const QString& path);
    static QString themeName(const QString& mcVersion);

   protected:
    void paintEvent(QPaintEvent*) override;

   private:
    uint m_seed;
    int m_theme = -1;
    QString m_label;
    QString m_imagePath;
    QPixmap m_image;
};

class InstanceCard : public QFrame {
    Q_OBJECT
   public:
    InstanceCard(MinecraftInstance* inst, QWidget* parent = nullptr);
    MinecraftInstance* instance() const { return m_inst; }
    void setSelected(bool s);

   signals:
    void clicked(MinecraftInstance*);
    void play(MinecraftInstance*);
    void edit(MinecraftInstance*);

   protected:
    void mousePressEvent(QMouseEvent*) override;

   private:
    MinecraftInstance* m_inst;
    QLabel* m_badge;
    PixelBanner* m_banner = nullptr;
    void chooseImage();
};

class FourFpsShell : public QWidget {
    Q_OBJECT
   public:
    FourFpsShell(FourFpsHooks hooks, QWidget* parent = nullptr);

    static bool pvpSupported(MinecraftInstance* inst);
    static bool pvpEnabled(MinecraftInstance* inst);
    static QStringList pvpFiles(MinecraftInstance* inst);
    static void setPvpEnabled(MinecraftInstance* inst, bool on);
    static QImage composeSkin(const QImage& base, bool slim, const QStringList& cosmetics, bool replaceOuter);
    static QString cosmeticsDir();
    static QString localCapesDir();
    static void seedStarterPack();

   public slots:
    void selectionChanged();
    void launchSelected();
    void installPotatoMods(MinecraftInstance* inst);
    void removePotatoMods(MinecraftInstance* inst);
    void applyFeatureSettings();
    void updateNowPlaying();
    void watchLaunch(BaseInstance* inst);
    void checkLaunchStuck(BaseInstance* inst, int tries);
    void launchAfterCapes(MinecraftInstance* inst);
    void useCapeInGame(const QImage& cape, const QString& name);
    void refreshWardrobe();
    void updateWardrobePreview();
    void applyWardrobe();
    void refreshCapes();
    void updateCapeButton();
    void rebuildInstances();
    void rebuildAccounts();
    // perfil, medallas y notificaciones
    void awardForServer(const QString& address);
    void scanServerBadges();
    void redeemCode(const QString& code);
    bool grantBadge(const QString& id, const QString& reason);
    void updateBell();
    void showNotifications();
    void refreshProfile();

   private:
    QWidget* buildSidebar();
    QWidget* buildBottomBar();
    QWidget* buildInstancesPage();
    QWidget* buildAccountPage();
    QWidget* buildVersionsPage();
    QWidget* buildModsPage();
    QWidget* buildFilesPage();
    QWidget* buildServersPage();
    QWidget* buildSkinsPage();
    QWidget* buildRenderPage();
    QWidget* buildProfilePage();
    static QPixmap badgeIcon(const QString& id, int size, bool locked = false);
    static QStringList ownedBadges();
    static QIcon bellIcon(int unread);
    QToolButton* m_bell = nullptr;
    QWidget* m_profilePage = nullptr;
    QVBoxLayout* m_profileLayout = nullptr;
    QWidget* buildWardrobe(QWidget* parent);
    QWidget* buildCapesPanel(QWidget* parent);
    void onPageShown(int i);
    void refreshVersions();
    void refreshLoaderVersions();
    void refreshMods();
    void refreshFiles();
    void refreshServers();
    void refreshSkins();
    QWidget* m_pages[10] = {};
    QWidget* header(const QString& title, const QString& subtitle, const QList<QWidget*>& right);
    void updateDetail();
    void updateBottom();
    void scheduleRebuild();
    void showPage(int i);
    static QString loaderOf(MinecraftInstance* inst);
    static QString mcVersionOf(MinecraftInstance* inst);

    FourFpsHooks m_hooks;
    QStackedWidget* m_stack = nullptr;
    QButtonGroup* m_nav = nullptr;
    QList<QPushButton*> m_navButtons;

    // instancias
    QGridLayout* m_grid = nullptr;
    QLineEdit* m_search = nullptr;
    QString m_filter = "Todas";
    QList<InstanceCard*> m_cards;
    QLabel* m_detailTitle = nullptr;
    QLabel* m_detailSub = nullptr;
    QLabel* m_detailRam = nullptr;
    QLabel* m_detailJava = nullptr;
    QWidget* m_detailBody = nullptr;
    QCheckBox* m_pvpToggle = nullptr;
    QLabel* m_pvpHint = nullptr;
    bool m_rebuildQueued = false;

    // cuenta
    QVBoxLayout* m_accountsList = nullptr;
    QLineEdit* m_offlineName = nullptr;

    // versiones
    QTreeWidget* m_versionTree = nullptr;
    QLineEdit* m_versionSearch = nullptr;
    QString m_versionType = "release";
    QLineEdit* m_newName = nullptr;
    QString m_loader = "Vanilla";
    QComboBox* m_loaderVersion = nullptr;
    QSlider* m_ram = nullptr;
    QLabel* m_ramLabel = nullptr;
    int m_pendingRam = 0;
    QString m_pendingName;
    bool m_versionsLoading = false;

    // mods
    QLabel* m_modsSubtitle = nullptr;
    QGridLayout* m_modsGrid = nullptr;
    QLineEdit* m_modsSearch = nullptr;
    QPushButton* m_modsInstalledChip = nullptr;
    MinecraftInstance* m_modsInstance = nullptr;
    bool m_modsRefreshQueued = false;

    // archivos
    int m_filesTab = 0;
    QList<QPushButton*> m_filesTabs;
    QGridLayout* m_filesGrid = nullptr;
    QLabel* m_filePreview = nullptr;
    QLabel* m_fileName = nullptr;
    QLabel* m_fileInfo = nullptr;
    QPushButton* m_fileCopy = nullptr;
    QString m_selectedFile;
    QWidget* m_fileDetail = nullptr;

    // servers
    QVBoxLayout* m_serversList = nullptr;
    QLineEdit* m_directAddress = nullptr;
    QComboBox* m_directInstance = nullptr;

    // skins
    QVBoxLayout* m_skinsHost = nullptr;
    QWidget* m_skinDialog = nullptr;
    QString m_skinAccountId;
    QLabel* m_skinsSubtitle = nullptr;

    // ropa y accesorios
    QGridLayout* m_wardrobeGrid = nullptr;
    QLineEdit* m_wardrobeSearch = nullptr;
    SkinRenderView* m_wardrobePreview = nullptr;
    QLabel* m_wardrobeCount = nullptr;
    QCheckBox* m_wardrobeReplace = nullptr;
    QStringList m_wardrobeSelected;
    QHash<QString, QPixmap> m_wardrobeThumbs;
    QStackedWidget* m_skinsStack = nullptr;
    QGridLayout* m_capesGrid = nullptr;
    SkinRenderView* m_capePreview = nullptr;
    QLabel* m_capeName = nullptr;
    QString m_selectedCape;
    QHash<QString, QImage> m_capeTextures;
    QSet<QString> m_capeDownloads;
    QSet<QString> m_capeFailed;
    QString m_selectedLocalCape;
    QList<QPair<QString, QString>> m_officialCapes;
    bool m_officialLoaded = false;
    bool m_officialLoading = false;
    bool m_officialError = false;
    QPushButton* m_capeApply = nullptr;
    QPushButton* m_capeGame = nullptr;
    QLabel* m_capeNote = nullptr;

    // render
    SkinRenderView* m_render = nullptr;
    QLabel* m_renderSubtitle = nullptr;

    // barra inferior
    QLabel* m_bottomFace = nullptr;
    QLabel* m_bottomName = nullptr;
    QComboBox* m_combo = nullptr;
    QPushButton* m_play = nullptr;
    QPushButton* m_streamToggle = nullptr;
    QPushButton* m_musicToggle = nullptr;
    QLabel* m_music = nullptr;
    QList<QPushButton*> m_musicControls;
    QPushButton* m_slowButton = nullptr;
    QLabel* m_slowHint = nullptr;
    QWidget* m_galleryPage = nullptr;
    QPushButton* m_galleryNav = nullptr;
    QString m_currentServer;
    // estado del juego (Iniciando / Jugando)
    QLabel* m_launchStatus = nullptr;
    QTimer* m_playTimer = nullptr;
    QPointer<BaseInstance> m_watched;
    QList<QMetaObject::Connection> m_watchConns;
    QDateTime m_playStart;
    bool m_starting = false;
    void setLaunchState(int state, const QString& detail = QString());  // 0 normal, 1 iniciando, 2 jugando, 3 error

};
