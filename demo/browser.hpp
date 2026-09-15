#pragma once

#include <filesystem>
#include "xui/theme.hpp"

namespace xui {

struct BrowserOptions {
    std::filesystem::path folder;
    ThemeMode theme = ThemeMode::dark;
    VisualStyle visual_style = VisualStyle::classic;
};

// Sample composition. Runs on the calling thread.
int run_file_browser(const BrowserOptions& options);

}
