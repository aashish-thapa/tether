// Generated with hyprwayland-scanner 0.4.6. Made with vaxry's keyboard and ❤️.
// ext_idle_notify_v1

/*
 This protocol's authors' copyright notice is:


    Copyright © 2015 Martin Gräßlin
    Copyright © 2022 Simon Ser

    Permission is hereby granted, free of charge, to any person obtaining a
    copy of this software and associated documentation files (the "Software"),
    to deal in the Software without restriction, including without limitation
    the rights to use, copy, modify, merge, publish, distribute, sublicense,
    and/or sell copies of the Software, and to permit persons to whom the
    Software is furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice (including the next
    paragraph) shall be included in all copies or substantial portions of the
    Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
    THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
    FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
    DEALINGS IN THE SOFTWARE.
  
*/

#define private public
#define HYPRWAYLAND_SCANNER_NO_INTERFACES
#include "ext-idle-notify-v1.hpp"
#undef private
#define F std::function

static const wl_interface* extIdleNotifyV1_dummyTypes[] = { nullptr };

// Reference all other interfaces.
// The reason why this is in snake is to
// be able to cooperate with existing
// wayland_scanner interfaces (they are interop)
extern const wl_interface ext_idle_notifier_v1_interface;
extern const wl_interface ext_idle_notification_v1_interface;
extern const wl_interface wl_seat_interface;

static const void* _CCExtIdleNotifierV1VTable[] = {
    nullptr,
};

void CCExtIdleNotifierV1::sendDestroy() {
    if (!pResource)
        return;
    destroyed = true;

    auto proxy = wl_proxy_marshal_flags(pResource, 0, nullptr, wl_proxy_get_version(pResource), 1);
    proxy;
}

wl_proxy* CCExtIdleNotifierV1::sendGetIdleNotification(uint32_t timeout, wl_proxy* seat) {
    if (!pResource)
        return nullptr;

    auto proxy = wl_proxy_marshal_flags(pResource, 1, &ext_idle_notification_v1_interface, wl_proxy_get_version(pResource), 0, nullptr, timeout, seat);

    return proxy;
}

wl_proxy* CCExtIdleNotifierV1::sendGetInputIdleNotification(uint32_t timeout, wl_proxy* seat) {
    if (!pResource)
        return nullptr;

    auto proxy = wl_proxy_marshal_flags(pResource, 2, &ext_idle_notification_v1_interface, wl_proxy_get_version(pResource), 0, nullptr, timeout, seat);

    return proxy;
}
static const wl_interface* _CExtIdleNotifierV1GetIdleNotificationTypes[] = {
    &ext_idle_notification_v1_interface,
    nullptr,
    &wl_seat_interface,
};
static const wl_interface* _CExtIdleNotifierV1GetInputIdleNotificationTypes[] = {
    &ext_idle_notification_v1_interface,
    nullptr,
    &wl_seat_interface,
};

static const wl_message _CExtIdleNotifierV1Requests[] = {
    { .name = "destroy", .signature = "", .types = extIdleNotifyV1_dummyTypes + 0},
    { .name = "get_idle_notification", .signature = "nuo", .types = _CExtIdleNotifierV1GetIdleNotificationTypes + 0},
    { .name = "get_input_idle_notification", .signature = "2nuo", .types = _CExtIdleNotifierV1GetInputIdleNotificationTypes + 0},
};

const wl_interface ext_idle_notifier_v1_interface = {
    .name = "ext_idle_notifier_v1", .version = 2,
    .method_count = 3, .methods = _CExtIdleNotifierV1Requests,
    .event_count = 0, .events = nullptr,
};

CCExtIdleNotifierV1::CCExtIdleNotifierV1(wl_proxy* resource) : pResource(resource) {

    if (!pResource)
        return;

    wl_proxy_add_listener(pResource, (void (**)(void))&_CCExtIdleNotifierV1VTable, this);
}

CCExtIdleNotifierV1::~CCExtIdleNotifierV1() {
    if (!destroyed)
        sendDestroy();
}

static void _CExtIdleNotificationV1Idled(void* data, void* resource) {
    const auto PO = (CCExtIdleNotificationV1*)data;
    if (PO && PO->requests.idled)
        PO->requests.idled(PO);
}

static void _CExtIdleNotificationV1Resumed(void* data, void* resource) {
    const auto PO = (CCExtIdleNotificationV1*)data;
    if (PO && PO->requests.resumed)
        PO->requests.resumed(PO);
}

static const void* _CCExtIdleNotificationV1VTable[] = {
    (void*)_CExtIdleNotificationV1Idled,
    (void*)_CExtIdleNotificationV1Resumed,
};

void CCExtIdleNotificationV1::sendDestroy() {
    if (!pResource)
        return;
    destroyed = true;

    auto proxy = wl_proxy_marshal_flags(pResource, 0, nullptr, wl_proxy_get_version(pResource), 1);
    proxy;
}

static const wl_message _CExtIdleNotificationV1Requests[] = {
    { .name = "destroy", .signature = "", .types = extIdleNotifyV1_dummyTypes + 0},
};

static const wl_message _CExtIdleNotificationV1Events[] = {
    { .name = "idled", .signature = "", .types = extIdleNotifyV1_dummyTypes + 0},
    { .name = "resumed", .signature = "", .types = extIdleNotifyV1_dummyTypes + 0},
};

const wl_interface ext_idle_notification_v1_interface = {
    .name = "ext_idle_notification_v1", .version = 2,
    .method_count = 1, .methods = _CExtIdleNotificationV1Requests,
    .event_count = 2, .events = _CExtIdleNotificationV1Events,
};

CCExtIdleNotificationV1::CCExtIdleNotificationV1(wl_proxy* resource) : pResource(resource) {

    if (!pResource)
        return;

    wl_proxy_add_listener(pResource, (void (**)(void))&_CCExtIdleNotificationV1VTable, this);
}

CCExtIdleNotificationV1::~CCExtIdleNotificationV1() {
    if (!destroyed)
        sendDestroy();
}

void CCExtIdleNotificationV1::setIdled(F<void(CCExtIdleNotificationV1*)> &&handler) {
    requests.idled = std::move(handler);
}

void CCExtIdleNotificationV1::setResumed(F<void(CCExtIdleNotificationV1*)> &&handler) {
    requests.resumed = std::move(handler);
}

#undef F
