#include "core/article_renderer.hpp"
#include "core/dictionary_library.hpp"
#include "core/rich_text.hpp"
#include "core/text_folding.hpp"

#include <glib.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <set>

using namespace ljdict;
namespace fs = std::filesystem;

namespace {

int failures = 0;

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            std::cerr << __FILE__ << ':' << __LINE__ << ": failed: " #condition "\n"; \
            ++failures; \
        } \
    } while (false)

#define CHECK_EQ(actual, expected) \
    do { \
        auto a_ = (actual); \
        decltype(a_) e_ = (expected); \
        if (!(a_ == e_)) { \
            std::cerr << __FILE__ << ':' << __LINE__ << ": " #actual " differs from " #expected "\n"; \
            ++failures; \
        } \
    } while (false)

using Strings = std::vector<std::string>;

class Fixture {
public:
    Fixture() : root_(fs::temp_directory_path() / ("ljdict-" + std::to_string(std::rand()) + std::to_string(counter_++))) {
        fs::create_directories(root_);
    }
    ~Fixture() { fs::remove_all(root_); }

    const fs::path& root() const { return root_; }

    void writeBook(const std::string& name, const std::vector<std::pair<std::string, std::string>>& words, int offsetBits = 32) {
        std::string idx;
        std::string dict;
        auto put32 = [&](uint32_t value) {
            for (int shift = 24; shift >= 0; shift -= 8) idx.push_back(static_cast<char>(value >> shift));
        };
        for (const auto& [word, article] : words) {
            idx += word;
            idx.push_back('\0');
            if (offsetBits == 64) put32(0);
            put32(static_cast<uint32_t>(dict.size()));
            put32(static_cast<uint32_t>(article.size()));
            dict += article;
        }
        fs::path folder = root_ / name;
        fs::create_directories(folder);
        std::ofstream(folder / (name + ".ifo")) << "StarDict's dict ifo file\nversion=2.4.2\nwordcount=" << words.size()
            << "\nidxfilesize=" << idx.size() << "\nbookname=" << name << "\nsametypesequence=m\n"
            << (offsetBits == 64 ? "idxoffsetbits=64\n" : "");
        std::ofstream(folder / (name + ".idx"), std::ios::binary) << idx;
        std::ofstream(folder / (name + ".dict"), std::ios::binary) << dict;
    }

private:
    static inline int counter_ = 0;
    fs::path root_;
};

Strings bookNames(const std::vector<Article>& articles) {
    Strings names;
    for (const Article& article : articles) names.push_back(article.bookName);
    return names;
}

void testFolding() {
    std::string key = folding::key("cim");
    for (const char* word : {"čim", "ćim", "CIM", "Čim", "c\xCC\x8Cim"}) CHECK_EQ(folding::key(word), key);
    CHECK_EQ(folding::key("đak"), folding::key("dak"));
    CHECK_EQ(folding::key("ёлка"), folding::key("елка"));
    CHECK_EQ(folding::key("mañana"), folding::key("manana"));
    CHECK_EQ(folding::key("үй"), folding::key("уй"));
    CHECK_EQ(folding::key("Straße"), std::string("strasse"));
}

void testInvalidBytesDoNotBreakFolding() {
    CHECK(!folding::key("a\xFF" "b").empty());
    CHECK(!folding::rowKey("a\xFF" "b").empty());
}

void testRecords() {
    CHECK((StarDictBook::parseRecord("abc", "m") == std::vector<ArticlePart>{{'m', "abc"}}));
    std::string inlineTypes("hone\0mtwo\0", 10);
    CHECK((StarDictBook::parseRecord(inlineTypes, std::nullopt) == std::vector<ArticlePart>{{'h', "one"}, {'m', "two"}}));
    std::string truncatedSize("W\0\0", 3);
    CHECK(StarDictBook::parseRecord(truncatedSize, std::nullopt).empty());
}

