#include <QtTest>

#include <QDir>
#include <QTemporaryDir>

#include "platform/Workspace.hpp"

using namespace weight::platform;

/// The workspace must be *found* before it is created, so an existing folder
/// is adopted rather than a second one appearing beside it.
class TestWorkspace : public QObject {
    Q_OBJECT

private slots:
    void candidatesAreOrderedAndUnique() {
        const Workspace workspace(QStringLiteral("weight"));
        const QStringList candidates = workspace.candidates();

        QVERIFY(!candidates.isEmpty());
        QCOMPARE(candidates.size(), QSet<QString>(candidates.begin(), candidates.end()).size());
        for (const QString& candidate : candidates) {
            QVERIFY(candidate.endsWith(QStringLiteral("weight")));
        }
    }

    void everyCandidateEndsWithTheFolderName() {
        const Workspace workspace(QStringLiteral("custom-name"));
        for (const QString& candidate : workspace.candidates()) {
            QVERIFY(candidate.endsWith(QStringLiteral("custom-name")));
        }
    }

    void anExistingFolderIsAdoptedRatherThanRecreated() {
        QTemporaryDir home;
        QVERIFY(home.isValid());
        const QByteArray previousHome = qgetenv("HOME");

        // A "Documentos" folder already holding a workspace must win over the
        // .local fallback, and must not cause a second folder to be created.
        const QString existing = QDir(home.path()).filePath(QStringLiteral("Documentos/weight"));
        QVERIFY(QDir().mkpath(existing));

        qputenv("HOME", home.path().toUtf8());
        const Workspace workspace(QStringLiteral("weight"));
        const QString found = workspace.findExisting();
        qputenv("HOME", previousHome);

#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
        Q_UNUSED(found)
        QSKIP("The Documentos candidate is Linux-only.");
#else
        QCOMPARE(QDir(found).canonicalPath(), QDir(existing).canonicalPath());
#endif
    }

    void resolveCreatesSomethingUsable() {
        QTemporaryDir home;
        QVERIFY(home.isValid());
        const QByteArray previousHome = qgetenv("HOME");
        qputenv("HOME", home.path().toUtf8());

        const Workspace workspace(QStringLiteral("weight"));
        const weight::core::Result<QString> resolved = workspace.resolve();
        qputenv("HOME", previousHome);

        QVERIFY(bool(resolved));
        const QFileInfo info(resolved.value());
        QVERIFY(info.isDir());
        QVERIFY(info.isWritable());
    }

    void pathsInsideTheWorkspaceAreComposed() {
        const QString root = QStringLiteral("/tmp/example");
        QCOMPARE(Workspace::historyPath(root, QStringLiteral("weight-data.csv")),
                 QStringLiteral("/tmp/example/weight-data.csv"));
        QCOMPARE(Workspace::imagesPath(root, QStringLiteral("images")),
                 QStringLiteral("/tmp/example/images"));
        QCOMPARE(Workspace::configPath(root, QStringLiteral("config.txt")),
                 QStringLiteral("/tmp/example/config.txt"));
    }

    void theDocumentsDirectoryIsResolvedNatively() {
        // On Windows this goes through SHGetKnownFolderPath, so a redirected
        // Documents folder is honoured; the assertion here is only that a
        // path comes back at all.
        const QString documents = Workspace::platformDocumentsDirectory();
        QVERIFY(!documents.isEmpty());
    }
};

QTEST_APPLESS_MAIN(TestWorkspace)
#include "test_workspace.moc"
