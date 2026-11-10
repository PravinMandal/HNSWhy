#pragma once

#include "httplib.h"
#include "product/app.h"

namespace hnswhy {

// Registers /api routes and mounts the built frontend when it exists.
// Call after setting the server payload limit.
void register_routes(httplib::Server& server, Application& app);

}  // namespace hnswhy
