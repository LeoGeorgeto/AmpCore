{
  "targets": [
    {
      "target_name": "windows_audio_controller",
      "cflags!": [ "-fno-exceptions" ],
      "cflags_cc!": [ "-fno-exceptions" ],
      "conditions": [
        # Windows-specific build settings
        ['OS=="win"', {
          "sources": [ 
            "native-modules/windows-audio-controller.cpp" 
          ],
          "include_dirs": [
            "<!@(node -p \"require('node-addon-api').include\")"
          ],
          "libraries": [
            "-lole32.lib",
            "-loleaut32.lib"
          ],
          'defines': [ 'NAPI_DISABLE_CPP_EXCEPTIONS' ],
          'msvs_settings': {
            'VCCLCompilerTool': {
              'ExceptionHandling': 1 # Enable structured exception handling in MSVC
            }
          }
        }],
        
        # macOS-specific build settings
        ['OS=="mac"', {
          "sources": [ 
            "native-modules/macos-audio-controller.mm" 
          ],
          "include_dirs": [
            "<!@(node -p \"require('node-addon-api').include\")"
          ],
          'link_settings': {
            'libraries': [
              '-framework CoreAudio',
              '-framework AudioToolbox',
              '-framework Foundation'
            ]
          },
          'defines': [ 'NAPI_DISABLE_CPP_EXCEPTIONS' ],
          'xcode_settings': {
            'GCC_ENABLE_CPP_EXCEPTIONS': 'YES', # Enable C++ exceptions for macOS
            'CLANG_CXX_LIBRARY': 'libc++', # Use the modern C++ standard library
            'MACOSX_DEPLOYMENT_TARGET': '10.15' # Target macOS 10.15 or later
          }
        }],
        
        # Linux-specific build settings
        ['OS=="linux"', {
          "sources": [ 
            "native-modules/linux-audio-controller.cpp" 
          ],
          "include_dirs": [
            "<!@(node -p \"require('node-addon-api').include\")",
            "<!@(pkg-config --cflags-only-I pulse)" # Include PulseAudio headers
          ],
          'link_settings': {
            'libraries': [
              '<!@(pkg-config --libs pulse)' # Link against PulseAudio
            ]
          },
          'defines': [ 'NAPI_DISABLE_CPP_EXCEPTIONS' ]
        }]
      ]
    }
  ]
}