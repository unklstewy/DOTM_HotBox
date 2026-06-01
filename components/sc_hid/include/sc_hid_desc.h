/*
 * sc_hid_desc.h — Descriptor table declarations for sc_hid
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include "tusb.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Descriptor accessors used by sc_hid.c when configuring tinyusb_driver_install(). */
const tusb_desc_device_t *sc_hid_get_device_descriptor(void);
const uint8_t *sc_hid_get_configuration_descriptor(void);
const uint8_t *sc_hid_get_hs_configuration_descriptor(void);
const tusb_desc_device_qualifier_t *sc_hid_get_qualifier_descriptor(void);
const char **sc_hid_get_string_descriptors(void);
int sc_hid_get_string_descriptor_count(void);

#ifdef __cplusplus
}
#endif
