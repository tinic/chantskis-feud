<script setup lang="ts">
import { ref, onMounted, onUnmounted, watch } from "vue";
import { invoke } from "@tauri-apps/api/core";
import Button from "primevue/button";
import Card from "primevue/card";
import Dropdown from "primevue/dropdown";
import Message from "primevue/message";
import InputText from "primevue/inputtext";
import Textarea from "primevue/textarea";
import Toolbar from "primevue/toolbar";
import Badge from "primevue/badge";
import InputNumber from "primevue/inputnumber";
import ProgressBar from "primevue/progressbar";
import Slider from "primevue/slider";

interface SerialPortInfo {
  name: string;
  path: string;
}

const selectedPort = ref<string | null>(null);
const serialPorts = ref<SerialPortInfo[]>([]);
const isConnected = ref(false);
const connectionStatus = ref("Disconnected");
const sendData = ref("");
const receivedData = ref("");
const isSending = ref(false);
let readInterval: number | null = null;

// Buffer for assembling fragmented serial data
let serialBuffer = "";

// Previous game state for detecting changes
let previousActivePlayer: 'A' | 'B' | null = null;
let lastSoundTime = 0; // Prevent duplicate sounds

// Game state monitoring
const gameState = ref({
  timerActive: false,
  timerPaused: false,
  timeRemaining: 0,
  totalTime: 0,
  playerAPressed: false,
  playerBPressed: false,
  activePlayer: null as 'A' | 'B' | null
});

// Timer duration with persistence
const timerDuration = ref(30); // Default 30 seconds

// Volume control with persistence
const soundVolume = ref(0.7); // Default 70% volume

async function refreshPorts() {
  try {
    const ports = await invoke<SerialPortInfo[]>("list_serial_ports");
    serialPorts.value = ports;
  } catch (error) {
    console.error("Failed to list serial ports:", error);
    connectionStatus.value = "Failed to list ports";
  }
}

async function connect() {
  if (selectedPort.value) {
    try {
      const message = await invoke<string>("connect_serial", { portPath: selectedPort.value });
      isConnected.value = true;
      connectionStatus.value = message;
      startReading();
      
      // Reset game state on connection
      await resetGame();
    } catch (error) {
      console.error("Failed to connect:", error);
      connectionStatus.value = `Failed to connect: ${error}`;
    }
  }
}

async function disconnect() {
  try {
    // Reset game state before disconnecting
    await resetGame();
    
    stopReading();
    const message = await invoke<string>("disconnect_serial");
    isConnected.value = false;
    connectionStatus.value = message;
    receivedData.value = "";
    serialBuffer = ""; // Clear serial buffer
  } catch (error) {
    console.error("Failed to disconnect:", error);
  }
}

async function reconnect() {
  if (selectedPort.value) {
    await connect();
  }
}

async function sendSerialData() {
  if (!sendData.value.trim() || isSending.value) return;
  
  isSending.value = true;
  try {
    const encoder = new TextEncoder();
    const data = encoder.encode(sendData.value + "\n");
    await invoke("send_serial_data", { data: Array.from(data) });
    sendData.value = ""; // Clear only on successful send
    connectionStatus.value = `Connected to ${selectedPort.value}`;
  } catch (error) {
    console.error("Failed to send data:", error);
    connectionStatus.value = `Send failed: ${error}`;
    // If send fails due to disconnection, update connection state
    if (String(error).includes("Not connected")) {
      isConnected.value = false;
      stopReading();
    }
  } finally {
    isSending.value = false;
  }
}

async function readSerialData() {
  try {
    const data = await invoke<number[]>("read_serial_data");
    if (data && data.length > 0) {
      const decoder = new TextDecoder();
      const text = decoder.decode(new Uint8Array(data));
      
      // Add to buffer for line assembly
      serialBuffer += text;
      
      // Process complete lines
      const lines = serialBuffer.split('\n');
      // Keep the last potentially incomplete line in the buffer
      serialBuffer = lines.pop() || '';
      
      for (const line of lines) {
        if (line.trim()) {
          processSerialLine(line.trim());
        }
      }
      
      // Auto-scroll to bottom with requestAnimationFrame to avoid blocking
      requestAnimationFrame(() => {
        const textarea = document.querySelector('.received-data') as HTMLTextAreaElement;
        if (textarea) {
          textarea.scrollTop = textarea.scrollHeight;
        }
      });
    }
  } catch (error) {
    // Ignore timeout errors, they're expected
    const errorStr = String(error);
    if (!errorStr.includes("Not connected") && !errorStr.includes("TimedOut")) {
      console.error("Failed to read data:", error);
    }
  }
}

