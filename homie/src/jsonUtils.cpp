#include <ebus/homie/jsonUtils.h>

bool jsonExists(JsonVariant variant) {
    return (variant != nullptr && !variant.isNull());
}