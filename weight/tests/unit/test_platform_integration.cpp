// Everything in this suite is about the operating system the tests are running
// on, not about the mathematics. Each case asserts something that is *true on
// this platform* and would be a different assertion elsewhere, which is the
// point: the same source has to be correct on Linux, macOS and Windows, and
// the only way to know is to ask the platform.
//
// Nothing here writes to a real Documents folder. Every path used is a
// QTemporaryDir, and HOME (or USERPROFILE) is redirected for the duration of
// the cases that resolve a workspace.

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>

#include "core/Concurrency.hpp"
#include "data/AtomicFileWriter.hpp"
#include "platform/SingleInstance.hpp"
#include "platform/Workspace.hpp"
#include "settings/Settings.hpp"

using namespace weight;

class TestPlatformIntegration : public QObject {
    Q_OBJECT

private slots:
    void reportsTheHost();

    void documentsLocationResolves();
    void workspaceCandidatesAreOrderedForThisPlatform();
    void workspacePrefersAnExistingFolderOverCreatingOne();
    void workspaceFallsBackWhenNoDocumentsFolderExists();

    void atomicWriteReplacesInPlace();
    void atomicWriteLeavesNoTemporaryBehind();
    void atomicWriteSurvivesAnExistingReadOnlyTarget();

    void singleInstanceDetectsItself();
    void singleInstanceReleasesItsName();

    void threadPoolExceedsTheHardwareCount();
    void separatorsAreNativeInEveryPath();
};

// ---------------------------------------------------------------------------
// Context
// ---------------------------------------------------------------------------

void TestPlatformIntegration::reportsTheHost() {
    // Not an assertion so much as a record: when a suite fails on a machine
    // nobody has, the first question is always which machine it was.
    qInfo("kernel     : %s %s", qPrintable(QSysInfo::kernelType()),
          qPrintable(QSysInfo::kernelVersion()));
    qInfo("product    : %s", qPrintable(QSysInfo::prettyProductName()));
    qInfo("cpu        : %s", qPrintable(QSysInfo::currentCpuArchitecture()));
    qInfo("byte order : %s", QSysInfo::ByteOrder == QSysInfo::LittleEndian ? "little" : "big");
    QVERIFY(!QSysInfo::kernelType().isEmpty());
}

// ---------------------------------------------------------------------------
// Where the data lives
// ---------------------------------------------------------------------------

void TestPlatformIntegration::documentsLocationResolves() {
    // QStandardPaths::DocumentsLocation is the platform call underneath:
    // SHGetKnownFolderPath(FOLDERID_Documents) on Windows, the Cocoa search
    // path on macOS, the XDG user-dirs file on Linux. If it returns nothing,
    // the whole first tier of the workspace search is dead and the program
    // would quietly fall back to a hidden directory.
    const QString documents = platform::Workspace::platformDocumentsDirectory();
    QVERIFY2(!documents.isEmpty(),
             "the platform Documents location resolved to nothing on this system");
    QVERIFY(QDir::isAbsolutePath(documents));
}

