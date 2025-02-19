const { app, BrowserWindow, ipcMain } = require('electron');
const path = require('path');
const audioController = require('bindings')('windows_audio_controller');

let mainWindow;
let lastAudioSessions = {};

/**
 * Creates the main application window.
 */
function createWindow() {
  mainWindow = new BrowserWindow({
    width: 800,
    height: 600,
    title: "AmpCore",
    webPreferences: {
      nodeIntegration: true,
      contextIsolation: false,
    }
  });

  mainWindow.loadFile('index.html');
  mainWindow.on('closed', () => { mainWindow = null; });

  // Notify the renderer process that it can start requesting data
  mainWindow.webContents.on('did-finish-load', () => {
    console.log("Renderer finished loading. Requesting audio sessions...");
    mainWindow.webContents.send('request-audio-sessions');
  });
}

// Initialize the application when ready
app.on('ready', createWindow);

// Quit the app when all windows are closed, except on macOS
app.on('window-all-closed', () => {
  if (process.platform !== 'darwin') app.quit();
});

// Re-create the window when clicking the dock icon (macOS behavior)
app.on('activate', () => {
  if (mainWindow === null) createWindow();
});

/**
 * Retrieves and sends the list of active audio sessions to the renderer.
 */
function sendAudioSessions() {
  try {
    const audioSessions = audioController.getAudioSessions();
    let updatedSessions = {};
    let hasChanges = false;

    // Track new and existing sessions
    audioSessions.forEach(session => {
      updatedSessions[session.id] = session;
      if (!lastAudioSessions[session.id]) {
        hasChanges = true; // New session detected
      }
    });

    // Detect removed sessions
    Object.keys(lastAudioSessions).forEach(sessionId => {
      if (!updatedSessions[sessionId]) {
        hasChanges = true;
      }
    });

    // Update renderer only if changes are detected
    if (hasChanges) {
      lastAudioSessions = updatedSessions;
      console.log("Audio session changes detected. Updating renderer.");
      if (mainWindow) {
        mainWindow.webContents.send('audio-sessions-update', Object.values(updatedSessions));
      }
    }
  } catch (error) {
    console.error('Error retrieving audio sessions:', error);
  }
}

/**
 * Handles volume adjustment requests from the renderer.
 */
ipcMain.on('set-volume', (event, { sessionId, volume }) => {
  console.log(`Setting volume: SessionID=${sessionId}, Volume=${volume}`);
  setImmediate(() => {
    audioController.setVolume(sessionId, volume);
  });
});

/**
 * Handles mute toggle requests from the renderer.
 */
ipcMain.on('toggle-mute', (event, { sessionId }) => {
  console.log(`Toggling mute: SessionID=${sessionId}`);
  setImmediate(() => {
    if (!lastAudioSessions[sessionId]) return;

    const newMuteState = !lastAudioSessions[sessionId].muted;
    audioController.setMute(sessionId, newMuteState);

    lastAudioSessions[sessionId].muted = newMuteState;

    event.sender.send('mute-updated', { sessionId, muted: newMuteState });
  });
});

/**
 * Sends the last known audio session data to the renderer upon request.
 */
ipcMain.on('request-audio-sessions', (event) => {
  console.log("Renderer requested audio sessions. Sending...");
  event.sender.send('audio-sessions-update', Object.values(lastAudioSessions));
});

// Set up periodic updates for audio sessions
const UPDATE_INTERVAL = 1000; // Update every second
setInterval(sendAudioSessions, UPDATE_INTERVAL);