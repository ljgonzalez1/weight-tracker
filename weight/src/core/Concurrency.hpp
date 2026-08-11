#pragma once

#include <QString>

namespace weight::core {

/// Process-wide thread policy.
///
/// The brief is explicit that concurrency must not be capped by the number of
/// physical cores: a machine with two hardware threads should still be able to
/// have every curve in flight at once, with the operating system deciding when
/// each one runs. That is the right call here because the work is short,
/// independent and compute-bound in bursts — oversubscribing costs a few
/// context switches and buys the ability to add curves without rewriting the
/// scheduling.
///
/// So the pool is sized from *policy*, not from `hardware_concurrency()`:
///
///     max(minimumThreads, curveCount + auxiliaryThreads)
///
/// where the auxiliary threads cover rendering and saving. `QThreadPool`
/// creates real OS threads, so the kernel scheduler multiplexes them onto
/// whatever hardware exists.
class Concurrency {
public:
    /// Configures the global pool. Call once, early, from the main thread.
    static void configure(int curveCount, int auxiliaryThreads, int minimumThreads);

    /// Threads the pool is allowed to run.
    [[nodiscard]] static int maxThreadCount();

    /// Hardware threads reported by the system, for the log only. Nothing
    /// scales off this number.
    [[nodiscard]] static int hardwareThreads();

    /// One-line description for the log.
    [[nodiscard]] static QString describe();
};

}  // namespace weight::core
