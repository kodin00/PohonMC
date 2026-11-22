// SPDX-License-Identifier: GPL-3.0-only
#include "modplatform/share/SharePack.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QTest>

class SharePackTest : public QObject {
    Q_OBJECT
    QJsonObject manifest() const
    {
        QJsonObject file{ { "path", "mods/example.jar" },
                          { "downloads", QJsonArray{ "https://cdn.modrinth.com/data/a/versions/b/example.jar" } },
                          { "hashes", QJsonObject{ { "sha512", QString(128, 'a') } } },
                          { "env", QJsonObject{ { "client", "required" }, { "server", "required" } } },
                          { "pohonServerRequirement", "required" } };
        return { { "formatVersion", 1 },
                 { "game", "minecraft" },
                 { "name", "Example" },
                 { "dependencies", QJsonObject{ { "minecraft", "1.21" }, { "fabric-loader", "0.16" } } },
                 { "files", QJsonArray{ file } } };
    }
   private slots:
    void roundTrip()
    {
        auto original = manifest();
        QString error;
        auto code = SharePack::encode(original, &error);
        QVERIFY2(!code.isEmpty(), qPrintable(error));
        QJsonObject decoded;
        QVERIFY2(SharePack::decode(code, &decoded, &error), qPrintable(error));
        QCOMPARE(decoded, original);
        QVERIFY(!SharePack::decode("POHON2:" + code.mid(7), &decoded));
        QVERIFY(!SharePack::decode(code + "!", &decoded));
        auto compressed = qCompress(QByteArray("{}"));
        compressed.append('x');
        QVERIFY(!SharePack::decode(
            "POHON1:" + QString::fromLatin1(compressed.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals)),
            &decoded));
    }
    void includesManualDownloads()
    {
        auto original = manifest();
        auto files = original.value("files").toArray();
        const auto downloadable = files.first().toObject();
        QJsonObject manual{ { "path", "mods/private.jar" }, { "pohonManualDownload", true }, { "pohonServerRequirement", "required" } };
        files.append(manual);
        original.insert("files", files);
        QString error;
        const auto code = SharePack::encode(original, &error);
        QVERIFY2(!code.isEmpty(), qPrintable(error));
        QJsonObject decoded;
        QVERIFY2(SharePack::decode(code, &decoded, &error), qPrintable(error));
        QCOMPARE(decoded, original);

        manual.insert("downloads", downloadable.value("downloads"));
        files.replace(1, manual);
        original.insert("files", files);
        QVERIFY(!SharePack::validate(original));
    }
    void rejectsUntrustedInputs()
    {
        auto m = manifest();
        m["formatVersion"] = 2;
        QVERIFY(!SharePack::validate(m));
        m = manifest();
        m["dependencies"] = QJsonObject{ { "minecraft", "../unsafe" } };
        QVERIFY(!SharePack::validate(m));
        for (const QString& path : { "mods/../evil.jar", "mods/CON.jar", "mods/a\\b.jar", "mods/a.jar/evil.jar", "mods/a!b.jar" }) {
            m = manifest();
            auto files = m["files"].toArray();
            auto file = files[0].toObject();
            file["path"] = path;
            files[0] = file;
            m["files"] = files;
            QVERIFY2(!SharePack::validate(m), qPrintable(path));
        }
        for (const QString& url :
             { "file:///tmp/a.jar", "https://cdn.modrinth.com.evil.test/a.jar", "https://user@cdn.modrinth.com/a.jar" }) {
            m = manifest();
            auto files = m["files"].toArray();
            auto file = files[0].toObject();
            file["downloads"] = QJsonArray{ url };
            files[0] = file;
            m["files"] = files;
            QVERIFY2(!SharePack::validate(m), qPrintable(url));
        }
        m = manifest();
        auto files = m["files"].toArray();
        auto file = files[0].toObject();
        file["hashes"] = QJsonObject{ { "sha512", "bad" } };
        files[0] = file;
        m["files"] = files;
        QVERIFY(!SharePack::validate(m));
    }
};
QTEST_GUILESS_MAIN(SharePackTest)
#include "SharePack_test.moc"
