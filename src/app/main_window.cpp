#include "app/main_window.hpp"

#include "core/text_folding.hpp"
#include "platform/platform.hpp"

using namespace ljdict;

namespace {

constexpr int panePadding = 4;
/// Shared by the side panes so that both lists start at the same height.
constexpr int headerHeight = 34;
constexpr unsigned articleDelayMs = 50;
/// How long an article has to stay on screen to count as looked up, so typing doesn't fill the history.
constexpr unsigned historyDelayMs = 2000;
#ifdef __APPLE__
constexpr const char* settingsKey = "⌘,";
#else
constexpr const char* settingsKey = "Ctrl+,";
#endif

}

MainWindow::MainWindow(AppModel& model) : model_(model), history_(model.settings()) {
    set_title("LjDict");
    const Settings& settings = model_.settings();
    set_default_size(settings.windowWidth, settings.windowHeight);
    set_size_request(560, 320);
    buildContent();

    model_.libraryChanged.connect([this] { runSearch(displayedWord_); });
    articleView_.onLookup = [this](const std::string& word) { lookUp(word); };
    matchList_.onSelectionChange = [this](std::optional<std::string> word) { scheduleDisplay(std::move(word)); };
    matchList_.onClick = [this](const std::string& word) { commit(word); };
    historyList_.onClick = [this](const std::string& word) { lookUp(word); };
    historyList_.onContextClick = [this](std::optional<std::string> word, double x, double y) { showHistoryMenu(std::move(word), x, y); };
    refreshHistory();
    installKeys();
    installSearchFocus();
    followSystemAppearance();
}

void MainWindow::focusSearch() {
    searchEntry_.grab_focus();
    searchEntry_.select_region(0, -1);
}

/// GTK does not follow the macOS appearance by itself. Checked on activation, which is when the
/// user has just been to System Settings.
void MainWindow::followSystemAppearance() {
    bool dark = platform::systemIsDark();
    articleView_.setDark(dark);
    Gtk::Settings::get_default()->property_gtk_interface_color_scheme() =
        dark ? Gtk::InterfaceColorScheme::DARK : Gtk::InterfaceColorScheme::LIGHT;
    if (dark) add_css_class("dark"); else remove_css_class("dark");
}

bool MainWindow::searchHasFocus() {
    Gtk::Widget* focus = get_focus();
    return focus && focus->is_ancestor(searchEntry_);
}

void MainWindow::saveGeometry() {
    Settings& settings = model_.settings();
    if (get_width() > 0) {
        settings.windowWidth = get_width();
        settings.windowHeight = get_height();
        settings.leftPane = outerPaned_.get_position();
        settings.articlePane = innerPaned_.get_position();
    }
    settings.save();
}

/// Closing only hides the window and the app keeps running, so the global shortcut stays alive.
bool MainWindow::on_close_request() {
    saveGeometry();
    set_visible(false);
    return true;
}

