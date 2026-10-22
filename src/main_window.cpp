#include "main_window.h"

#ifdef Q_OS_MACOS
#include "macos_dock.h"
#include "macos_about.h"
#endif

#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QBuffer>
#include <QCloseEvent>
#include <QCursor>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QKeySequence>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPdfSearchModel>
#include <QDialog>
#include <QHBoxLayout>
#include <QPushButton>
#include <QScreen>
#include <QMoveEvent>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPdfDocument>
#include <QPdfSelection>
#include <QResizeEvent>
#include <QSettings>
#include <QPixmap>
#include <QScrollArea>
#include <QToolBar>
#include <QScrollBar>
#include <QSizePolicy>
#include <QStatusBar>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>

// Some Qt versions return one rectangle per glyph rather than per text line.
static QList<QRectF> searchHighlightBounds(const QPdfLink &match)
{
    QList<QRectF> lines;
    for (const QRectF &bounds : match.rectangles()) {
        if (bounds.isEmpty()) continue;
        if (!lines.isEmpty()) {
            QRectF &line = lines.last();
            const double overlap = qMin(line.bottom(), bounds.bottom())
                                 - qMax(line.top(), bounds.top());
            const double gap = qMax(line.left(), bounds.left())
                             - qMin(line.right(), bounds.right());
            if (overlap > 0 && gap <= qMax(line.height(), bounds.height()) * 2) {
                line = line.united(bounds);
                continue;
            }
        }
        lines.append(bounds);
    }
    return lines;
}

// ── PageWidget ────────────────────────────────────────────────────────────────

PageWidget::PageWidget(QPdfDocument *document, int pageIndex, QWidget *parent)
    : QWidget(parent), m_document(document), m_page(pageIndex),
      m_pageSize(document->pagePointSize(pageIndex)),
      m_textBounds(document->getAllText(pageIndex).bounds())
{
    setMouseTracking(true);
    setCursor(Qt::ArrowCursor);
}

void PageWidget::setPixmap(const QPixmap &px)
{
    m_pixmap = px;
    m_selecting = false;
    clearSelection();
    setFixedSize(px.size());
    setCursor(isOverText(mapFromGlobal(QCursor::pos())) ? Qt::IBeamCursor : Qt::ArrowCursor);
    update();
}

QPointF PageWidget::pdfPosition(const QPointF &position) const
{
    return QPointF(position.x() * m_pageSize.width() / width(),
                   position.y() * m_pageSize.height() / height());
}

bool PageWidget::isOverText(const QPointF &position) const
{
    const QPointF point = pdfPosition(position);
    for (const QPolygonF &bounds : m_textBounds) {
        if (bounds.containsPoint(point, Qt::OddEvenFill))
            return true;
    }
    return false;
}

void PageWidget::updateSelection(const QPointF &position)
{
    m_selBounds.clear();
    m_selectedText.clear();
    if (position != m_pressPos) {
        // Keep the anchor and endpoint in drag order, including backwards drags.
        const QPdfSelection selection = m_document->getSelection(
            m_page, pdfPosition(m_pressPos), pdfPosition(position));
        m_selBounds = selection.bounds();
        m_selectedText = selection.text();
    }
    emit selectionChanged(m_selectedText);
    update();
}

void PageWidget::clearSelection()
{
    m_selBounds.clear();
    m_selectedText.clear();
    emit selectionChanged(m_selectedText);
    update();
}

void PageWidget::setSearchHighlights(const QList<QRectF> &matches, const QList<QRectF> &current)
{
    m_searchBounds = matches;
    m_currentSearchBounds = current;
    update();
}

void PageWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), Qt::white);
    p.drawPixmap(0, 0, m_pixmap);

    // PDF text boundaries are in points; scale them to the actual rendered size.
    p.scale(width() / m_pageSize.width(), height() / m_pageSize.height());
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 220, 0, 100));
    for (const QRectF &bounds : m_searchBounds)
        p.drawRect(bounds);
    p.setBrush(QColor(255, 140, 0, 160));
    for (const QRectF &bounds : m_currentSearchBounds)
        p.drawRect(bounds);
    p.setBrush(QColor(70, 130, 230, 110));
    for (const QPolygonF &bounds : m_selBounds)
        p.drawPolygon(bounds);
}

void PageWidget::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton) {
        clearSelection();
        emit selectionStarted();
        m_pressPos = e->position();
        m_selecting = isOverText(m_pressPos);
        setCursor(m_selecting ? Qt::IBeamCursor : Qt::ArrowCursor);
    }
}

