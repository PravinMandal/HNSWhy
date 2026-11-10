#include "product/chunker.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <sys/wait.h>
#include <unistd.h>

#include "product/errors.h"
#include "product/text.h"

namespace hnswhy {
namespace {

struct WordSpan {
    std::size_t begin;
    std::size_t end;
};

// Whitespace-free scripts (CJK, Thai, long URLs, minified code) otherwise
// become one enormous "word" and therefore one chunk. Split only long runs,
// at UTF-8 boundaries, so ordinary words keep their word-level chunking.
constexpr std::size_t kLongSpanBytes = 64;

void append_span(std::vector<WordSpan>& result, std::string_view text, std::size_t begin,
                 std::size_t end) {
    if (end - begin <= kLongSpanBytes) {
        result.push_back(WordSpan{begin, end});
        return;
    }
    std::size_t i = begin;
    while (i < end) {
        const std::size_t codepoint_begin = i;
        ++i;
        while (i < end && (static_cast<unsigned char>(text[i]) & 0xC0U) == 0x80U) {
            ++i;
        }
        result.push_back(WordSpan{codepoint_begin, i});
    }
}

std::vector<WordSpan> words(std::string_view text) {
    std::vector<WordSpan> result;
    std::size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i])) != 0) {
            ++i;
        }
        if (i == text.size()) {
            break;
        }
        const std::size_t begin = i;
        while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i])) == 0) {
            ++i;
        }
        append_span(result, text, begin, i);
    }
    return result;
}

std::string lower_extension(std::string_view filename) {
    const std::filesystem::path path{std::string(filename)};
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return extension;
}

std::string lower_content_type(std::string_view content_type) {
    const std::size_t semicolon = content_type.find(';');
    std::string value(content_type.substr(0, semicolon));
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

bool is_text_format(const std::string& extension, const std::string& content_type) {
    return extension == ".txt" || extension == ".md" || extension == ".markdown" ||
           extension == ".text" || content_type == "text/plain" ||
           content_type == "text/markdown";
}

bool is_pdf_format(const std::string& extension, const std::string& content_type) {
    return extension == ".pdf" || content_type == "application/pdf";
}

class PdfTempFile {
public:
    PdfTempFile() {
        std::string pattern =
            (std::filesystem::temp_directory_path() / "hnswhy_pdf_XXXXXX").string();
        std::vector<char> buffer(pattern.begin(), pattern.end());
        buffer.push_back('\0');
        fd_ = mkstemp(buffer.data());
        if (fd_ < 0) {
            throw ProductError("EXTRACTION_FAILED", "cannot create a temporary PDF file", 422);
        }
        // Assigning a path allocates. If it throws, the constructor never
        // completes and its destructor does not run, so clean up explicitly.
        try {
            path_ = buffer.data();
        } catch (...) {
            ::close(fd_);
            fd_ = -1;
            std::error_code ignored;
            std::filesystem::remove(buffer.data(), ignored);
            throw;
        }
    }

    ~PdfTempFile() {
        if (fd_ >= 0) {
            ::close(fd_);
        }
        std::error_code error;
        std::filesystem::remove(path_, error);
    }

    PdfTempFile(const PdfTempFile&) = delete;
    PdfTempFile& operator=(const PdfTempFile&) = delete;

    void write(std::string_view bytes) {
        std::size_t written = 0;
        while (written < bytes.size()) {
            const ssize_t count =
                ::write(fd_, bytes.data() + written, bytes.size() - written);
            if (count <= 0) {
                if (count < 0 && errno == EINTR) {
                    continue;
                }
                throw ProductError("EXTRACTION_FAILED", "cannot write the temporary PDF", 422);
            }
            written += static_cast<std::size_t>(count);
        }
        if (::close(fd_) != 0) {
            fd_ = -1;
            throw ProductError("EXTRACTION_FAILED", "cannot close the temporary PDF", 422);
        }
        fd_ = -1;
    }

    const std::filesystem::path& path() const { return path_; }

private:
    int fd_ = -1;
    std::filesystem::path path_;
};

std::string shell_quote(std::string_view value) {
    std::string quoted = "'";
    for (const char character : value) {
        if (character == '\'') {
            quoted += "'\\''";
        } else {
            quoted.push_back(character);
        }
    }
    quoted.push_back('\'');
    return quoted;
}

struct PipeCloser {
    void operator()(FILE* pipe) const {
        if (pipe != nullptr) {
            ::pclose(pipe);
        }
    }
};

std::string extract_pdf(std::string_view bytes) {
    PdfTempFile file;
    file.write(bytes);

    const std::string command =
        "timeout 30s pdftotext -layout -enc UTF-8 -- " + shell_quote(file.path().string()) +
        " - 2>/dev/null";
    FILE* raw_pipe = ::popen(command.c_str(), "r");
    if (raw_pipe == nullptr) {
        throw ProductError("EXTRACTION_FAILED",
                           "pdftotext is not available; install poppler-utils", 422);
    }
    // Own the pipe so an allocation failure while appending output cannot leak
    // the child process or its file descriptor.
    std::unique_ptr<FILE, PipeCloser> pipe(raw_pipe);

    std::string output;
    std::array<char, 8192> buffer{};
    while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe.get()) != nullptr) {
        output.append(buffer.data());
        if (output.size() > 32U * 1024U * 1024U) {
            throw ProductError("DOCUMENT_TOO_LARGE", "extracted PDF text is too large", 422);
        }
    }
    const int status = ::pclose(pipe.release());
    if (status != 0) {
        const int exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        if (exit_code == 127) {
            throw ProductError("EXTRACTION_FAILED",
                               "pdftotext is not available; install poppler-utils", 422);
        }
        if (exit_code == 124) {
            throw ProductError("EXTRACTION_FAILED", "PDF extraction timed out", 422);
        }
        throw ProductError("EXTRACTION_FAILED",
                           "pdftotext could not read this PDF; it may be encrypted or corrupt",
                           422);
    }

    std::replace(output.begin(), output.end(), '\f', '\n');
    if (!is_valid_utf8(output)) {
        throw ProductError("EXTRACTION_FAILED",
                           "the PDF produced invalid UTF-8 text; try another export", 422);
    }
    if (trim_copy(output).empty()) {
        throw ProductError("EXTRACTION_FAILED",
                           "the PDF has no extractable text (scanned PDFs are not supported)",
                           422);
    }
    return output;
}

