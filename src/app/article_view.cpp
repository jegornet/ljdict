#include "app/article_view.hpp"

#include "core/article_renderer.hpp"
#include "core/rich_text.hpp"

#include <algorithm>

using namespace ljdict;

namespace {

constexpr int sideMargin = 10;
constexpr int indentStep = 15;
constexpr int deepestIndent = 8;
constexpr double dragThreshold = 4;

struct Palette {
    const char* text;
    const char* band;
    const char* bandText;
    const char* link;
    const char* abbreviation;
    const char* example;
    const char* comment;
    const char* transcription;
};

constexpr Palette light{"#1c1c1c", "#efeddc", "#5c5a4c", "#0b57b0", "#1a7a35", "#3f5f8a", "#6b6b6b", "#8a3b12"};
constexpr Palette dark{"#e6e4da", "#33322d", "#b5b2a4", "#7db4f5", "#7fcb8f", "#9dbbe0", "#a3a198", "#e0a37e"};

Glib::ustring indentTag(int level) {
    return "indent" + std::to_string(std::min(level, deepestIndent));
}

}

ArticleView::ArticleView() {
    buffer_ = Gtk::TextBuffer::create();
    createTags();
    applyPalette();

    text_.set_buffer(buffer_);
    text_.set_editable(false);
    text_.set_cursor_visible(false);
    text_.set_wrap_mode(Gtk::WrapMode::WORD_CHAR);
    text_.add_css_class("content");
    text_.add_css_class("article");
    set_child(text_);
    set_hexpand(true);
    set_vexpand(true);
    set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);

    auto click = Gtk::GestureClick::create();
    click->set_button(GDK_BUTTON_PRIMARY);
    click->signal_pressed().connect([this](int, double, double) { pointerDragged_ = false; });
    click->signal_update().connect([this, click = click.get()](const Gdk::EventSequence*) {
        // A drag selects text, so it must not follow a link it happens to end on.
        Gdk::Rectangle moved;
        if (click->get_bounding_box(moved) && (moved.get_width() > dragThreshold || moved.get_height() > dragThreshold)) pointerDragged_ = true;
    });
    click->signal_released().connect([this](int, double x, double y) {
        if (pointerDragged_ || buffer_->get_has_selection()) return;
        if (const LinkRange* link = linkAt(x, y)) {
            std::string href = link->href;
            // Showing an article replaces the buffer, which the text view is still dispatching for.
            Glib::signal_idle().connect_once([this, href] { open(href); });
        }
    });
    text_.add_controller(click);

    auto motion = Gtk::EventControllerMotion::create();
    motion->signal_motion().connect([this](double x, double y) {
        text_.set_cursor(linkAt(x, y) ? "pointer" : "text");
    });
    text_.add_controller(motion);
}

void ArticleView::createTags() {
    auto table = buffer_->get_tag_table();
    auto add = [&](const char* name) {
        auto tag = Gtk::TextTag::create(name);
        table->add(tag);
        return tag;
    };

    auto book = add("book");
    book->property_scale() = 12.0 / 15.0;
    book->property_pixels_above_lines() = 3;
    book->property_pixels_below_lines() = 3;
    book->property_left_margin() = sideMargin;

    auto article = add("article");
    article->property_left_margin() = sideMargin;
    article->property_right_margin() = sideMargin;
    article->property_pixels_inside_wrap() = 1;

    auto message = add("message");
    message->property_left_margin() = sideMargin;
    message->property_pixels_above_lines() = 12;

    auto gap = add("gap");
    gap->property_scale() = 0.5;

    auto headword = add("headword");
    headword->property_scale() = 18.0 / 15.0;
    headword->property_weight() = Pango::Weight::SEMIBOLD;

    add("bold")->property_weight() = Pango::Weight::BOLD;
    add("italic")->property_style() = Pango::Style::ITALIC;
    add("underline")->property_underline() = Pango::Underline::SINGLE;
    add("abbreviation")->property_style() = Pango::Style::ITALIC;
    add("example");
    add("comment");
    add("transcription");
    add("link");

    auto subscript = add("subscript");
    subscript->property_scale() = 0.75;
    subscript->property_rise() = -3 * Pango::SCALE;
    auto superscript = add("superscript");
    superscript->property_scale() = 0.75;
    superscript->property_rise() = 5 * Pango::SCALE;

    for (int level = 1; level <= deepestIndent; ++level) {
        add(indentTag(level).c_str())->property_left_margin() = sideMargin + level * indentStep;
    }
}

