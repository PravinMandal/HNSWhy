#include "product/chunker.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "framework/test_framework.h"
#include "product/errors.h"
#include "product/text.h"

namespace {

std::string read_fixture(const std::string& name) {
    const std::filesystem::path path =
        std::filesystem::path(HNSWHY_TEST_FIXTURES_DIR) / name;
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw hnswhytest::Failure("cannot read fixture: " + path.string());
    }
    return std::string(std::istreambuf_iterator<char>(input),
                       std::istreambuf_iterator<char>());
}

std::string repeat_words(std::size_t count, const std::string& word = "word") {
    std::string result;
    for (std::size_t i = 0; i < count; ++i) {
        if (i != 0) {
            result.push_back(' ');
        }
        result += word;
    }
    return result;
}

}  // namespace

HNSWHY_TEST(chunker_short_text_is_one_chunk) {
    const std::string text = "  Vector search finds neighbours.  ";
    const std::vector<hnswhy::TextChunk> chunks = hnswhy::chunk_text(text);
    CHECK_EQ(chunks.size(), std::size_t{1});
    CHECK_EQ(chunks[0].text, std::string("Vector search finds neighbours."));
    CHECK_EQ(chunks[0].begin, std::size_t{2});
    CHECK_EQ(chunks[0].end, text.size() - 2);
}

HNSWHY_TEST(chunker_windows_overlap_and_cover_text) {
    const std::string text = repeat_words(1000);
    hnswhy::ChunkOptions options;
    options.target_words = 100;
    options.overlap_words = 20;
    options.max_chunks = 100;
    const std::vector<hnswhy::TextChunk> chunks = hnswhy::chunk_text(text, options);

    CHECK_EQ(chunks.size(), std::size_t{13});
    CHECK_EQ(chunks.front().begin, std::size_t{0});
    CHECK_EQ(chunks.back().end, text.size());
    for (std::size_t i = 0; i < chunks.size(); ++i) {
        CHECK(!chunks[i].text.empty());
        CHECK(chunks[i].begin < chunks[i].end);
        CHECK(chunks[i].end <= text.size());
        if (i > 0) {
            CHECK(chunks[i].begin < chunks[i - 1].end);  // overlap, not a gap
        }
    }
}

HNSWHY_TEST(chunker_normalizes_line_endings_and_whitespace) {
    CHECK_EQ(hnswhy::normalize_newlines("a\r\nb\rc\n"), std::string("a\nb\nc\n"));
    CHECK_EQ(hnswhy::trim_copy(" \t\nhello \r\n"), std::string("hello"));
    CHECK_EQ(hnswhy::trim_copy("   "), std::string());
}

HNSWHY_TEST(chunker_empty_and_rejects_bad_options) {
    CHECK(hnswhy::chunk_text(" \n\t ").empty());
    hnswhy::ChunkOptions options;
    options.target_words = 10;
    options.overlap_words = 10;
    CHECK_THROWS_AS(hnswhy::chunk_text("one two", options), std::invalid_argument);
    options.overlap_words = 0;
    options.target_words = 0;
    CHECK_THROWS_AS(hnswhy::chunk_text("one two", options), std::invalid_argument);
}

HNSWHY_TEST(chunker_extracts_plain_text_and_strips_bom) {
    const std::string bytes = "\xEF\xBB\xBFhello\r\nworld";
    CHECK_EQ(hnswhy::extract_text(bytes, "notes.txt", "text/plain"), std::string("hello\r\nworld"));
    CHECK_EQ(hnswhy::extract_text("markdown", "notes.md", ""), std::string("markdown"));
}

HNSWHY_TEST(chunker_rejects_binary_and_unknown_formats) {
    CHECK_THROWS_AS(hnswhy::extract_text(std::string("a\0b", 3), "notes.txt", ""), hnswhy::ProductError);
    CHECK_THROWS_AS(hnswhy::extract_text("data", "archive.zip", "application/zip"), hnswhy::ProductError);
    CHECK_THROWS_AS(hnswhy::extract_text("", "notes.txt", "text/plain"), hnswhy::ProductError);
}

