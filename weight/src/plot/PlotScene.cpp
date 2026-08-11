#include "plot/PlotScene.hpp"

// PlotScene is a plain aggregate; this translation unit exists so the header
// is compiled on its own and stays self-contained.
namespace weight::plot {
namespace {
[[maybe_unused]] void staticInterfaceCheck() {
    static_assert(sizeof(PlotScene) > 0, "PlotScene must be a complete type");
}
}  // namespace
}  // namespace weight::plot
