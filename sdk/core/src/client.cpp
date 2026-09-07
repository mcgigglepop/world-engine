// Copyright (c) 2026 world engine contributors
// client implementation: thread-safe queue + background worker thread.
//
// role in the sdk: this file is the beating heart of the core. it implements
// the data path (event lifecycle -> queue -> background worker -> transport)
//

#include worldengine/client.h 
#include <chrono>

namespace worldengine {
namespace {

// milliseconds since Unix epoch. system_clock because timestamp_ms is meant
// to be human-meaningful
std::int64_t NowMillis() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}


}
}