function processSerialLine(line: string) {
  // Check if this is a status message
  if (line.startsWith("status:") || isStatusMessageFragment(line)) {
    parseGameMessage(line);
    // Don't show status messages in command output
  } else {
    // Only add non-status messages to received data
    // Prevent memory leaks by limiting received data size
    if (receivedData.value.length > 50000) {
      receivedData.value = receivedData.value.slice(-40000) + line + '\n';
    } else {
      receivedData.value += line + '\n';
    }
  }
}

function isStatusMessageFragment(line: string): boolean {
  // Check for status message patterns (even if fragmented)
  const statusPatterns = [
    /timer=\d+/,
    /playera=[01]/,
    /playerb=[01]/,
    /active=[ABN]/,
    /^(timer=|playera=|playerb=|active=)/,
    /^\d+\s+playera=/,  // fragments like "29 playera=0"
    /^playera=\d+\s+playerb=/,  // fragments like "playera=0 playerb=0"
    /^playerb=\d+\s+active=/,   // fragments like "playerb=0 active=N"
    /^active=[ABN]$/,           // fragments like just "active=N"
    /layerb=/,                  // typo fragments like "layerb=0"
    /^[abn]=\d/                 // single character fragments
  ];
  
  return statusPatterns.some(pattern => pattern.test(line));
}

function startReading() {
  if (readInterval) return;
  readInterval = window.setInterval(readSerialData, 100); // 10 FPS (100ms interval) for stability
}

function stopReading() {
  if (readInterval) {
    clearInterval(readInterval);
    readInterval = null;
  }
}

function clearReceivedData() {
  receivedData.value = "";
}

// Game control functions
async function startTimer() {
  if (!isConnected.value) return;
  
  try {
    let command: string;
    
    // Check if timer is paused - if so, resume instead of starting new
    if (gameState.value.timerPaused) {
      command = "resume_timer";
      console.log("Resuming paused timer");
    } else {
      command = `start_timer ${timerDuration.value}`;
      console.log(`Starting new timer: duration=${timerDuration.value}`);
    }
    
    const encoder = new TextEncoder();
    const data = encoder.encode(command + "\n");
    await invoke("send_serial_data", { data: Array.from(data) });
    
    // Update local state immediately for UI responsiveness
    if (!gameState.value.timerPaused) {
      // Only update these for new timers, not resumed ones
      gameState.value.timeRemaining = timerDuration.value;
      gameState.value.totalTime = timerDuration.value;
      gameState.value.playerAPressed = false;
      gameState.value.playerBPressed = false;
      gameState.value.activePlayer = null;
      previousActivePlayer = null; // Reset player state tracking
    }
    
    gameState.value.timerActive = true;
    gameState.value.timerPaused = false;
    lastSoundTime = 0; // Reset sound debounce
  } catch (error) {
    console.error("Failed to start/resume timer:", error);
  }
}

async function stopTimer() {
  if (!isConnected.value) return;
  
  try {
    const encoder = new TextEncoder();
    const data = encoder.encode("stop_timer\n");
    await invoke("send_serial_data", { data: Array.from(data) });
  } catch (error) {
    console.error("Failed to stop timer:", error);
  }
}

async function resetGame() {
  if (!isConnected.value) return;
  
  try {
    const encoder = new TextEncoder();
    const data = encoder.encode("force_reset\n");
    await invoke("send_serial_data", { data: Array.from(data) });
  } catch (error) {
    console.error("Failed to reset game:", error);
  }
}


