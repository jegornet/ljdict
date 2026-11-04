#pragma once

#include <gtkmm.h>

#include <functional>
#include <optional>
#include <string>
#include <vector>

/// A single-column list of words. It never takes keyboard focus, so typing stays in the search field.
class WordList : public Gtk::ScrolledWindow {
public:
    WordList();

    const std::vector<std::string>& words() const { return words_; }
    /// Drops the selection without notifying.
    void setWords(std::vector<std::string> words);

    std::optional<std::string> selectedWord() const;
    void select(guint row);
    void clearSelection();
    void moveSelection(int delta);

    /// Selection moved, by click or by arrow keys in the search field.
    std::function<void(std::optional<std::string>)> onSelectionChange;
    std::function<void(const std::string&)> onClick;
    /// Secondary click, at a point in this widget. No word when it missed the rows.
    std::function<void(std::optional<std::string>, double, double)> onContextClick;

private:
    void setUpRow(const Glib::RefPtr<Gtk::ListItem>& item);
    void selectionChanged();

    std::vector<std::string> words_;
    Glib::RefPtr<Gtk::StringList> model_;
    Glib::RefPtr<Gtk::SingleSelection> selection_;
    Gtk::ListView list_;
    bool suppressNotifications_ = false;
};