HNSWHY_TEST(chunker_extracts_text_from_a_pdf) {
    const std::string bytes = read_fixture("pdf-text.pdf");
    const std::string text = hnswhy::extract_text(bytes, "paper.pdf", "");
    CHECK(text.find("HNSWhy PDF extraction fixture.") != std::string::npos);
    CHECK(text.find("HNSW search descends the layered graph.") != std::string::npos);

    // Detection is by MIME type as well as extension, so an upload without a
    // .pdf suffix still takes the PDF path.
    const std::string by_mime = hnswhy::extract_text(bytes, "upload.bin", "application/pdf");
    CHECK(by_mime.find("HNSWhy PDF extraction fixture.") != std::string::npos);
}

HNSWHY_TEST(chunker_rejects_corrupt_and_scanned_pdfs) {
    const std::string corrupt = "%PDF-1.4\nthis is not a real PDF";
    try {
        (void)hnswhy::extract_text(corrupt, "broken.pdf", "application/pdf");
        CHECK(false);
    } catch (const hnswhy::ProductError& error) {
        CHECK_EQ(error.code(), std::string("EXTRACTION_FAILED"));
    }

    const std::string empty = read_fixture("pdf-empty.pdf");
    try {
        (void)hnswhy::extract_text(empty, "scan.pdf", "");
        CHECK(false);
    } catch (const hnswhy::ProductError& error) {
        CHECK_EQ(error.code(), std::string("EXTRACTION_FAILED"));
        CHECK(std::string(error.what()).find("no extractable text") != std::string::npos);
    }
}

HNSWHY_TEST(chunker_reports_missing_pdftotext) {
    const std::string bytes = read_fixture("pdf-text.pdf");
    const char* old_path = std::getenv("PATH");
    const std::string saved = old_path == nullptr ? std::string() : std::string(old_path);
    setenv("PATH", "/nonexistent", 1);
    try {
        (void)hnswhy::extract_text(bytes, "paper.pdf", "application/pdf");
        CHECK(false);
    } catch (const hnswhy::ProductError& error) {
        CHECK_EQ(error.code(), std::string("EXTRACTION_FAILED"));
    }
    if (old_path == nullptr) {
        unsetenv("PATH");
    } else {
        setenv("PATH", saved.c_str(), 1);
    }
}

HNSWHY_TEST(text_validates_utf8_and_truncates_at_codepoint_boundaries) {
    CHECK(hnswhy::is_valid_utf8("plain ASCII"));
    CHECK(hnswhy::is_valid_utf8("café 中文 😀"));
    CHECK(!hnswhy::is_valid_utf8(std::string("\xC3", 1)));
    CHECK(!hnswhy::is_valid_utf8(std::string("\x80", 1)));
    CHECK(!hnswhy::is_valid_utf8(std::string("\xC0\xAF", 2)));  // overlong '/'
    CHECK(!hnswhy::is_valid_utf8(std::string("\xED\xA0\x80", 3)));  // UTF-16 surrogate

    const std::string accented = "a\xC3\xA9z";  // a, é, z
    CHECK_EQ(hnswhy::truncate_utf8(accented, 2), std::string("a"));
    CHECK_EQ(hnswhy::truncate_utf8(accented, 3), std::string("a\xC3\xA9"));
    CHECK_EQ(hnswhy::utf8_length(accented), std::size_t{3});
    CHECK_EQ(hnswhy::truncate_utf8_words("alpha beta gamma", 8), std::string("alpha..."));
    CHECK_THROWS_AS(hnswhy::extract_text(std::string("\xC3\x28", 2), "bad.txt", "text/plain"),
                    hnswhy::ProductError);
}

HNSWHY_TEST(chunker_splits_whitespace_free_text_instead_of_one_giant_chunk) {
    std::string chinese;
    for (int i = 0; i < 1000; ++i) {
        chinese += "\xE4\xB8\xAD";  // U+4E2D, three UTF-8 bytes
    }
    const std::vector<hnswhy::TextChunk> chunks = hnswhy::chunk_text(chinese);
    CHECK(chunks.size() > 1);
    for (const hnswhy::TextChunk& chunk : chunks) {
        CHECK(!chunk.text.empty());
        CHECK(chunk.text.size() <= 350 * 3 + 3);
        CHECK(hnswhy::is_valid_utf8(chunk.text));
    }
    CHECK_EQ(chunks.front().begin, std::size_t{0});
    CHECK_EQ(chunks.back().end, chinese.size());
}
