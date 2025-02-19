#include <napi.h>
#include <pulse/pulseaudio.h>
#include <pulse/context.h>
#include <pulse/mainloop.h>
#include <pulse/introspect.h>
#include <vector>
#include <string>
#include <map>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>

// Structure to hold audio session information
struct AudioSession {
    std::string id;
    std::string name;
    float volume;
    bool muted;
}

// Node.js Native API bindings
Napi::Array GetAudioSessionsWrapper(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    
    std::vector<AudioSession> sessions = GetAudioSessions();
    Napi::Array result = Napi::Array::New(env, sessions.size());
    
    for (size_t i = 0; i < sessions.size(); i++) {
        Napi::Object sessionObj = Napi::Object::New(env);
        sessionObj.Set("id", sessions[i].id);
        sessionObj.Set("name", sessions[i].name);
        sessionObj.Set("volume", sessions[i].volume);
        sessionObj.Set("muted", sessions[i].muted);
        
        result[i] = sessionObj;
    }
    
    return result;
}

Napi::Boolean SetVolumeWrapper(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    
    if (info.Length() < 2 || !info[0].IsString() || !info[1].IsNumber()) {
        Napi::TypeError::New(env, "Expected sessionId (string) and volume (number)").ThrowAsJavaScriptException();
        return Napi::Boolean::New(env, false);
    }
    
    std::string sessionId = info[0].As<Napi::String>();
    float volume = info[1].As<Napi::Number>().FloatValue();
    
    bool success = SetVolume(sessionId, volume);
    return Napi::Boolean::New(env, success);
}

Napi::Boolean SetMuteWrapper(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    
    if (info.Length() < 2 || !info[0].IsString() || !info[1].IsBoolean()) {
        Napi::TypeError::New(env, "Expected sessionId (string) and mute (boolean)").ThrowAsJavaScriptException();
        return Napi::Boolean::New(env, false);
    }
    
    std::string sessionId = info[0].As<Napi::String>();
    bool mute = info[1].As<Napi::Boolean>().Value();
    
    bool success = SetMute(sessionId, mute);
    return Napi::Boolean::New(env, success);
}

// Initialize Node.js module
Napi::Object Init(Napi::Env env, Napi::Object exports) {
    exports.Set("getAudioSessions", Napi::Function::New(env, GetAudioSessionsWrapper));
    exports.Set("setVolume", Napi::Function::New(env, SetVolumeWrapper));
    exports.Set("setMute", Napi::Function::New(env, SetMuteWrapper));
    
    return exports;
}

NODE_API_MODULE(linux_audio_controller, Init);

// Global state for PulseAudio
struct PulseState {
    pa_mainloop* mainloop;
    pa_mainloop_api* mainloop_api;
    pa_context* context;
    bool ready;
    std::vector<AudioSession> sessions;
    std::mutex mutex;
    std::condition_variable cv;
    bool operation_done;
    bool success;
};

// Create and initialize the PulseAudio state
std::unique_ptr<PulseState> CreatePulseState() {
    auto state = std::make_unique<PulseState>();
    
    state->mainloop = pa_mainloop_new();
    if (!state->mainloop) {
        return nullptr;
    }
    
    state->mainloop_api = pa_mainloop_get_api(state->mainloop);
    state->context = pa_context_new(state->mainloop_api, "Audio Mixer");
    state->ready = false;
    state->operation_done = false;
    state->success = false;
    
    return state;
}

// Cleanup PulseAudio state
void CleanupPulseState(PulseState* state) {
    if (state->context) {
        pa_context_disconnect(state->context);
        pa_context_unref(state->context);
    }
    
    if (state->mainloop) {
        pa_mainloop_free(state->mainloop);
    }
}

// Context state callback
void ContextStateCallback(pa_context* context, void* userdata) {
    PulseState* state = static_cast<PulseState*>(userdata);
    
    switch (pa_context_get_state(context)) {
        case PA_CONTEXT_READY:
            state->ready = true;
            state->cv.notify_all();
            break;
        case PA_CONTEXT_FAILED:
        case PA_CONTEXT_TERMINATED:
            state->ready = false;
            state->cv.notify_all();
            break;
        default:
            break;
    }
}

