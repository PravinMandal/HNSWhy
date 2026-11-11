#include <sys/stat.h>

#include <exception>
#include <iostream>

#include "api/routes.h"
#include "httplib.h"
#include "product/app.h"
#include "product/config.h"
#include "product/errors.h"

int main(int argc, char** argv) {
    using namespace hnswhy;

    // The server stores uploaded documents and embeddings. Make every file it
    // creates owner-only by default (SQLite WAL/SHM, temporary PDFs, logs),
    // not just the database file that DocumentStore chmods after opening.
    ::umask(0077);

    try {
        const AppConfig config = load_config(argc, argv);
        if (config.show_help) {
            std::cout << usage();
            return 0;
        }

        httplib::Server server;
        server.set_payload_max_length(25U * 1024U * 1024U);

        // Bind before the expensive startup work so a busy port fails fast
        // instead of after opening SQLite and rebuilding the vector index.
        if (!server.bind_to_port(config.host, config.port)) {
            std::cerr << "[hnswhy] failed to bind " << config.host << ':' << config.port
                      << '\n';
            return 1;
        }

        Application app(config);
        register_routes(server, app);
        std::cout << "[hnswhy] listening on http://" << config.host << ':' << config.port
                  << '\n';
        if (!server.listen_after_bind()) {
            std::cerr << "[hnswhy] server stopped unexpectedly\n";
            return 1;
        }
        return 0;
    } catch (const ProductError& error) {
        std::cerr << "[hnswhy] " << error.code() << ": " << error.what() << '\n';
        return 1;
    } catch (const std::exception& error) {
        std::cerr << "[hnswhy] fatal: " << error.what() << '\n';
        return 1;
    }
}
