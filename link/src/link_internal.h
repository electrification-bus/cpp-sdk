#pragma once
// Shared by link.cpp and link_controller.cpp; not part of the public API.
#include <ebus/homie/Device.h>
#include <ebus/homie/Node.h>
#include <ebus/homie/Property.h>
#include <ebus/link/link.h>
#include <string.h>

// Device::getNode() and Node::getProperty() log every miss; a link looks every interval.
static inline Property* link_find_in_node(Node* n, const char* prop_id) {
    for (int j = 0; j < n->numProperties(); j++) {
        Property* p = n->propertyAt(j);
        if (p && strcmp(p->id(), prop_id) == 0) return p;
    }
    return nullptr;
}

static inline Property* link_find_property(Device* d, const char* node_id, const char* prop_id) {
    if (!d) return nullptr;
    for (int i = 0; i < d->numNodes(); i++) {
        Node* n = d->nodeAt(i);
        if (n && strcmp(n->id(), node_id) == 0) return link_find_in_node(n, prop_id);
    }
    return nullptr;
}

// Copy a source value in, keeping the last good one when there is none. A controller's
// copy keeps the last text in value() after a retraction and clears has_value(); a local
// property's has_value() is false until its first reading.
static inline void link_take_value(EbusLinkSource& s, Property* prop) {
    if (ebus_link_take_value(s.value, sizeof(s.value), prop->value(), prop->has_value())) {
        s.has_value = true;
    }
}
