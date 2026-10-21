#include "main_window.h"

#ifdef Q_OS_MACOS
#include "macos_dock.h"
#endif

#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QKeySequence>
#include <QMenuBar>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QPdfDocument>
#include <QPdfSelection>
#include <QResizeEvent>
#include <QSettings>
#include <QSvgRenderer>
#include <QPixmap>
#include <QScrollArea>
#include <QToolBar>
#include <QScrollBar>
#include <QSizePolicy>
#include <QStatusBar>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>

static QIcon svgIcon(const QString &path, int size = 22)
{
    QIcon icon;
    for (int dpr : {1, 2}) {
        QPixmap pix(size * dpr, size * dpr);
        pix.setDevicePixelRatio(dpr);
        pix.fill(Qt::transparent);
        QPainter painter(&pix);
        QSvgRenderer renderer(path);
        renderer.render(&painter);
        icon.addPixmap(pix);
    }
    return icon;
}

// ── PageWidget ────────────────────────────────────────────────────────────────

PageWidget::PageWidget(int pageIndex, QWidget *parent)
    : QWidget(parent), m_page(pageIndex)
{
    setCursor(Qt::IBeamCursor);
}

void PageWidget::setPixmap(const QPixmap &px)
{
    m_pixmap   = px;
    m_dragRect = QRect();
    m_selBounds.clear();
    setFixedSize(px.size());
    update();
}

void PageWidget::highlightSelection(const QRect &selRect)
{
    m_selBounds.clear();
    if (m_pixmap.isNull() || !selRect.isValid()) { update(); return; }

    const QImage img = m_pixmap.toImage().convertToFormat(QImage::Format_RGB32);
    const int L = qBound(0, selRect.left(),   img.width()  - 1);
    const int R = qBound(0, selRect.right(),  img.width()  - 1);
    const int T = qBound(0, selRect.top(),    img.height() - 1);
    const int B = qBound(0, selRect.bottom(), img.height() - 1);
    if (L >= R || T >= B) { update(); return; }

    // For each row, find the leftmost/rightmost dark pixel (text)
    const int nRows = B - T + 1;
    QVector<int>  spanL(nRows, R + 1), spanR(nRows, L - 1);
    QVector<bool> rowHasText(nRows, false);

    for (int y = T; y <= B; y++) {
        const QRgb *scan = reinterpret_cast<const QRgb*>(img.constScanLine(y));
        const int   ri   = y - T;
        for (int x = L; x <= R; x++) {
            if (qGray(scan[x]) < 180) {
                if (!rowHasText[ri]) spanL[ri] = x;
                spanR[ri] = x;
                rowHasText[ri] = true;
            }
        }
    }

    // Group text rows into line segments; gaps up to MAX_GAP blank rows are bridged
    struct Seg { int top, bot, left, right; };
    QVector<Seg> segs;
    Seg cur{-1, -1, R + 1, L - 1};
    int blankRun = 0;
    const int MAX_GAP = 4;

    for (int i = 0; i < nRows; i++) {
        if (rowHasText[i]) {
            if (cur.top < 0)
                cur = {i, i, spanL[i], spanR[i]};
            else {
                cur.bot   = i;
                cur.left  = qMin(cur.left,  spanL[i]);
                cur.right = qMax(cur.right, spanR[i]);
            }
            blankRun = 0;
        } else if (cur.top >= 0) {
            if (++blankRun > MAX_GAP) {
                segs.append(cur);
                cur = {-1, -1, R + 1, L - 1};
                blankRun = 0;
            }
        }
    }
    if (cur.top >= 0) segs.append(cur);

    for (const Seg &s : segs) {
        if (s.left <= s.right)
            m_selBounds << QPolygonF(QRectF(s.left, T + s.top,
                                             s.right - s.left + 1,
                                             s.bot   - s.top  + 1));
    }
    update();
}

void PageWidget::clearSelection()
{
    if (!m_selBounds.isEmpty() || !m_dragRect.isNull()) {
        m_selBounds.clear();
        m_dragRect = QRect();
        update();
    }
}

void PageWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), Qt::white);
    p.drawPixmap(0, 0, m_pixmap);

    // Translucent rectangle while dragging
    if (m_selecting && m_dragRect.isValid()) {
        p.fillRect(m_dragRect, QColor(70, 130, 230, 40));
        p.setPen(QPen(QColor(70, 130, 230, 120), 1));
        p.drawRect(m_dragRect.adjusted(0, 0, -1, -1));
    }

    // Text-line polygon highlights after release
    if (!m_selBounds.isEmpty()) {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(70, 130, 230, 110));
        for (const QPolygonF &poly : m_selBounds)
            p.drawPolygon(poly);
    }
}

void PageWidget::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton) {
        m_pressPos  = e->pos();
        m_dragRect  = QRect();
        m_selBounds.clear();
        m_selecting = true;
        emit selectionStarted();
        update();
    }
}

void PageWidget::mouseMoveEvent(QMouseEvent *e)
{
    if (m_selecting && (e->buttons() & Qt::LeftButton)) {
        m_dragRect = QRect(m_pressPos, e->pos()).normalized();
        update();
    }
}

void PageWidget::mouseReleaseEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton && m_selecting) {
        m_selecting = false;
        m_dragRect  = QRect();
        const QRect sel = QRect(m_pressPos, e->pos()).normalized();
        if (sel.width() > 3 && sel.height() > 3)
            emit selectionMade(m_page, sel);
        else
            update();
    }
}

// ── MainWindow ────────────────────────────────────────────────────────────────

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    m_debounce = new QTimer(this);
    m_debounce->setSingleShot(true);
    connect(m_debounce, &QTimer::timeout, this, &MainWindow::doRefresh);

    m_resizeTimer = new QTimer(this);
    m_resizeTimer->setSingleShot(true);
    connect(m_resizeTimer, &QTimer::timeout, this, &MainWindow::applyFit);

    loadRecentFiles();
    initUi();
    setAcceptDrops(true);
}

MainWindow::~MainWindow() = default;

// ── UI setup ──────────────────────────────────────────────────────────────────