std::string extract_plain(std::string_view bytes) {
    if (bytes.find('\0') != std::string_view::npos) {
        throw ProductError("EXTRACTION_FAILED", "the document looks binary, not text", 422);
    }
    if (bytes.size() >= 3 && static_cast<unsigned char>(bytes[0]) == 0xEF &&
        static_cast<unsigned char>(bytes[1]) == 0xBB &&
        static_cast<unsigned char>(bytes[2]) == 0xBF) {
        bytes.remove_prefix(3);
    }
    if (!is_valid_utf8(bytes)) {
        throw ProductError("INVALID_UTF8",
                           "the document contains invalid UTF-8; save it as UTF-8 and retry",
                           422);
    }
    return std::string(bytes);
}

}  // namespace

std::string normalize_newlines(std::string_view text) {
    std::string result;
    result.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\r') {
            if (i + 1 < text.size() && text[i + 1] == '\n') {
                ++i;
            }
            result.push_back('\n');
        } else {
            result.push_back(text[i]);
        }
    }
    return result;
}

std::string trim_copy(std::string_view text) {
    std::size_t begin = 0;
    while (begin < text.size() &&
           std::isspace(static_cast<unsigned char>(text[begin])) != 0) {
        ++begin;
    }
    std::size_t end = text.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
        --end;
    }
    return std::string(text.substr(begin, end - begin));
}

std::vector<TextChunk> chunk_text(std::string_view text, const ChunkOptions& options) {
    if (options.target_words < 1 || options.overlap_words >= options.target_words ||
        options.max_chunks < 1) {
        throw std::invalid_argument("chunk_text: invalid chunk options");
    }

    const std::vector<WordSpan> spans = words(text);
    if (spans.empty()) {
        return {};
    }

    if (spans.size() <= options.target_words) {
        const std::size_t begin = spans.front().begin;
        const std::size_t end = spans.back().end;
        return {TextChunk{std::string(text.substr(begin, end - begin)), begin, end}};
    }

    const std::size_t step = options.target_words - options.overlap_words;
    std::vector<TextChunk> chunks;
    for (std::size_t start = 0; start < spans.size(); start += step) {
        const std::size_t stop = std::min(start + options.target_words, spans.size());
        const std::size_t begin = spans[start].begin;
        const std::size_t end = spans[stop - 1].end;
        chunks.push_back(TextChunk{std::string(text.substr(begin, end - begin)), begin, end});
        if (chunks.size() > options.max_chunks) {
            throw ProductError("DOCUMENT_TOO_LARGE",
                               "the document produces too many chunks", 422);
        }
        if (stop == spans.size()) {
            break;
        }
    }
    return chunks;
}

std::string extract_text(std::string_view bytes, std::string_view filename,
                         std::string_view content_type) {
    if (bytes.empty()) {
        throw ProductError("EMPTY_DOCUMENT", "the uploaded document is empty", 422);
    }
    if (bytes.size() > 20U * 1024U * 1024U) {
        throw ProductError("DOCUMENT_TOO_LARGE", "the upload exceeds the 20 MB limit", 413);
    }

    const std::string extension = lower_extension(filename);
    const std::string mime = lower_content_type(content_type);

    if (is_pdf_format(extension, mime)) {
        return extract_pdf(bytes);
    }
    if (is_text_format(extension, mime)) {
        return extract_plain(bytes);
    }

    throw ProductError("UNSUPPORTED_FORMAT",
                       "supported formats are PDF, Markdown and plain text", 415);
}

}  // namespace hnswhy