// Connect to PulseAudio server
bool ConnectToPulseAudio(PulseState* state) {
    pa_context_set_state_callback(state->context, ContextStateCallback, state);
    
    if (pa_context_connect(state->context, NULL, PA_CONTEXT_NOFLAGS, NULL) < 0) {
        return false;
    }
    
    // Wait for the context to be ready
    std::unique_lock<std::mutex> lock(state->mutex);
    if (!state->cv.wait_for(lock, std::chrono::seconds(5), [state]() { return state->ready; })) {
        return false;
    }
    
    return state->ready;
}

// Sink input callback for application audio streams
void SinkInputCallback(pa_context* context, const pa_sink_input_info* info, int eol, void* userdata) {
    PulseState* state = static_cast<PulseState*>(userdata);
    
    if (eol > 0) {
        state->operation_done = true;
        state->cv.notify_all();
        return;
    }
    
    if (info) {
        AudioSession session;
        session.id = std::to_string(info->index);
        
        // Get application name
        if (info->proplist && pa_proplist_contains(info->proplist, PA_PROP_APPLICATION_NAME)) {
            const char* appName = pa_proplist_gets(info->proplist, PA_PROP_APPLICATION_NAME);
            if (appName) {
                session.name = appName;
            } else {
                session.name = "Unknown Application";
            }
        } else {
            session.name = "Unknown Application";
        }
        
        // Get volume
        pa_volume_t avgVolume = pa_cvolume_avg(&info->volume);
        session.volume = (static_cast<float>(avgVolume) * 100.0f) / PA_VOLUME_NORM;
        
        // Get mute state
        session.muted = info->mute == 1;
        
        // Add to sessions
        state->sessions.push_back(session);
    }
}

// Sink callback for system audio
void SinkCallback(pa_context* context, const pa_sink_info* info, int eol, void* userdata) {
    PulseState* state = static_cast<PulseState*>(userdata);
    
    if (eol > 0) {
        state->operation_done = true;
        state->cv.notify_all();
        return;
    }
    
    if (info) {
        AudioSession session;
        session.id = "system-" + std::to_string(info->index);
        session.name = info->description ? info->description : "System Output";
        
        // Get volume
        pa_volume_t avgVolume = pa_cvolume_avg(&info->volume);
        session.volume = (static_cast<float>(avgVolume) * 100.0f) / PA_VOLUME_NORM;
        
        // Get mute state
        session.muted = info->mute == 1;
        
        // Add to sessions
        state->sessions.push_back(session);
    }
}

// Success callback for operations
void SuccessCallback(pa_context* context, int success, void* userdata) {
    PulseState* state = static_cast<PulseState*>(userdata);
    state->success = success;
    state->operation_done = true;
    state->cv.notify_all();
}

// Get all audio sessions (system and applications)
std::vector<AudioSession> GetAudioSessions() {
    auto state = CreatePulseState();
    if (!state || !ConnectToPulseAudio(state.get())) {
        return {};
    }
    
    state->sessions.clear();
    state->operation_done = false;
    
    // Get system audio devices (sinks)
    pa_operation* op = pa_context_get_sink_info_list(state->context, SinkCallback, state.get());
    if (!op) {
        CleanupPulseState(state.get());
        return {};
    }
    
    // Wait for operation to complete
    {
        std::unique_lock<std::mutex> lock(state->mutex);
        if (!state->cv.wait_for(lock, std::chrono::seconds(5), [state]() { return state->operation_done; })) {
            pa_operation_cancel(op);
            pa_operation_unref(op);
            CleanupPulseState(state.get());
            return {};
        }
    }
    
    pa_operation_unref(op);
    state->operation_done = false;
    
    // Get application streams (sink inputs)
    op = pa_context_get_sink_input_info_list(state->context, SinkInputCallback, state.get());
    if (!op) {
        CleanupPulseState(state.get());
        return state->sessions; // Return at least system devices
    }
    
    // Wait for operation to complete
    {
        std::unique_lock<std::mutex> lock(state->mutex);
        if (!state->cv.wait_for(lock, std::chrono::seconds(5), [state]() { return state->operation_done; })) {
            pa_operation_cancel(op);
            pa_operation_unref(op);
            CleanupPulseState(state.get());
            return state->sessions; // Return what we have so far
        }
    }
    
    pa_operation_unref(op);
    
    // Run mainloop to process events
    int retval;
    if (pa_mainloop_run(state->mainloop, &retval) < 0) {
        // Handle error
    }
    
    auto sessions = state->sessions;
    CleanupPulseState(state.get());
    
    return sessions;
}