void testRenderer() {
    using namespace ArticleRenderer;
    CHECK_EQ(xdxf("<k>нев<nu />[&apos;]<nu />о<nu />[/&apos;]<nu />льный</k>"), std::string("<div class=\"k\">нево\xCC\x81льный</div>"));
    CHECK_EQ(xdxf("<ex>нев<nu />'<nu />оля</ex>"), std::string("<span class=\"ex\">нево\xCC\x81ля</span>"));
    CHECK_EQ(xdxf("[']а[/'] b"), std::string("а\xCC\x81 b"));
    CHECK_EQ(xdxf("echar <nu />[soltar] <nu />sapos"), std::string("echar [soltar] sapos"));
    CHECK_EQ(xdxf("<nu />[com][c][i][p]мн.[/p][/i][/c][/com] <nu />x"), std::string("мн. x"));
    CHECK_EQ(xdxf("a<rref>a.wav</rref>b"), std::string("ab"));

    std::string reference = xdxf("<kref>Her Majesty&apos;s Office</kref>");
    CHECK(reference.ends_with(">Her Majesty&apos;s Office</a>"));
    size_t hrefStart = reference.find('"') + 1;
    CHECK_EQ(lookupWord(reference.substr(hrefStart, reference.find('"', hrefStart) - hrefStart)), std::optional<std::string>("Her Majesty's Office"));

    CHECK_EQ(lookupWord(lookupURL("čim & co?")), std::optional<std::string>("čim & co?"));
    CHECK_EQ(lookupWord("bword://%C4%8Dim"), std::optional<std::string>("čim"));
    CHECK(!lookupWord("https://example.com/?w=x"));
    CHECK_EQ(xdxf("<script>x</script> a < b"), std::string("x a &lt; b"));
    CHECK(html({'m', "<b>"}, "w").find("&lt;b&gt;") != std::string::npos);
}

void testLayout() {
    CHECK_EQ(layoutHTML("<div class=\"k\">word</div>a &amp; <b>b</b>"), (std::vector<RichSpan>{
        {"word", Headword, 0, ""}, {"\na & ", 0, 0, ""}, {"b", Bold, 0, ""}}));
    CHECK_EQ(layoutHTML("<blockquote>x<blockquote>y</blockquote></blockquote>"), (std::vector<RichSpan>{
        {"x\n", 0, 1, ""}, {"y", 0, 2, ""}}));
    CHECK_EQ(layoutHTML("<a href=\"ljdict://lookup?w=a&amp;b\">t</a>"), (std::vector<RichSpan>{
        {"t", 0, 0, "ljdict://lookup?w=a&b"}}));
    CHECK_EQ(layoutHTML("<span class=\"tr\">[a]</span> <span class=\"abr\">b</span>"), (std::vector<RichSpan>{
        {"[a]", Transcription, 0, ""}, {" ", 0, 0, ""}, {"b", Abbreviation, 0, ""}}));
}

void testInlineStyles() {
    CHECK_EQ(layoutHTML("<div style=\"margin-left:1em\"><i class=\"p\" style=\"color:green\">m.</i> x</div>"), (std::vector<RichSpan>{
        {"m.", Italic, 1, "", "green"}, {" x", 0, 1, "", ""}}));
    CHECK_EQ(layoutHTML("<span style=\"color: #0a0 ; font-size:40px\">a</span>"), (std::vector<RichSpan>{{"a", 0, 0, "", "#0a0"}}));
    CHECK_EQ(layoutHTML("<span style=\"color:url(x)\">a</span><span style=\"margin-left:0\">b</span>"), (std::vector<RichSpan>{{"ab", 0, 0, "", ""}}));
}

void testLayoutOfHostileHTML() {
    CHECK(layoutHTML("<script>alert(1)</script><style>a{}</style>").empty());
    CHECK(layoutHTML("<SCRIPT>alert(1)").empty());
    CHECK_EQ(layoutHTML("a<!-- b -->c"), (std::vector<RichSpan>{{"ac", 0, 0, ""}}));
    CHECK_EQ(layoutHTML("1 < 2 &bogus; &#65;"), (std::vector<RichSpan>{{"1 < 2 &bogus; A", 0, 0, ""}}));
    CHECK_EQ(layoutHTML("<img src=\"http://x/y.png\" onerror=\"z\">t"), (std::vector<RichSpan>{{"t", 0, 0, ""}}));
    CHECK_EQ(layoutHTML("<b>a</i>b"), (std::vector<RichSpan>{{"ab", Bold, 0, ""}}));
    CHECK_EQ(layoutHTML("a<div"), (std::vector<RichSpan>{{"a<div", 0, 0, ""}}));
}