void MainWindow::initUi()
{
    setWindowTitle(tr("SlimPDF"));
    resize(900, 700);

    m_scroll = new QScrollArea(this);
    m_scroll->setWidgetResizable(true);
    m_scroll->setAlignment(Qt::AlignHCenter);
    m_scroll->setStyleSheet("QScrollArea { background: #3a3a3a; border: none; }");

    m_pagesWidget = new QWidget;
    m_pagesWidget->setStyleSheet("background: #3a3a3a;");

    m_pagesLayout = new QVBoxLayout(m_pagesWidget);
    m_pagesLayout->setSpacing(12);
    m_pagesLayout->setContentsMargins(20, 20, 20, 20);
    m_pagesLayout->setAlignment(Qt::AlignTop | Qt::AlignHCenter);

    m_scroll->setWidget(m_pagesWidget);
    setCentralWidget(m_scroll);

    connect(m_scroll->verticalScrollBar(), &QScrollBar::valueChanged,
            this, &MainWindow::updatePageIndicator);

    // Actions
    auto *zoomInAct  = new QAction(svgIcon(":/icons/zoom-in.svg"),    tr("Zoom In"),    this);
    auto *zoomOutAct = new QAction(svgIcon(":/icons/zoom-out.svg"),   tr("Zoom Out"),   this);
    auto *zoomRstAct = new QAction(svgIcon(":/icons/zoom-reset.svg"), tr("Actual Size"), this);
    m_fitWAct = new QAction(svgIcon(":/icons/fit-width.svg"),  tr("Fit Width"),  this);
    m_fitHAct = new QAction(svgIcon(":/icons/fit-height.svg"), tr("Fit Height"), this);
    m_fitPAct = new QAction(svgIcon(":/icons/fit-page.svg"),   tr("Fit Page"),   this);

    zoomInAct->setShortcut(QKeySequence::ZoomIn);
    zoomOutAct->setShortcut(QKeySequence::ZoomOut);
    zoomRstAct->setShortcut(QKeySequence("Ctrl+0"));
    zoomRstAct->setToolTip(tr("Actual Size (100%)"));

    m_fitWAct->setCheckable(true);
    m_fitHAct->setCheckable(true);
    m_fitPAct->setCheckable(true);
    auto *fitGroup = new QActionGroup(this);
    fitGroup->setExclusionPolicy(QActionGroup::ExclusionPolicy::ExclusiveOptional);
    fitGroup->addAction(m_fitWAct);
    fitGroup->addAction(m_fitHAct);
    fitGroup->addAction(m_fitPAct);

    connect(zoomInAct,  &QAction::triggered, this, [this] { setZoom(m_zoom * 1.25); });
    connect(zoomOutAct, &QAction::triggered, this, [this] { setZoom(m_zoom / 1.25); });
    connect(zoomRstAct, &QAction::triggered, this, [this] { setZoom(1.0); });
    connect(m_fitWAct, &QAction::triggered, this, &MainWindow::fitWidth);
    connect(m_fitHAct, &QAction::triggered, this, &MainWindow::fitHeight);
    connect(m_fitPAct, &QAction::triggered, this, &MainWindow::fitPage);

    // File menu
    auto *fm = menuBar()->addMenu(tr("&File"));
    auto *openA = fm->addAction(tr("&Open…"));
    openA->setShortcut(QKeySequence::Open);
    connect(openA, &QAction::triggered, this, &MainWindow::openFileDialog);

    m_recentMenu = new QMenu(tr("Open Recent"), this);
    fm->addMenu(m_recentMenu);
    updateRecentMenu();

#ifdef Q_OS_MACOS
    // Defer until after the event loop starts so NSApp's delegate is ready.
    QTimer::singleShot(0, this, [this] {
        setupMacOSDockMenu([this](const QString &path) { loadFile(path); });
        updateMacOSDockMenu(m_recentFiles);
    });
#endif

    fm->addSeparator();
    auto *quitA = fm->addAction(tr("&Quit"));
    quitA->setShortcut(QKeySequence::Quit);
    connect(quitA, &QAction::triggered, qApp, &QApplication::quit);


    // View menu
    auto *vm = menuBar()->addMenu(tr("&View"));
    vm->addAction(zoomInAct);
    vm->addAction(zoomOutAct);
    vm->addAction(zoomRstAct);
    vm->addSeparator();
    vm->addAction(m_fitWAct);
    vm->addAction(m_fitHAct);
    vm->addAction(m_fitPAct);

    // Toolbar
    auto *tb = addToolBar(tr("Controls"));
    tb->setMovable(false);
    tb->setIconSize(QSize(22, 22));
    tb->setToolButtonStyle(Qt::ToolButtonIconOnly);
    tb->setStyleSheet(
        "QToolButton { border-radius: 5px; padding: 3px; }"
        "QToolButton:hover { background: rgba(255,255,255,0.15); }"
        "QToolButton:checked { background: rgba(70,130,230,0.75); }"
        "QToolButton:checked:hover { background: rgba(90,150,255,0.85); }"
    );
    tb->addAction(zoomOutAct);
    tb->addAction(zoomInAct);
    tb->addAction(zoomRstAct);
    tb->addSeparator();
    tb->addAction(m_fitWAct);
    tb->addAction(m_fitHAct);
    tb->addAction(m_fitPAct);
}

// ── File loading ──────────────────────────────────────────────────────────────

void MainWindow::openFileDialog()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Open PDF"), {}, tr("PDF Files (*.pdf);;All Files (*)"));
    if (!path.isEmpty())
        loadFile(path);
}

void MainWindow::loadFile(const QString &path)
{
    m_currentFile = QFileInfo(path).absoluteFilePath();
    addRecentFile(m_currentFile);
    stopWatcher();
    render(0);
    startWatcher();
}

