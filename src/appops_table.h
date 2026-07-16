#pragma once

#include <string_view>

namespace appops {

struct OpInfo {
    std::string_view opstr;   // stable string id, e.g. "android:camera" —
                              // what actually gets written into rules.json
    std::string_view label;  // short human label for the picker UI
    std::string_view category;
};

// Sourced from AOSP frameworks/base core/java/android/app/AppOpsManager.java
// (OPSTR_* constants). This list is intentionally *string*-keyed only — no
// numeric op codes are embedded anywhere in yay, since AOSP itself documents
// those integers as unstable across platform versions and OEM builds (see
// frameworks/base's AppOps.md: "the integer values ... are not part of the
// API, they might (and have) changed"). `cmd appops` accepts the string form
// directly, and rules_engine.cpp already treats a string `op` the same as an
// integer one.
inline constexpr OpInfo kKnownOps[] = {
    // ── Location ──────────────────────────────────────────────────────
    {"android:coarse_location",            "Coarse location",              "Location"},
    {"android:fine_location",              "Fine location",                "Location"},
    {"android:gps",                        "GPS",                          "Location"},
    {"android:monitor_location",           "Monitor location",             "Location"},
    {"android:monitor_location_high_power","Monitor location (high power)","Location"},
    {"android:mock_location",              "Mock location",                "Location"},

    // ── Camera / Microphone ───────────────────────────────────────────
    {"android:camera",                     "Camera",                       "Camera & Mic"},
    {"android:record_audio",               "Record audio",                 "Camera & Mic"},
    {"android:mute_microphone",            "Mute microphone",              "Camera & Mic"},
    {"android:phone_call_microphone",      "Microphone during phone call", "Camera & Mic"},
    {"android:phone_call_camera",          "Camera during phone call",     "Camera & Mic"},
    {"android:record_audio_hotword",       "Hotword audio recording",      "Camera & Mic"},

    // ── Contacts / Calendar / Calls ───────────────────────────────────
    {"android:read_contacts",              "Read contacts",                "Contacts & Calls"},
    {"android:write_contacts",             "Write contacts",               "Contacts & Calls"},
    {"android:read_call_log",              "Read call log",                "Contacts & Calls"},
    {"android:write_call_log",             "Write call log",               "Contacts & Calls"},
    {"android:read_calendar",              "Read calendar",                "Contacts & Calls"},
    {"android:write_calendar",             "Write calendar",               "Contacts & Calls"},
    {"android:call_phone",                 "Call phone",                   "Contacts & Calls"},
    {"android:answer_phone_calls",         "Answer phone calls",           "Contacts & Calls"},
    {"android:accept_handover",            "Accept call handover",         "Contacts & Calls"},
    {"android:process_outgoing_calls",     "Process outgoing calls",       "Contacts & Calls"},
    {"android:add_voicemail",              "Add voicemail",                "Contacts & Calls"},
    {"android:use_sip",                    "Use SIP",                      "Contacts & Calls"},
    {"android:get_accounts",               "Get accounts",                 "Contacts & Calls"},

    // ── SMS / MMS ──────────────────────────────────────────────────────
    {"android:read_sms",                   "Read SMS",                     "SMS & MMS"},
    {"android:write_sms",                  "Write SMS",                    "SMS & MMS"},
    {"android:receive_sms",                "Receive SMS",                  "SMS & MMS"},
    {"android:send_sms",                   "Send SMS",                     "SMS & MMS"},
    {"android:receive_mms",                "Receive MMS",                  "SMS & MMS"},
    {"android:receive_wap_push",           "Receive WAP push",             "SMS & MMS"},
    {"android:read_icc_sms",               "Read ICC SMS",                 "SMS & MMS"},
    {"android:write_icc_sms",              "Write ICC SMS",                "SMS & MMS"},
    {"android:receive_emergency_broadcast","Receive emergency broadcast",  "SMS & MMS"},

    // ── Storage ────────────────────────────────────────────────────────
    {"android:read_external_storage",      "Read external storage",       "Storage"},
    {"android:write_external_storage",     "Write external storage",      "Storage"},

    // ── Phone / Device state ──────────────────────────────────────────
    {"android:read_phone_state",           "Read phone state",            "Phone & Device"},
    {"android:read_phone_numbers",         "Read phone numbers",          "Phone & Device"},
    {"android:use_fingerprint",            "Use fingerprint",             "Phone & Device"},
    {"android:body_sensors",               "Body sensors",                "Phone & Device"},
    {"android:read_cell_broadcasts",       "Read cell broadcasts",        "Phone & Device"},
    {"android:neighboring_cells",          "Neighboring cells",           "Phone & Device"},
    {"android:turn_screen_on",             "Turn screen on",              "Phone & Device"},
    {"android:vibrate",                    "Vibrate",                     "Phone & Device"},

    // ── Network ────────────────────────────────────────────────────────
    {"android:wifi_scan",                  "Wi-Fi scan",                  "Network"},
    {"android:change_wifi_state",          "Change Wi-Fi state",          "Network"},
    {"android:bluetooth_scan",             "Bluetooth scan",              "Network"},
    {"android:activate_vpn",               "Activate VPN",                "Network"},

    // ── Usage stats / background ──────────────────────────────────────
    {"android:get_usage_stats",            "Get usage stats",             "Usage & Background"},
    {"android:run_in_background",          "Run in background",          "Usage & Background"},
    {"android:run_any_in_background",      "Run in background (any)",    "Usage & Background"},
    {"android:instant_app_start_foreground","Instant app start foreground","Usage & Background"},
    {"android:start_foreground",           "Start foreground service",   "Usage & Background"},

    // ── UI / windows ───────────────────────────────────────────────────
    {"android:system_alert_window",        "Draw over other apps",        "UI & Windows"},
    {"android:toast_window",               "Show toast windows",          "UI & Windows"},
    {"android:post_notification",          "Post notifications",          "UI & Windows"},
    {"android:access_notifications",       "Access notifications",        "UI & Windows"},
    {"android:picture_in_picture",         "Picture-in-picture",          "UI & Windows"},
    {"android:write_wallpaper",            "Write wallpaper",             "UI & Windows"},

    // ── Audio volume streams ──────────────────────────────────────────
    {"android:audio_master_volume",        "Audio: master volume",        "Audio Volume"},
    {"android:audio_voice_volume",         "Audio: voice volume",         "Audio Volume"},
    {"android:audio_ring_volume",          "Audio: ring volume",          "Audio Volume"},
    {"android:audio_media_volume",         "Audio: media volume",         "Audio Volume"},
    {"android:audio_alarm_volume",         "Audio: alarm volume",         "Audio Volume"},
    {"android:audio_notification_volume",  "Audio: notification volume",  "Audio Volume"},
    {"android:audio_bluetooth_volume",     "Audio: bluetooth volume",     "Audio Volume"},
    {"android:audio_accessibility_volume", "Audio: accessibility volume", "Audio Volume"},
    {"android:play_audio",                 "Play audio",                  "Audio Volume"},
    {"android:take_audio_focus",           "Take audio focus",            "Audio Volume"},
    {"android:take_media_buttons",         "Take media buttons",          "Audio Volume"},

    // ── Misc / system ──────────────────────────────────────────────────
    {"android:write_settings",             "Write settings",              "System"},
    {"android:request_install_packages",   "Request install packages",    "System"},
    {"android:request_delete_packages",    "Request delete packages",     "System"},
    {"android:bind_accessibility_service", "Bind accessibility service",  "System"},
    {"android:manage_ipsec_tunnels",       "Manage IPsec tunnels",        "System"},
    {"android:wake_lock",                  "Wake lock",                   "System"},
    {"android:project_media",              "Project media (screen record/cast)", "System"},
    {"android:assist_structure",           "Assist structure",            "System"},
    {"android:assist_screenshot",          "Assist screenshot",           "System"},
    {"android:read_clipboard",             "Read clipboard",              "System"},
    {"android:write_clipboard",            "Write clipboard",             "System"},
};

inline constexpr size_t kKnownOpsCount = sizeof(kKnownOps) / sizeof(kKnownOps[0]);

} // namespace appops
