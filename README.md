# AmpCore - Personal Audio Mixer

## Overview
AmpCore is a cross-platform desktop application built using Electron, allowing users to control the volume of individual applications from a single interface. The application provides features like real-time audio session tracking, volume adjustment, and mute toggling.

## Features
- Real-time audio session monitoring
- Control application volumes individually
- Mute/unmute applications
- Cross-platform support (Windows, macOS, Linux (pending testing))

## Installation
### Prerequisites
Ensure you have the following dependencies installed:
- [Node.js](https://nodejs.org/) (LTS version recommended)
- [Electron](https://www.electronjs.org/)
- [Node-gyp](https://github.com/nodejs/node-gyp)
- OS-specific build dependencies:
  - **Windows**: Microsoft Visual Studio Build Tools, Windows SDK
  - **macOS**: Xcode command line tools
  - **Linux**: `build-essential`, `libpulse-dev`

### Clone and Install Dependencies
```sh
git clone https://github.com/yourusername/ampcore.git
cd ampcore
npm install
```

### Running the Application
```sh
npm start
```

## Building for Different Platforms
### Windows
```sh
npm run build:win
```
### macOS
```sh
npm run build:mac
```
### Linux
```sh
npm run build:linux
```

## Project Structure
```
Mixer/
│── .vscode/                  # VSCode configuration files
│── build/                    # Build artifacts
│── dist/                     # Distribution packages
│── native-modules/           # Native bindings for audio control
│   ├── linux-audio-controller.cpp
│   ├── macos-audio-controller.mm
│   ├── windows-audio-controller.cpp
│── node_modules/             # Node dependencies
│── resources/                # Application resources (icons, assets)
│── binding.gyp               # Node addon build config
│── build-win.bat             # Windows-specific build script
│── index.html                # Application UI
│── main.js                   # Electron main process
│── package.json              # Project metadata and scripts
│── styles.css                # UI styling
```

## Development
### Setting up the Development Environment
- Ensure dependencies are installed (`npm install`).
- Use `npm start` for live development.
- Modify `main.js` for backend logic.
- Modify `index.html` and `styles.css` for UI improvements.

### Debugging
- Use `console.log()` in `main.js` for debugging IPC events.
- Open Chrome Developer Tools in Electron using `Ctrl+Shift+I`.
- Use `npm run rebuild` if native modules are modified.

## License
This project is licensed under the MIT License - see the LICENSE file for details.
