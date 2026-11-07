// Firmir networking module example: fetches a URL over HTTP(S).
// Usage: firmir-example-networking-fetch <url>

#include <firmir/networkaccessmanager.hpp>
#include <firmir/networkreply.hpp>

#include <chrono>
#include <cstdio>
#include <thread>

using namespace Firmir;

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <url>\n", argv[0]);
        return 1;
    }

    NetworkAccessManager manager;
    NetworkRequest request(argv[1]);
    request.setHeader("User-Agent", "firmir-example/1.0");

    NetworkReply* reply = manager.get(request);
    while (!reply->isFinished())
        std::this_thread::sleep_for(std::chrono::milliseconds(20));

    if (reply->error() != NetworkReply::Error::NoError) {
        std::fprintf(stderr, "error: %s\n",
                     reply->errorString().toStdString().c_str());
        return 2;
    }

    std::printf("HTTP %d\n", reply->httpStatus());
    for (const auto& header : reply->headers())
        std::printf("%s: %s\n", header.first.toStdString().c_str(),
                    header.second.toStdString().c_str());
    std::printf("\n%s\n", reply->readAll().toStdString().c_str());
    return 0;
}
