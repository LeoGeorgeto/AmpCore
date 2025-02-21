const { app, BrowserWindow, ipcMain } = require('electron');
const path = require('path');
const fs = require('fs');
const audioController = require('bindings')('windows_audio_controller');

let mainWindow;
let lastAudioSessions = {};
const EXCLUSIONS_FILE = path.join(app.getPath('userData'), 'exclusions.json');
const MUTE_STATES_FILE = path.join(app.getPath('userData'), 'muteStates.json');

// Load exclusions from file
function loadExclusions() {
  try {
    if (fs.existsSync(EXCLUSIONS_FILE)) {
      const data = fs.readFileSync(EXCLUSIONS_FILE, 'utf8');
      return new Set(JSON.parse(data));
    }
  } catch (error) {
    console.error('Error loading exclusions:', error);
  }
  return new Set();
}

// Save exclusions to file
function saveExclusions(exclusions) {
  try {
    fs.writeFileSync(EXCLUSIONS_FILE, JSON.stringify(Array.from(exclusions)));
  } catch (error) {
    console.error('Error saving exclusions:', error);
  }
}

// Load mute states from file
function loadMuteStates() {
  try {
    if (fs.existsSync(MUTE_STATES_FILE)) {
      const data = fs.readFileSync(MUTE_STATES_FILE, 'utf8');
      return JSON.parse(data);
    }
  } catch (error) {
    console.error('Error loading mute states:', error);
  }
  return {};
}

// Save mute states to file
function saveMuteStates(muteStates) {
  try {
    fs.writeFileSync(MUTE_STATES_FILE, JSON.stringify(muteStates));
  } catch (error) {
    console.error('Error saving mute states:', error);
  }
}

// Initialize mute states on app startup
let savedMuteStates = loadMuteStates();

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
      
      // Apply saved mute state if available
      if (savedMuteStates[session.name] !== undefined) {
        // Only update if the current mute state differs from saved state
        if (session.muted !== savedMuteStates[session.name]) {
          audioController.setMute(session.id, savedMuteStates[session.name]);
          session.muted = savedMuteStates[session.name];
        }
      }
      
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
ipcMain.on('toggle-mute', (event, { sessionId, newMuteState }) => {
  console.log(`Toggling mute: SessionID=${sessionId}`);
  setImmediate(() => {
    audioController.setMute(sessionId, newMuteState);
    lastAudioSessions[sessionId].muted = newMuteState;
    
    // Save the mute state by application name
    const sessionName = lastAudioSessions[sessionId].name;
    savedMuteStates[sessionName] = newMuteState;
    saveMuteStates(savedMuteStates);

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

// Handle exclusion updates
ipcMain.on('update-exclusions', (event, exclusions) => {
  saveExclusions(exclusions);
});

// Send initial exclusions to renderer
ipcMain.on('request-exclusions', (event) => {
  const exclusions = loadExclusions();
  event.reply('exclusions-loaded', Array.from(exclusions));
});

// Send saved mute states to renderer when requested
ipcMain.on('request-mute-states', (event) => {
  event.reply('mute-states-loaded', savedMuteStates);
});

// Handle saving mute state from renderer
ipcMain.on('save-mute-state', (event, { appName, muted }) => {
  if (!appName) return;
  
  console.log(`Saving mute state for ${appName}: ${muted}`);
  
  // Update the in-memory state
  savedMuteStates[appName] = muted;
  
  // Save to disk
  saveMuteStates(savedMuteStates);
});

// Set up periodic updates for audio sessions
const UPDATE_INTERVAL = 1000; // Update every second
setInterval(sendAudioSessions, UPDATE_INTERVAL);