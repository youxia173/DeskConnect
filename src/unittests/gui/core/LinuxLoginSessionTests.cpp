/*
 * SPDX-FileCopyrightText: (C) 2026 DeskConnect Contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "gui/core/LinuxLoginSession.h"
#include <QTest>

class LinuxLoginSessionTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void preservesOtherLoginSettings()
  {
    const QByteArray input = "# login\n[daemon]\n#WaylandEnable=false\nAutomaticLoginEnable=True\n"
                             "[security]\nDisallowTCP=true\n";
    const auto output = deskflow::gui::gdmConfigWithDefaultXorg(input);
    QCOMPARE(output, QByteArray("# login\n[daemon]\nWaylandEnable=false\nAutomaticLoginEnable=True\n"
                                "[security]\nDisallowTCP=true\n"));
    QCOMPARE(deskflow::gui::gdmConfigWithDefaultXorg(output), output);
  }
  void addsSettingToCorrectSection()
  {
    QCOMPARE(deskflow::gui::gdmConfigWithDefaultXorg("[daemon]\nTimedLoginEnable=false\n[debug]\nEnable=true"),
             QByteArray("[daemon]\nTimedLoginEnable=false\nWaylandEnable=false\n[debug]\nEnable=true"));
  }
  void addsMissingDaemonSection()
  {
    QCOMPARE(deskflow::gui::gdmConfigWithDefaultXorg("[debug]\nEnable=false"),
             QByteArray("[debug]\nEnable=false\n[daemon]\nWaylandEnable=false\n"));
  }
  void removesConflictingDaemonValuesOnly()
  {
    QCOMPARE(deskflow::gui::gdmConfigWithDefaultXorg("[daemon]\nWaylandEnable=true\n # WaylandEnable=true\n"
                                                   "[other]\nWaylandEnable=true\n"),
             QByteArray("[daemon]\nWaylandEnable=false\nWaylandEnable=false\n[other]\nWaylandEnable=true\n"));
  }
  void preservesWindowsLineEndings()
  {
    QCOMPARE(deskflow::gui::gdmConfigWithDefaultXorg("[daemon]\r\n#WaylandEnable=false\r\n[debug]\r\n"),
             QByteArray("[daemon]\r\nWaylandEnable=false\r\n[debug]\r\n"));
  }
};

QTEST_GUILESS_MAIN(LinuxLoginSessionTests)
#include "LinuxLoginSessionTests.moc"
