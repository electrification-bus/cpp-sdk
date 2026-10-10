#pragma once
// Command-line flags the two demo programs share.
#include <ebus/link/link.h>
#include <memory>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

struct BrokerArgs {
    const char* host = "localhost";
    int port = 1883;
    const char* user = nullptr;
    const char* password = nullptr;   // or EBUS_MQTT_PASSWORD, which stays out of `ps`
    const char* domain = "ebus";
    unsigned reconnect_ms = 2000;
    bool quiet = false;
};

static const char BROKER_USAGE[] =
    "  --host HOST          broker host (default localhost)\n"
    "  --port PORT          broker port (default 1883)\n"
    "  --user USER          broker username (default anonymous)\n"
    "  --password PASS      broker password (or set EBUS_MQTT_PASSWORD)\n"
    "  --domain DOMAIN      Homie topic domain (default ebus)\n"
    "  --reconnect-ms MS    reconnect interval (default 2000)\n"
    "  --quiet              no core or transport log on stderr\n";

// "--name VALUE" or "--name=VALUE". Advances *i past the value. Null when argv[*i] is not
// --name; exits when the value is missing.
inline const char* flag_value(int argc, char** argv, int* i, const char* name) {
    const char* a = argv[*i];
    size_t n = strlen(name);
    if (strncmp(a, name, n) != 0) return nullptr;
    if (a[n] == '=') return a + n + 1;
    if (a[n] != '\0') return nullptr;
    if (*i + 1 >= argc) {
        fprintf(stderr, "%s needs a value\n", name);
        exit(2);
    }
    return argv[++*i];
}

// Consumes argv[*i] (and its value) if it is a broker flag; returns whether it was.
inline bool parse_broker_flag(int argc, char** argv, int* i, BrokerArgs* b) {
    const char* v;
    if ((v = flag_value(argc, argv, i, "--host"))) { b->host = v; return true; }
    if ((v = flag_value(argc, argv, i, "--port"))) { b->port = atoi(v); return true; }
    if ((v = flag_value(argc, argv, i, "--user"))) { b->user = v; return true; }
    if ((v = flag_value(argc, argv, i, "--password"))) { b->password = v; return true; }
    if ((v = flag_value(argc, argv, i, "--domain"))) { b->domain = v; return true; }
    if ((v = flag_value(argc, argv, i, "--reconnect-ms"))) {
        b->reconnect_ms = (unsigned)atoi(v);
        return true;
    }
    if (strcmp(argv[*i], "--quiet") == 0) { b->quiet = true; return true; }
    return false;
}

inline void broker_args_finish(BrokerArgs* b) {
    if (!b->password) b->password = getenv("EBUS_MQTT_PASSWORD");
    if (b->port <= 0 || b->port > 65535) {
        fprintf(stderr, "--port must be 1-65535\n");
        exit(2);
    }
}

// --link SOURCES=>TARGET, repeatable, and the options that apply to the --link before them.
// Each becomes one EbusLink (ebus/link/link.h, doc/link.md).
struct LinkArg {
    std::string id, source, target, format = "%s";
    int decimals = -1;
    unsigned interval_ms = 1000;
    std::unique_ptr<EbusLink> link;
};

static const char LINK_USAGE[] =
    "  --link SRC=>TARGET   copy up to 3 comma-separated SRC properties into TARGET (repeatable;\n"
    "                       see doc/link.md)\n"
    "  --link-format FMT    text for the last --link: %1..%3, %s, %% (default %s)\n"
    "  --link-decimals N    round numeric values for the last --link (default -1, as is)\n"
    "  --link-interval-ms MS  poll interval for the last --link (default 1000)\n";

// Consumes argv[*i] (and its value) if it is a link flag; returns whether it was. Exits on
// a malformed one.
inline bool parse_link_flag(int argc, char** argv, int* i,
                            std::vector<std::unique_ptr<LinkArg>>* links) {
    const char* v;
    if ((v = flag_value(argc, argv, i, "--link"))) {
        const char* arrow = strstr(v, "=>");
        if (!arrow || arrow == v || !arrow[2]) {
            fprintf(stderr, "--link wants SOURCES=>TARGET, got '%s'\n", v);
            exit(2);
        }
        std::unique_ptr<LinkArg> l(new LinkArg());
        l->source.assign(v, (size_t)(arrow - v));
        l->target = arrow + 2;
        l->id = "link-" + std::to_string(links->size() + 1);
        links->push_back(std::move(l));
        return true;
    }
    bool fmt = false, dec = false, ivl = false;
    if ((v = flag_value(argc, argv, i, "--link-format"))) fmt = true;
    else if ((v = flag_value(argc, argv, i, "--link-decimals"))) dec = true;
    else if ((v = flag_value(argc, argv, i, "--link-interval-ms"))) ivl = true;
    else return false;
    if (links->empty()) {
        fprintf(stderr, "--link-format, --link-decimals and --link-interval-ms apply to the "
                        "--link before them, and there is none\n");
        exit(2);
    }
    LinkArg& l = *links->back();
    if (fmt) l.format = v;
    if (dec) l.decimals = atoi(v);
    if (ivl) l.interval_ms = (unsigned)atoi(v);
    return true;
}

// Set up every link; returns false if one is disabled (the core logged why).
inline bool setup_links(std::vector<std::unique_ptr<LinkArg>>& links, EbusLink::Mode mode,
                        Device* root) {
    bool ok = true;
    for (auto& l : links) {
        l->link.reset(new EbusLink(l->id.c_str(), l->source.c_str(), l->target.c_str(),
                                   l->format.c_str(), l->decimals, l->interval_ms));
        if (!l->link->setup(mode, root)) {
            fprintf(stderr, "%s (%s=>%s) is disabled\n", l->id.c_str(), l->source.c_str(),
                    l->target.c_str());
            ok = false;
        }
    }
    return ok;
}
