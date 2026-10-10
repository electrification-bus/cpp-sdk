// homie_format_float() against the float32 vectors shared with python-sdk and rust-sdk.

#include <unity.h>
#include <stdio.h>
#include <string.h>
#include <ebus/homie/homie_datatype.h>
#include "float32_vectors.h"

void setUp(void) {}
void tearDown(void) {}

static void test_every_float32_vector_formats_as_python_sdk_publishes_it(void) {
    char out[32];
    char msg[64];
    for (const Float32Vector& v : FLOAT32_VECTORS) {
        float value;
        memcpy(&value, &v.bits, sizeof(value));
        bool ok = homie_format_float(value, out, sizeof(out));
        snprintf(msg, sizeof(msg), "bits 0x%08lx", (unsigned long)v.bits);
        if (v.payload == nullptr) {
            TEST_ASSERT_FALSE_MESSAGE(ok, msg);
            TEST_ASSERT_EQUAL_STRING_MESSAGE("", out, msg);
        } else {
            TEST_ASSERT_TRUE_MESSAGE(ok, msg);
            TEST_ASSERT_EQUAL_STRING_MESSAGE(v.payload, out, msg);
        }
    }
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_every_float32_vector_formats_as_python_sdk_publishes_it);
    return UNITY_END();
}