// Parse status updates from pico
function parseGameMessage(data: string) {
  // Status updates: "status: timer=15 playera=0 playerb=1 active=B expired=1" or older format without expired
  const statusMatch = data.match(/status:\s*timer=(\d+)\s*playera=([01])\s*playerb=([01])\s*active=(\w+)(?:\s*expired=([01]))?/);
  if (statusMatch) {
    const newTimeRemaining = parseInt(statusMatch[1]);
    const newPlayerAPressed = statusMatch[2] === '1';
    const newPlayerBPressed = statusMatch[3] === '1';
    const newActivePlayer = statusMatch[4] === 'A' ? 'A' : statusMatch[4] === 'B' ? 'B' : null;
    const timerExpiredNaturally = statusMatch[5] === '1'; // Will be undefined for old format, which is falsy
    const newTimerActive = newTimeRemaining > 0;
    
    // Play timeout sound only if the Pico explicitly indicates natural expiration
    if (timerExpiredNaturally) {
      playTimerExpiredSound();
    }
    
    // Check if a player just pressed their button
    if (previousActivePlayer === null && newActivePlayer !== null) {
      // Debounce sound to prevent duplicates
      const currentTime = Date.now();
      if (currentTime - lastSoundTime > 200) { // 200ms debounce
        playPlayerBuzzerSound();
        lastSoundTime = currentTime;
      }
      // Note: Timer is automatically paused by firmware when button is pressed
    }
    
    // Parse game state to determine if paused
    // Check for "timer_paused" in the full message (not just the status line)
    const isPaused = data.includes('timer_paused') || (newPlayerAPressed || newPlayerBPressed);
    
    // Update game state
    gameState.value.timeRemaining = newTimeRemaining;
    gameState.value.playerAPressed = newPlayerAPressed;
    gameState.value.playerBPressed = newPlayerBPressed;
    gameState.value.activePlayer = newActivePlayer;
    gameState.value.timerActive = newTimerActive;
    gameState.value.timerPaused = isPaused && newTimerActive;
    
    // Update previous state
    previousActivePlayer = newActivePlayer;
    
    // Set totalTime if this is the first status update for a new timer
    if (gameState.value.totalTime === 0 && newTimeRemaining > 0) {
      gameState.value.totalTime = newTimeRemaining;
    }
    
    console.log(`Status update: timer=${newTimeRemaining}, total=${gameState.value.totalTime}, active=${gameState.value.timerActive}, paused=${gameState.value.timerPaused}`);
  }
}

function playTimerExpiredSound() {
  try {
    const audio = new Audio('/family-feud-no-ding.mp3');
    audio.volume = soundVolume.value;
    audio.play().catch(error => {
      console.error('Failed to play timer expired sound:', error);
    });
  } catch (error) {
    console.error('Failed to create audio for timer expired sound:', error);
  }
}

function playPlayerBuzzerSound() {
  try {
    const audio = new Audio('/family-feud-buzzer-sound-effect.mp3');
    audio.volume = soundVolume.value;
    audio.play().catch(error => {
      console.error('Failed to play player buzzer sound:', error);
    });
  } catch (error) {
    console.error('Failed to create audio for player buzzer sound:', error);
  }
}

// Save settings to localStorage
function saveVolumeSettings() {
  try {
    localStorage.setItem('feudAppVolume', soundVolume.value.toString());
  } catch (error) {
    console.error('Failed to save volume settings:', error);
  }
}

function saveTimerDurationSettings() {
  try {
    localStorage.setItem('feudAppTimerDuration', timerDuration.value.toString());
  } catch (error) {
    console.error('Failed to save timer duration settings:', error);
  }
}

// Load settings from localStorage
function loadVolumeSettings() {
  try {
    const savedVolume = localStorage.getItem('feudAppVolume');
    if (savedVolume !== null) {
      const volume = parseFloat(savedVolume);
      if (!isNaN(volume) && volume >= 0 && volume <= 1) {
        soundVolume.value = volume;
      }
    }
  } catch (error) {
    console.error('Failed to load volume settings:', error);
  }
}

function loadTimerDurationSettings() {
  try {
    const savedDuration = localStorage.getItem('feudAppTimerDuration');
    if (savedDuration !== null) {
      const duration = parseInt(savedDuration);
      if (!isNaN(duration) && duration >= 1 && duration <= 300) {
        timerDuration.value = duration;
      }
    }
  } catch (error) {
    console.error('Failed to load timer duration settings:', error);
  }
}

// Watch for changes and save them
watch(soundVolume, () => {
  saveVolumeSettings();
});

watch(timerDuration, () => {
  saveTimerDurationSettings();
});

onMounted(() => {
  refreshPorts();
  loadVolumeSettings();
  loadTimerDurationSettings();
});

onUnmounted(() => {
  stopReading();
});
</script>