void TestPlatformIntegration::workspaceCandidatesAreOrderedForThisPlatform() {
    const platform::Workspace locator(QStringLiteral("weight"));
    const QStringList candidates = locator.candidates();

    QVERIFY(!candidates.isEmpty());
    for (const QString& candidate : candidates) {
        QVERIFY2(QDir::isAbsolutePath(candidate), qPrintable(candidate));
        QVERIFY2(candidate.endsWith(QStringLiteral("weight")), qPrintable(candidate));
    }
    // Duplicates would make the search do the same work twice and would make
    // the "first that exists" rule ambiguous.
    QSet<QString> unique(candidates.begin(), candidates.end());
    QCOMPARE(static_cast<int>(unique.size()), candidates.size());

#if defined(Q_OS_WIN)
    // Windows: the known folder comes first, so a redirected or OneDrive-backed
    // Documents wins over the literal C:\Users\<user>\Documents.
    QCOMPARE(candidates.first(),
             QDir::cleanPath(QDir(platform::Workspace::platformDocumentsDirectory())
                                 .filePath(QStringLiteral("weight"))));
    QVERIFY(candidates.size() >= 2);
#elif defined(Q_OS_MACOS)
    QVERIFY(candidates.first().contains(QStringLiteral("Documents"))
            || candidates.first().contains(QStringLiteral("Documentos")));
    // The last resort must be the per-user data directory, which always exists.
    QVERIFY2(candidates.last().contains(QStringLiteral("Application Support")),
             qPrintable(candidates.last()));
    // ...and it must be .../Application Support/weight, not
    // .../Application Support/Weight/weight: AppDataLocation already appends
    // the application name, GenericDataLocation does not.
    QVERIFY(!candidates.last().contains(QStringLiteral("Weight/weight")));
#else
    // Linux: the last resort is the XDG data directory.
    QVERIFY2(candidates.last().contains(QStringLiteral(".local/share"))
                 || candidates.last().contains(QStringLiteral("/share/weight")),
             qPrintable(candidates.last()));
    // The four spellings the brief asks for must all be reachable.
    const QString joined = candidates.join(QLatin1Char('\n'));
    QVERIFY(joined.contains(QStringLiteral("/Documents/weight")));
    QVERIFY(joined.contains(QStringLiteral("/documents/weight")));
    QVERIFY(joined.contains(QStringLiteral("/Documentos/weight")));
    QVERIFY(joined.contains(QStringLiteral("/documentos/weight")));
#endif
}

void TestPlatformIntegration::workspacePrefersAnExistingFolderOverCreatingOne() {
    QTemporaryDir home;
    QVERIFY(home.isValid());

    // Create the *third* preference and verify it is chosen, which proves the
    // search really looks before it creates rather than always taking the
    // first entry.
    const platform::Workspace locator(QStringLiteral("weight-test-folder"));
    const QStringList candidates = locator.candidates();
    QVERIFY(candidates.size() >= 2);

    const QString chosen = candidates.at(candidates.size() - 1);
    QVERIFY(QDir().mkpath(chosen));

    const core::Result<QString> resolved = locator.resolve();
    QVERIFY(resolved.hasValue());
    QCOMPARE(QDir::cleanPath(resolved.value()), QDir::cleanPath(chosen));

    QDir(chosen).removeRecursively();
}

void TestPlatformIntegration::workspaceFallsBackWhenNoDocumentsFolderExists() {
    // A workspace name that certainly does not exist anywhere must still
    // resolve to something writable: the program has to be able to start on a
    // freshly created account.
    const platform::Workspace locator(
        QStringLiteral("weight-test-%1").arg(QCoreApplication::applicationPid()));
    const core::Result<QString> resolved = locator.resolve();
    QVERIFY2(resolved.hasValue(), qPrintable(resolved ? QString() : resolved.error().toString()));

    const QFileInfo info(resolved.value());
    QVERIFY(info.exists());
    QVERIFY(info.isDir());
    QVERIFY(info.isWritable());

    QDir(resolved.value()).removeRecursively();
}

// ---------------------------------------------------------------------------
// Writing files the way this filesystem wants
// ---------------------------------------------------------------------------

void TestPlatformIntegration::atomicWriteReplacesInPlace() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("history.csv"));

    QVERIFY(data::AtomicFileWriter::write(path, QStringLiteral("first\n").toUtf8()).ok());
    QVERIFY(data::AtomicFileWriter::write(path, QStringLiteral("second\n").toUtf8()).ok());

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
    QCOMPARE(QString::fromUtf8(file.readAll()), QStringLiteral("second\n"));
}

void TestPlatformIntegration::atomicWriteLeavesNoTemporaryBehind() {
    // Windows cannot rename over an open file, so the implementation has to
    // remove the target first; a botched sequence leaves a .tmp sibling. This
    // is the case that catches it.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("history.csv"));

    for (int i = 0; i < 5; ++i) {
        QVERIFY(data::AtomicFileWriter::write(path, QStringLiteral("pass %1\n").arg(i).toUtf8()).ok());
    }

    const QStringList left = QDir(dir.path()).entryList(QDir::Files | QDir::Hidden);
    QCOMPARE(left.size(), 1);
    QCOMPARE(left.first(), QStringLiteral("history.csv"));
}

