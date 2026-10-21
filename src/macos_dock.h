#pragma once
#ifdef Q_OS_MACOS
#include <QStringList>
#include <functional>
int macOSRecentFileLimit();
void setupMacOSDockMenu(std::function<void(const QString &)> openCallback);
void updateMacOSDockMenu(const QStringList &recentFiles);
#endif