<template>
  <Toolbar class="main-toolbar">
    <template #start>
      <div class="game-status" v-if="isConnected">
        <!-- Timer Display -->
        <div class="timer-display">
          <span class="timer-label">Timer:</span>
          <Badge 
            :value="gameState.timerPaused ? `${gameState.timeRemaining}s (Paused)` : gameState.timerActive ? `${gameState.timeRemaining}s` : 'Stopped'"
            :severity="gameState.timerPaused ? 'warning' : gameState.timerActive ? (gameState.timeRemaining <= 5 ? 'danger' : 'success') : 'secondary'"
          />
        </div>
        
        <!-- Player Status -->
        <div class="player-status">
          <div class="player-indicator">
            <span>Player A:</span>
            <Badge 
              :value="gameState.playerAPressed ? 'PRESSED' : 'Ready'"
              :severity="gameState.activePlayer === 'A' ? 'warning' : gameState.playerAPressed ? 'success' : 'secondary'"
            />
          </div>
          <div class="player-indicator">
            <span>Player B:</span>
            <Badge 
              :value="gameState.playerBPressed ? 'PRESSED' : 'Ready'"
              :severity="gameState.activePlayer === 'B' ? 'warning' : gameState.playerBPressed ? 'success' : 'secondary'"
            />
          </div>
        </div>
      </div>
    </template>
    <template #center>
    </template>
    <template #end>
      <!-- Volume Control -->
      <div class="volume-control">
        <i class="pi pi-volume-up"></i>
        <Slider 
          v-model="soundVolume" 
          :min="0" 
          :max="1" 
          :step="0.1"
          class="volume-slider"
        />
        <span class="volume-label">{{ Math.round(soundVolume * 100) }}%</span>
      </div>
      
      <Button 
        v-if="!isConnected"
        label="Connect" 
        icon="pi pi-link"
        @click="reconnect"
        :disabled="!selectedPort"
        severity="success"
        size="small"
      />
      <Button 
        v-else
        label="Disconnect" 
        icon="pi pi-times"
        @click="disconnect"
        severity="danger"
        size="small"
      />
    </template>
  </Toolbar>
  
  <div class="app-container">
    <!-- Connection Setup Card (only shown when not connected) -->
    <Card class="connection-card" v-if="!isConnected">
      <template #title>Serial Connection Setup</template>
      <template #content>
        <div class="connection-controls">
          <div class="port-selection">
            <Dropdown 
              v-model="selectedPort" 
              :options="serialPorts" 
              optionLabel="name" 
              optionValue="path"
              placeholder="Select a serial port"
              class="port-dropdown"
            />
            <Button 
              icon="pi pi-refresh" 
              @click="refreshPorts"
              severity="secondary"
            />
          </div>
          
          <Button 
            label="Connect" 
            icon="pi pi-link"
            @click="connect"
            :disabled="!selectedPort"
            severity="success"
          />
        </div>
        
        <Message 
          :severity="'info'" 
          :closable="false"
          class="status-message"
        >
          {{ connectionStatus }}
        </Message>
      </template>
    </Card>

    <!-- Quick Actions Area (center) -->
    <div class="quick-actions-area" v-if="isConnected">
      <Card class="quick-actions-card">
        <template #title>Game Controls</template>
        <template #content>
          <div class="quick-actions-grid">
            <!-- Timer Duration Setting -->
            <div class="timer-setting">
              <label for="timer-duration">Timer Duration (seconds):</label>
              <InputNumber 
                id="timer-duration"
                v-model="timerDuration" 
                :min="1" 
                :max="300" 
                :disabled="gameState.timerActive || gameState.timerPaused"
              />
            </div>
            
            <!-- Game Control Buttons -->
            <div class="control-buttons">
              <Button 
                :label="gameState.timerPaused ? 'Resume Timer' : 'Start Timer'"
                icon="pi pi-play"
                @click="startTimer"
                :disabled="gameState.timerActive && !gameState.timerPaused"
                severity="success"
                size="large"
              />
              <Button 
                label="Stop Timer" 
                icon="pi pi-stop"
                @click="stopTimer"
                :disabled="!gameState.timerActive"
                severity="danger"
                size="large"
              />
              <Button 
                label="Reset Game" 
                icon="pi pi-refresh"
                @click="resetGame"
                severity="info"
                size="large"
              />
            </div>
            
            <!-- Timer Progress Bar -->
            <div class="timer-progress" v-if="gameState.timerActive">
              <label>Time Remaining: {{ gameState.timeRemaining }}s / {{ gameState.totalTime }}s</label>
              <ProgressBar 
                :value="gameState.totalTime > 0 ? (gameState.timeRemaining / gameState.totalTime) * 100 : 0"
                :class="{ 'timer-critical': gameState.timeRemaining <= 5 }"
                :showValue="false"
              />
            </div>
          </div>
        </template>
      </Card>
    </div>
  </div>

  <!-- Command Prompt Area (bottom) -->
  <div class="serial-communication-area" v-if="isConnected">
    <Card class="communication-card">
      <template #title>Command Prompt</template>
      <template #content>
        <div class="communication-controls">
          <div class="send-controls">
            <InputText 
              v-model="sendData" 
              placeholder="Enter command"
              class="send-input"
              @keyup.enter="sendSerialData"
              autocomplete="off"
              spellcheck="false"
              autocorrect="off"
              autocapitalize="off"
            />
            <Button 
              label="Execute" 
              icon="pi pi-play"
              @click="sendSerialData"
              :disabled="!sendData || isSending"
              :loading="isSending"
              severity="primary"
            />
          </div>
          
          <div class="received-section">
            <div class="received-header">
              <span>Output:</span>
              <Button 
                label="Clear" 
                icon="pi pi-trash"
                @click="clearReceivedData"
                severity="secondary"
                size="small"
              />
            </div>
            <Textarea 
              v-model="receivedData" 
              :readonly="true"
              rows="8"
              class="received-data"
              placeholder="Command output will appear here..."
            />
          </div>
        </div>
      </template>
    </Card>
  </div>
