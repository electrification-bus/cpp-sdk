#pragma once
// Moved to ebus_homie; this path keeps existing includes working.
#include <ebus/homie/homie_limits.h>

// esp32-sdk's ./ebus-esp32 generate reads these four values from this path, so they are
// repeated here. A repeated #define must match the original token for token or the
// compiler warns that it is redefined (an error in CI), so a change to
// ebus/homie/homie_limits.h that is not made here too fails the build.
#define HOMIE_DEVICE_ID_MAX    63
#define HOMIE_NODE_ID_MAX      31
#define HOMIE_PROPERTY_ID_MAX  31
#define HOMIE_TOPIC_MAX        127