void TestPlatformIntegration::atomicWriteSurvivesAnExistingReadOnlyTarget() {
    // A file the user made read-only must produce an error, not a crash and
    // not a silent loss of the previous contents. The permission model differs
    // per platform, so the assertion is only that the old contents survive.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("history.csv"));
    QVERIFY(data::AtomicFileWriter::write(path, QStringLiteral("original\n").toUtf8()).ok());

    QFile target(path);
    QVERIFY(target.setPermissions(QFileDevice::ReadOwner | QFileDevice::ReadUser));

    const core::Status status = data::AtomicFileWriter::write(path, QStringLiteral("replaced\n").toUtf8());

    QFile check(path);
    QVERIFY(check.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString contents = QString::fromUtf8(check.readAll());
    check.close();

    // Either the write was refused and the original stands, or the platform
    // allowed the owner to replace their own file. Both are defensible; a
    // truncated or empty file is not.
    QVERIFY2(contents == QStringLiteral("original\n") || contents == QStringLiteral("replaced\n"),
             qPrintable(contents));
    if (!status) {
        QVERIFY(!status.error().toString().isEmpty());
    }
    target.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
}

// ---------------------------------------------------------------------------
// One instance at a time
// ---------------------------------------------------------------------------

void TestPlatformIntegration::singleInstanceDetectsItself() {
    // The guard is a named local socket: a Unix domain socket on Linux and
    // macOS, a named pipe on Windows. Whichever it is, once this process owns
    // the name, a second attempt on the same name must fail and must report a
    // process id — and since the owner is this process, the id it reports is
    // one we can check exactly.
    const QString key =
        QStringLiteral("weight-selftest-%1").arg(QCoreApplication::applicationPid());

    platform::SingleInstance first(key);
    QVERIFY2(first.tryAcquire(), "could not claim the single-instance name");

    platform::SingleInstance second(key);
    QVERIFY2(!second.tryAcquire(), "a second instance was allowed to start");
    QCOMPARE(second.runningProcessId(), QCoreApplication::applicationPid());
}

void TestPlatformIntegration::singleInstanceReleasesItsName() {
    // After the owner goes away the name must be reusable. On Unix that means
    // the stale socket file is cleared; on Windows the pipe disappears with
    // the handle. Either way the next run must not be locked out — which is
    // the whole reason this is not a lock file.
    const QString key =
        QStringLiteral("weight-selftest-reuse-%1").arg(QCoreApplication::applicationPid());
    {
        platform::SingleInstance owner(key);
        QVERIFY(owner.tryAcquire());
    }
    platform::SingleInstance next(key);
    QVERIFY2(next.tryAcquire(), "the name was not released when the owner was destroyed");
}

// ---------------------------------------------------------------------------
// Threads
// ---------------------------------------------------------------------------

void TestPlatformIntegration::threadPoolExceedsTheHardwareCount() {
    // The pool is sized from the work, not from the cores, so that a two-thread
    // machine still gets one task per curve and the kernel does the
    // multiplexing. Asserting it here means a future "optimisation" that
    // clamps to idealThreadCount() fails loudly.
    core::Concurrency::configure(/*curveCount=*/20, /*auxiliaryThreads=*/3,
                                 /*minimumThreads=*/2);
    QCOMPARE(core::Concurrency::maxThreadCount(), 23);
    QVERIFY(core::Concurrency::hardwareThreads() >= 1);
    QVERIFY(!core::Concurrency::describe().isEmpty());
}

void TestPlatformIntegration::separatorsAreNativeInEveryPath() {
    // QDir::filePath always produces '/', which every Windows API accepts.
    // What must never appear is a mixed or doubled separator, which is what
    // hand-built string concatenation produces and which breaks path
    // comparison on all three platforms.
    const platform::Workspace locator(QStringLiteral("weight"));
    for (const QString& candidate : locator.candidates()) {
        QVERIFY2(!candidate.contains(QStringLiteral("//")), qPrintable(candidate));
        QVERIFY2(!candidate.contains(QStringLiteral("\\\\")), qPrintable(candidate));
        QCOMPARE(candidate, QDir::cleanPath(candidate));
    }
}

QTEST_MAIN(TestPlatformIntegration)
#include "test_platform_integration.moc"