void MainWindow::render(int scrollTo)
{
    if (m_currentFile.isEmpty())
        return;

    QPdfDocument doc;
    if (doc.load(m_currentFile) != QPdfDocument::Error::None) {
        statusBar()->showMessage(
            tr("Error: could not open %1").arg(m_currentFile));
        return;
    }

    const double scale = BASE_DPI / 72.0;
    const int count = doc.pageCount();

    m_pageImages.clear();
    m_pageImages.reserve(count);

    for (int i = 0; i < count; ++i) {
        const QSizeF pts = doc.pagePointSize(i);
        const QSize  sz(static_cast<int>(pts.width()  * scale),
                        static_cast<int>(pts.height() * scale));
        m_pageImages.append(doc.render(i, sz));
    }

    doc.close(); // File handle released — file may now be renamed or moved

    rebuildPageLabels();
    setWindowTitle(tr("SlimPDF — %1").arg(QFileInfo(m_currentFile).fileName()));
    updateStatus();

    m_firstVisible = 0;
    m_lastVisible  = 0;

    QTimer::singleShot(50, this, [this, scrollTo] {
        m_scroll->verticalScrollBar()->setValue(scrollTo);
        updatePageIndicator();
    });
}

void MainWindow::rebuildPageLabels()
{
    while (m_pagesLayout->count()) {
        QLayoutItem *item = m_pagesLayout->takeAt(0);
        if (QWidget *w = item->widget())
            w->deleteLater();
        delete item;
    }

    for (int i = 0; i < m_pageImages.size(); ++i) {
        auto *pw = new PageWidget(i);
        pw->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        connect(pw, &PageWidget::selectionStarted, this, [this, pw] {
            for (int j = 0; j < m_pagesLayout->count(); ++j) {
                auto *w = qobject_cast<PageWidget *>(m_pagesLayout->itemAt(j)->widget());
                if (w && w != pw) w->clearSelection();
            }
        });
        connect(pw, &PageWidget::selectionMade, this, &MainWindow::onSelectionMade);
        m_pagesLayout->addWidget(pw);
    }

    if (m_fitMode != FitMode::None)
        applyFit();
    else
        applyZoom();
}

void MainWindow::applyZoom()
{
    for (int i = 0; i < m_pagesLayout->count() && i < m_pageImages.size(); ++i) {
        auto *pw = qobject_cast<PageWidget *>(m_pagesLayout->itemAt(i)->widget());
        if (!pw) continue;
        QPixmap pix = QPixmap::fromImage(m_pageImages[i]);
        if (m_zoom != 1.0)
            pix = pix.scaled(
                static_cast<int>(pix.width()  * m_zoom),
                static_cast<int>(pix.height() * m_zoom),
                Qt::KeepAspectRatio,
                Qt::SmoothTransformation);
        pw->setPixmap(pix);
    }
}

// ── Zoom ──────────────────────────────────────────────────────────────────────

void MainWindow::setZoom(double level)
{
    m_fitMode = FitMode::None;
    if (m_fitWAct) m_fitWAct->setChecked(false);
    if (m_fitHAct) m_fitHAct->setChecked(false);
    if (m_fitPAct) m_fitPAct->setChecked(false);
    m_zoom = qBound(MIN_ZOOM, level, MAX_ZOOM);
    applyZoom();
    updateStatus();
}

void MainWindow::updateStatus()
{
    const int n = m_pageImages.size();
    if (n == 0) { statusBar()->showMessage({}); return; }

    QString pageStr = (m_firstVisible == m_lastVisible)
        ? tr("Page %1 of %2").arg(m_firstVisible + 1).arg(n)
        : tr("Pages %1–%2 of %3").arg(m_firstVisible + 1).arg(m_lastVisible + 1).arg(n);

    statusBar()->showMessage(tr("%1  •  %2%").arg(pageStr).arg(qRound(m_zoom * 100)));
}