void PageWidget::mouseMoveEvent(QMouseEvent *e)
{
    if (m_selecting && (e->buttons() & Qt::LeftButton)) {
        updateSelection(e->position());
    } else {
        setCursor(isOverText(e->position()) ? Qt::IBeamCursor : Qt::ArrowCursor);
    }
}

void PageWidget::mouseReleaseEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton && m_selecting) {
        updateSelection(e->position());
        m_selecting = false;
        setCursor(isOverText(e->position()) ? Qt::IBeamCursor : Qt::ArrowCursor);
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
    connect(m_scroll->horizontalScrollBar(), &QScrollBar::valueChanged,
            this, &MainWindow::updatePageIndicator);

    // Actions
    auto *zoomInAct  = new QAction(QIcon(":/icons/zoom-in.svg"),    tr("Zoom In"),    this);
    auto *zoomOutAct = new QAction(QIcon(":/icons/zoom-out.svg"),   tr("Zoom Out"),   this);
    auto *zoomRstAct = new QAction(QIcon(":/icons/zoom-reset.svg"), tr("Actual Size"), this);
    m_fitWAct = new QAction(QIcon(":/icons/fit-width.svg"),  tr("Fit Width"),  this);
    m_fitHAct = new QAction(QIcon(":/icons/fit-height.svg"), tr("Fit Height"), this);
    m_fitPAct = new QAction(QIcon(":/icons/fit-page.svg"),   tr("Fit Page"),   this);

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
    m_fitWAct->setChecked(true);

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
    connect(m_recentMenu, &QMenu::aboutToShow, this, &MainWindow::updateRecentMenu);
    connect(qApp, &QGuiApplication::applicationStateChanged, this,
            [this](Qt::ApplicationState state) {
                if (state == Qt::ApplicationActive)
                    updateRecentMenu();
            });

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


    // Edit menu
    auto *em = menuBar()->addMenu(tr("&Edit"));
    m_copyAct = em->addAction(tr("&Copy"));
    m_copyAct->setShortcut(QKeySequence::Copy);
    m_copyAct->setEnabled(false);
    connect(m_copyAct, &QAction::triggered, this, &MainWindow::copySelection);

    em->addSeparator();
    auto *findAct = em->addAction(tr("&Find…"));
    findAct->setShortcut(QKeySequence::Find);
    connect(findAct, &QAction::triggered, this, &MainWindow::showFind);
    m_findNextAct = em->addAction(tr("Find &Next"));
    m_findNextAct->setObjectName("findNext");
    m_findNextAct->setShortcuts(QKeySequence::keyBindings(QKeySequence::FindNext));
    m_findNextAct->setEnabled(false);
    connect(m_findNextAct, &QAction::triggered, this, [this] { navigateSearch(1); });
    m_findPreviousAct = em->addAction(tr("Find &Previous"));
    m_findPreviousAct->setObjectName("findPrevious");
    m_findPreviousAct->setShortcuts(QKeySequence::keyBindings(QKeySequence::FindPrevious));
    m_findPreviousAct->setEnabled(false);
    connect(m_findPreviousAct, &QAction::triggered, this, [this] { navigateSearch(-1); });

    // View menu
    auto *vm = menuBar()->addMenu(tr("&View"));
    vm->addAction(zoomInAct);
    vm->addAction(zoomOutAct);
    vm->addAction(zoomRstAct);
    vm->addSeparator();
    vm->addAction(m_fitWAct);
    vm->addAction(m_fitHAct);
    vm->addAction(m_fitPAct);

    // Qt places AboutRole in the application menu on macOS.
    auto *helpMenu = menuBar()->addMenu(tr("&Help"));
    auto *aboutAct = new QAction(tr("&About SlimPDF…"), this);
    aboutAct->setMenuRole(QAction::AboutRole);
    helpMenu->addAction(aboutAct);
    connect(aboutAct, &QAction::triggered, this, [this] {
        const QString description = tr("A lightweight PDF reader with continuous scrolling, "
                                       "text selection, and automatic reloads.");
        const QString credits = tr("Built with C++17 and Qt 6. PDF rendering powered by Qt PDF.");
        const QString license = tr("MIT License");
        const QString website = QStringLiteral("https://github.com/tsengf/slimpdf");
        const QString version = QCoreApplication::applicationVersion();
#ifdef Q_OS_MACOS
        showMacOSAboutPanel(version, description, credits, license, website);
#else
        QMessageBox::about(this, tr("About SlimPDF"),
            tr("<h3>SlimPDF</h3><p>Version %1</p>"
               "<p>%2</p><p>%3</p><p>%4</p>"
               "<p><a href=\"%5\">%5</a></p>")
                .arg(version.toHtmlEscaped(), description.toHtmlEscaped(),
                     credits.toHtmlEscaped(), license.toHtmlEscaped(), website));
#endif
    });

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

    m_findDialog = new QDialog(this, Qt::Tool);
    m_findDialog->setObjectName("findDialog");
    m_findDialog->setWindowTitle(tr("Find"));
    m_findDialog->setModal(false);
    m_findDialog->setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    auto *findLayout = new QVBoxLayout(m_findDialog);
    findLayout->setSizeConstraint(QLayout::SetFixedSize);
    m_findEdit = new QLineEdit(m_findDialog);
    m_findEdit->setObjectName("findText");
    m_findEdit->setAccessibleName(tr("Find text in document"));
    m_findEdit->setPlaceholderText(tr("Search this document"));
    m_findEdit->setClearButtonEnabled(true);
    m_findEdit->setMinimumWidth(300);
    m_findEdit->installEventFilter(this);
    findLayout->addWidget(m_findEdit);
    auto *navigation = new QHBoxLayout;
    m_findStatus = new QLabel(m_findDialog);
    m_findStatus->setObjectName("findStatus");
    m_findStatus->setMinimumWidth(90);
    navigation->addWidget(m_findStatus, 1);
    for (const auto &entry : {qMakePair(m_findPreviousAct, tr("Previous")),
                              qMakePair(m_findNextAct, tr("Next"))}) {
        auto *button = new QPushButton(entry.second, m_findDialog);
        button->setAutoDefault(false);
        QAction *action = entry.first;
        button->setEnabled(action->isEnabled());
        connect(button, &QPushButton::clicked, action, &QAction::trigger);
        connect(action, &QAction::changed, button,
                [action, button] { button->setEnabled(action->isEnabled()); });
        navigation->addWidget(button);
    }
    findLayout->addLayout(navigation);
    m_closeFindAct = new QAction(tr("Close Find"), this);
    m_closeFindAct->setShortcut(QKeySequence(Qt::Key_Escape));
    m_closeFindAct->setEnabled(false);
    addAction(m_closeFindAct);
    // Keep document shortcuts available while the modeless dialog has focus.
    m_findDialog->addActions({findAct, m_findPreviousAct, m_findNextAct, m_closeFindAct});
    connect(m_closeFindAct, &QAction::triggered, this, &MainWindow::closeFind);
    connect(m_findDialog, &QDialog::rejected, this, &MainWindow::closeFind);
    connect(m_findEdit, &QLineEdit::textChanged, this, &MainWindow::startSearch);
    m_searchTimer = new QTimer(this);
    m_searchTimer->setInterval(0);
    connect(m_searchTimer, &QTimer::timeout, this, &MainWindow::searchNextPage);
    m_findDialog->hide();
}

