#include <ebus/discovery/txt_records.h>
#include <ebus/discovery/mdns_strings.h>
#include <string.h>

static bool is_set(const char* s) {
  return s != nullptr && s[0] != '\0';
}

static char lower(char c) {
  return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

static bool key_equal(const char* a, const char* b) {
  while (*a != '\0' && lower(*a) == lower(*b)) {
    a++;
    b++;
  }
  return *a == '\0' && *b == '\0';
}

// RFC 6763 6.4: at least one printable US-ASCII character, not '='.
static bool key_valid(const char* key) {
  if (!is_set(key)) return false;
  for (const char* p = key; *p != '\0'; p++) {
    if (*p < 0x20 || *p > 0x7e || *p == '=') return false;
  }
  return true;
}

void txt_record_clear(TxtRecord* rec) {
  rec->count = 0;
}

const char* txt_find(const TxtPair* pairs, uint8_t count, const char* key) {
  if (pairs == nullptr || key == nullptr) return nullptr;
  for (uint8_t i = 0; i < count; i++) {
    if (pairs[i].key != nullptr && key_equal(pairs[i].key, key)) return pairs[i].value;
  }
  return nullptr;
}

TxtResult txt_record_add(TxtRecord* rec, const char* key, const char* value) {
  if (!key_valid(key) || value == nullptr) return TXT_INVALID;
  if (strlen(key) + 1 + strlen(value) > EBUS_TXT_STRING_MAX) return TXT_TOO_LONG;
  if (txt_find(rec->pairs, rec->count, key) != nullptr) return TXT_DUPLICATE;
  if (rec->count >= EBUS_TXT_MAX_PAIRS) return TXT_FULL;
  rec->pairs[rec->count].key = key;
  rec->pairs[rec->count].value = value;
  rec->count++;
  return TXT_OK;
}

size_t txt_record_wire_size(const TxtRecord* rec) {
  size_t total = 0;
  for (uint8_t i = 0; i < rec->count; i++) {
    total += 1 + strlen(rec->pairs[i].key) + 1 + strlen(rec->pairs[i].value);
  }
  return total;
}

// Indexed by the bit set; the order within each is the one framework.md lists them in.
static const char* const ROLE_STRINGS[8] = {
  "",
  MDNS_VAL_ROLE_DEVICE,
  MDNS_VAL_ROLE_CONTROLLER,
  MDNS_VAL_ROLE_DEVICE "," MDNS_VAL_ROLE_CONTROLLER,
  MDNS_VAL_ROLE_BROKER_HOST,
  MDNS_VAL_ROLE_DEVICE "," MDNS_VAL_ROLE_BROKER_HOST,
  MDNS_VAL_ROLE_CONTROLLER "," MDNS_VAL_ROLE_BROKER_HOST,
  MDNS_VAL_ROLE_DEVICE "," MDNS_VAL_ROLE_CONTROLLER "," MDNS_VAL_ROLE_BROKER_HOST,
};

static const char* const AUTH_STRINGS[8] = {
  "",
  MDNS_VAL_AUTH_PASSPHRASE,
  MDNS_VAL_AUTH_PRECONFIGURED,
  MDNS_VAL_AUTH_PASSPHRASE "," MDNS_VAL_AUTH_PRECONFIGURED,
  MDNS_VAL_AUTH_MTLS,
  MDNS_VAL_AUTH_PASSPHRASE "," MDNS_VAL_AUTH_MTLS,
  MDNS_VAL_AUTH_PRECONFIGURED "," MDNS_VAL_AUTH_MTLS,
  MDNS_VAL_AUTH_PASSPHRASE "," MDNS_VAL_AUTH_PRECONFIGURED "," MDNS_VAL_AUTH_MTLS,
};

const char* ebus_roles_string(uint8_t roles) {
  return roles < 8 ? ROLE_STRINGS[roles] : "";
}

const char* ebus_auth_methods_string(uint8_t methods) {
  return methods < 8 ? AUTH_STRINGS[methods] : "";
}

// One or more ids joined with commas, none of them empty.
static bool device_id_list_valid(const char* ids) {
  size_t run = 0;
  for (const char* p = ids; *p != '\0'; p++) {
    if (*p == ',') {
      if (run == 0) return false;
      run = 0;
    } else {
      run++;
    }
  }
  return run > 0;
}

static TxtResult check_device_id(const EbusIdentity* id) {
  if (!is_set(id->device_id)) return TXT_MISSING;
  return device_id_list_valid(id->device_id) ? TXT_OK : TXT_INVALID;
}

// Appends while every step succeeds; the first failure is kept and the record emptied.
struct Builder {
  TxtRecord* rec;
  TxtResult result;

  void put(const char* key, const char* value) {
    if (result == TXT_OK) result = txt_record_add(rec, key, value);
  }
  void put_if_set(const char* key, const char* value) {
    if (is_set(value)) put(key, value);
  }
  TxtResult finish() {
    if (result != TXT_OK) txt_record_clear(rec);
    return result;
  }
};

static Builder start(TxtRecord* rec, TxtResult first_check) {
  txt_record_clear(rec);
  return Builder{rec, first_check};
}

TxtResult txt_build_ebus(const EbusIdentity* id, TxtRecord* rec) {
  TxtResult check = check_device_id(id);
  if (check == TXT_OK && (id->roles == 0 || id->roles >= 8)) check = TXT_INVALID;
  if (check == TXT_OK && id->auth_methods >= 8) check = TXT_INVALID;
  Builder b = start(rec, check);
  b.put(MDNS_TXT_TXTVERS, MDNS_VAL_TXTVERS);
  b.put(MDNS_TXT_EBUS_VERSION, is_set(id->ebus_version) ? id->ebus_version : EBUS_SPEC_VERSION);
  b.put(MDNS_TXT_ROLES, ebus_roles_string(id->roles));
  b.put(MDNS_TXT_DEVICE_ID, id->device_id);
  b.put_if_set(MDNS_TXT_DEVICE_TYPE, id->device_type);
  b.put_if_set(MDNS_TXT_NAME, id->name);
  b.put_if_set(MDNS_TXT_MANUFACTURER, id->manufacturer);
  b.put_if_set(MDNS_TXT_MODEL, id->model);
  b.put_if_set(MDNS_TXT_FW_VERSION, id->fw_version);
  b.put_if_set(MDNS_TXT_REGISTER, id->register_path);
  b.put_if_set(MDNS_TXT_BROKER_CA, id->broker_ca);
  b.put_if_set(MDNS_TXT_AUTH_METHODS, ebus_auth_methods_string(id->auth_methods));
  if (is_set(id->homie_domain)) {
    b.put(MDNS_TXT_HOMIE_DOMAIN, id->homie_domain);
    b.put(MDNS_TXT_HOMIE_VERSION, is_set(id->homie_version) ? id->homie_version : "5");
    b.put_if_set(MDNS_TXT_HOMIE_ROLES,
                 ebus_roles_string(id->roles & (EBUS_ROLE_DEVICE | EBUS_ROLE_CONTROLLER)));
  }
  return b.finish();
}

TxtResult txt_build_device_info(const EbusIdentity* id, TxtRecord* rec) {
  TxtResult check = TXT_OK;
  if (!is_set(id->manufacturer) || !is_set(id->model) || !is_set(id->serial_number)) {
    check = TXT_MISSING;
  }
  Builder b = start(rec, check);
  b.put(MDNS_TXT_TXTVERS, MDNS_VAL_TXTVERS);
  b.put(MDNS_TXT_MANUFACTURER, id->manufacturer);
  b.put(MDNS_TXT_MODEL, id->model);
  b.put(MDNS_TXT_SERIAL_NUMBER, id->serial_number);
  b.put_if_set(MDNS_TXT_FW_VERSION, id->fw_version);
  b.put_if_set(MDNS_TXT_HW_VERSION, id->hw_version);
  b.put_if_set(MDNS_TXT_OS_VERSION, id->os_version);
  b.put_if_set(MDNS_TXT_MAC, id->mac);
  return b.finish();
}

TxtResult txt_build_http(const EbusIdentity* id, const EbusHttpInfo* http, TxtRecord* rec) {
  TxtResult check = check_device_id(id);
  if (check == TXT_OK && !is_set(http->version)) check = TXT_MISSING;
  Builder b = start(rec, check);
  b.put(MDNS_TXT_TXTVERS, MDNS_VAL_TXTVERS);
  b.put(MDNS_TXT_HTTP_PATH, is_set(http->path) ? http->path : MDNS_VAL_HTTP_PATH);
  b.put(MDNS_TXT_HTTP_VERSION, http->version);
  b.put(MDNS_TXT_DEVICE_ID, id->device_id);
  b.put_if_set(MDNS_TXT_DEVICE_TYPE, id->device_type);
  b.put_if_set(MDNS_TXT_HTTP_OPENAPI, http->openapi);
  return b.finish();
}

TxtResult txt_build_log(const EbusIdentity* id, TxtRecord* rec) {
  Builder b = start(rec, check_device_id(id));
  b.put(MDNS_TXT_TXTVERS, MDNS_VAL_TXTVERS);
  b.put(MDNS_TXT_DEVICE_ID, id->device_id);
  b.put(MDNS_TXT_LOG_KIND, MDNS_VAL_LOG_KIND);
  return b.finish();
}
