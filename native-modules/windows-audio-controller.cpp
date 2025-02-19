#include <node_api.h>
#include <napi.h>
#include <mmdeviceapi.h>
#include <audiopolicy.h>
#include <endpointvolume.h>
#include <functiondiscoverykeys_devpkey.h>
#include <vector>
#include <string>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

// Helper class to initialize and release COM
class COMInitializer {
public:
    COMInitializer() : initialized(false) {
        HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
        if (SUCCEEDED(hr)) {
            initialized = true;
        }
    }

    ~COMInitializer() {
        if (initialized) {
            CoUninitialize();
        }
    }

private:
    bool initialized;
};

// Structure to hold audio session information
struct AudioSession {
    std::string id;
    std::string name;
    float volume;
    bool muted;
};

// Get all active audio sessions
std::vector<AudioSession> GetAudioSessions() {
    std::vector<AudioSession> sessions;
    COMInitializer comInit;

    ComPtr<IMMDeviceEnumerator> deviceEnumerator;
    HRESULT hr = CoCreateInstance(
        __uuidof(MMDeviceEnumerator),
        NULL, CLSCTX_ALL,
        __uuidof(IMMDeviceEnumerator),
        (void**)&deviceEnumerator);
    
    if (FAILED(hr)) {
        return sessions;
    }

    // Get default audio endpoint
    ComPtr<IMMDevice> defaultDevice;
    hr = deviceEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &defaultDevice);
    if (FAILED(hr)) {
        return sessions;
    }

    // Activate the session manager
    ComPtr<IAudioSessionManager2> sessionManager;
    hr = defaultDevice->Activate(
        __uuidof(IAudioSessionManager2),
        CLSCTX_ALL, 
        nullptr,
        (void**)&sessionManager);
    
    if (FAILED(hr)) {
        return sessions;
    }

    // Get the session enumerator
    ComPtr<IAudioSessionEnumerator> sessionEnumerator;
    hr = sessionManager->GetSessionEnumerator(&sessionEnumerator);
    if (FAILED(hr)) {
        return sessions;
    }

    // Get session count
    int sessionCount;
    hr = sessionEnumerator->GetCount(&sessionCount);
    if (FAILED(hr)) {
        return sessions;
    }

    // Iterate through all sessions
    for (int i = 0; i < sessionCount; i++) {
        ComPtr<IAudioSessionControl> sessionControl;
        hr = sessionEnumerator->GetSession(i, &sessionControl);
        if (FAILED(hr)) {
            continue;
        }

        ComPtr<IAudioSessionControl2> sessionControl2;
        hr = sessionControl.As(&sessionControl2);
        if (FAILED(hr)) {
            continue;
        }

        // Check if it's a system sound
        hr = sessionControl2->IsSystemSoundsSession();
        if (SUCCEEDED(hr) && hr == S_OK) {
            // Add system sounds as a session
            AudioSession session;
            session.id = "system-sounds";
            session.name = "System Sounds";
            
            ComPtr<ISimpleAudioVolume> audioVolume;
            hr = sessionControl.As(&audioVolume);
            if (SUCCEEDED(hr)) {
                float volume;
                BOOL muted;
                audioVolume->GetMasterVolume(&volume);
                audioVolume->GetMute(&muted);
                
                session.volume = volume * 100.0f;
                session.muted = (muted == TRUE);
            }
            
            sessions.push_back(session);
            continue;
        }

        // Skip sessions with no audio
        AudioSessionState state;
        hr = sessionControl->GetState(&state);
        if (SUCCEEDED(hr) && state != AudioSessionStateActive) {
            continue;
        }

        // Get the session identifier
        DWORD processId;
        hr = sessionControl2->GetProcessId(&processId);
        if (FAILED(hr)) {
            continue;
        }

        // Get session display name
        LPWSTR displayName = nullptr;
        hr = sessionControl->GetDisplayName(&displayName);
        std::wstring wName = SUCCEEDED(hr) && displayName ? displayName : L"Unknown";
        std::string name(wName.begin(), wName.end());
        
        if (displayName) {
            CoTaskMemFree(displayName);
        }

        // If no proper name, try to get it from process name
        if (name == "Unknown" || name.empty()) {
            HANDLE processHandle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
            if (processHandle) {
                wchar_t processName[MAX_PATH];
                DWORD size = MAX_PATH;
                if (QueryFullProcessImageNameW(processHandle, 0, processName, &size)) {
                    // Extract filename from path
                    wchar_t* fileName = wcsrchr(processName, L'\\');
                    if (fileName) {
                        fileName++; // Skip the backslash
                        // Remove .exe extension if present
                        wchar_t* extension = wcsrchr(fileName, L'.');
                        if (extension) {
                            *extension = L'\0';
                        }
                        std::wstring wProcName(fileName);
                        name = std::string(wProcName.begin(), wProcName.end());
                    }
                }
                CloseHandle(processHandle);
            }
        }

        // Get volume information
        ComPtr<ISimpleAudioVolume> audioVolume;
        hr = sessionControl.As(&audioVolume);
        if (FAILED(hr)) {
            continue;
        }

        float volume;
        BOOL muted;
        hr = audioVolume->GetMasterVolume(&volume);
        if (FAILED(hr)) {
            continue;
        }

        hr = audioVolume->GetMute(&muted);
        if (FAILED(hr)) {
            continue;
        }

        // Create and add the session
        AudioSession session;
        session.id = std::to_string(processId);
        session.name = name;
        session.volume = volume * 100.0f;
        session.muted = (muted == TRUE);
        
        sessions.push_back(session);
    }

    return sessions;
}

