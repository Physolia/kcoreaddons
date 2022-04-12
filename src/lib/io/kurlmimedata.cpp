/*
    This file is part of the KDE libraries

    SPDX-FileCopyrightText: 2005-2012 David Faure <faure@kde.org>
    SPDX-FileCopyrightText: 2022 Harald Sitter <sitter@kde.org>

    SPDX-License-Identifier: LGPL-2.0-or-later
*/

#include "kurlmimedata.h"
#include "config-kdirwatch.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <optional>

#include <QMimeData>
#include <QStringList>

#include "kcoreaddons_debug.h"
#if HAVE_QTDBUS
#include "org.freedesktop.portal.FileTransfer.h"
#include "org.kde.KIOFuse.VFS.h"
#endif

static QString kdeUriListMime()
{
    return QStringLiteral("application/x-kde4-urilist");
} // keep this name "kde4" for compat.

static QByteArray uriListData(const QList<QUrl> &urls)
{
    // compatible with qmimedata.cpp encoding of QUrls
    QByteArray result;
    for (int i = 0; i < urls.size(); ++i) {
        result += urls.at(i).toEncoded();
        result += "\r\n";
    }
    return result;
}

void KUrlMimeData::setUrls(const QList<QUrl> &urls, const QList<QUrl> &mostLocalUrls, QMimeData *mimeData)
{
    // Export the most local urls as text/uri-list and plain text, for non KDE apps.
    mimeData->setUrls(mostLocalUrls); // set text/uri-list and text/plain

    // Export the real KIO urls as a kde-specific mimetype
    mimeData->setData(kdeUriListMime(), uriListData(urls));
}

void KUrlMimeData::setMetaData(const MetaDataMap &metaData, QMimeData *mimeData)
{
    QByteArray metaDataData; // :)
    for (auto it = metaData.cbegin(); it != metaData.cend(); ++it) {
        metaDataData += it.key().toUtf8();
        metaDataData += "$@@$";
        metaDataData += it.value().toUtf8();
        metaDataData += "$@@$";
    }
    mimeData->setData(QStringLiteral("application/x-kio-metadata"), metaDataData);
}

QStringList KUrlMimeData::mimeDataTypes()
{
    return QStringList{kdeUriListMime(), QStringLiteral("text/uri-list")};
}

static QList<QUrl> extractKdeUriList(const QMimeData *mimeData)
{
    QList<QUrl> uris;
    const QByteArray ba = mimeData->data(kdeUriListMime());
    // Code from qmimedata.cpp
    QList<QByteArray> urls = ba.split('\n');
    uris.reserve(urls.size());
    for (int i = 0; i < urls.size(); ++i) {
        QByteArray data = urls.at(i).trimmed();
        if (!data.isEmpty()) {
            uris.append(QUrl::fromEncoded(data));
        }
    }
    return uris;
}

#if HAVE_QTDBUS
static QString kioFuseServiceName()
{
    return QStringLiteral("org.kde.KIOFuse");
}

static QString portalServiceName()
{
    return QStringLiteral("org.freedesktop.portal.Documents");
}

static bool isKIOFuseAvailable()
{
    static bool available = QDBusConnection::sessionBus().interface()
        && QDBusConnection::sessionBus().interface()->activatableServiceNames().value().contains(kioFuseServiceName());
    return available;
}

static bool isDocumentsPortalAvailable()
{
    static bool available = QDBusConnection::sessionBus().interface()
        && QDBusConnection::sessionBus().interface()->activatableServiceNames().value().contains(portalServiceName());
    return available;
}

static QString portalFormat() { return QStringLiteral("application/vnd.portal.filetransfer"); }

static QList<QUrl> extractPortalUriList(const QMimeData *mimeData)
{
    const auto transferId = mimeData->data(portalFormat());
    qCDebug(KCOREADDONS_DEBUG) << "Picking up portal urls from transfer" << transferId;
    auto iface = new OrgFreedesktopPortalFileTransferInterface(portalServiceName(),
                                                                QStringLiteral("/org/freedesktop/portal/documents"),
                                                                QDBusConnection::sessionBus());
    const QStringList list = iface->RetrieveFiles(QString::fromUtf8(transferId), {});
    QList<QUrl> uris;
    uris.reserve(list.size());
    for (const auto &path : list) {
        uris.append(QUrl::fromLocalFile(path));
    }
    qCDebug(KCOREADDONS_DEBUG) << "Urls from portal" << uris;
    return uris;
}
#endif

