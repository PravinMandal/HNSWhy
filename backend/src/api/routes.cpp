#include "api/routes.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>

#include "json.hpp"
#include "product/chunker.h"
#include "product/errors.h"

namespace hnswhy {
namespace {

using json = nlohmann::json;

void send_json(httplib::Response& response, int status, const json& body) {
    response.status = status;
    // SQLite or an external provider can contain malformed UTF-8. Replace bad
    // bytes instead of throwing from the response path and turning a readable
    // response into a 500.
    response.set_content(
        body.dump(-1, ' ', false, json::error_handler_t::replace),
        "application/json; charset=utf-8");
}

void send_error(httplib::Response& response, int status, std::string code,
                std::string message) {
    send_json(response, status, json{{"error", {{"code", std::move(code)},
                                                {"message", std::move(message)}}}});
}

template <typename Handler>
void guarded(httplib::Response& response, Handler&& handler) {
    try {
        handler();
    } catch (const ProductError& error) {
        send_error(response, error.status(), error.code(), error.what());
    } catch (const std::exception& error) {
        std::cerr << "[hnswhy] request failed: " << error.what() << '\n';
        send_error(response, 500, "INTERNAL_ERROR", "internal server error");
    }
}

json parse_json_body(const httplib::Request& request) {
    if (request.body.empty()) {
        throw validation_error("a JSON request body is required");
    }
    try {
        return json::parse(request.body);
    } catch (const std::exception&) {
        throw validation_error("the request body is not valid JSON");
    }
}

std::string trimmed_lowercase(std::string text) {
    const auto not_space = [](unsigned char c) { return std::isspace(c) == 0; };
    text.erase(text.begin(), std::find_if(text.begin(), text.end(), not_space));
    text.erase(std::find_if(text.rbegin(), text.rend(), not_space).base(), text.end());
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

// Routes that read JSON must say so. Parsing an arbitrary body as JSON turns a
// client mistake (wrong Content-Type) into a confusing "invalid JSON" 400.
void require_json_content_type(const httplib::Request& request) {
    const std::string header = request.get_header_value("Content-Type");
    const std::string type = trimmed_lowercase(header.substr(0, header.find(';')));
    const bool json_type =
        type == "application/json" ||
        (type.size() > 5 && type.compare(type.size() - 5, 5, "+json") == 0);
    if (!json_type) {
        throw ProductError("UNSUPPORTED_MEDIA_TYPE",
                           "this endpoint requires Content-Type: application/json", 415);
    }
}

std::string required_string(const json& body, const char* field) {
    if (!body.contains(field) || !body[field].is_string()) {
        throw validation_error(std::string(field) + " must be a string");
    }
    return body[field].get<std::string>();
}

std::string optional_string(const json& body, const char* field,
                            const std::string& fallback = {}) {
    if (!body.contains(field) || body[field].is_null()) {
        return fallback;
    }
    if (!body[field].is_string()) {
        throw validation_error(std::string(field) + " must be a string");
    }
    return body[field].get<std::string>();
}

Id parse_id(const std::string& text) {
    try {
        std::size_t consumed = 0;
        const long long value = std::stoll(text, &consumed);
        if (consumed != text.size() || value <= 0) {
            throw std::invalid_argument("id");
        }
        return static_cast<Id>(value);
    } catch (const std::exception&) {
        throw validation_error("invalid document id");
    }
}

std::size_t parse_limit(const httplib::Request& request, std::size_t fallback,
                        std::size_t maximum) {
    if (!request.has_param("limit")) {
        return fallback;
    }
    const std::string value = request.get_param_value("limit");
    try {
        std::size_t consumed = 0;
        const unsigned long long parsed = std::stoull(value, &consumed);
        if (consumed != value.size() || parsed == 0 || parsed > maximum) {
            throw std::out_of_range("limit");
        }
        return static_cast<std::size_t>(parsed);
    } catch (const std::exception&) {
        throw validation_error("limit must be between 1 and " + std::to_string(maximum));
    }
}

json document_json(const DocumentSummary& document) {
    return json{{"id", document.id},
                {"title", document.title},
                {"source", document.source},
                {"chunk_count", document.chunk_count},
                {"char_count", document.char_count},
                {"created_at", document.created_at}};
}

json source_json(const RagSource& source) {
    return json{{"chunk_id", source.chunk_id},
                {"document_id", source.document_id},
                {"title", source.title},
                {"ordinal", source.ordinal},
                {"text", source.text},
                {"distance", source.distance}};
}

json status_json(const StatusView& status) {
    return json{{"database", status.database},
                {"store",
                 {{"documents", status.store.documents},
                  {"chunks", status.store.chunks},
                  {"embeddings", status.store.embeddings}}},
                {"index",
                 {{"live", status.index_live},
                  {"slots", status.index_slots},
                  {"tombstones", status.index_tombstones},
                  {"dim", status.dim}}},
                {"embedding",
                 {{"provider", status.embedding_provider},
                  {"model", status.embedding_model},
                  {"dim", status.embedding_dim}}},
                {"chat", {{"provider", status.chat_provider}, {"model", status.chat_model}}},
                {"query_ef", status.query_ef},
                {"sample_loaded", status.sample_loaded},
                {"benchmark_available", status.benchmark_available}};
}

}  // namespace

void register_routes(httplib::Server& server, Application& app) {
    server.Get("/api/health", [&app](const httplib::Request&, httplib::Response& response) {
        guarded(response, [&] {
            const HealthView health = app.health();
            const std::string status =
                !health.ok ? "error"
                           : (health.embedding.available && health.chat.available ? "ok"
                                                                                 : "degraded");
            send_json(response, 200,
                      json{{"status", status},
                           {"database", health.database},
                           {"store",
                            {{"documents", health.store.documents},
                             {"chunks", health.store.chunks},
                             {"embeddings", health.store.embeddings}}},
                           {"embedding",
                            {{"provider", health.embedding.provider},
                             {"model", health.embedding.model},
                             {"available", health.embedding.available}}},
                           {"chat",
                            {{"provider", health.chat.provider},
                             {"model", health.chat.model},
                             {"available", health.chat.available}}}});
        });
    });

    server.Get("/api/status", [&app](const httplib::Request&, httplib::Response& response) {
        guarded(response, [&] { send_json(response, 200, status_json(app.status())); });
    });

    server.Get("/api/documents",
               [&app](const httplib::Request&, httplib::Response& response) {
                   guarded(response, [&] {
                       json documents = json::array();
                       for (const DocumentSummary& document : app.list_documents()) {
                           documents.push_back(document_json(document));
                       }
                       send_json(response, 200, documents);
                   });
               });

    server.Get(R"(/api/documents/(\d+))",
               [&app](const httplib::Request& request, httplib::Response& response) {
                   guarded(response, [&] {
                       const Id id = parse_id(request.matches[1].str());
                       const auto detail = app.get_document(id);
                       if (!detail.has_value()) {
                           throw not_found_error("document not found");
                       }
                       json chunks = json::array();
                       for (const ChunkRecord& chunk : detail->chunks) {
                           chunks.push_back({{"id", chunk.id},
                                             {"ordinal", chunk.ordinal},
                                             {"text", chunk.text}});
                       }
                       send_json(response, 200,
                                 json{{"document", document_json(detail->summary)},
                                      {"text", detail->text},
                                      {"chunks", std::move(chunks)}});
                   });
               });

    server.Post("/api/documents",
                [&app](const httplib::Request& request, httplib::Response& response) {
                    guarded(response, [&] {
                        std::string title;
                        std::string source;
                        std::string text;
                        if (request.is_multipart_form_data()) {
                            const auto file = request.get_file_value("file");
                            if (file.filename.empty() || file.content.empty()) {
                                throw validation_error(
                                    "attach a non-empty file in the 'file' field");
                            }
                            const auto title_field = request.get_file_value("title");
                            if (!title_field.content.empty()) {
                                title = title_field.content;
                            }
                            source = std::filesystem::path(file.filename).filename().string();
                            text = extract_text(file.content, file.filename, file.content_type);
                        } else {
                            require_json_content_type(request);
                            const json body = parse_json_body(request);
                            text = required_string(body, "text");
                            title = optional_string(body, "title");
                            source = optional_string(body, "source", "paste");
                        }

                        const DocumentSummary document =
                            app.add_document(title, source, std::move(text));
                        send_json(response, 201, json{{"document", document_json(document)}});
                    });
                });

    server.Delete(R"(/api/documents/(\d+))",
                  [&app](const httplib::Request& request, httplib::Response& response) {
                      guarded(response, [&] {
                          const Id id = parse_id(request.matches[1].str());
                          if (!app.delete_document(id)) {
                              throw not_found_error("document not found");
                          }
                          send_json(response, 200, json{{"deleted", true}, {"id", id}});
                      });
                  });

    server.Post("/api/ask", [&app](const httplib::Request& request,
                                   httplib::Response& response) {
        guarded(response, [&] {
            require_json_content_type(request);
            const json body = parse_json_body(request);
            const std::string question = required_string(body, "question");
            std::size_t k = app.config().default_k;
            if (body.contains("k") && !body["k"].is_null()) {
                if (!body["k"].is_number_integer()) {
                    throw validation_error("k must be an integer");
                }
                const long long value = body["k"].get<long long>();
                if (value < 1 || value > 20) {
                    throw validation_error("k must be between 1 and 20");
                }
                k = static_cast<std::size_t>(value);
            }

            const RagAnswer answer = app.ask(question, k);
            json sources = json::array();
            for (const RagSource& source : answer.sources) {
                sources.push_back(source_json(source));
            }
            send_json(response, 200,
                      json{{"answer", answer.answer},
                           {"sources", std::move(sources)},
                           {"query_vector", answer.query_vector},
                           {"timings_ms",
                            {{"embedding", answer.timings.embedding_ms},
                             {"search", answer.timings.search_ms},
                             {"generate", answer.timings.generate_ms},
                             {"total", answer.timings.total_ms}}},
                           {"models",
                            {{"embedding_provider", answer.embedding_provider},
                             {"embedding_model", answer.embedding_model},
                             {"chat_provider", answer.chat_provider},
                             {"chat_model", answer.chat_model}}}});
        });
    });

    server.Get("/api/map", [&app](const httplib::Request& request,
                                  httplib::Response& response) {
        guarded(response, [&] {
            const std::size_t limit = parse_limit(request, 800, 2000);
            const MapView map = app.map(limit);
            json points = json::array();
            for (const MapPoint& point : map.points) {
                points.push_back(json{{"chunk_id", point.chunk_id},
                                      {"document_id", point.document_id},
                                      {"title", point.title},
                                      {"ordinal", point.ordinal},
                                      {"preview", point.preview},
                                      {"vector", point.vector}});
            }
            send_json(response, 200,
                      json{{"total_chunks", map.total_chunks},
                           {"sampled", points.size()},
                           {"points", std::move(points)}});
        });
    });

    server.Get("/api/benchmark", [&app](const httplib::Request&,
                                        httplib::Response& response) {
        guarded(response, [&] {
            const BenchmarkFile benchmark = app.benchmark_json();
            if (!benchmark.error.empty()) {
                throw ProductError("BENCHMARK_UNAVAILABLE", benchmark.error, 500);
            }
            if (!benchmark.contents.has_value()) {
                send_json(response, 200,
                          json{{"available", false},
                               {"message",
                                "Run the offline benchmark to generate docs/results/bench.json."},
                               {"command", "./scripts/run_bench.sh"}});
                return;
            }
            try {
                send_json(response, 200,
                          json{{"available", true}, {"report", json::parse(*benchmark.contents)}});
            } catch (const std::exception&) {
                throw ProductError("BENCHMARK_INVALID",
                                   "docs/results/bench.json is not valid JSON", 500);
            }
        });
    });

    server.set_error_handler([](const httplib::Request& request,
                                httplib::Response& response) {
        if (request.path.rfind("/api/", 0) == 0 && response.body.empty()) {
            send_error(response, response.status, "HTTP_ERROR",
                       "the request could not be handled");
        }
    });

    const std::filesystem::path& static_dir = app.config().static_dir;
    std::error_code error;
    if (std::filesystem::is_directory(static_dir, error)) {
        if (!server.set_mount_point("/", static_dir.string())) {
            std::cerr << "[hnswhy] could not mount frontend at " << static_dir << '\n';
        }
    } else {
        std::cerr << "[hnswhy] frontend build not found at " << static_dir
                  << "; API-only mode\n";
    }
}

}  // namespace hnswhy
