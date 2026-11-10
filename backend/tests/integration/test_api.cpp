#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include "api/routes.h"
#include "framework/temp_path.h"
#include "framework/test_framework.h"
#include "httplib.h"
#include "json.hpp"
#include "product/app.h"

namespace {

using json = nlohmann::json;
using hnswhytest::TempDir;
using hnswhytest::TempPath;

hnswhy::AppConfig api_config(const TempPath& db, const TempDir& dir) {
    hnswhy::AppConfig config;
    config.db_path = db.path();
    config.bench_report = dir.path() / "bench.json";
    config.static_dir = dir.path() / "web";
    config.samples_dir = dir.path() / "samples";
    config.load_sample = false;
    config.embedding.provider = "hash";
    config.embedding.dim = 256;
    config.chat.provider = "extractive";
    config.chunk_target_words = 40;
    config.chunk_overlap_words = 10;
    return config;
}

class ApiFixture {
public:
    explicit ApiFixture(hnswhy::AppConfig config) : app_(std::move(config)) {
        server_.set_payload_max_length(25U * 1024U * 1024U);
        hnswhy::register_routes(server_, app_);
        port_ = server_.bind_to_any_port("127.0.0.1");
        CHECK(port_ > 0);
        thread_ = std::thread([this] { server_.listen_after_bind(); });
        server_.wait_until_ready();
    }

