#include "app/settings_window.hpp"

#include "platform/platform.hpp"

using namespace ljdict;

namespace {

Gtk::Label* makeCaption(const Glib::ustring& text) {
    auto label = Gtk::make_managed<Gtk::Label>(text);
    label->set_xalign(0);
    label->set_wrap(true);
    label->add_css_class("caption");
    label->add_css_class("dim-label");
    return label;
}

Gtk::Frame* makeGroup(const Glib::ustring& title, Gtk::Widget& content) {
    auto frame = Gtk::make_managed<Gtk::Frame>(title);
    content.set_margin(8);
    frame->set_child(content);
    return frame;
}

}

SettingsWindow::SettingsWindow(AppModel& model, std::function<void()> hotKeyChanged, std::function<void()> hideToMenuBarChanged)
    : model_(model), hotKeyChanged_(std::move(hotKeyChanged)), hideToMenuBarChanged_(std::move(hideToMenuBarChanged)) {
    set_title("LjDict Settings");
    set_default_size(520, 740);
    set_size_request(460, 560);
    set_hide_on_close(true);

    auto column = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 16);
    column->set_margin(20);
    column->append(*buildDictionaries());
    column->append(*buildShortcut());
    column->append(*buildStartup());
    set_child(*column);

    model_.libraryChanged.connect([this] { refreshBooks(); });
    refresh();
}

void SettingsWindow::refresh() {
    pathEntry_.set_text(model_.dictionaryPath());
    refreshBooks();
    refreshHotKey();
    updatingLoginItem_ = true;
    launchAtLogin_.set_active(platform::loginItemEnabled());
    updatingLoginItem_ = false;
    auto notice = platform::loginItemNotice();
    loginItemMessage_.set_text(notice.value_or(""));
    loginItemMessage_.set_visible(notice.has_value());
}

Gtk::Widget* SettingsWindow::buildDictionaries() {
    auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 8);

    auto pathRow = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
    pathEntry_.set_placeholder_text("~/.dict");
    pathEntry_.set_hexpand(true);
    pathEntry_.signal_activate().connect(sigc::mem_fun(*this, &SettingsWindow::applyPath));
    auto choose = Gtk::make_managed<Gtk::Button>("Choose…");
    choose->signal_clicked().connect(sigc::mem_fun(*this, &SettingsWindow::chooseFolder));
    auto rescan = Gtk::make_managed<Gtk::Button>("Rescan");
    rescan->signal_clicked().connect([this] {
        if (pathEntry_.get_text() == model_.dictionaryPath()) model_.reload(); else applyPath();
    });
    pathRow->append(pathEntry_);
    pathRow->append(*choose);
    pathRow->append(*rescan);
    box->append(*pathRow);

    bookList_.set_selection_mode(Gtk::SelectionMode::NONE);
    auto scroller = Gtk::make_managed<Gtk::ScrolledWindow>();
    scroller->set_child(bookList_);
    scroller->set_has_frame(true);
    scroller->set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);

    emptyLabel_.add_css_class("dim-label");
    emptyLabel_.set_can_target(false);
    spinner_.set_halign(Gtk::Align::CENTER);
    spinner_.set_valign(Gtk::Align::CENTER);
    auto overlay = Gtk::make_managed<Gtk::Overlay>();
    overlay->set_child(*scroller);
    overlay->add_overlay(emptyLabel_);
    overlay->add_overlay(spinner_);
    overlay->set_vexpand(true);
    box->append(*overlay);
    box->append(failureBox_);
    box->append(*makeCaption("Articles appear in this order. Drag a dictionary to move it."));
    return makeGroup("Dictionaries", *box);
}