// Set volume for a specific audio session
bool SetVolume(const std::string& sessionId, float volume) {
    if (volume < 0.0f || volume > 100.0f) {
        return false;
    }

    COMInitializer comInit;
    
    // Convert to actual volume (0.0 - 1.0)
    float normalizedVolume = volume / 100.0f;
    
    ComPtr<IMMDeviceEnumerator> deviceEnumerator;
    HRESULT hr = CoCreateInstance(
        __uuidof(MMDeviceEnumerator),
        NULL, CLSCTX_ALL,
        __uuidof(IMMDeviceEnumerator),
        (void**)&deviceEnumerator);
    
    if (FAILED(hr)) {
        return false;
    }

    // Get default audio endpoint
    ComPtr<IMMDevice> defaultDevice;
    hr = deviceEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &defaultDevice);
    if (FAILED(hr)) {
        return false;
    }

    // Activate the session manager
    ComPtr<IAudioSessionManager2> sessionManager;
    hr = defaultDevice->Activate(
        __uuidof(IAudioSessionManager2),
        CLSCTX_ALL, 
        nullptr,
        (void**)&sessionManager);
    
    if (FAILED(hr)) {
        return false;
    }

    // Special case for system sounds
    if (sessionId == "system-sounds") {
        ComPtr<IAudioSessionEnumerator> sessionEnumerator;
        hr = sessionManager->GetSessionEnumerator(&sessionEnumerator);
        if (FAILED(hr)) {
            return false;
        }
        
        int sessionCount;
        hr = sessionEnumerator->GetCount(&sessionCount);
        if (FAILED(hr)) {
            return false;
        }
        
        for (int i = 0; i < sessionCount; i++) {
            ComPtr<IAudioSessionControl> sessionControl;
            hr = sessionEnumerator->GetSession(i, &sessionControl);
            if (FAILED(hr)) {
                continue;
            }
            
            ComPtr<IAudioSessionControl2> sessionControl2;
            hr = sessionControl.As(&sessionControl2);
            if (FAILED(hr)) {
                continue;
            }
            
            hr = sessionControl2->IsSystemSoundsSession();
            if (SUCCEEDED(hr) && hr == S_OK) {            
                ComPtr<ISimpleAudioVolume> audioVolume;
                hr = sessionControl.As(&audioVolume);
                if (SUCCEEDED(hr)) {
                    hr = audioVolume->SetMasterVolume(normalizedVolume, NULL);
                    return SUCCEEDED(hr);
                }
            }
        }
        return false;
    }
    
    // For regular applications
    DWORD processId = std::stoul(sessionId);
    
    // Get the session enumerator
    ComPtr<IAudioSessionEnumerator> sessionEnumerator;
    hr = sessionManager->GetSessionEnumerator(&sessionEnumerator);
    if (FAILED(hr)) {
        return false;
    }

    // Get session count
    int sessionCount;
    hr = sessionEnumerator->GetCount(&sessionCount);
    if (FAILED(hr)) {
        return false;
    }

    // Find and modify the target session
    for (int i = 0; i < sessionCount; i++) {
        ComPtr<IAudioSessionControl> sessionControl;
        hr = sessionEnumerator->GetSession(i, &sessionControl);
        if (FAILED(hr)) {
            continue;
        }

        ComPtr<IAudioSessionControl2> sessionControl2;
        hr = sessionControl.As(&sessionControl2);
        if (FAILED(hr)) {
            continue;
        }

        DWORD currentProcessId;
        hr = sessionControl2->GetProcessId(&currentProcessId);
        if (FAILED(hr)) {
            continue;
        }

        if (currentProcessId == processId) {
            ComPtr<ISimpleAudioVolume> audioVolume;
            hr = sessionControl.As(&audioVolume);
            if (FAILED(hr)) {
                return false;
            }

            hr = audioVolume->SetMasterVolume(normalizedVolume, NULL);
            return SUCCEEDED(hr);
        }
    }

    return false;
}

