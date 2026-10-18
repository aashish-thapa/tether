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

#pragma once

#include <functional>
#include <cstdint>
#include <string>
#include <wayland-client.h>

#define F std::function

struct wl_proxy;


class CCExtIdleNotifierV1;
class CCExtIdleNotificationV1;
class CCWlSeat;
class CCExtIdleNotificationV1;
class CCWlSeat;
class CCExtIdleNotificationV1;

#ifndef HYPRWAYLAND_SCANNER_NO_INTERFACES
extern const wl_interface ext_idle_notifier_v1_interface;
extern const wl_interface ext_idle_notification_v1_interface;

#endif


class CCExtIdleNotifierV1 {
  public:
    CCExtIdleNotifierV1(wl_proxy*);
    ~CCExtIdleNotifierV1();


    // set the data for this resource
    void setData(void* data) {{
        pData = data;
    }}

    // get the data for this resource
    void* data() {{
        return pData;
    }}

    // get the raw wl_resource (wl_proxy) ptr
    wl_proxy* resource() {{
        return pResource;
    }}

    // get the raw wl_proxy ptr
    wl_proxy* proxy() {{
        return pResource;
    }}

    // get the resource version
    int version() {{
        return wl_proxy_get_version(pResource);
    }}
            
    // --------------- Requests --------------- //


    // --------------- Events --------------- //

    void sendDestroy();
    wl_proxy* sendGetIdleNotification(uint32_t, wl_proxy*);
    wl_proxy* sendGetInputIdleNotification(uint32_t, wl_proxy*);

  private:
    struct {
    } requests;

    wl_proxy* pResource = nullptr;

    bool destroyed = false;

    void* pData = nullptr;
};



class CCExtIdleNotificationV1 {
  public:
    CCExtIdleNotificationV1(wl_proxy*);
    ~CCExtIdleNotificationV1();


    // set the data for this resource
    void setData(void* data) {{
        pData = data;
    }}

    // get the data for this resource
    void* data() {{
        return pData;
    }}

    // get the raw wl_resource (wl_proxy) ptr
    wl_proxy* resource() {{
        return pResource;
    }}

    // get the raw wl_proxy ptr
    wl_proxy* proxy() {{
        return pResource;
    }}

    // get the resource version
    int version() {{
        return wl_proxy_get_version(pResource);
    }}
            
    // --------------- Requests --------------- //

    void setIdled(F<void(CCExtIdleNotificationV1*)> &&handler);
    void setResumed(F<void(CCExtIdleNotificationV1*)> &&handler);

    // --------------- Events --------------- //

    void sendDestroy();

  private:
    struct {
        F<void(CCExtIdleNotificationV1*)> idled;
        F<void(CCExtIdleNotificationV1*)> resumed;
    } requests;

    wl_proxy* pResource = nullptr;

    bool destroyed = false;

    void* pData = nullptr;
};



#undef F
