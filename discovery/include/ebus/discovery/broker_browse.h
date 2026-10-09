#pragma once
// Broker discovery over an MdnsBackend: browse the list's service types in priority order
// and take the first instance with a resolved address; later, resolve its host name again
// when the address stops answering (BROKER_RERESOLVE_AFTER_FAILURES). Retrying a browse
// that found nothing is the caller's, since it needs a clock.
#include <stdint.h>
#include <ebus/discovery/broker_discovery.h>
#include <ebus/discovery/mdns_backend.h>

struct BrokerFound {
  uint8_t service;                        // BrokerService it was found under
  char address[EBUS_MDNS_ADDRESS_MAX];    // where to connect
  char hostname[EBUS_MDNS_HOST_MAX];      // bare mDNS name, or "" when the SRV target was
                                          // empty or too long (then it is never re-resolved)
  uint16_t port;                          // SRV port, or the service's default port
};

// Browse each service type in list, in order, for up to timeout_ms each. Returns true with
// the first instance that has an address in out; false, with out cleared, when none has.
bool broker_browse(MdnsBackend* backend, const BrokerDiscoveryList* list, uint32_t timeout_ms,
                   BrokerFound* out);

enum BrokerReresolve {
  BROKER_RERESOLVE_UNCHANGED = 0,   // the host answered from the same address
  BROKER_RERESOLVE_MOVED,           // the host answered from a new address, now in found
  BROKER_RERESOLVE_FAILED           // no host name, or no answer; found is unchanged
};

// Resolve found->hostname again, one query.
BrokerReresolve broker_reresolve(MdnsBackend* backend, BrokerFound* found, uint32_t timeout_ms);