void testSearchPutsExactMatchFirst() {
    Fixture fixture;
    fixture.writeBook("One", {{"čim", "1"}, {"ćim", "2"}, {"cim", "3"}, {"cimet", "4"}, {"dom", "5"}});
    auto library = DictionaryLibrary::load(fixture.root());
    Strings words = library->search("cim", library->books());
    CHECK_EQ(words.front(), std::string("cim"));
    CHECK_EQ(std::set<std::string>(words.begin(), words.end()), (std::set<std::string>{"cim", "čim", "ćim", "cimet"}));
    CHECK_EQ(words.back(), std::string("cimet"));
    CHECK_EQ(library->search("čim", library->books()).front(), std::string("čim"));
}

void testRowsMerge() {
    Fixture fixture;
    fixture.writeBook("One", {{"House", "a"}});
    fixture.writeBook("Two", {{"house", "b"}, {"hóuse", "c"}}, 64);
    auto library = DictionaryLibrary::load(fixture.root());
    CHECK_EQ(library->search("house", library->books()).size(), size_t(2));
    auto articles = library->articles("house", library->books());
    CHECK_EQ(bookNames(articles), (Strings{"One", "Two"}));
    CHECK(articles.at(1).html.find('b') != std::string::npos);
}

void testDisabledBooks() {
    Fixture fixture;
    fixture.writeBook("One", {{"alpha", "a"}});
    fixture.writeBook("Two", {{"beta", "b"}});
    auto library = DictionaryLibrary::load(fixture.root());
    CHECK_EQ(library->books().at(0)->id(), std::string("One/One.ifo"));
    CHECK_EQ(library->books().at(1)->id(), std::string("Two/Two.ifo"));
    CHECK(library->search("alpha", {library->books()[1]}).empty());
}

void testCustomOrder() {
    Fixture fixture;
    fixture.writeBook("A", {{"word", "a"}});
    fixture.writeBook("B", {{"word", "b"}});
    fixture.writeBook("C", {{"word", "c"}});
    auto library = DictionaryLibrary::load(fixture.root());
    BookList ordered = library->booksInOrder({"C/C.ifo", "gone/gone.ifo", "A/A.ifo"});
    Strings names;
    for (const auto& book : ordered) names.push_back(book->name());
    CHECK_EQ(names, (Strings{"C", "A", "B"}));
    CHECK_EQ(bookNames(library->articles("word", ordered)), (Strings{"C", "A", "B"}));
}

void testMissingData() {
    Fixture fixture;
    fixture.writeBook("One", {{"alpha", "a"}});
    fs::remove(fixture.root() / "One/One.dict");
    auto library = DictionaryLibrary::load(fixture.root());
    CHECK(library->books().empty());
    CHECK_EQ(library->failures().at(0).reason, std::string("missing .dict"));
}

void testRealDictionaries() {
    fs::path path = fs::path(g_get_home_dir()) / ".dict";
    if (!fs::exists(path)) {
        std::cout << "skipped: no ~/.dict\n";
        return;
    }
    auto start = std::chrono::steady_clock::now();
    auto library = DictionaryLibrary::load(path);
    std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - start;
    size_t words = 0;
    for (const auto& book : library->books()) words += book->wordCount();
    std::cout << "Loaded " << library->books().size() << " books, " << words << " words in " << elapsed.count() << "s\n";
    CHECK(library->failures().empty());
    CHECK(!library->articles("čeljad", library->books()).empty());
    Strings found = library->search("celjad", library->books());
    CHECK(std::find(found.begin(), found.end(), "čeljad") != found.end());
}

}

int main() {
    std::srand(static_cast<unsigned>(std::chrono::steady_clock::now().time_since_epoch().count()));
    for (const auto& test : std::initializer_list<std::function<void()>>{
             testFolding, testInvalidBytesDoNotBreakFolding, testRecords, testRenderer, testLayout, testInlineStyles, testLayoutOfHostileHTML,
             testSearchPutsExactMatchFirst, testRowsMerge, testDisabledBooks, testCustomOrder, testMissingData, testRealDictionaries}) {
        test();
    }
    if (failures == 0) std::cout << "All tests passed\n";
    return failures == 0 ? 0 : 1;
}
