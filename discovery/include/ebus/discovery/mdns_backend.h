#pragma once
// What eBus needs from a port's mDNS stack: advertise a service, browse a service type,
// resolve a host name. A port implements it over its stack (ESPmDNS on ESP32; Avahi or a
// portable mDNS library on POSIX). The host name claim belongs to the stack's own setup,
// since every stack ties it to initialization, so it is not here.
#include <stddef.h>
#include <stdint.h>
#include <ebus/discovery/txt_records.h>

#define EBUS_MDNS_HOST_MAX 64       // a host name with its terminator
#define EBUS_MDNS_ADDRESS_MAX 46    // an IPv6 address as text with its terminator

// One instance a browse found. The strings and the TXT pairs belong to the backend and are
// valid only during the callback.
struct MdnsInstance {
  const char* instance;   // instance name, or ""
  const char* host;       // SRV target as received ("broker-1.local" or "broker-1.local.")
  const char* address;    // the target's resolved IPv4 or IPv6 address as text, or ""
  uint16_t port;          // SRV port, or 0 when there is none
  const TxtPair* txt;
  uint8_t txt_count;
};

// Called once per instance; return false to end the browse.
typedef bool (*mdns_instance_fn)(void* ctx, const MdnsInstance* instance);

class MdnsBackend {
 public:
  // Register one service type ("_ebus._tcp") on port under the host name the port claimed,
  // with that name as the instance name. The stack copies the pairs. Returns false when
  // the stack refuses the service.
  virtual bool advertise(const char* service_type, uint16_t port, const TxtPair* txt,
                         uint8_t txt_count) = 0;

  // Query service_type ("_secure-mqtt._tcp") for up to timeout_ms and pass each instance
  // found to fn, until fn returns false. Returns the number passed to fn, or -1 when the
  // query could not be sent. Blocks for up to timeout_ms.
  virtual int browse(const char* service_type, uint32_t timeout_ms, mdns_instance_fn fn,
                     void* ctx) = 0;

  // Resolve a bare mDNS host name ("broker-1", without ".local") to an address, written
  // as text into address. Returns false when no host answers within timeout_ms.
  virtual bool resolve(const char* hostname, uint32_t timeout_ms, char* address,
                       size_t address_size) = 0;

 protected:
  ~MdnsBackend() {}
};
