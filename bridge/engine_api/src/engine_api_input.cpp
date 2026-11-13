#include "engine_api_internal.h"

#if defined(ENGINE_API_USE_KRKR2_RUNTIME)

namespace engine_api {

std::mutex g_input_queue_mutex;
std::deque<engine_input_event_t> g_input_queue;
std::atomic<engine_handle_t> g_input_target_handle{nullptr};

bool IsFinitePointerValue(double value) { return std::isfinite(value); }

engine_result_t DispatchInputEventNow(engine_handle_s * /*impl*/,
                                      const engine_input_event_t &event,
                                      const char **out_error_message) {
    auto *loop = EngineLoop::GetInstance();
    if (loop == nullptr) {
        if (out_error_message) *out_error_message = "engine loop is unavailable";
        return ENGINE_RESULT_INVALID_STATE;
    }
    EngineInputEvent core_event;
    core_event.type = event.type;
    core_event.x = event.x;
    core_event.y = event.y;
    core_event.delta_x = event.delta_x;
    core_event.delta_y = event.delta_y;
    core_event.pointer_id = event.pointer_id;
    core_event.button = event.button;
    core_event.key_code = event.key_code;
    core_event.modifiers = event.modifiers;
    core_event.unicode_codepoint = event.unicode_codepoint;

    if (!loop->HandleInputEvent(core_event)) {
        if (out_error_message) *out_error_message = "input event dispatch failed (no active window?)";
        return ENGINE_RESULT_INVALID_STATE;
    }
    if (out_error_message) *out_error_message = nullptr;
    return ENGINE_RESULT_OK;
}

void EnqueueInputEvent(const engine_input_event_t &event) {
    std::lock_guard<std::mutex> lk(g_input_queue_mutex);
    g_input_queue.push_back(event);
    if (g_input_queue.size() > kMaxQueuedInputs) g_input_queue.pop_front();
}

std::deque<engine_input_event_t> TakeQueuedInputEvents() {
    std::deque<engine_input_event_t> out;
    std::lock_guard<std::mutex> lk(g_input_queue_mutex);
    out.swap(g_input_queue);
    return out;
}

void DropQueuedInputEvents() { TakeQueuedInputEvents(); }

engine_result_t DrainQueuedInputEvents(const char **out_error_message) {
    std::deque<engine_input_event_t> queued = TakeQueuedInputEvents();
    for (const auto &queued_event : queued) {
        const char *dispatch_error = nullptr;
        const engine_result_t rc = DispatchInputEventNow(nullptr, queued_event, &dispatch_error);
        if (rc != ENGINE_RESULT_OK) {
            if (out_error_message) *out_error_message = dispatch_error;
            return rc;
        }
    }
    if (out_error_message) *out_error_message = nullptr;
    return ENGINE_RESULT_OK;
}

} // namespace engine_api

extern "C" {

engine_result_t engine_send_input(engine_handle_t handle, const engine_input_event_t *event) {
    using namespace engine_api;
    if (event == nullptr) {
        return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT, "event is null");
    }
    if (event->struct_size < sizeof(engine_input_event_t)) {
        return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                       "engine_input_event_t.struct_size is too small");
    }
    if (handle == nullptr ||
        handle != g_input_target_handle.load(std::memory_order_acquire)) {
        return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_STATE,
                                       "engine handle is invalid or not the active runtime");
    }

    switch (event->type) {
        case ENGINE_INPUT_EVENT_POINTER_DOWN:
        case ENGINE_INPUT_EVENT_POINTER_MOVE:
        case ENGINE_INPUT_EVENT_POINTER_UP:
        case ENGINE_INPUT_EVENT_POINTER_SCROLL:
        case ENGINE_INPUT_EVENT_KEY_DOWN:
        case ENGINE_INPUT_EVENT_KEY_UP:
        case ENGINE_INPUT_EVENT_TEXT_INPUT:
        case ENGINE_INPUT_EVENT_BACK:
            break;
        default:
            return SetThreadErrorAndReturn(ENGINE_RESULT_NOT_SUPPORTED,
                                           "unsupported input event type");
    }

    if (event->type == ENGINE_INPUT_EVENT_POINTER_DOWN ||
        event->type == ENGINE_INPUT_EVENT_POINTER_MOVE ||
        event->type == ENGINE_INPUT_EVENT_POINTER_UP ||
        event->type == ENGINE_INPUT_EVENT_POINTER_SCROLL) {
        if (!IsFinitePointerValue(event->x) || !IsFinitePointerValue(event->y) ||
            !IsFinitePointerValue(event->delta_x) || !IsFinitePointerValue(event->delta_y)) {
            return SetThreadErrorAndReturn(ENGINE_RESULT_INVALID_ARGUMENT,
                                           "pointer coordinates contain non-finite values");
        }
    }

    EnqueueInputEvent(*event);
    SetThreadError(nullptr);
    return ENGINE_RESULT_OK;
}

} // extern "C"

#endif