// ── Find ──────────────────────────────────────────────────────────────────────

bool MainWindow::eventFilter(QObject *object, QEvent *event)
{
    if (object == m_findEdit && event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
            navigateSearch(key->modifiers() & Qt::ShiftModifier ? -1 : 1);
            return true;
        }
    }
    return QMainWindow::eventFilter(object, event);
}

void MainWindow::showFind()
{
    const bool wasHidden = m_findDialog->isHidden();
    m_findDialog->show();
    positionFindDialog();
    m_findDialog->raise();
    m_findDialog->activateWindow();
    m_closeFindAct->setEnabled(true);
    m_findEdit->setFocus();
    m_findEdit->selectAll();
    if (wasHidden)
        startSearch();
}

void MainWindow::positionFindDialog()
{
    if (!m_findDialog || m_findDialog->isHidden()) return;
    const QPoint corner = m_scroll->viewport()->mapToGlobal(
        QPoint(m_scroll->viewport()->width(), 0));
    const QSize size = m_findDialog->frameGeometry().size();
    QPoint position(corner.x() - size.width() - 12, corner.y() + 12);
    QScreen *targetScreen = QGuiApplication::screenAt(corner);
    if (!targetScreen) targetScreen = screen();
    if (targetScreen) {
        const QRect available = targetScreen->availableGeometry();
        position.setX(qBound(available.left(), position.x(),
            qMax(available.left(), available.right() - size.width() + 1)));
        position.setY(qBound(available.top(), position.y(),
            qMax(available.top(), available.bottom() - size.height() + 1)));
    }
    m_findDialog->move(position);
}

void MainWindow::closeFind()
{
    m_findDialog->hide();
    m_closeFindAct->setEnabled(false);
    clearSearch();
    activateWindow();
    m_scroll->setFocus();
}

