#include <exception>
#include <iostream>

#include "api/routes.h"
#include "httplib.h"
#include "product/app.h"
#include "product/config.h"
#include "product/errors.h"

int main(int argc, char** argv) {
    using namespace hnswhy;

    try {
        const AppConfig config = load_config(argc, argv);
        if (config.show_help) {
            std::cout << usage();
            return 0;
        }

        Application app(config);
        httplib::Server server;
        server.set_payload_max_length(25U * 1024U * 1024U);
        register_routes(server, app);

        std::cout << "[hnswhy] listening on http://" << config.host << ':' << config.port
                  << '\n';
        if (!server.listen(config.host, config.port)) {
            std::cerr << "[hnswhy] failed to bind " << config.host << ':' << config.port
                      << '\n';
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