void MainWindow::updatePageIndicator()
{
    const int count = m_pagesLayout->count();
    if (count == 0) return;

    const int scrollY = m_scroll->verticalScrollBar()->value();
    const int viewH   = m_scroll->viewport()->height();

    int first = -1, last = -1;
    for (int i = 0; i < count; ++i) {
        auto *w = m_pagesLayout->itemAt(i)->widget();
        if (!w) continue;
        if (w->y() + w->height() > scrollY && w->y() < scrollY + viewH) {
            if (first < 0) first = i;
            last = i;
        }
    }

    if (first < 0 || (first == m_firstVisible && last == m_lastVisible))
        return;

    m_firstVisible = first;
    m_lastVisible  = last;
    updateStatus();
}

// ── Recent files ──────────────────────────────────────────────────────────────

static constexpr int MAX_RECENT = 10;

void MainWindow::loadRecentFiles()
{
    QSettings s;
    m_recentFiles = s.value("recentFiles").toStringList();
}

void MainWindow::saveRecentFiles()
{
    QSettings s;
    s.setValue("recentFiles", m_recentFiles);
}

void MainWindow::addRecentFile(const QString &path)
{
    m_recentFiles.removeAll(path);
    m_recentFiles.prepend(path);
    while (m_recentFiles.size() > MAX_RECENT)
        m_recentFiles.removeLast();
    saveRecentFiles();
    updateRecentMenu();
}

void MainWindow::updateRecentMenu()
{
    m_recentMenu->clear();

    for (const QString &path : std::as_const(m_recentFiles)) {
        auto *a = m_recentMenu->addAction(QFileInfo(path).fileName());
        a->setToolTip(path);
        connect(a, &QAction::triggered, this, [this, path] {
            if (QFile::exists(path))
                loadFile(path);
            else
                statusBar()->showMessage(tr("File not found: %1").arg(path), 4000);
        });
    }

    if (m_recentFiles.isEmpty()) {
        auto *empty = m_recentMenu->addAction(tr("No recent files"));
        empty->setEnabled(false);
    } else {
        m_recentMenu->addSeparator();
        auto *clearA = m_recentMenu->addAction(tr("Clear Menu"));
        connect(clearA, &QAction::triggered, this, [this] {
            m_recentFiles.clear();
            saveRecentFiles();
            updateRecentMenu();
        });
    }

#ifdef Q_OS_MACOS
    updateMacOSDockMenu(m_recentFiles);
#endif
}

// ── Fit modes ─────────────────────────────────────────────────────────────────

void MainWindow::fitWidth()
{
    if (!m_fitWAct->isChecked()) { m_fitMode = FitMode::None; return; }
    m_fitMode = FitMode::Width;
    applyFit();
}

void MainWindow::fitHeight()
{
    if (!m_fitHAct->isChecked()) { m_fitMode = FitMode::None; return; }
    m_fitMode = FitMode::Height;
    applyFit();
}

void MainWindow::fitPage()
{
    if (!m_fitPAct->isChecked()) { m_fitMode = FitMode::None; return; }
    m_fitMode = FitMode::Page;
    applyFit();
}

void MainWindow::applyFit()
{
    if (m_pageImages.isEmpty() || m_fitMode == FitMode::None) return;

    const auto mg = m_pagesLayout->contentsMargins();
    const int vw = m_scroll->viewport()->width()  - mg.left() - mg.right();
    const int vh = m_scroll->viewport()->height() - mg.top()  - mg.bottom();

    int maxW = 0;
    for (const QImage &img : m_pageImages)
        maxW = qMax(maxW, img.width());
    const int h = m_pageImages.first().height();

    double newZoom = m_zoom;
    switch (m_fitMode) {
    case FitMode::Width:  if (maxW > 0) newZoom = static_cast<double>(vw) / maxW; break;
    case FitMode::Height: if (h   > 0) newZoom = static_cast<double>(vh) / h;    break;
    case FitMode::Page:
        if (maxW > 0 && h > 0)
            newZoom = qMin(static_cast<double>(vw) / maxW,
                           static_cast<double>(vh) / h);
        break;
    case FitMode::None: break;
    }

    m_zoom = qBound(MIN_ZOOM, newZoom, MAX_ZOOM);
    applyZoom();
    updateStatus();
}