void MainWindow::clearSearch()
{
    m_searchTimer->stop();
    m_searchModel.reset();
    m_searchResults.clear();
    m_searchBoundsByPage.clear();
    m_currentMatch = -1;
    m_nextSearchPage = 0;
    updateSearchHighlights();
    updateFindStatus();
}

void MainWindow::startSearch()
{
    clearSearch();
    if (!m_document || m_findDialog->isHidden() || m_findEdit->text().isEmpty())
        return;
    m_searchBoundsByPage.resize(m_document->pageCount());
    m_searchModel = std::make_unique<QPdfSearchModel>();
    m_searchModel->setDocument(m_document.get());
    m_searchModel->setSearchString(m_findEdit->text());
    m_searchTimer->start();
    updateFindStatus();
}

void MainWindow::searchNextPage()
{
    if (!m_searchModel || m_nextSearchPage >= m_document->pageCount()) {
        m_searchTimer->stop();
        m_searchModel.reset();
        updateFindStatus();
        return;
    }

    const int page = m_nextSearchPage++;
    const auto matches = m_searchModel->resultsOnPage(page);
    for (const QPdfLink &match : matches) {
        m_searchResults.append(match);
        m_searchBoundsByPage[page].append(searchHighlightBounds(match));
    }
    const bool firstMatch = m_currentMatch < 0 && !m_searchResults.isEmpty();
    if (firstMatch)
        m_currentMatch = 0;
    if (auto *widget = qobject_cast<PageWidget *>(m_pagesLayout->itemAt(page)->widget())) {
        const bool active = m_currentMatch >= 0 && m_searchResults[m_currentMatch].page() == page;
        widget->setSearchHighlights(m_searchBoundsByPage[page],
            active ? searchHighlightBounds(m_searchResults[m_currentMatch]) : QList<QRectF>{});
    }
    if (firstMatch)
        scrollToCurrentMatch();
    if (m_nextSearchPage >= m_document->pageCount()) {
        m_searchTimer->stop();
        m_searchModel.reset();
    }
    updateFindStatus();
}

void MainWindow::updateFindStatus()
{
    const int count = m_searchResults.size();
    m_findNextAct->setEnabled(count > 0);
    m_findPreviousAct->setEnabled(count > 0);
    if (m_findDialog->isHidden() || m_findEdit->text().isEmpty())
        m_findStatus->clear();
    else if (count == 0)
        m_findStatus->setText(m_searchTimer->isActive() ? tr("Searching…") : tr("No matches"));
    else
        m_findStatus->setText(tr("%1 of %2%3").arg(m_currentMatch + 1).arg(count)
            .arg(m_searchTimer->isActive() ? QStringLiteral("…") : QString()));
}

void MainWindow::updateSearchHighlights()
{
    const QPdfLink current = m_currentMatch >= 0 ? m_searchResults[m_currentMatch] : QPdfLink();
    for (int page = 0; page < m_pagesLayout->count(); ++page) {
        auto *widget = qobject_cast<PageWidget *>(m_pagesLayout->itemAt(page)->widget());
        if (widget)
            widget->setSearchHighlights(m_searchBoundsByPage.value(page),
                current.isValid() && current.page() == page ? searchHighlightBounds(current) : QList<QRectF>{});
    }
}

void MainWindow::navigateSearch(int direction)
{
    if (m_searchResults.isEmpty()) return;
    const int count = m_searchResults.size();
    m_currentMatch = (m_currentMatch + direction + count) % count;
    updateSearchHighlights();
    updateFindStatus();
    scrollToCurrentMatch();
}

void MainWindow::scrollToCurrentMatch()
{
    if (m_currentMatch < 0) return;
    const QPdfLink &match = m_searchResults[m_currentMatch];
    auto *widget = m_pagesLayout->itemAt(match.page())->widget();
    const QSizeF size = m_document->pagePointSize(match.page());
    const QPointF point = match.rectangles().isEmpty() ? match.location()
                                                    : match.rectangles().first().center();
    m_scroll->ensureVisible(widget->x() + qRound(point.x() * widget->width() / size.width()),
                           widget->y() + qRound(point.y() * widget->height() / size.height()), 40, 40);
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

    // Keep a snapshot in memory so selection never holds the source file open.
    QFile file(m_currentFile);
    if (!file.open(QIODevice::ReadOnly)) {
        statusBar()->showMessage(tr("Error: could not open %1").arg(m_currentFile));
        return;
    }
    auto buffer = std::make_unique<QBuffer>();
    buffer->setData(file.readAll());
    const bool readFailed = file.error() != QFileDevice::NoError;
    file.close();
    buffer->open(QIODevice::ReadOnly);
    auto document = std::make_unique<QPdfDocument>();
    document->load(buffer.get());
    if (readFailed || document->status() != QPdfDocument::Status::Ready) {
        statusBar()->showMessage(tr("Error: could not open %1").arg(m_currentFile));
        return;
    }
    QPdfDocument &doc = *document;

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

    clearSearch();
    // Keep the old snapshot alive until rebuildPageLabels deletes its widgets.
    m_document.swap(document);
    m_pdfBuffer.swap(buffer);

    rebuildPageLabels();
    setWindowTitle(tr("SlimPDF — %1").arg(QFileInfo(m_currentFile).fileName()));
    updateStatus();

    m_firstVisible = 0;
    m_lastVisible  = 0;

    QTimer::singleShot(50, this, [this, scrollTo] {
        m_scroll->verticalScrollBar()->setValue(scrollTo);
        updatePageIndicator();
        if (m_findDialog->isVisible())
            startSearch();
    });
}

