#include <ebus/discovery/broker_browse.h>
#include <string.h>

static void clear(BrokerFound* out) {
  memset(out, 0, sizeof(*out));
}

namespace {
struct BrowseState {
  uint8_t service;
  BrokerFound* out;
  bool found;
};
}  // namespace

static bool on_instance(void* ctx, const MdnsInstance* inst) {
  BrowseState* st = (BrowseState*)ctx;
  if (inst->address == nullptr || inst->address[0] == '\0') return true;   // unresolved
  size_t len = strlen(inst->address);
  if (len >= sizeof(st->out->address)) return true;
  memcpy(st->out->address, inst->address, len + 1);
  st->out->service = st->service;
  st->out->port = inst->port > 0 ? inst->port : broker_service_default_port(st->service);
  broker_copy_hostname(inst->host, st->out->hostname, sizeof(st->out->hostname));
  st->found = true;
  return false;
}

bool broker_browse(MdnsBackend* backend, const BrokerDiscoveryList* list, uint32_t timeout_ms,
                   BrokerFound* out) {
  clear(out);
  for (uint8_t k = 0; k < list->count; k++) {
    BrowseState st = {list->services[k], out, false};
    backend->browse(broker_service_type(st.service), timeout_ms, on_instance, &st);
    if (st.found) return true;
  }
  clear(out);
  return false;
}

BrokerReresolve broker_reresolve(MdnsBackend* backend, BrokerFound* found, uint32_t timeout_ms) {
  if (found->hostname[0] == '\0') return BROKER_RERESOLVE_FAILED;
  char address[EBUS_MDNS_ADDRESS_MAX];
  address[0] = '\0';
  if (!backend->resolve(found->hostname, timeout_ms, address, sizeof(address)) ||
      address[0] == '\0') {
    return BROKER_RERESOLVE_FAILED;
  }
  address[sizeof(address) - 1] = '\0';
  if (strcmp(address, found->address) == 0) return BROKER_RERESOLVE_UNCHANGED;
  memcpy(found->address, address, sizeof(address));
  return BROKER_RERESOLVE_MOVED;
}
