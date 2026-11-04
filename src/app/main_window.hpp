#pragma once

#include "app/app_model.hpp"
#include "app/article_view.hpp"
#include "app/search_history.hpp"
#include "app/word_list.hpp"

#include <gtkmm.h>

#include <memory>
#include <optional>
#include <string>

/// The GoldenDict layout: search and matches on the left, articles in the middle, history on the right.
class MainWindow : public Gtk::ApplicationWindow {
public:
    explicit MainWindow(AppModel& model);

    void focusSearch();
    /// Remembers the window's size and panes, which GTK leaves to the app.
    void saveGeometry();

protected:
    bool on_close_request() override;

private:
    bool searchHasFocus();
    void followSystemAppearance();
    void buildContent();
    void installKeys();
    void installSearchFocus();
    void showHistoryMenu(std::optional<std::string> word, double x, double y);

    void runSearch(const std::optional<std::string>& target = std::nullopt);
    void lookUp(const std::string& word);
    void handleEscape();

    void scheduleDisplay(std::optional<std::string> word);
    void display(const std::string& word);
    void showPlaceholder(const std::string& query);

    void commit(const std::string& word);
    void addToHistory(const std::string& word);
    void refreshHistory();

    AppModel& model_;
    SearchHistory history_;

    Gtk::SearchEntry searchEntry_;
    WordList matchList_;
    WordList historyList_;
    Gtk::Label historyCount_;
    ArticleView articleView_;
    Gtk::Paned outerPaned_{Gtk::Orientation::HORIZONTAL};
    Gtk::Paned innerPaned_{Gtk::Orientation::HORIZONTAL};
    Gtk::PopoverMenu historyMenu_;
    std::string historyMenuWord_;

    sigc::connection pendingDisplay_;
    sigc::connection pendingHistory_;
    std::optional<std::string> displayedWord_;
    bool hadFocusAtPress_ = true;
};