void MainWindow::buildContent() {
    auto left = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL);
    searchEntry_.set_placeholder_text("Search");
    searchEntry_.set_search_delay(0);
    searchEntry_.set_valign(Gtk::Align::CENTER);
    auto searchHeader = Gtk::make_managed<Gtk::Box>();
    searchHeader->set_size_request(-1, headerHeight);
    searchHeader->append(searchEntry_);
    searchEntry_.set_hexpand(true);
    left->append(*searchHeader);
    left->append(matchList_);
    left->set_margin(panePadding);
    left->set_margin_top(0);
    left->set_size_request(130, -1);

    auto right = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL);
    auto historyTitle = Gtk::make_managed<Gtk::Label>("History:");
    historyTitle->set_hexpand(true);
    historyTitle->set_xalign(0);
    auto historyHeader = Gtk::make_managed<Gtk::Box>();
    historyHeader->set_size_request(-1, headerHeight);
    historyHeader->set_margin_start(2);
    historyHeader->set_margin_end(2);
    historyHeader->append(*historyTitle);
    historyHeader->append(historyCount_);
    right->append(*historyHeader);
    right->append(historyList_);
    right->set_margin(panePadding);
    right->set_margin_top(0);
    right->set_size_request(110, -1);

    articleView_.set_size_request(200, -1);

    innerPaned_.set_start_child(articleView_);
    innerPaned_.set_end_child(*right);
    innerPaned_.set_resize_end_child(false);
    innerPaned_.set_shrink_start_child(false);
    innerPaned_.set_shrink_end_child(false);
    innerPaned_.set_position(model_.settings().articlePane);

    outerPaned_.set_start_child(*left);
    outerPaned_.set_end_child(innerPaned_);
    outerPaned_.set_resize_start_child(false);
    outerPaned_.set_shrink_start_child(false);
    outerPaned_.set_shrink_end_child(false);
    outerPaned_.set_position(model_.settings().leftPane);
    set_child(outerPaned_);

    add_action("remove-history-word", [this] {
        history_.remove(historyMenuWord_);
        refreshHistory();
    });
    add_action("clear-history", [this] {
        history_.clear();
        refreshHistory();
    });
    historyMenu_.set_parent(historyList_);
    historyMenu_.set_has_arrow(false);
}

void MainWindow::showHistoryMenu(std::optional<std::string> word, double x, double y) {
    auto menu = Gio::Menu::create();
    if (word) {
        historyMenuWord_ = *word;
        menu->append("Remove “" + *word + "”", "win.remove-history-word");
    }
    menu->append("Clear History", "win.clear-history");
    historyMenu_.set_menu_model(menu);
    historyMenu_.set_pointing_to(Gdk::Rectangle(static_cast<int>(x), static_cast<int>(y), 1, 1));
    historyMenu_.popup();
}

void MainWindow::installKeys() {
    searchEntry_.signal_search_changed().connect([this] { runSearch(); });
    searchEntry_.signal_activate().connect([this] {
        if (auto word = matchList_.selectedWord()) commit(*word);
    });
    // The field gets Esc the normal way, so that it can still cancel input method composition.
    searchEntry_.signal_stop_search().connect([this] { handleEscape(); });

    auto arrows = Gtk::EventControllerKey::create();
    arrows->set_propagation_phase(Gtk::PropagationPhase::CAPTURE);
    arrows->signal_key_pressed().connect([this](guint key, guint, Gdk::ModifierType) {
        if (key == GDK_KEY_Down) matchList_.moveSelection(1);
        else if (key == GDK_KEY_Up) matchList_.moveSelection(-1);
        else return false;
        return true;
    }, false);
    searchEntry_.add_controller(arrows);

    // Everywhere else, including the article view.
    auto escape = Gtk::EventControllerKey::create();
    escape->set_propagation_phase(Gtk::PropagationPhase::CAPTURE);
    escape->signal_key_pressed().connect([this](guint key, guint, Gdk::ModifierType state) {
        constexpr auto modifiers = Gdk::ModifierType::SHIFT_MASK | Gdk::ModifierType::CONTROL_MASK
            | Gdk::ModifierType::ALT_MASK | Gdk::ModifierType::META_MASK;
        if (key != GDK_KEY_Escape || (state & modifiers) != Gdk::ModifierType{}) return false;
        if (searchHasFocus()) return false;
        handleEscape();
        return true;
    }, false);
    add_controller(escape);
}