</template>

<style scoped>
.app-container {
  padding: 2rem;
  max-width: 800px;
  margin: 0 auto;
  flex: 1;
  display: flex;
  flex-direction: column;
}

.main-toolbar {
  margin-bottom: 0;
}

.toolbar-title {
  font-weight: 600;
  font-size: 1.1rem;
}

.volume-control {
  display: flex;
  align-items: center;
  gap: 0.5rem;
  margin-right: 1rem;
}

.volume-slider {
  width: 80px;
}

.volume-label {
  font-size: 0.75rem;
  color: var(--text-color-secondary);
  min-width: 35px;
}

.connection-card {
  margin: 2rem auto;
  max-width: 600px;
}

.quick-actions-area {
  flex: 1;
  padding: 2rem 0;
}

.quick-actions-card {
  height: 100%;
}

.quick-actions-grid {
  display: flex;
  flex-direction: column;
  gap: 2rem;
  padding: 1rem;
}

.timer-setting {
  display: flex;
  flex-direction: column;
  gap: 0.5rem;
  align-items: center;
}

.timer-setting label {
  font-weight: 600;
}

.control-buttons {
  display: flex;
  gap: 1rem;
  justify-content: center;
  flex-wrap: wrap;
}

.timer-progress {
  display: flex;
  flex-direction: column;
  gap: 0.5rem;
}

.timer-progress label {
  font-weight: 600;
  text-align: center;
}

.timer-critical {
  --p-progressbar-background: #fee2e2;
  --p-progressbar-value-background: #dc2626;
}

.game-status {
  display: flex;
  align-items: center;
  gap: 2rem;
}

.timer-display {
  display: flex;
  align-items: center;
  gap: 0.5rem;
}

.timer-label {
  font-weight: 600;
}

.player-status {
  display: flex;
  gap: 1rem;
}

.player-indicator {
  display: flex;
  align-items: center;
  gap: 0.5rem;
  font-size: 0.875rem;
}

.serial-communication-area {
  padding: 1rem 0 0 0;
  width: 100%;
}

.connection-controls {
  display: flex;
  flex-direction: column;
  gap: 1rem;
}

.port-selection {
  display: flex;
  gap: 0.5rem;
  align-items: center;
}

.port-dropdown {
  flex: 1;
}

.status-message {
  margin-top: 1rem;
}

.serial-communication-area .communication-card {
  margin: 0;
  border-radius: 0;
}

.communication-controls {
  display: flex;
  flex-direction: column;
  gap: 1.5rem;
}

.send-controls {
  display: flex;
  gap: 0.5rem;
  align-items: center;
}

.send-input {
  flex: 1;
}

.received-section {
  display: flex;
  flex-direction: column;
  gap: 0.5rem;
}

.received-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
}

.received-data {
  font-family: monospace;
  font-size: 0.875rem;
  resize: vertical;
}
</style>

<style>
body {
  margin: 0;
  padding: 0;
  font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif;
  height: 100vh;
}

#app {
  height: 100vh;
  display: flex;
  flex-direction: column;
}
</style>