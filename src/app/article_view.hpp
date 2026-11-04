#pragma once

#include "core/dictionary_library.hpp"

#include <gtkmm.h>

#include <functional>
#include <optional>
#include <string>
#include <vector>

/// Shows rendered articles as styled text. Dictionary HTML is untrusted, and it never reaches
/// anything that could run it or fetch for it: see `layoutHTML`. Only http(s) links leave the app.
class ArticleView : public Gtk::ScrolledWindow {
public:
    ArticleView();

    void show(const std::vector<ljdict::Article>& articles, const std::optional<std::string>& message = std::nullopt);
    void setDark(bool dark);

    std::function<void(const std::string&)> onLookup;

private:
    struct LinkRange {
        int start;
        int end;
        std::string href;
    };

    void createTags();
    void applyPalette();
    void append(const std::string& text, std::vector<Glib::ustring> tags);
    bool ensureColorTag(const std::string& color);
    void appendArticle(const ljdict::Article& article);
    const LinkRange* linkAt(double x, double y);
    void open(const std::string& href);

    Gtk::TextView text_;
    Glib::RefPtr<Gtk::TextBuffer> buffer_;
    std::vector<LinkRange> links_;
    bool dark_ = false;
    bool pointerDragged_ = false;
};