QList<QUrl> KUrlMimeData::urlsFromMimeData(const QMimeData *mimeData, DecodeOptions decodeOptions, MetaDataMap *metaData)
{
    QList<QUrl> uris;

#if HAVE_QTDBUS
    if (isDocumentsPortalAvailable() && mimeData->hasFormat(portalFormat())) {
        uris = extractPortalUriList(mimeData);
    }
#endif

    if (uris.isEmpty()) {
        if (decodeOptions == PreferLocalUrls) {
            // Extracting uris from text/uri-list, use the much faster QMimeData method urls()
            uris = mimeData->urls();
            if (uris.isEmpty()) {
                uris = extractKdeUriList(mimeData);
            }
        } else {
            uris = extractKdeUriList(mimeData);
            if (uris.isEmpty()) {
                uris = mimeData->urls();
            }
        }
    }

    if (metaData) {
        const QByteArray metaDataPayload = mimeData->data(QStringLiteral("application/x-kio-metadata"));
        if (!metaDataPayload.isEmpty()) {
            QString str = QString::fromUtf8(metaDataPayload.constData());
            Q_ASSERT(str.endsWith(QLatin1String("$@@$")));
            str.chop(4);
            const QStringList lst = str.split(QStringLiteral("$@@$"));
            bool readingKey = true; // true, then false, then true, etc.
            QString key;
            for (const QString &s : lst) {
                if (readingKey) {
                    key = s;
                } else {
                    metaData->insert(key, s);
                }
                readingKey = !readingKey;
            }
            Q_ASSERT(readingKey); // an odd number of items would be, well, odd ;-)
        }
    }
    return uris;
}

#if HAVE_QTDBUS
static std::optional<QStringList> fuseRedirect(QList<QUrl> urls)
{
    qCDebug(KCOREADDONS_DEBUG) << "mounting urls with fuse" << urls;

    OrgKdeKIOFuseVFSInterface kiofuse_iface(kioFuseServiceName(), QStringLiteral("/org/kde/KIOFuse"), QDBusConnection::sessionBus());
    struct MountRequest {
        QDBusPendingReply<QString> reply;
        int urlIndex;
        QString basename;
    };
    QVector<MountRequest> requests;
    requests.reserve(urls.count());
    for (int i = 0; i < urls.count(); ++i) {
        QUrl url = urls.at(i);
        if (!url.isLocalFile()) {
            const QString path(url.path());
            const int slashes = path.count(QLatin1Char('/'));
            QString basename;
            if (slashes > 1) {
                url.setPath(path.section(QLatin1Char('/'), 0, slashes - 1));
                basename = path.section(QLatin1Char('/'), slashes, slashes);
            }
            requests.push_back({kiofuse_iface.mountUrl(url.toString()), i, basename});
        }
    }

    for (auto &request : requests) {
        request.reply.waitForFinished();
        if (request.reply.isError()) {
            qWarning() << "FUSE request failed:" << request.reply.error();
            return std::nullopt;
        }

        urls[request.urlIndex] = QUrl::fromLocalFile(request.reply.value() + QLatin1Char('/') + request.basename);
    };

    qCDebug(KCOREADDONS_DEBUG) << "mounted urls with fuse, maybe" << urls;

    QStringList list;
    for (const auto &url : urls) {
        list << url.toLocalFile();
    }
    return list;
}
#endif

bool KUrlMimeData::exportUrlsToPortal(QMimeData *mimeData)
{
#if HAVE_QTDBUS
    if (!isDocumentsPortalAvailable() || !isKIOFuseAvailable()) {
        return false;
    }

    auto iface = new OrgFreedesktopPortalFileTransferInterface(portalServiceName(),
                                                 QStringLiteral("/org/freedesktop/portal/documents"),
                                                 QDBusConnection::sessionBus());

    const QString transferId = iface->StartTransfer({});
    qDebug() << "TRANSFERING" << transferId<< QFile::encodeName(transferId);
    mimeData->setData(QStringLiteral("application/vnd.portal.filetransfer"), QFile::encodeName(transferId));

    auto optionalPaths = fuseRedirect(mimeData->urls());
    if (!optionalPaths.has_value()) {
        qCWarning(KCOREADDONS_DEBUG) << "Failed to mount with fuse!";
        return false;
    }

    for (const auto &path : optionalPaths.value()) {
        qCDebug(KCOREADDONS_DEBUG) << path;
        const int fd = open(QFile::encodeName(path).constData(), O_RDONLY | O_CLOEXEC | O_NONBLOCK);
        if (fd == -1) {
            const int error = errno;
            qCWarning(KCOREADDONS_DEBUG) << "Failed to open" << path << strerror(error);
        }
        iface->AddFiles(transferId, {QDBusUnixFileDescriptor(fd)}, {});
        close(fd);
    }
    QObject::connect(mimeData, &QObject::destroyed, mimeData, [transferId, iface] {
        qDebug() << "stopping TRANSFER";
        iface->StopTransfer(transferId);
        iface->deleteLater();
    });
    QObject::connect(iface, &OrgFreedesktopPortalFileTransferInterface::TransferClosed, mimeData, [iface]() {
        qDebug() << "TransferClosed";
        iface->deleteLater();
    });

    return true;
#else
    return false;
#endif
}