void ArticleView::applyPalette() {
    const Palette& palette = dark_ ? dark : light;
    auto table = buffer_->get_tag_table();
    auto color = [&](const char* tag, const char* value) { table->lookup(tag)->property_foreground() = value; };
    color("book", palette.bandText);
    table->lookup("book")->property_paragraph_background() = palette.band;
    color("link", palette.link);
    color("abbreviation", palette.abbreviation);
    color("example", palette.example);
    color("comment", palette.comment);
    color("message", palette.comment);
    color("transcription", palette.transcription);
}

void ArticleView::setDark(bool isDark) {
    if (dark_ == isDark) return;
    dark_ = isDark;
    applyPalette();
}

void ArticleView::append(const std::string& text, std::vector<Glib::ustring> tags) {
    buffer_->insert_with_tags_by_name(buffer_->end(), text, tags);
}

/// False when the dictionary's color means nothing to GDK.
bool ArticleView::ensureColorTag(const std::string& color) {
    auto table = buffer_->get_tag_table();
    if (table->lookup("color-" + color)) return true;
    Gdk::RGBA rgba;
    if (!rgba.set(color)) return false;
    auto tag = Gtk::TextTag::create("color-" + color);
    tag->property_foreground_rgba() = rgba;
    table->add(tag);
    return true;
}

void ArticleView::appendArticle(const Article& article) {
    append(article.bookName + "\n", {"book"});
    for (const RichSpan& span : layoutHTML(article.html)) {
        std::vector<Glib::ustring> tags = {"article"};
        if (span.indent > 0) tags.push_back(indentTag(span.indent));
        if (span.styles & Bold) tags.push_back("bold");
        if (span.styles & Italic) tags.push_back("italic");
        if (span.styles & Underline) tags.push_back("underline");
        if (span.styles & Subscript) tags.push_back("subscript");
        if (span.styles & Superscript) tags.push_back("superscript");
        if (span.styles & Headword) tags.push_back("headword");
        if (span.styles & Transcription) tags.push_back("transcription");
        if (span.styles & Abbreviation) tags.push_back("abbreviation");
        if (span.styles & Example) tags.push_back("example");
        if (span.styles & Comment) tags.push_back("comment");
        if (!span.color.empty() && ensureColorTag(span.color)) tags.push_back("color-" + span.color);
        int start = buffer_->end().get_offset();
        if (!span.href.empty()) tags.push_back("link");
        append(span.text, std::move(tags));
        if (!span.href.empty()) links_.push_back({start, buffer_->end().get_offset(), span.href});
    }
    append("\n\n", {"gap"});
}

void ArticleView::show(const std::vector<Article>& articles, const std::optional<std::string>& message) {
    buffer_->set_text("");
    links_.clear();
    if (message) append(*message + "\n", {"message"});
    for (const Article& article : articles) appendArticle(article);
    buffer_->place_cursor(buffer_->begin());
    get_vadjustment()->set_value(0);
}

const ArticleView::LinkRange* ArticleView::linkAt(double x, double y) {
    int bufferX, bufferY;
    text_.window_to_buffer_coords(Gtk::TextWindowType::WIDGET, static_cast<int>(x), static_cast<int>(y), bufferX, bufferY);
    Gtk::TextIter iter;
    if (!text_.get_iter_at_location(iter, bufferX, bufferY)) return nullptr;
    int offset = iter.get_offset();
    auto found = std::lower_bound(links_.begin(), links_.end(), offset, [](const LinkRange& link, int value) { return link.end <= value; });
    return found != links_.end() && found->start <= offset ? &*found : nullptr;
}

void ArticleView::open(const std::string& href) {
    if (auto word = ArticleRenderer::lookupWord(href)) {
        if (onLookup) onLookup(*word);
        return;
    }
    std::string scheme = Glib::uri_parse_scheme(href);
    if (scheme != "http" && scheme != "https") return;
    auto root = dynamic_cast<Gtk::Window*>(get_root());
    if (root) Gtk::UriLauncher::create(href)->launch(*root, [](const Glib::RefPtr<Gio::AsyncResult>&) {});
}
