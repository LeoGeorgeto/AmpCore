#import <napi.h>
#import <Foundation/Foundation.h>
#import <CoreAudio/CoreAudio.h>
#import <AudioToolbox/AudioToolbox.h>
#include <map>
#include <string>
#include <vector>

// Structure to hold audio session information
struct AudioSession {
    std::string id;
    std::string name;
    float volume;
    bool muted;
};

// Get the default output device ID
static AudioDeviceID GetDefaultOutputDevice() {
    AudioDeviceID outputDevice = 0;
    UInt32 propertySize = sizeof(outputDevice);
    
    AudioObjectPropertyAddress propertyAddress = {
        kAudioHardwarePropertyDefaultOutputDevice,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    };
    
    OSStatus status = AudioObjectGetPropertyData(
        kAudioObjectSystemObject,
        &propertyAddress,
        0,
        NULL,
        &propertySize,
        &outputDevice
    );
    
    if (status != noErr) {
        return 0;
    }
    
    return outputDevice;
}

// Get the system volume
static Float32 GetSystemVolume(AudioDeviceID deviceID) {
    Float32 volume = 0.0;
    UInt32 propertySize = sizeof(volume);
    
    AudioObjectPropertyAddress propertyAddress = {
        kAudioDevicePropertyVolumeScalar,
        kAudioDevicePropertyScopeOutput,
        kAudioObjectPropertyElementMain
    };
    
    OSStatus status = AudioObjectGetPropertyData(
        deviceID,
        &propertyAddress,
        0,
        NULL,
        &propertySize,
        &volume
    );
    
    if (status != noErr) {
        return 0.0;
    }
    
    return volume;
}

// Check if system audio is muted
static bool IsSystemMuted(AudioDeviceID deviceID) {
    UInt32 muted = 0;
    UInt32 propertySize = sizeof(muted);
    
    AudioObjectPropertyAddress propertyAddress = {
        kAudioDevicePropertyMute,
        kAudioDevicePropertyScopeOutput,
        kAudioObjectPropertyElementMain
    };
    
    OSStatus status = AudioObjectGetPropertyData(
        deviceID,
        &propertyAddress,
        0,
        NULL,
        &propertySize,
        &muted
    );
    
    if (status != noErr) {
        return false;
    }
    
    return muted != 0;
}

// Get application audio sessions
// Note: macOS doesn't provide direct API access to per-application volume
// This is a simplified implementation that returns system audio and running audio apps
std::vector<AudioSession> GetAudioSessions() {
    std::vector<AudioSession> sessions;
    
    // Get the default output device
    AudioDeviceID outputDevice = GetDefaultOutputDevice();
    if (outputDevice == 0) {
        return sessions;
    }
    
    // Add system audio as a session
    AudioSession systemSession;
    systemSession.id = "system";
    systemSession.name = "System Audio";
    systemSession.volume = GetSystemVolume(outputDevice) * 100.0f;
    systemSession.muted = IsSystemMuted(outputDevice);
    sessions.push_back(systemSession);
    
    // Get list of running audio applications
    // This requires additional implementation using AudioHAL or CoreAudio
    // For now, we'll just return the system audio
    
    return sessions;
}

// Set system volume
bool SetSystemVolume(float volume) {
    if (volume < 0.0f || volume > 100.0f) {
        return false;
    }
    
    AudioDeviceID outputDevice = GetDefaultOutputDevice();
    if (outputDevice == 0) {
        return false;
    }
    
    Float32 normalizedVolume = volume / 100.0f;
    
    AudioObjectPropertyAddress propertyAddress = {
        kAudioDevicePropertyVolumeScalar,
        kAudioDevicePropertyScopeOutput,
        kAudioObjectPropertyElementMain
    };
    
    OSStatus status = AudioObjectSetPropertyData(
        outputDevice,
        &propertyAddress,
        0,
        NULL,
        sizeof(normalizedVolume),
        &normalizedVolume
    );
    
    return status == noErr;
}

// Set system mute state
bool SetSystemMute(bool mute) {
    AudioDeviceID outputDevice = GetDefaultOutputDevice();
    if (outputDevice == 0) {
        return false;
    }
    
    UInt32 muteValue = mute ? 1 : 0;
    
    AudioObjectPropertyAddress propertyAddress = {
        kAudioDevicePropertyMute,
        kAudioDevicePropertyScopeOutput,
        kAudioObjectPropertyElementMain
    };
    
    OSStatus status = AudioObjectSetPropertyData(
        outputDevice,
        &propertyAddress,
        0,
        NULL,
        sizeof(muteValue),
        &muteValue
    );
    
    return status == noErr;
}

// Set volume for a specific audio session
bool SetVolume(const std::string& sessionId, float volume) {
    if (sessionId == "system") {
        return SetSystemVolume(volume);
    }
    
    // For application-specific volume (requires additional implementation)
    return false;
}

// Set mute state for a specific audio session
bool SetMute(const std::string& sessionId, bool mute) {
    if (sessionId == "system") {
        return SetSystemMute(mute);
    }
    
    // For application-specific mute (requires additional implementation)
    return false;
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

NODE_API_MODULE(macos_audio_controller, Init)