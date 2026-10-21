#include "main_window.h"

#include <QApplication>
#include <QBuffer>
#include <QMouseEvent>
#include <QPainter>
#include <QPdfDocument>
#include <QPdfSelection>
#include <QPdfWriter>
#include <QSignalSpy>
#include <QtTest>

class TextSelectionTest : public QObject {
    Q_OBJECT
private slots:
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
        QSignalSpy copied(&widget, &PageWidget::selectionMade);
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
        QCOMPARE(copied.count(), 0);
        QCOMPARE(image(), plain);

        press(start);
        drag(end);
        const QImage highlighted = image();
        QVERIFY(highlighted != plain); // Highlight is visible before release.
        // The space between lines stays white even inside the drag region.
        QCOMPARE(highlighted.pixelColor(QPoint(45 * zoom, 75 * zoom)), Qt::white);
        QCOMPARE(copied.count(), 0);
        release(end);
        QCOMPARE(copied.count(), 1);
        const QString forward = copied.takeFirst().first().toString();
        QVERIFY(forward.contains("first line"));
        QVERIFY(forward.contains("Seco"));
        QCOMPARE(image(), highlighted);

        press(end);
        drag(start);
        release(start);
        QCOMPARE(copied.count(), 1);
        QCOMPARE(copied.takeFirst().first().toString(), forward);
        QCOMPARE(image(), highlighted);

        // Same-line horizontal drags also select text.
        const QPointF sameLine = document.getSelectionAtIndex(0, 12, 1).boundingRectangle().center() * zoom;
        press(start);
        drag(sameLine);
        release(sameLine);
        QCOMPARE(copied.count(), 1);
        QVERIFY(copied.takeFirst().first().toString().contains("first"));

        // Clicking whitespace clears the selection without copying anything.
        press(QPointF(10, 10) * zoom);
        drag(end);
        release(end);
        QCOMPARE(copied.count(), 0);
        QCOMPARE(image(), plain);
    }
};

QTEST_MAIN(TextSelectionTest)
#include "text_selection_test.moc"
