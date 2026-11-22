// SPDX-License-Identifier: GPL-3.0-only
#include "modplatform/share/SharePack.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QTest>
#include <QtEndian>

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

    static QJsonObject downloadable(const QString& path)
    {
        return { { "path", path },
                 { "downloads", QJsonArray{ "https://cdn.modrinth.com/data/a/versions/b/example.jar" } },
                 { "hashes", QJsonObject{ { "sha512", QString(128, 'a') } } },
                 { "pohonServerRequirement", "required" } };
    }
    QJsonObject withFiles(const QJsonArray& files) const
    {
        auto m = manifest();
        m["files"] = files;
        return m;
    }
    static QString encodeRaw(const QByteArray& json, quint32 declared)
    {
        QByteArray raw = qCompress(json);  // 4-byte big-endian length + zlib stream
        qToBigEndian<quint32>(declared, reinterpret_cast<uchar*>(raw.data()));
        return "POHON1:" + QString::fromLatin1(raw.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
    }

   private slots:
    void exportSanitisesAwkwardText()
    {
        auto file = downloadable("mods/a.jar");
        file["name"] = QString("Evil\nName\t\x01 ") + QString(300, 'x');
        file["version"] = "\n\t \x7f";
        auto sanitised = SharePack::sanitizeForExport(withFiles({ file, downloadable("mods/b.jar") }));
        QString error;
        QVERIFY2(SharePack::validate(sanitised, &error), qPrintable(error));
        QVERIFY(!SharePack::encode(sanitised).isEmpty());
        const auto out = sanitised["files"].toArray()[0].toObject();
        QVERIFY(out["name"].toString().startsWith("EvilName"));
        QVERIFY(out["name"].toString().size() <= 256);
        QVERIFY(!out.contains("version"));
        QCOMPARE(SharePack::sanitizeText("  \n\t "), QString());
        QCOMPARE(SharePack::sanitizeText(" a\tb\n "), QString("ab"));
    }
    void exportHandlesUnshippableEntries()
    {
        auto badHash = downloadable("mods/hash.jar");
        badHash["hashes"] = QJsonObject{ { "sha512", "bad" } };
        QJsonObject manualWithName{ { "path", "mods/m.jar" }, { "pohonManualDownload", true }, { "name", "Ok" } };
        QJsonArray files{ downloadable("mods/ok.jar"),
                          downloadable("mods/bad!name.jar"),
                          downloadable("mods/" + QString(300, 'a') + ".jar"),
                          downloadable("mods/OK.jar"),
                          badHash,
                          manualWithName,
                          downloadable("other/x.jar") };
        QStringList skipped;
        auto sanitised = SharePack::sanitizeForExport(withFiles(files), &skipped);
        QString error;
        QVERIFY2(SharePack::validate(sanitised, &error), qPrintable(error));
        QCOMPARE(skipped.size(), 4);
        const auto out = sanitised["files"].toArray();
        QCOMPARE(out.size(), 3);
        QCOMPARE(out[0].toObject()["path"].toString(), QString("mods/ok.jar"));
        QVERIFY(!SharePack::requiresManualDownload(out[0].toObject()));
        QCOMPARE(out[1].toObject()["path"].toString(), QString("mods/hash.jar"));
        QVERIFY(SharePack::requiresManualDownload(out[1].toObject()));
        QVERIFY(!out[1].toObject().contains("hashes"));
        QCOMPARE(out[2].toObject()["name"].toString(), QString("Ok"));
    }
    void exportFixesHeaderName()
    {
        auto m = manifest();
        m["name"] = "\n\t";
        QVERIFY(SharePack::validate(SharePack::sanitizeForExport(m)));
    }
    void decodeStripsAllWhitespace()
    {
        const auto original = manifest();
        const auto code = SharePack::encode(original);
        QVERIFY(!code.isEmpty());
        QString wrapped;
        for (int i = 0; i < code.size(); i += 20)
            wrapped += code.mid(i, 20) + (i % 40 ? "\r\n" : " \t");
        wrapped = "  \n" + wrapped + QString(QChar(0x00A0)) + QString(QChar(0x200B)) + QString(QChar(0xFEFF)) + "\n";
        QJsonObject decoded;
        QString error;
        QVERIFY2(SharePack::decode(wrapped, &decoded, &error), qPrintable(error));
        QCOMPARE(decoded, original);
        // Whitespace inside the prefix too.
        QVERIFY(SharePack::decode(code.left(3) + "\n" + code.mid(3), &decoded));
        // Stripping must not let other characters through.
        QVERIFY(!SharePack::decode(code + " !", &decoded));
        QVERIFY(!SharePack::decode(code.left(20) + "+/" + code.mid(20), &decoded));
    }
    void decodeRejectsLyingHeaders()
    {
        const QByteArray json = QJsonDocument(manifest()).toJson(QJsonDocument::Compact);
        QJsonObject decoded;
        QVERIFY(SharePack::decode(encodeRaw(json, json.size()), &decoded));
        QVERIFY(!SharePack::decode(encodeRaw(json, json.size() + 10), &decoded));
        QVERIFY(!SharePack::decode(encodeRaw(json, json.size() - 10), &decoded));
        QVERIFY(!SharePack::decode(encodeRaw(json, 0), &decoded));
        QVERIFY(!SharePack::decode(encodeRaw(json, 4 * 1024 * 1024 + 1), &decoded));
    }
    void decodeRejectsEmptyAndOversized()
    {
        QJsonObject decoded;
        QVERIFY(!SharePack::decode("POHON1:", &decoded));
        QVERIFY(!SharePack::decode("  POHON1: \n ", &decoded));
        QVERIFY(!SharePack::decode("", &decoded));
        QVERIFY(!SharePack::decode("POHON1:" + QString(6 * 1024 * 1024 + 1, 'A'), &decoded));
        // Whitespace padding must not defeat the cap, nor be counted towards it.
        QVERIFY(!SharePack::decode("POHON1:" + QString(6 * 1024 * 1024, 'A') + QString(10, ' '), &decoded));
        QVERIFY(!SharePack::decode("POHON1:" + QString(20 * 1024 * 1024, ' ') + "AAAA", &decoded));
        // Valid empty-object payload decodes to a manifest that validate rejects.
        QVERIFY(!SharePack::decode(encodeRaw("{}", 2), &decoded));
        QVERIFY(!SharePack::decode(encodeRaw("", 0), &decoded));
    }
    void validateLimits()
    {
        QJsonArray many;
        for (int i = 0; i < 5001; ++i)
            many.append(downloadable(QString("mods/m%1.jar").arg(i)));
        QVERIFY(!SharePack::validate(withFiles(many)));
        many.removeLast();
        QString error;
        QVERIFY2(SharePack::validate(withFiles(many), &error), qPrintable(error));

        QVERIFY(!SharePack::validate(withFiles({ downloadable("mods/a.jar"), downloadable("mods/A.JAR") })));
        QVERIFY(!SharePack::validate(withFiles({ downloadable("mods/a.jar"), downloadable("mods/a.jar") })));

        for (const QString& url : { "http://cdn.modrinth.com/a.jar", "https://example.com/a.jar", "https://cdn.modrinth.com:8443/a.jar",
                                    "https://cdn.modrinth.com/a.jar#x" }) {
            auto f = downloadable("mods/a.jar");
            f["downloads"] = QJsonArray{ url };
            QVERIFY2(!SharePack::validate(withFiles({ f })), qPrintable(url));
        }
        auto two = downloadable("mods/a.jar");
        two["downloads"] = QJsonArray{ "https://cdn.modrinth.com/a.jar", "https://cdn.modrinth.com/b.jar" };
        QVERIFY(!SharePack::validate(withFiles({ two })));
    }
    void requiredOnlyDeduplicates()
    {
        auto optional = downloadable("mods/o.jar");
        optional["pohonServerRequirement"] = "optional";
        auto disabled = downloadable("mods/a.jar.disabled");
        disabled["name"] = "disabled-one";
        auto enabled = downloadable("mods/a.jar");
        enabled["name"] = "enabled-one";

        for (const auto& order : { QJsonArray{ disabled, enabled, optional }, QJsonArray{ enabled, disabled, optional } }) {
            const auto src = withFiles(order);
            const auto out = SharePack::requiredOnly(src);
            QString error;
            QVERIFY2(SharePack::validate(out, &error), qPrintable(error));
            const auto files = out["files"].toArray();
            QCOMPARE(files.size(), 1);
            QCOMPARE(files[0].toObject()["path"].toString(), QString("mods/a.jar"));
            QCOMPARE(files[0].toObject()["name"].toString(), QString("enabled-one"));
        }
        // A lone disabled mod is still renamed.
        const auto lone = SharePack::requiredOnly(withFiles({ disabled }));
        QCOMPARE(lone["files"].toArray()[0].toObject()["path"].toString(), QString("mods/a.jar"));
        QVERIFY(SharePack::validate(lone));
    }
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