// Set volume for a specific audio session
bool SetVolume(const std::string& sessionId, float volume) {
    if (volume < 0.0f || volume > 100.0f) {
        return false;
    }
    
    auto state = CreatePulseState();
    if (!state || !ConnectToPulseAudio(state.get())) {
        return false;
    }
    
    state->operation_done = false;
    state->success = false;
    
    // Convert volume to PulseAudio format
    pa_volume_t paVolume = (pa_volume_t)((volume / 100.0f) * PA_VOLUME_NORM);
    
    pa_operation* op = nullptr;
    
    // Check if it's a system device
    if (sessionId.find("system-") == 0) {
        std::string indexStr = sessionId.substr(7); // Remove "system-" prefix
        uint32_t sinkIndex = std::stoul(indexStr);
        
        // Create volume structure
        pa_cvolume cvolume;
        pa_cvolume_set(&cvolume, 2, paVolume); // Assuming stereo
        
        op = pa_context_set_sink_volume_by_index(
            state->context,
            sinkIndex,
            &cvolume,
            SuccessCallback,
            state.get()
        );
    } else {
        // It's an application stream
        uint32_t streamIndex = std::stoul(sessionId);
        
        // Create volume structure
        pa_cvolume cvolume;
        pa_cvolume_set(&cvolume, 2, paVolume); // Assuming stereo
        
        op = pa_context_set_sink_input_volume(
            state->context,
            streamIndex,
            &cvolume,
            SuccessCallback,
            state.get()
        );
    }
    
    if (!op) {
        CleanupPulseState(state.get());
        return false;
    }
    
    // Wait for operation to complete
    {
        std::unique_lock<std::mutex> lock(state->mutex);
        if (!state->cv.wait_for(lock, std::chrono::seconds(5), [state]() { return state->operation_done; })) {
            pa_operation_cancel(op);
            pa_operation_unref(op);
            CleanupPulseState(state.get());
            return false;
        }
    }
    
    pa_operation_unref(op);
    
    // Run mainloop to process events
    int retval;
    if (pa_mainloop_run(state->mainloop, &retval) < 0) {
        // Handle error
    }
    
    bool success = state->success;
    CleanupPulseState(state.get());
    
    return success;
}

// Set mute state for a specific audio session
bool SetMute(const std::string& sessionId, bool mute) {
    auto state = CreatePulseState();
    if (!state || !ConnectToPulseAudio(state.get())) {
        return false;
    }
    
    state->operation_done = false;
    state->success = false;
    
    pa_operation* op = nullptr;
    
    // Check if it's a system device
    if (sessionId.find("system-") == 0) {
        std::string indexStr = sessionId.substr(7); // Remove "system-" prefix
        uint32_t sinkIndex = std::stoul(indexStr);
        
        op = pa_context_set_sink_mute_by_index(
            state->context,
            sinkIndex,
            mute ? 1 : 0,
            SuccessCallback,
            state.get()
        );
    } else {
        // It's an application stream
        uint32_t streamIndex = std::stoul(sessionId);
        
        op = pa_context_set_sink_input_mute(
            state->context,
            streamIndex,
            mute ? 1 : 0,
            SuccessCallback,
            state.get()
        );
    }
    
    if (!op) {
        CleanupPulseState(state.get());
        return false;
    }
    
    // Wait for operation to complete
    {
        std::unique_lock<std::mutex> lock(state->mutex);
        if (!state->cv.wait_for(lock, std::chrono::seconds(5), [state]() { return state->operation_done; })) {
            pa_operation_cancel(op);
            pa_operation_unref(op);
            CleanupPulseState(state.get());
            return false;
        }
    }
    
    pa_operation_unref(op);
    
    // Run mainloop to process events
    int retval;
    if (pa_mainloop_run(state->mainloop, &retval) < 0) {
        // Handle error
    }
    
    bool success = state->success;
    CleanupPulseState(state.get());
    
    return success;
}