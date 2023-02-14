/*
 * SPDX-FileCopyrightText: 2016 Mathieu Stefani
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
   Mathieu Stefani, 07 février 2016

   Example of a REST endpoint with routing
*/

#include <algorithm>
#include <iomanip>
#include <pistache/endpoint.h>
#include <pistache/http.h>
#include <pistache/router.h>

using namespace Pistache;

void printCookies(const Http::Request& req)
{
    auto cookies = req.cookies();
    std::cout << "Cookies: [" << std::endl;
    const std::string indent(4, ' ');
    for (const auto& c : cookies)
    {
        std::cout << indent << c.name << " = " << c.value << std::endl;
    }
    std::cout << "]" << std::endl;
}

namespace Generic
{

    void handleReady(const Rest::Request&, Http::ResponseWriter response)
    {
        response.send(Http::Code::Ok, "1");
    }

}

class StatsEndpoint
{
public:
    explicit StatsEndpoint(Address addr)
        : httpEndpoint(std::make_shared<Http::Endpoint>(addr))
    { }

    void init(size_t thr = 2, bool use_ssl = false)
    {
        auto opts = Http::Endpoint::options()
                        .threads(static_cast<int>(thr))
                        .flags(Pistache::Tcp::Options::ReuseAddr);
        httpEndpoint->init(opts);

        if (use_ssl) {
            std::string cert_file = "/home/jiankyu/workspace/pistache/certs/om.cert";
            std::string key_file = "/home/jiankyu/workspace/pistache/certs/om.key";
            httpEndpoint->useSSL(cert_file, key_file);
        }

        setupRoutes();
    }

    void start()
    {
        httpEndpoint->setHandler(router.handler());
        httpEndpoint->serve();
    }

private:
    // limit the max response size to 4MiB.
    static constexpr int limit = 4194304;
    static constexpr char content[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789\n";
    static constexpr int content_len = sizeof(content)/sizeof(char)-1;
    static constexpr char prefix[] = "[00000]";
    static constexpr int prefix_len = sizeof(prefix)/sizeof(char)-1;
    static constexpr int iter_len = prefix_len + content_len;

    void setupRoutes()
    {
        using namespace Rest;

        Routes::Post(router, "/record/:name/:value?", Routes::bind(&StatsEndpoint::doRecordMetric, this));
        Routes::Get(router, "/value/:name", Routes::bind(&StatsEndpoint::doGetMetric, this));
        Routes::Get(router, "/ready", Routes::bind(&Generic::handleReady));
        Routes::Get(router, "/auth", Routes::bind(&StatsEndpoint::doAuth, this));
        Routes::Get(router, "/echo/:value", Routes::bind(&StatsEndpoint::doEcho, this));
    }

    void doEcho(const Rest::Request& request, Http::ResponseWriter response) {
        auto int_size = request.param(":value").as<int>();

        int num_iterations = int_size / StatsEndpoint::iter_len;
        int num_padding = int_size % StatsEndpoint::iter_len;
        std::stringstream ss;

        for (int i = 1; i <= num_iterations; i++) {
            ss << "[" << std::setw(5) << std::setfill('0') << i << "]" << StatsEndpoint::content;
        }

        if (num_padding) {
            ss << std::setw(num_padding) << std::setfill('x') << "\n";
        }

        std::cout << "response size: " << ss.str().size() << std::endl;
        response.send(Pistache::Http::Code::Ok, ss.str());
    }

    void doRecordMetric(const Rest::Request& request, Http::ResponseWriter response)
    {
        auto name = request.param(":name").as<std::string>();

        Guard guard(metricsLock);
        auto it = std::find_if(metrics.begin(), metrics.end(), [&](const Metric& metric) {
            return metric.name() == name;
        });

        int val = 1;
        if (request.hasParam(":value"))
        {
            auto value = request.param(":value");
            val        = value.as<int>();
        }

        if (it == std::end(metrics))
        {
            metrics.emplace_back(std::move(name), val);
            response.send(Http::Code::Created, std::to_string(val));
        }
        else
        {
            auto& metric = *it;
            metric.incr(val);
            response.send(Http::Code::Ok, std::to_string(metric.value()));
        }
    }

    void doGetMetric(const Rest::Request& request, Http::ResponseWriter response)
    {
        auto name = request.param(":name").as<std::string>();

        Guard guard(metricsLock);
        auto it = std::find_if(metrics.begin(), metrics.end(), [&](const Metric& metric) {
            return metric.name() == name;
        });

        if (it == std::end(metrics))
        {
            response.send(Http::Code::Not_Found, "Metric does not exist");
        }
        else
        {
            const auto& metric = *it;
            response.send(Http::Code::Ok, std::to_string(metric.value()));
        }
    }

    void doAuth(const Rest::Request& request, Http::ResponseWriter response)
    {
        printCookies(request);
        response.cookies()
            .add(Http::Cookie("lang", "en-US"));
        response.send(Http::Code::Ok);
    }

    class Metric
    {
    public:
        explicit Metric(std::string name, int initialValue = 1)
            : name_(std::move(name))
            , value_(initialValue)
        { }

        int incr(int n = 1)
        {
            int old = value_;
            value_ += n;
            return old;
        }

        int value() const
        {
            return value_;
        }

        const std::string& name() const
        {
            return name_;
        }

    private:
        std::string name_;
        int value_;
    };

    using Lock  = std::mutex;
    using Guard = std::lock_guard<Lock>;
    Lock metricsLock;
    std::vector<Metric> metrics;

    std::shared_ptr<Http::Endpoint> httpEndpoint;
    Rest::Router router;
};

int main(int argc, char* argv[])
{
    Port port(9080);

    int thr = 2;
    bool use_ssl = false;

    if (argc >= 2)
    {
        port = static_cast<uint16_t>(std::stol(argv[1]));

        if (argc >= 3) {
            thr = std::stoi(argv[2]);

            if (argc == 4) {
                use_ssl = true;
            }
        }

    }

    Address addr(Ipv4::any(), port);

    std::cout << "Cores = " << hardware_concurrency() << std::endl;
    std::cout << "Using " << thr << " threads" << std::endl;
    if (use_ssl)
        std::cout << "Using SSL" << std::endl;

    StatsEndpoint stats(addr);

    stats.init(thr, use_ssl);
    stats.start();
}
