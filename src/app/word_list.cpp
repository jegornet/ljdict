#include "app/word_list.hpp"

#include <algorithm>

WordList::WordList() {
    model_ = Gtk::StringList::create({});
    selection_ = Gtk::SingleSelection::create(model_);
    selection_->set_autoselect(false);
    selection_->set_can_unselect(true);
    selection_->property_selected().signal_changed().connect(sigc::mem_fun(*this, &WordList::selectionChanged));

    auto factory = Gtk::SignalListItemFactory::create();
    factory->signal_setup().connect(sigc::mem_fun(*this, &WordList::setUpRow));
    factory->signal_bind().connect([](const Glib::RefPtr<Gtk::ListItem>& item) {
        auto label = dynamic_cast<Gtk::Label*>(item->get_child());
        auto text = std::dynamic_pointer_cast<Gtk::StringObject>(item->get_item());
        if (label && text) label->set_text(text->get_string());
    });

    list_.set_model(selection_);
    list_.set_factory(factory);
    list_.set_focusable(false);
    list_.add_css_class("word-list");
    list_.add_css_class("content");

    set_child(list_);
    set_has_frame(true);
    set_focusable(false);
    set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    set_hexpand(true);
    set_vexpand(true);

    auto secondary = Gtk::GestureClick::create();
    secondary->set_button(GDK_BUTTON_SECONDARY);
    secondary->signal_pressed().connect([this](int, double x, double y) {
        if (onContextClick) onContextClick(std::nullopt, x, y);
    });
    add_controller(secondary);
}

void WordList::setUpRow(const Glib::RefPtr<Gtk::ListItem>& item) {
    item->set_focusable(false);
    item->set_activatable(false);

    auto label = Gtk::make_managed<Gtk::Label>();
    label->set_xalign(0);
    label->set_hexpand(true);
    label->set_ellipsize(Pango::EllipsizeMode::END);
    label->add_css_class("word");
    item->set_child(*label);

    // The row's own gesture would claim the press and hide the release from us, so the click is
    // handled here. The raw pointer is safe: the item owns the label, which owns the gestures.
    Gtk::ListItem* rawItem = item.get();
    auto primary = Gtk::GestureClick::create();
    primary->set_button(GDK_BUTTON_PRIMARY);
    primary->signal_pressed().connect([primary = primary.get()](int, double, double) {
        primary->set_state(Gtk::EventSequenceState::CLAIMED);
    });
    primary->signal_released().connect([this, rawItem](int, double, double) {
        guint row = rawItem->get_position();
        if (row >= words_.size()) return;
        std::string word = words_[row];
        select(row);
        if (onClick) onClick(word);
    });
    label->add_controller(primary);

    auto secondary = Gtk::GestureClick::create();
    secondary->set_button(GDK_BUTTON_SECONDARY);
    secondary->signal_pressed().connect([this, rawItem, label, secondary = secondary.get()](int, double x, double y) {
        secondary->set_state(Gtk::EventSequenceState::CLAIMED);
        guint row = rawItem->get_position();
        double listX = x;
        double listY = y;
        label->translate_coordinates(*this, x, y, listX, listY);
        if (row < words_.size() && onContextClick) onContextClick(words_[row], listX, listY);
    });
    label->add_controller(secondary);
}

void WordList::setWords(std::vector<std::string> words) {
    suppressNotifications_ = true;
    selection_->set_selected(GTK_INVALID_LIST_POSITION);
    std::vector<Glib::ustring> strings(words.begin(), words.end());
    model_->splice(0, model_->get_n_items(), strings);
    words_ = std::move(words);
    selection_->set_selected(GTK_INVALID_LIST_POSITION);
    suppressNotifications_ = false;
}

std::optional<std::string> WordList::selectedWord() const {
    guint row = selection_->get_selected();
    if (row >= words_.size()) return std::nullopt;
    return words_[row];
}

void WordList::select(guint row) {
    if (row >= words_.size()) return;
    selection_->set_selected(row);
    list_.scroll_to(row, Gtk::ListScrollFlags::NONE);
}

void WordList::clearSelection() {
    selection_->set_selected(GTK_INVALID_LIST_POSITION);
}

void WordList::moveSelection(int delta) {
    if (words_.empty()) return;
    guint current = selection_->get_selected();
    int base = current == GTK_INVALID_LIST_POSITION ? -1 : static_cast<int>(current);
    select(static_cast<guint>(std::clamp(base + delta, 0, static_cast<int>(words_.size()) - 1)));
}

void WordList::selectionChanged() {
    if (!suppressNotifications_ && onSelectionChange) onSelectionChange(selectedWord());
}
