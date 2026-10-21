#pragma once

#include <QImage>
#include <QList>
#include <QMainWindow>
#include <QPixmap>
#include <QPolygonF>
#include <QVector>
#include <QWidget>

class QFileSystemWatcher;
class QMenu;
class QMouseEvent;

// ── PageWidget ────────────────────────────────────────────────────────────────

class PageWidget : public QWidget {
    Q_OBJECT
public:
    explicit PageWidget(int pageIndex, QWidget *parent = nullptr);
    void setPixmap(const QPixmap &px);
    void highlightSelection(const QRect &selRect);
    void clearSelection();

signals:
    void selectionStarted();
    void selectionMade(int page, QRect widgetRect);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;

private:
    int              m_page;
    QPixmap          m_pixmap;
    QPoint           m_pressPos;
    QRect            m_dragRect;               // shown while dragging
    QList<QPolygonF> m_selBounds;             // text-line polygons after release
    bool             m_selecting = false;
};
class QScrollArea;
class QTimer;
class QVBoxLayout;
class QWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    void loadFile(const QString &path);

protected:
    void closeEvent(QCloseEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private slots:
    void openFileDialog();
    void onFileChanged();
    void doRefresh();
    void fitWidth();
    void fitHeight();
    void fitPage();
    void updatePageIndicator();
    void onSelectionMade(int page, QRect widgetRect);

private:
    enum class FitMode { None, Width, Height, Page };

    void initUi();
    void addRecentFile(const QString &path);
    void updateRecentMenu();
    void loadRecentFiles();
    void saveRecentFiles();
    void render(int scrollTo);
    void rebuildPageLabels();
    void applyZoom();
    void applyFit();
    void setZoom(double level);
    void updateStatus();
    void startWatcher();
    void stopWatcher();

protected:
    void resizeEvent(QResizeEvent *event) override;

    QScrollArea        *m_scroll      = nullptr;
    QWidget            *m_pagesWidget = nullptr;
    QVBoxLayout        *m_pagesLayout = nullptr;
    QFileSystemWatcher *m_watcher     = nullptr;
    QTimer             *m_debounce    = nullptr;
    QTimer             *m_resizeTimer = nullptr;
    QMenu              *m_recentMenu  = nullptr;
    QAction            *m_fitWAct     = nullptr;
    QAction            *m_fitHAct     = nullptr;
    QAction            *m_fitPAct     = nullptr;

    QString         m_currentFile;
    QStringList     m_recentFiles;
    QVector<QImage> m_pageImages;
    double          m_zoom         = 1.0;
    int             m_firstVisible = 0;
    int             m_lastVisible  = 0;
    FitMode         m_fitMode      = FitMode::None;

    static constexpr double BASE_DPI    = 150.0;
    static constexpr double MIN_ZOOM    = 0.25;
    static constexpr double MAX_ZOOM    = 4.0;
    static constexpr int    DEBOUNCE_MS = 400;
};