void MainWindow::rebuildPageLabels()
{
    onSelectionChanged({});
    while (m_pagesLayout->count()) {
        QLayoutItem *item = m_pagesLayout->takeAt(0);
        delete item->widget();
        delete item;
    }

    for (int i = 0; i < m_pageImages.size(); ++i) {
        auto *pw = new PageWidget(m_document.get(), i);
        pw->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        connect(pw, &PageWidget::selectionStarted, this, [this, pw] {
            for (int j = 0; j < m_pagesLayout->count(); ++j) {
                auto *w = qobject_cast<PageWidget *>(m_pagesLayout->itemAt(j)->widget());
                if (w && w != pw) w->clearSelection();
            }
        });
        connect(pw, &PageWidget::selectionChanged, this, &MainWindow::onSelectionChanged);
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
    QTimer::singleShot(0, this, &MainWindow::updatePageIndicator);
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

    const QRect viewport(m_scroll->horizontalScrollBar()->value(),
                         m_scroll->verticalScrollBar()->value(),
                         m_scroll->viewport()->width(), m_scroll->viewport()->height());

    int first = -1, last = -1, dominantPage = -1;
    double largestVisibleFraction = 0.0;
    for (int i = 0; i < count; ++i) {
        auto *w = m_pagesLayout->itemAt(i)->widget();
        if (!w || w->width() <= 0 || w->height() <= 0) continue;
        const QRect visible = w->geometry().intersected(viewport);
        if (visible.isEmpty()) continue;

        if (first < 0) first = i;
        last = i;
        const double visibleFraction = (double(visible.width()) * visible.height())
                                     / (double(w->width()) * w->height());
        if (visibleFraction >= 0.9 && visibleFraction > largestVisibleFraction) {
            dominantPage = i;
            largestVisibleFraction = visibleFraction;
        }
    }

    if (dominantPage >= 0)
        first = last = dominantPage;

    if (first < 0 || (first == m_firstVisible && last == m_lastVisible))
        return;

    m_firstVisible = first;
    m_lastVisible  = last;
    updateStatus();
}

// ── Recent files ──────────────────────────────────────────────────────────────

static int recentFileLimit()
{
#ifdef Q_OS_MACOS
    return macOSRecentFileLimit();
#else
    return 10;
#endif
}

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
    updateRecentMenu();
}

void MainWindow::updateRecentMenu()
{
    const int limit = recentFileLimit();
    while (m_recentFiles.size() > limit)
        m_recentFiles.removeLast();
    saveRecentFiles();
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

void MainWindow::onSelectionChanged(const QString &text)
{
    m_selectedText = text;
    m_copyAct->setEnabled(!text.trimmed().isEmpty());
}

void MainWindow::copySelection()
{
    if (m_selectedText.trimmed().isEmpty()) return;
    const QString &text = m_selectedText;
    QApplication::clipboard()->setText(text);
    statusBar()->showMessage(
        tr("Copied %1 character%2").arg(text.length()).arg(text.length() == 1 ? "" : "s"),
        3000);
}

// ── Resize ────────────────────────────────────────────────────────────────────

void MainWindow::moveEvent(QMoveEvent *event)
{
    QMainWindow::moveEvent(event);
    QTimer::singleShot(0, this, &MainWindow::positionFindDialog);
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    QTimer::singleShot(0, this, &MainWindow::positionFindDialog);
    if (m_fitMode != FitMode::None && !m_pageImages.isEmpty())
        m_resizeTimer->start(80);
    else
        QTimer::singleShot(0, this, &MainWindow::updatePageIndicator);
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