Gtk::Widget* SettingsWindow::buildShortcut() {
    auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 8);

    auto row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
    auto title = Gtk::make_managed<Gtk::Label>("Global shortcut");
    title->set_xalign(0);
    title->set_hexpand(true);
    recorder_.set_size_request(130, -1);
    recorder_.signal_clicked().connect(sigc::mem_fun(*this, &SettingsWindow::startRecording));
    auto focus = Gtk::EventControllerFocus::create();
    focus->signal_leave().connect(sigc::mem_fun(*this, &SettingsWindow::stopRecording));
    recorder_.add_controller(focus);
    resetHotKey_.signal_clicked().connect([this] { setHotKey(platform::defaultHotKey()); });
    row->append(*title);
    row->append(recorder_);
    row->append(resetHotKey_);
    box->append(*row);

    hotKeyTaken_.set_xalign(0);
    hotKeyTaken_.add_css_class("error");
    hotKeyTaken_.set_visible(false);
    box->append(hotKeyTaken_);
    if (platform::systemOwnsHotKey()) {
        resetHotKey_.set_visible(false);
        box->append(*makeCaption("The desktop asks which keys to use and keeps them in its own settings."));
    }
    hotKeyNotice_.set_xalign(0);
    hotKeyNotice_.set_wrap(true);
    hotKeyNotice_.add_css_class("dim-label");
    box->append(hotKeyNotice_);

    if (platform::hasMenuBarIcon()) {
        hideToMenuBar_.set_active(model_.settings().hideToMenuBar);
        hideToMenuBar_.property_active().signal_changed().connect([this] {
            model_.settings().hideToMenuBar = hideToMenuBar_.get_active();
            model_.settings().save();
            hideToMenuBarChanged_();
        });
        box->append(*makeSwitchRow("Hide to menu bar", hideToMenuBar_));
        box->append(*makeCaption("Shows an icon in the menu bar and removes LjDict from the Dock while its windows are hidden."));
    }
    return makeGroup("Show or hide window", *box);
}

Gtk::Widget* SettingsWindow::buildStartup() {
    auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 8);

    launchAtLogin_.property_active().signal_changed().connect([this] {
        if (!updatingLoginItem_) setLaunchAtLogin(launchAtLogin_.get_active());
    });
    box->append(*makeSwitchRow("Launch at login", launchAtLogin_));

    startMinimized_.set_active(model_.settings().startMinimized);
    startMinimized_.property_active().signal_changed().connect([this] {
        model_.settings().startMinimized = startMinimized_.get_active();
        model_.settings().save();
    });
    box->append(*makeSwitchRow("Start minimized", startMinimized_));

    loginItemMessage_.set_xalign(0);
    loginItemMessage_.set_wrap(true);
    loginItemMessage_.add_css_class("dim-label");
    box->append(loginItemMessage_);
    return makeGroup("Startup", *box);
}

Gtk::Widget* SettingsWindow::makeSwitchRow(const Glib::ustring& title, Gtk::Switch& control) {
    auto row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
    auto label = Gtk::make_managed<Gtk::Label>(title);
    label->set_xalign(0);
    label->set_hexpand(true);
    control.set_valign(Gtk::Align::CENTER);
    row->append(*label);
    row->append(control);
    return row;
}

Gtk::Widget* SettingsWindow::makeBookRow(size_t index, const std::shared_ptr<const StarDictBook>& book) {
    auto row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    row->set_margin(4);

    auto handle = Gtk::make_managed<Gtk::Label>("≡");
    handle->add_css_class("dim-label");
    row->append(*handle);

    auto texts = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL);
    texts->set_hexpand(true);
    auto name = Gtk::make_managed<Gtk::Label>(book->name());
    name->set_xalign(0);
    name->set_ellipsize(Pango::EllipsizeMode::END);
    texts->append(*name);
    auto count = Gtk::make_managed<Gtk::Label>(std::to_string(book->wordCount()) + " words");
    count->set_xalign(0);
    count->add_css_class("caption");
    count->add_css_class("dim-label");
    texts->append(*count);
    row->append(*texts);

    auto enabled = Gtk::make_managed<Gtk::Switch>();
    enabled->set_valign(Gtk::Align::CENTER);
    enabled->set_active(model_.isEnabled(*book));
    enabled->property_active().signal_changed().connect([this, enabled, book] {
        if (enabled->get_active() != model_.isEnabled(*book)) model_.setEnabled(enabled->get_active(), *book);
    });
    row->append(*enabled);

    auto drag = Gtk::DragSource::create();
    drag->set_actions(Gdk::DragAction::MOVE);
    drag->signal_prepare().connect([index](double, double) {
        Glib::Value<int> value;
        value.init(Glib::Value<int>::value_type());
        value.set(static_cast<int>(index));
        return Gdk::ContentProvider::create(value);
    }, false);
    row->add_controller(drag);

    auto drop = Gtk::DropTarget::create(Glib::Value<int>::value_type(), Gdk::DragAction::MOVE);
    drop->signal_drop().connect([this, index, row](const Glib::ValueBase& value, double, double y) {
        Glib::Value<int> source;
        source.init(value.gobj());
        size_t destination = y > row->get_height() / 2.0 ? index + 1 : index;
        size_t from = static_cast<size_t>(source.get());
        // The rows are rebuilt, and this one is still handling the drop.
        Glib::signal_idle().connect_once([this, from, destination] { model_.moveBook(from, destination); });
        return true;
    }, false);
    row->add_controller(drop);
    return row;
}

