/*
 * Copyright 2014 Alex Richardson <arichardson.kde@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) version 3, or any
 * later version accepted by the membership of KDE e.V. (or its
 * successor approved by the membership of KDE e.V.), which shall
 * act as a proxy defined in Section 6 of version 3 of the license.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library.  If not, see
 * <http://www.gnu.org/licenses/>.
 */

#include <QtTest>

#include "kpluginindexer.h"
#include "kpluginindexer_debug.h"

#include <KPluginMetaData>
#include <KPluginLoader>

#include <QLocale>


namespace QTest
{
template<> inline char *toString(const QJsonValue &val)
{
    // simply reuse the QDebug representation
    QString result;
    QDebug(&result) << val;
    return QTest::toString(result);
}
}

class KPluginIndexTest : public QObject
{
    Q_OBJECT

private:
    QString m_pluginDir;
    QString m_jsonPlugin;
    qint64 savings = 0;
    qint64 duration = 0;

    void createPluginDir(const int number)
    {
        QString jsonLocation = QFINDTESTDATA(JSONPLUGIN_FILE);
        QFileInfo jfi(jsonLocation);
        auto pluginpath = jfi.absolutePath();
        m_pluginDir = pluginpath + QStringLiteral("/plugins/");
        m_jsonPlugin = jsonLocation.remove(jfi.absolutePath() + QLatin1Char('/'));
        //qCDebug(KPI) << "jfi:" << m_jsonPlugin;

        QStringList pluginFiles;
        pluginFiles << QFINDTESTDATA(JSONPLUGIN_FILE);
//         pluginFiles << QFINDTESTDATA(VERSIONEDPLUGIN_FILE);
//         pluginFiles << QFINDTESTDATA(UNVERSIONEDPLUGIN_FILE);
//         pluginFiles << QFINDTESTDATA(MULTIPLUGIN_FILE);
//         pluginFiles << QFINDTESTDATA(ALWAYSUNLOADPLUGIN_FILE);

        QDir dir(m_pluginDir);
        QVERIFY(dir.removeRecursively());
        QVERIFY(dir.mkpath(m_pluginDir));

        int i = 0;
        //qCDebug(KPI) << "JSONPLUGIN_FILE" << JSONPLUGIN_FILE;
        while (number > i) {
            foreach (const QString &filename, pluginFiles) {
                const auto newname = QString("%1%2%3").arg(m_pluginDir, QString::number(i), m_jsonPlugin);
                QVERIFY(QFile::copy(filename, newname));
                i++;
                if (number >= i) {
                    continue;
                }
            }
        }
        //qCDebug(KPI) << "Plugin directory with" << number << " plugins created.";

    }

private Q_SLOTS:
    void init()
    {
        createPluginDir(0);
    }

    void cleanup()
    {
        qCWarning(KPI) << "Total savings:" << savings / 1000000 << "msec, Total time:" << duration / 1000000 << "msec; overall speedup" << qRound((savings / (qreal)(qMax((qint64)1, duration))) * 100) << "%";
        //qCWarning(KPI) << "Total time:" << duration / 1000000;

        QDir dir(m_pluginDir);
        dir.removeRecursively();
    }

    void initTestCase()
    {
    }

    void cleanupTestCase()
    {
        qunsetenv("KPLUGIN_SKIP_INDEX");
    }

    void testIndexer()
    {
        createPluginDir(100);
        QElapsedTimer t1;
        auto kpi = KPluginIndexer();
        QVERIFY(kpi.resolveFiles());
        QVERIFY(!kpi.m_pluginDirectories.isEmpty());
        QVERIFY(!kpi.isCacheUpToDate(m_pluginDir));

        t1.start();
        QVERIFY(kpi.createDirectoryIndex(m_pluginDir));
        auto ms = t1.nsecsElapsed() / 1000000;
        qCDebug(KPI) << ms << "msec for 100";
        QVERIFY(kpi.isCacheUpToDate(m_pluginDir));

        createPluginDir(100);
        QVERIFY(!kpi.isCacheUpToDate(m_pluginDir));
        t1.start();
        QVERIFY(kpi.createDirectoryIndex(m_pluginDir));
        ms = t1.nsecsElapsed() / 1000000;
        QVERIFY(kpi.isCacheUpToDate(m_pluginDir));
        qCDebug(KPI) << ms << "msec for 500";

        // mtime resolution is only 1 second, so we need to wait a bit
        // if we modify the directory within 1 second, we get the same
        // mtime, hence the cache seems up to date, but isn't.
        QSignalSpy spy(this, &QObject::destroyed);
        spy.wait(1100);

        QVERIFY(kpi.isCacheUpToDate(m_pluginDir));

        auto indexfile = m_pluginDir + kpi.m_indexFileName;
        QVERIFY(QFile::exists(indexfile));

        QFile plugin0(m_pluginDir + "0" + m_jsonPlugin);
        QVERIFY(plugin0.exists());
        QVERIFY(plugin0.remove());

        QVERIFY(!plugin0.exists());
        QVERIFY(!kpi.isCacheUpToDate(m_pluginDir));

        QVERIFY(kpi.createDirectoryIndex(m_pluginDir));
        QVERIFY(kpi.isCacheUpToDate(m_pluginDir));

        QFile ixinfo(indexfile);
        qCDebug(KPI) << "index size:" << ixinfo.size() / 1024 << "kb";

        QVERIFY(ixinfo.remove());
        QVERIFY(!kpi.isCacheUpToDate(m_pluginDir));
    }