/// The query is selected whenever the field gains focus, so typing after the window reappears
/// replaces the old query.
void MainWindow::installSearchFocus() {
    property_is_active().signal_changed().connect([this] {
        if (is_active() && searchHasFocus()) searchEntry_.select_region(0, -1);
        followSystemAppearance();
    });

    // A click that brings focus to the field selects the query; further clicks place the caret as
    // usual. A legacy controller, because the text widget claims the gesture.
    auto clicks = Gtk::EventControllerLegacy::create();
    clicks->set_propagation_phase(Gtk::PropagationPhase::CAPTURE);
    clicks->signal_event().connect([this](const Glib::RefPtr<const Gdk::Event>& event) {
        if (event->get_event_type() == Gdk::Event::Type::BUTTON_PRESS) {
            hadFocusAtPress_ = searchHasFocus();
        } else if (event->get_event_type() == Gdk::Event::Type::BUTTON_RELEASE && !hadFocusAtPress_) {
            hadFocusAtPress_ = true;
            int start, end;
            // A drag has already selected what it wanted.
            if (!searchEntry_.get_selection_bounds(start, end)) searchEntry_.select_region(0, -1);
        }
        return false;
    }, false);
    searchEntry_.add_controller(clicks);
}

void MainWindow::handleEscape() {
    // An article dismissed right away was not read, so it stays out of the history.
    pendingHistory_.disconnect();
    saveGeometry();
    platform::hideApp();
}

void MainWindow::runSearch(const std::optional<std::string>& target) {
    std::string query = searchEntry_.get_text();
    auto words = model_.library().search(query, model_.enabledBooks());
    matchList_.setWords(words);
    if (words.empty()) {
        pendingDisplay_.disconnect();
        pendingHistory_.disconnect();
        showPlaceholder(query);
        return;
    }
    guint row = 0;
    if (target) {
        std::string rowKey = folding::rowKey(*target);
        for (guint i = 0; i < words.size(); ++i) {
            if (folding::rowKey(words[i]) == rowKey) {
                row = i;
                break;
            }
        }
    }
    // Replacing the words drops the selection without notifying, so an unchanged row must not be skipped.
    displayedWord_.reset();
    matchList_.select(row);
}

/// Shows a word from outside the search field: a link in an article or a history row.
void MainWindow::lookUp(const std::string& word) {
    searchEntry_.set_text(word);
    runSearch(word);
    if (auto selected = matchList_.selectedWord()) commit(*selected);
}

void MainWindow::scheduleDisplay(std::optional<std::string> word) {
    if (!word || word == displayedWord_) return;
    pendingDisplay_.disconnect();
    pendingDisplay_ = Glib::signal_timeout().connect([this, word = *word] {
        display(word);
        return false;
    }, articleDelayMs);
}

void MainWindow::display(const std::string& word) {
    displayedWord_ = word;
    set_title(word + " - LjDict");
    articleView_.show(model_.library().articles(word, model_.enabledBooks()));

    pendingHistory_.disconnect();
    pendingHistory_ = Glib::signal_timeout().connect([this, word] {
        addToHistory(word);
        return false;
    }, historyDelayMs);
}

void MainWindow::showPlaceholder(const std::string& query) {
    displayedWord_.reset();
    set_title("LjDict");
    std::optional<std::string> message;
    if (model_.isLoading()) {
        message = "Loading dictionaries…";
    } else if (model_.library().books().empty()) {
        message = "No dictionaries found in " + model_.dictionaryPath() + ". Choose a folder in Settings (" + settingsKey + ").";
    } else if (model_.enabledBooks().empty()) {
        message = std::string("All dictionaries are turned off. Turn some on in Settings (") + settingsKey + ").";
    } else if (!folding::trim(query).empty()) {
        message = "“" + query + "” not found.";
    }
    articleView_.show({}, message);
}

/// An explicit choice counts at once, without waiting for `historyDelayMs`.
void MainWindow::commit(const std::string& word) {
    pendingDisplay_.disconnect();
    if (word != displayedWord_) display(word);
    pendingHistory_.disconnect();
    addToHistory(word);
}

void MainWindow::addToHistory(const std::string& word) {
    history_.add(word);
    refreshHistory();
}

void MainWindow::refreshHistory() {
    historyList_.setWords(history_.words());
    historyCount_.set_text(std::to_string(history_.words().size()) + "/" + std::to_string(SearchHistory::limit));
}