void SettingsWindow::applyPath() {
    model_.setDictionaryPath(pathEntry_.get_text());
}

void SettingsWindow::chooseFolder() {
    auto dialog = Gtk::FileDialog::create();
    dialog->set_title("Choose Dictionary Folder");
    std::string path = pathEntry_.get_text();
    if (path == "~" || path.starts_with("~/")) path = Glib::get_home_dir() + path.substr(1);
    if (!path.empty()) dialog->set_initial_folder(Gio::File::create_for_path(path));
    dialog->select_folder(*this, [this, dialog](const Glib::RefPtr<Gio::AsyncResult>& result) {
        try {
            auto folder = dialog->select_folder_finish(result);
            if (!folder) return;
            pathEntry_.set_text(folder->get_path());
            model_.setDictionaryPath(folder->get_path());
        } catch (const Gtk::DialogError&) {
            // Dismissed.
        }
    });
}

void SettingsWindow::refreshBooks() {
    while (auto row = bookList_.get_row_at_index(0)) bookList_.remove(*row);
    BookList books = model_.orderedBooks();
    for (size_t index = 0; index < books.size(); ++index) bookList_.append(*makeBookRow(index, books[index]));

    spinner_.set_visible(model_.isLoading());
    spinner_.set_spinning(model_.isLoading());
    emptyLabel_.set_visible(!model_.isLoading() && books.empty() && model_.library().failures().empty());

    while (auto child = failureBox_.get_first_child()) failureBox_.remove(*child);
    for (const auto& failure : model_.library().failures()) {
        auto label = Gtk::make_managed<Gtk::Label>("⚠ " + failure.path + ": " + failure.reason);
        label->set_xalign(0);
        label->set_wrap(true);
        label->add_css_class("dim-label");
        failureBox_.append(*label);
    }
}

void SettingsWindow::refreshHotKey() {
    recorder_.set_label(isRecording_ ? "Press shortcut…" : platform::displayName(model_.settings().hotKey));
    resetHotKey_.set_sensitive(!(model_.settings().hotKey == platform::defaultHotKey()));
    auto notice = platform::hotKeyNotice();
    hotKeyNotice_.set_text(notice.value_or(""));
    hotKeyNotice_.set_visible(notice.has_value());
}

/// Click, then press the new shortcut. Esc cancels. The shortcut is off meanwhile, so that it can
/// be pressed again without triggering.
void SettingsWindow::startRecording() {
    if (platform::systemOwnsHotKey()) return platform::configureHotKey();
    if (isRecording_) return;
    isRecording_ = true;
    refreshHotKey();
    recorder_.grab_focus();
    platform::unregisterHotKey();
    platform::captureHotKey([this](std::optional<platform::HotKey> key) {
        isRecording_ = false;
        if (key) {
            setHotKey(*key);
        } else {
            refreshHotKey();
            hotKeyChanged_();
        }
    });
}

void SettingsWindow::stopRecording() {
    if (!isRecording_) return;
    isRecording_ = false;
    platform::cancelHotKeyCapture();
    refreshHotKey();
    hotKeyChanged_();
}

void SettingsWindow::setHotKey(const platform::HotKey& key) {
    model_.settings().hotKey = key;
    model_.settings().save();
    refreshHotKey();
    hotKeyChanged_();
}

void SettingsWindow::setLaunchAtLogin(bool enabled) {
    auto message = platform::setLoginItemEnabled(enabled);
    loginItemMessage_.set_text(message.value_or(""));
    loginItemMessage_.set_visible(message.has_value());
    updatingLoginItem_ = true;
    launchAtLogin_.set_active(platform::loginItemEnabled());
    updatingLoginItem_ = false;
}
