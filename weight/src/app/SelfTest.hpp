#pragma once

#include "settings/Settings.hpp"

namespace weight::app {

/// Exercises the program on the machine it was built for, using only the
/// compiled binary.
///
/// This complements the unit suite rather than replacing it. The unit tests
/// prove the algorithms; this proves the *deployed artefact*: that the font is
/// really embedded, that the icon really made it into the binary, that the
/// numerics give the right answer on this architecture, that a chart can be
/// rendered and written, and that the platform integration works on this
/// kernel. A static binary carried to another machine can be checked with a
/// single command.
///
/// Everything runs in a temporary directory that is removed on exit; nothing
/// touches the real Documents folder.
///
/// Returns 0 when every check passed.
[[nodiscard]] int runSelfTest(const settings::Settings& baseSettings);

}  // namespace weight::app