// Set mute state for a specific audio session
bool SetMute(const std::string& sessionId, bool mute) {
    COMInitializer comInit;
    
    ComPtr<IMMDeviceEnumerator> deviceEnumerator;
    HRESULT hr = CoCreateInstance(
        __uuidof(MMDeviceEnumerator),
        NULL, CLSCTX_ALL,
        __uuidof(IMMDeviceEnumerator),
        (void**)&deviceEnumerator);
    
    if (FAILED(hr)) {
        return false;
    }

    // Get default audio endpoint
    ComPtr<IMMDevice> defaultDevice;
    hr = deviceEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &defaultDevice);
    if (FAILED(hr)) {
        return false;
    }

    // Activate the session manager
    ComPtr<IAudioSessionManager2> sessionManager;
    hr = defaultDevice->Activate(
        __uuidof(IAudioSessionManager2),
        CLSCTX_ALL, 
        nullptr,
        (void**)&sessionManager);
    
    if (FAILED(hr)) {
        return false;
    }

    // Special case for system sounds
    if (sessionId == "system-sounds") {
        ComPtr<IAudioSessionEnumerator> sessionEnumerator;
        hr = sessionManager->GetSessionEnumerator(&sessionEnumerator);
        if (FAILED(hr)) {
            return false;
        }
        
        int sessionCount;
        hr = sessionEnumerator->GetCount(&sessionCount);
        if (FAILED(hr)) {
            return false;
        }
        
        for (int i = 0; i < sessionCount; i++) {
            ComPtr<IAudioSessionControl> sessionControl;
            hr = sessionEnumerator->GetSession(i, &sessionControl);
            if (FAILED(hr)) {
                continue;
            }
            
            ComPtr<IAudioSessionControl2> sessionControl2;
            hr = sessionControl.As(&sessionControl2);
            if (FAILED(hr)) {
                continue;
            }
            
            hr = sessionControl2->IsSystemSoundsSession();
            if (SUCCEEDED(hr) && hr == S_OK) {
                ComPtr<ISimpleAudioVolume> audioVolume;
                hr = sessionControl.As(&audioVolume);
                if (SUCCEEDED(hr)) {
                    hr = audioVolume->SetMute(mute, NULL);
                    return SUCCEEDED(hr);
                }
            }
        }
        return false;
    }
    
    // For regular applications
    DWORD processId = std::stoul(sessionId);
    
    // Get the session enumerator
    ComPtr<IAudioSessionEnumerator> sessionEnumerator;
    hr = sessionManager->GetSessionEnumerator(&sessionEnumerator);
    if (FAILED(hr)) {
        return false;
    }

    // Get session count
    int sessionCount;
    hr = sessionEnumerator->GetCount(&sessionCount);
    if (FAILED(hr)) {
        return false;
    }

    // Find and modify the target session
    for (int i = 0; i < sessionCount; i++) {
        ComPtr<IAudioSessionControl> sessionControl;
        hr = sessionEnumerator->GetSession(i, &sessionControl);
        if (FAILED(hr)) {
            continue;
        }

        ComPtr<IAudioSessionControl2> sessionControl2;
        hr = sessionControl.As(&sessionControl2);
        if (FAILED(hr)) {
            continue;
        }

        DWORD currentProcessId;
        hr = sessionControl2->GetProcessId(&currentProcessId);
        if (FAILED(hr)) {
            continue;
        }

        if (currentProcessId == processId) {
            ComPtr<ISimpleAudioVolume> audioVolume;
            hr = sessionControl.As(&audioVolume);
            if (FAILED(hr)) {
                return false;
            }

            hr = audioVolume->SetMute(mute, NULL);
            return SUCCEEDED(hr);
        }
    }

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

NODE_API_MODULE(windows_audio_controller, Init)