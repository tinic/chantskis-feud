<script setup lang="ts">
import { ref, onMounted, onUnmounted } from "vue";
import { invoke } from "@tauri-apps/api/core";
import Button from "primevue/button";
import Card from "primevue/card";
import Dropdown from "primevue/dropdown";
import Message from "primevue/message";
import InputText from "primevue/inputtext";
import Textarea from "primevue/textarea";

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
let readInterval: number | null = null;

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
    } catch (error) {
      console.error("Failed to connect:", error);
      connectionStatus.value = `Failed to connect: ${error}`;
    }
  }
}

async function disconnect() {
  try {
    stopReading();
    const message = await invoke<string>("disconnect_serial");
    isConnected.value = false;
    connectionStatus.value = message;
    selectedPort.value = null;
    receivedData.value = "";
  } catch (error) {
    console.error("Failed to disconnect:", error);
  }
}

async function sendSerialData() {
  if (!sendData.value) return;
  
  try {
    const encoder = new TextEncoder();
    const data = encoder.encode(sendData.value + "\n");
    await invoke("send_serial_data", { data: Array.from(data) });
    sendData.value = "";
  } catch (error) {
    console.error("Failed to send data:", error);
  }
}

async function readSerialData() {
  try {
    const data = await invoke<number[]>("read_serial_data");
    if (data && data.length > 0) {
      const decoder = new TextDecoder();
      const text = decoder.decode(new Uint8Array(data));
      receivedData.value += text;
      
      // Auto-scroll to bottom
      const textarea = document.querySelector('.received-data') as HTMLTextAreaElement;
      if (textarea) {
        textarea.scrollTop = textarea.scrollHeight;
      }
    }
  } catch (error) {
    // Ignore timeout errors, they're expected
    if (!error.toString().includes("Not connected")) {
      console.error("Failed to read data:", error);
    }
  }
}

function startReading() {
  if (readInterval) return;
  readInterval = window.setInterval(readSerialData, 100);
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

onMounted(() => {
  refreshPorts();
});

onUnmounted(() => {
  stopReading();
});
</script>

<template>
  <div class="app-container">
    <h1 class="app-title">Chantskis Feud</h1>
    
    <Card class="connection-card">
      <template #title>Serial Connection</template>
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
              :disabled="isConnected"
            />
            <Button 
              icon="pi pi-refresh" 
              @click="refreshPorts"
              :disabled="isConnected"
              severity="secondary"
            />
          </div>
          
          <Button 
            v-if="!isConnected"
            label="Connect" 
            icon="pi pi-link"
            @click="connect"
            :disabled="!selectedPort"
            severity="success"
          />
          <Button 
            v-else
            label="Disconnect" 
            icon="pi pi-times"
            @click="disconnect"
            severity="danger"
          />
        </div>
        
        <Message 
          :severity="isConnected ? 'success' : 'info'" 
          :closable="false"
          class="status-message"
        >
          {{ connectionStatus }}
        </Message>
      </template>
    </Card>

    <Card class="communication-card" v-if="isConnected">
      <template #title>Serial Communication</template>
      <template #content>
        <div class="communication-controls">
          <div class="send-controls">
            <InputText 
              v-model="sendData" 
              placeholder="Enter data to send"
              class="send-input"
              @keyup.enter="sendSerialData"
            />
            <Button 
              label="Send" 
              icon="pi pi-send"
              @click="sendSerialData"
              :disabled="!sendData"
              severity="primary"
            />
          </div>
          
          <div class="received-section">
            <div class="received-header">
              <span>Received Data:</span>
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
              rows="10"
              class="received-data"
              placeholder="No data received yet..."
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
}

.app-title {
  text-align: center;
  margin-bottom: 2rem;
  color: var(--primary-color);
}

.connection-card {
  margin-bottom: 2rem;
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

.communication-card {
  margin-bottom: 1rem;
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
}
</style>