    ~ApiFixture() {
        server_.stop();
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    ApiFixture(const ApiFixture&) = delete;
    ApiFixture& operator=(const ApiFixture&) = delete;

    httplib::Client client() const {
        httplib::Client client("127.0.0.1", port_);
        client.set_connection_timeout(5, 0);
        client.set_read_timeout(10, 0);
        return client;
    }

private:
    hnswhy::Application app_;
    httplib::Server server_;
    std::thread thread_;
    int port_ = 0;
};

json body_of(const httplib::Result& result) {
    CHECK(result);
    CHECK(!result->body.empty());
    return json::parse(result->body);
}

}  // namespace

HNSWHY_TEST(api_health_upload_ask_map_and_delete_flow) {
    TempPath db(".db");
    TempDir dir;
    ApiFixture fixture(api_config(db, dir));
    httplib::Client client = fixture.client();

    auto response = client.Get("/api/health");
    CHECK(response);
    CHECK_EQ(response->status, 200);
    json body = body_of(response);
    CHECK_EQ(body["status"], std::string("ok"));
    CHECK_EQ(body["database"], std::string("ok"));
    CHECK_EQ(body["embedding"]["provider"], std::string("hash"));
    CHECK_EQ(body["embedding"]["available"], true);
    CHECK_EQ(body["chat"]["provider"], std::string("extractive"));

    body = body_of(client.Get("/api/status"));
    CHECK_EQ(body["store"]["documents"], 0);
    CHECK_EQ(body["index"]["dim"], 0);
    CHECK_EQ(body["embedding"]["dim"], 256);
    CHECK_EQ(body["sample_loaded"], false);

    const json upload = {
        {"title", "API notes"},
        {"source", "api.md"},
        {"text",
         "HNSW is a layered graph for approximate nearest neighbor search. "
         "efSearch controls the candidate list size during a query. "
         "SQLite is the source of truth for documents and embeddings."},
    };
    response = client.Post("/api/documents", upload.dump(), "application/json");
    CHECK(response);
    CHECK_EQ(response->status, 201);
    body = body_of(response);
    const hnswhy::Id document_id = body["document"]["id"].get<hnswhy::Id>();
    CHECK(document_id > 0);
    CHECK_EQ(body["document"]["title"], std::string("API notes"));
    CHECK(body["document"]["chunk_count"].get<int>() >= 1);

    body = body_of(client.Get("/api/documents"));
    CHECK_EQ(body.size(), std::size_t{1});
    CHECK_EQ(body[0]["id"].get<hnswhy::Id>(), document_id);

    body = body_of(client.Get("/api/status"));
    CHECK_EQ(body["index"]["dim"], 256);
    CHECK_EQ(body["embedding"]["dim"], 256);
    CHECK_EQ(body["store"]["chunks"], body["index"]["live"]);

    body = body_of(client.Get("/api/documents/" + std::to_string(document_id)));
    CHECK_EQ(body["document"]["source"], std::string("api.md"));
    CHECK(body["text"].get<std::string>().find("efSearch") != std::string::npos);
    CHECK(body["chunks"].size() >= 1);

    const json ask = {{"question", "What does efSearch control?"}, {"k", 2}};
    response = client.Post("/api/ask", ask.dump(), "application/json");
    CHECK(response);
    CHECK_EQ(response->status, 200);
    body = body_of(response);
    CHECK(body["answer"].get<std::string>().find("efSearch") != std::string::npos);
    CHECK(body["sources"].size() >= 1);
    CHECK_EQ(body["query_vector"].size(), std::size_t{256});
    CHECK(body["timings_ms"].contains("total"));
    CHECK_EQ(body["models"]["embedding_provider"], std::string("hash"));

    body = body_of(client.Get("/api/map?limit=50"));
    CHECK_EQ(body["total_chunks"], body["sampled"]);
    CHECK(body["points"].size() >= 1);
    CHECK_EQ(body["points"][0]["vector"].size(), std::size_t{256});
    CHECK(body["points"][0].contains("preview"));

    body = body_of(client.Get("/api/benchmark"));
    CHECK_EQ(body["available"], false);
    CHECK(body.contains("command"));

    response = client.Delete("/api/documents/" + std::to_string(document_id));
    CHECK(response);
    CHECK_EQ(response->status, 200);
    CHECK_EQ(body_of(response)["deleted"], true);
    body = body_of(client.Get("/api/documents"));
    CHECK(body.empty());

    response = client.Delete("/api/documents/" + std::to_string(document_id));
    CHECK(response);
    CHECK_EQ(response->status, 404);
    CHECK_EQ(body_of(response)["error"]["code"], std::string("NOT_FOUND"));
}

HNSWHY_TEST(api_validates_bad_requests_and_unknown_documents) {
    TempPath db(".db");
    TempDir dir;
    ApiFixture fixture(api_config(db, dir));
    httplib::Client client = fixture.client();

    auto response = client.Post("/api/documents", "{not json", "application/json");
    CHECK(response);
    CHECK_EQ(response->status, 400);
    CHECK_EQ(body_of(response)["error"]["code"], std::string("INVALID_REQUEST"));

    response = client.Post("/api/documents", "{}", "application/json");
    CHECK_EQ(response->status, 400);

    response = client.Post("/api/ask", json{{"question", ""}}.dump(), "application/json");
    CHECK_EQ(response->status, 400);
    response = client.Post("/api/ask", json{{"question", "q"}, {"k", 0}}.dump(),
                           "application/json");
    CHECK_EQ(response->status, 400);
    response = client.Post("/api/ask", json{{"question", "q"}, {"k", 1.5}}.dump(),
                           "application/json");
    CHECK_EQ(response->status, 400);

    response = client.Get("/api/map?limit=0");
    CHECK_EQ(response->status, 400);
    response = client.Get("/api/map?limit=nope");
    CHECK_EQ(response->status, 400);

    response = client.Get("/api/documents/999");
    CHECK_EQ(response->status, 404);
    response = client.Get("/api/documents/not-a-number");
    CHECK_EQ(response->status, 404);
    response = client.Delete("/api/documents/0");
    CHECK_EQ(response->status, 400);

    response = client.Get("/api/does-not-exist");
    CHECK_EQ(response->status, 404);
    CHECK_EQ(body_of(response)["error"]["code"], std::string("HTTP_ERROR"));
}

HNSWHY_TEST(api_accepts_multipart_uploads) {
    TempPath db(".db");
    TempDir dir;
    ApiFixture fixture(api_config(db, dir));
    httplib::Client client = fixture.client();

    httplib::MultipartFormDataItems items = {
        {"file",
         "Markdown upload text about vector search and SQLite storage.",
         "notes.md",
         "text/markdown"},
        {"title", "Multipart title", "", ""},
    };
    auto response = client.Post("/api/documents", items);
    CHECK(response);
    CHECK_EQ(response->status, 201);
    const json body = body_of(response);
    const hnswhy::Id document_id = body["document"]["id"].get<hnswhy::Id>();
    CHECK_EQ(body["document"]["title"], std::string("Multipart title"));
    CHECK_EQ(body["document"]["source"], std::string("notes.md"));

    const json detail = body_of(client.Get("/api/documents/" + std::to_string(document_id)));
    CHECK(detail["text"].get<std::string>().find("Markdown upload") != std::string::npos);

    httplib::MultipartFormDataItems unsupported = {
        {"file", "PK\x03\x04", "archive.zip", "application/zip"},
    };
    response = client.Post("/api/documents", unsupported);
    CHECK(response);
    CHECK_EQ(response->status, 415);
    CHECK_EQ(body_of(response)["error"]["code"], std::string("UNSUPPORTED_FORMAT"));

    httplib::MultipartFormDataItems missing = {
        {"title", "No file here", "", ""},
    };
    response = client.Post("/api/documents", missing);
    CHECK_EQ(response->status, 400);
}

HNSWHY_TEST(api_serves_the_benchmark_report_when_present) {
    TempPath db(".db");
    TempDir dir;
    hnswhy::AppConfig config = api_config(db, dir);
    {
        std::ofstream report(config.bench_report);
        report << R"({"schema_version":1,"dataset":{"name":"sift100k"},"results":[]})";
    }
    ApiFixture fixture(config);
    httplib::Client client = fixture.client();

    const json body = body_of(client.Get("/api/benchmark"));
    CHECK_EQ(body["available"], true);
    CHECK_EQ(body["report"]["dataset"]["name"], std::string("sift100k"));
    CHECK(body["report"]["results"].is_array());
}
