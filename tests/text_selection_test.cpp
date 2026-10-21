#include "main_window.h"

#include <QApplication>
#include <QBuffer>
#include <QAction>
#include <QClipboard>
#include <QMenu>
#include <QSettings>
#include <QTemporaryDir>
#include <QMouseEvent>
#include <QPainter>
#include <QPdfDocument>
#include <QPdfSelection>
#include <QPdfWriter>
#include <QSvgRenderer>
#include <QtTest>

class TextSelectionTest : public QObject {
    Q_OBJECT
    QTemporaryDir m_settingsDir;

private slots:
    void initTestCase()
    {
        QVERIFY(m_settingsDir.isValid());
        QCoreApplication::setOrganizationName("SlimPDFTests");
        QCoreApplication::setApplicationName("TextSelectionTests");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settingsDir.path());
    }

    void toolbarIcons_data()
    {
        QTest::addColumn<QString>("label");
        QTest::addColumn<QString>("path");
        QTest::addColumn<int>("dpr");
        const QList<QPair<QString, QString>> icons = {
            {"Zoom In", "zoom-in"}, {"Zoom Out", "zoom-out"},
            {"Actual Size", "zoom-reset"}, {"Fit Width", "fit-width"},
            {"Fit Height", "fit-height"}, {"Fit Page", "fit-page"}
        };
        for (const auto &icon : icons) {
            for (int dpr : {1, 2, 3}) {
                const QByteArray name = (icon.second + QString::number(dpr)).toUtf8();
                QTest::newRow(name.constData()) << icon.first
                    << QString(":/icons/%1.svg").arg(icon.second) << dpr;
            }
        }
    }

    void toolbarIcons()
    {
        QFETCH(QString, label);
        QFETCH(QString, path);
        QFETCH(int, dpr);
        MainWindow window;
        QAction *action = nullptr;
        for (auto *candidate : window.findChildren<QAction *>()) {
            if (candidate->text() == label) action = candidate;
        }
        QVERIFY(action);
        QVERIFY(!action->icon().isNull());
        const QSize physicalSize(22 * dpr, 22 * dpr);
        QImage actual = action->icon().pixmap(QSize(22, 22), qreal(dpr)).toImage();
        QCOMPARE(actual.size(), physicalSize);
        actual.setDevicePixelRatio(1.0);

        // Render independently in physical pixels to catch clipping from applying
        // the screen scale twice to the SVG viewport.
        QImage expected(physicalSize, QImage::Format_ARGB32_Premultiplied);
        expected.fill(Qt::transparent);
        QSvgRenderer renderer(path);
        QVERIFY(renderer.isValid());
        {
            QPainter painter(&expected);
            renderer.render(&painter, QRectF(QPointF(0, 0), QSizeF(physicalSize)));
        }
        QCOMPARE(actual, expected);
    }

    void copyCommand()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath("selection.pdf");
        {
            QPdfWriter writer(path);
            writer.setResolution(72);
            writer.setPageSize(QPageSize(QSizeF(300, 300), QPageSize::Point));
            writer.setPageMargins(QMarginsF(0, 0, 0, 0));
            QPainter painter(&writer);
            painter.setFont(QFont("Helvetica", 16));
            painter.drawText(QPointF(30, 60), "Select this text");
        }
        QPdfDocument document;
        QCOMPARE(document.load(path), QPdfDocument::Error::None);
        const QPointF start = document.getSelectionAtIndex(0, 1, 1).boundingRectangle().center();
        const QPointF end = document.getSelectionAtIndex(0, 12, 1).boundingRectangle().center();
        document.close();

        MainWindow window;
        QAction *copy = nullptr;
        for (auto *menu : window.findChildren<QMenu *>()) {
            if (menu->title() != "&Edit") continue;
            for (auto *action : menu->actions()) {
                if (action->shortcut() == QKeySequence(QKeySequence::Copy))
                    copy = action;
            }
        }
        QVERIFY(copy);
        QVERIFY(!copy->isEnabled());
        window.loadFile(path);
        auto *page = window.findChild<PageWidget *>();
        QVERIFY(page);
        const double scale = page->width() / 300.0;
        auto mouse = [&](QEvent::Type type, QPointF position, Qt::MouseButtons buttons) {
            QMouseEvent event(type, position * scale, position * scale,
                              type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
                              buttons, Qt::NoModifier);
            QApplication::sendEvent(page, &event);
        };
        auto select = [&] {
            mouse(QEvent::MouseButtonPress, start, Qt::LeftButton);
            mouse(QEvent::MouseMove, end, Qt::LeftButton);
            mouse(QEvent::MouseButtonRelease, end, Qt::NoButton);
        };

        auto *clipboard = QApplication::clipboard();
        clipboard->setText("Existing clipboard contents");
        select();
        QVERIFY(copy->isEnabled());
        QCOMPARE(clipboard->text(), "Existing clipboard contents");
        copy->trigger();
        QVERIFY(clipboard->text().contains("this"));
        const QString copiedText = clipboard->text();
        QVERIFY(copy->isEnabled()); // Copy preserves the selection.

        mouse(QEvent::MouseButtonPress, QPointF(10, 10), Qt::LeftButton);
        mouse(QEvent::MouseButtonRelease, QPointF(10, 10), Qt::NoButton);
        QVERIFY(!copy->isEnabled());
        clipboard->setText("Keep this clipboard text");
        copy->trigger();
        QCOMPARE(clipboard->text(), "Keep this clipboard text");

        select();
        copy->trigger();
        QCOMPARE(clipboard->text(), copiedText);
        window.loadFile(path);
        QVERIFY(!copy->isEnabled()); // Reloading cannot copy the old selection.
        clipboard->setText("After reload");
        copy->trigger();
        QCOMPARE(clipboard->text(), "After reload");
    }

    void selection_data()
    {
        QTest::addColumn<double>("zoom");
        QTest::newRow("small") << 0.5;
        QTest::newRow("normal") << 1.0;
        QTest::newRow("large") << 2.0;
    }

    void selection()
    {
        QFETCH(double, zoom);
        QByteArray pdf;
        QBuffer output(&pdf);
        QVERIFY(output.open(QIODevice::WriteOnly));
        {
            QPdfWriter writer(&output);
            writer.setResolution(72);
            writer.setPageSize(QPageSize(QSizeF(300, 300), QPageSize::Point));
            writer.setPageMargins(QMarginsF(0, 0, 0, 0));
            QPainter painter(&writer);
            painter.setFont(QFont("Helvetica", 16));
            painter.drawText(QPointF(30, 60), "Hello first line");
            painter.drawText(QPointF(30, 100), "Second text line");
            // Dark graphics must not become selectable text.
            painter.fillRect(QRectF(200, 200, 50, 50), Qt::black);
        }
        output.close();
        QBuffer input(&pdf);
        QVERIFY(input.open(QIODevice::ReadOnly));
        QPdfDocument document;
        document.load(&input);
        QCOMPARE(document.status(), QPdfDocument::Status::Ready);
        const QString text = document.getAllText(0).text();
        QVERIFY(text.contains("Hello first line"));
        const int endIndex = text.indexOf("Second") + 5;
        const QPointF start = document.getSelectionAtIndex(0, 1, 1).boundingRectangle().center() * zoom;
        const QPointF end = document.getSelectionAtIndex(0, endIndex, 1).boundingRectangle().center() * zoom;

        PageWidget widget(&document, 0);
        widget.setPixmap(QPixmap::fromImage(document.render(0, QSize(300 * zoom, 300 * zoom))));
        QString selectedText;
        connect(&widget, &PageWidget::selectionChanged, this,
                [&](const QString &text) { selectedText = text; });
        auto mouse = [&](QEvent::Type type, QPointF point, Qt::MouseButton button, Qt::MouseButtons buttons) {
            QMouseEvent event(type, point, point, button, buttons, Qt::NoModifier);
            QApplication::sendEvent(&widget, &event);
        };
        auto move = [&](QPointF point) {
            mouse(QEvent::MouseMove, point, Qt::NoButton, Qt::NoButton);
        };
        auto press = [&](QPointF point) {
            mouse(QEvent::MouseButtonPress, point, Qt::LeftButton, Qt::LeftButton);
        };
        auto drag = [&](QPointF point) {
            mouse(QEvent::MouseMove, point, Qt::NoButton, Qt::LeftButton);
        };
        auto release = [&](QPointF point) {
            mouse(QEvent::MouseButtonRelease, point, Qt::LeftButton, Qt::NoButton);
        };
        auto image = [&] {
            QImage result(widget.size(), QImage::Format_ARGB32);
            widget.render(&result);
            return result;
        };

        move(start);
        QCOMPARE(widget.cursor().shape(), Qt::IBeamCursor);
        move(QPointF(10, 10) * zoom);
        QCOMPARE(widget.cursor().shape(), Qt::ArrowCursor);
        move(QPointF(220, 220) * zoom);
        QCOMPARE(widget.cursor().shape(), Qt::ArrowCursor);

        const QImage plain = image();
        press(start);
        release(start);
        QVERIFY(selectedText.isEmpty());
        QCOMPARE(image(), plain);

        press(start);
        drag(end);
        const QImage highlighted = image();
        QVERIFY(highlighted != plain); // Highlight is visible before release.
        // The space between lines stays white even inside the drag region.
        QCOMPARE(highlighted.pixelColor(QPoint(45 * zoom, 75 * zoom)), Qt::white);
        QVERIFY(!selectedText.isEmpty());
        release(end);
        const QString forward = selectedText;
        QVERIFY(forward.contains("first line"));
        QVERIFY(forward.contains("Seco"));
        QCOMPARE(image(), highlighted);

        press(end);
        drag(start);
        release(start);
        QCOMPARE(selectedText, forward);
        QCOMPARE(image(), highlighted);

        // Same-line horizontal drags also select text.
        const QPointF sameLine = document.getSelectionAtIndex(0, 12, 1).boundingRectangle().center() * zoom;
        press(start);
        drag(sameLine);
        release(sameLine);
        QVERIFY(selectedText.contains("first"));

        // Clicking whitespace clears the selection without copying anything.
        press(QPointF(10, 10) * zoom);
        drag(end);
        release(end);
        QVERIFY(selectedText.isEmpty());
        QCOMPARE(image(), plain);
    }
};

QTEST_MAIN(TextSelectionTest)
#include "text_selection_test.moc"
