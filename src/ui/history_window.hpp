#pragma once

// The History browser window (spec §576-§590): a search box over a list of
// formatted history rows. Live full-text search as the user types. The rows are
// supplied by a data-source callback the app wires to HistoryService +
// history_formatting, so this file stays a thin Win32 shell (no <Windows.h> in
// the header) and the query/formatting logic is unit-tested elsewhere.

#include <functional>
#include <string>
#include <vector>

namespace ir {

class HistoryWindow {
public:
    // Given the current search text (empty = most-recent), returns the display
    // rows to show. Called on the UI thread.
    using DataSource =
        std::function<std::vector<std::string>(const std::string& query)>;

    HistoryWindow() = default;

    HistoryWindow(const HistoryWindow&) = delete;
    HistoryWindow& operator=(const HistoryWindow&) = delete;

    bool create(void* hinstance, void* owner_hwnd, DataSource source);
    void show(int show_command);
    void* handle() const { return hwnd_; }

    long long handle_message(void* hwnd, unsigned msg, unsigned long long wparam,
                             long long lparam);

private:
    void on_create();
    void on_size(int width, int height);
    void refresh();  // re-query the data source and repopulate the list

    void* hwnd_ = nullptr;
    void* search_edit_ = nullptr;
    void* list_box_ = nullptr;
    DataSource source_;
};

}  // namespace ir