// ── Text selection ────────────────────────────────────────────────────────────

void MainWindow::onSelectionMade(int page, QRect widgetRect)
{
    if (m_currentFile.isEmpty()) return;

    // Map widget pixels → PDF points (BASE_DPI pixels/inch, 72 points/inch)
    const double pxPerPt = m_zoom * BASE_DPI / 72.0;
    const QRectF pdfRect(widgetRect.left()   / pxPerPt,
                         widgetRect.top()    / pxPerPt,
                         widgetRect.width()  / pxPerPt,
                         widgetRect.height() / pxPerPt);

    QPdfDocument doc;
    if (doc.load(m_currentFile) != QPdfDocument::Error::None) return;
    const QPdfSelection sel = doc.getSelection(page, pdfRect.topLeft(), pdfRect.bottomRight());
    doc.close();

    // Show word-processor-style per-line highlights derived from the page image
    if (auto *pw = qobject_cast<PageWidget*>(m_pagesLayout->itemAt(page)->widget()))
        pw->highlightSelection(widgetRect);

    const QString text = sel.text().trimmed();
    if (text.isEmpty()) {
        statusBar()->showMessage(tr("No text found in selection"), 2000);
        return;
    }
    QApplication::clipboard()->setText(text);
    statusBar()->showMessage(
        tr("Copied %1 character%2").arg(text.length()).arg(text.length() == 1 ? "" : "s"),
        3000);
}

// ── Resize ────────────────────────────────────────────────────────────────────

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    if (m_fitMode != FitMode::None && !m_pageImages.isEmpty())
        m_resizeTimer->start(80);
}

// ── File watching ─────────────────────────────────────────────────────────────

void MainWindow::startWatcher()
{
    if (m_currentFile.isEmpty())
        return;
    m_watcher = new QFileSystemWatcher(this);
    m_watcher->addPath(m_currentFile);
    connect(m_watcher, &QFileSystemWatcher::fileChanged,
            this, &MainWindow::onFileChanged);
}

void MainWindow::stopWatcher()
{
    if (m_watcher) {
        m_watcher->deleteLater();
        m_watcher = nullptr;
    }
}

void MainWindow::onFileChanged()
{
    // Some backends (inotify, kqueue) remove the watch after a modification;
    // re-add it so subsequent saves keep triggering refreshes.
    if (m_watcher && QFile::exists(m_currentFile))
        m_watcher->addPath(m_currentFile);

    m_debounce->start(DEBOUNCE_MS);
}

void MainWindow::doRefresh()
{
    if (!QFile::exists(m_currentFile)) {
        statusBar()->showMessage(
            tr("File no longer available: %1").arg(m_currentFile));
        return;
    }
    const int pos = m_scroll->verticalScrollBar()->value();
    render(pos);
}

// ── Drag and drop ─────────────────────────────────────────────────────────────

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls()) {
        const auto urls = event->mimeData()->urls();
        if (!urls.isEmpty() &&
            urls.first().toLocalFile().endsWith(".pdf", Qt::CaseInsensitive))
            event->acceptProposedAction();
    }
}

void MainWindow::dropEvent(QDropEvent *event)
{
    const auto urls = event->mimeData()->urls();
    if (!urls.isEmpty())
        loadFile(urls.first().toLocalFile());
}

// ── Ctrl+scroll to zoom ───────────────────────────────────────────────────────

void MainWindow::wheelEvent(QWheelEvent *event)
{
    if (event->modifiers() & Qt::ControlModifier) {
        const double factor = event->angleDelta().y() > 0 ? 1.1 : 1.0 / 1.1;
        setZoom(m_zoom * factor);
    } else {
        QMainWindow::wheelEvent(event);
    }
}

// ── Cleanup ───────────────────────────────────────────────────────────────────

void MainWindow::closeEvent(QCloseEvent *event)
{
    stopWatcher();
    QMainWindow::closeEvent(event);
}