    void testFindPlugins()
    {
        createPluginDir(6);
        auto kpi = KPluginIndexer();
        QVERIFY(!kpi.isCacheUpToDate(m_pluginDir));
        QVERIFY(kpi.createDirectoryIndex(m_pluginDir));
        QVERIFY(kpi.isCacheUpToDate(m_pluginDir));

        QElapsedTimer t1;
        t1.start();
        auto plugins =  KPluginLoader::findPlugins(m_pluginDir);
        int t_with = t1.nsecsElapsed();

        QVERIFY(plugins.count());
        int with_index = plugins.count();
        qCDebug(KPI) << "Found plugins:" << plugins.count();

        qputenv("KPLUGIN_SKIP_INDEX", "1");
        t1.start();
        auto plugins_noindex =  KPluginLoader::findPlugins(m_pluginDir);
        int t_without = t1.nsecsElapsed();
        int without_index = plugins.count();

        QCOMPARE(with_index, without_index);
        qunsetenv("KPLUGIN_SKIP_INDEX");

        qCDebug(KPI) << "Timing (with/without):" << t_with << t_without << "Good?" << (t_with < t_without) << ((qreal)t_with / (qreal)t_without) << (qreal)((t_without - t_with) / 1000000) << "msec faster!";
    }

    void testCommonScenarios_data()
    {
        QTest::addColumn<int>("iterations");
        QTest::addColumn<QString>("pluginpath");

        // This is roughly what Plasma does on startup
        QElapsedTimer t;
        t.start();
        QTest::newRow("             empty") << 15 << QString();
        QTest::newRow("           applets") << 18 << QStringLiteral("plasma/applets");
        QTest::newRow("  packagestructure") << 24 << QStringLiteral("kpackage/packagestructure/");
        QTest::newRow("     scriptengines") << 18 << QStringLiteral("plasma/scriptengines");
        QTest::newRow("containmentactions") << 13 << QStringLiteral("plasma/containmentactions");
        QTest::newRow("        dataengine") << 10 << QStringLiteral("plasma/dataengine");
        QTest::newRow("               kio") << 10 << QStringLiteral("kf5/kio");
        QTest::newRow("              kded") << 1 << QStringLiteral("kf5/kded");
        QTest::newRow("     kfilemetadata") << 5 << QStringLiteral("kf5/kfilemetadata");
        QTest::newRow("kwin/efects/config") << 1 << QStringLiteral("kf5/kfilemetadata");
        QTest::newRow("           kscreen") << 3 << QStringLiteral("kf5/kscreen");
        QTest::newRow("          kdevelop") << 3 << QStringLiteral("kdevplatform/25/");

    }
    void testCommonScenarios()
    {
        QFETCH(int, iterations);
        QFETCH(QString, pluginpath);

        qunsetenv("KPLUGIN_SKIP_INDEX");
        QElapsedTimer t;
        t.start();
        for (int i = 0; i < iterations; i++) {
            auto plugins = KPluginLoader::findPlugins(pluginpath);
        }
        auto t_cached = t.nsecsElapsed();

        qputenv("KPLUGIN_SKIP_INDEX", "1");
        t.start();
        for (int i = 0; i < iterations; i++) {
            auto plugins = KPluginLoader::findPlugins(pluginpath);
        }
        auto t_nocache = t.nsecsElapsed();
        qCDebug(KPI) << "cached / nocache:" << /*t_cached << t_nocache << */ ((qreal)t_cached / (qreal)t_nocache) << "saved" << (t_nocache - t_cached) / 1000000 << "msec; spent msec: " << t_nocache / 1000000;
        //qCDebug(KPI) << "TIME:" << (t.nsecsElapsed() / 1000000);
        savings = savings + (t_nocache - t_cached);
        duration = duration + t_nocache;
    }

};

QTEST_MAIN(KPluginIndexTest)

#include "kpluginindextest.moc"
