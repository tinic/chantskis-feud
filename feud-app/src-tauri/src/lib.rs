use serde::{Deserialize, Serialize};
use serialport::{available_ports, SerialPortType};
use std::sync::Arc;
use std::time::Duration;
use tauri::State;
use std::io::Write;
use tokio::sync::Mutex;

#[derive(Serialize, Deserialize, Debug)]
struct SerialPortInfo {
    name: String,
    path: String,
}

struct SerialConnection {
    port: Option<Box<dyn serialport::SerialPort + Send>>,
}

type SerialState = Arc<Mutex<SerialConnection>>;

#[tauri::command]
fn list_serial_ports() -> Result<Vec<SerialPortInfo>, String> {
    match available_ports() {
        Ok(ports) => {
            let port_list: Vec<SerialPortInfo> = ports
                .into_iter()
                .map(|p| {
                    let name = match &p.port_type {
                        SerialPortType::UsbPort(info) => {
                            format!(
                                "{} ({})",
                                p.port_name,
                                info.product.as_deref().unwrap_or("Unknown Device")
                            )
                        }
                        _ => p.port_name.clone(),
                    };
                    SerialPortInfo {
                        name,
                        path: p.port_name,
                    }
                })
                .collect();
            Ok(port_list)
        }
        Err(e) => Err(format!("Failed to list serial ports: {e}")),
    }
}

#[tauri::command]
async fn connect_serial(port_path: String, state: State<'_, SerialState>) -> Result<String, String> {
    let mut connection = state.lock().await;
    
    // Close existing connection if any
    if connection.port.is_some() {
        connection.port = None;
    }

    // Open new connection
    match serialport::new(&port_path, 115200)
        .timeout(Duration::from_millis(1000))
        .open()
    {
        Ok(port) => {
            connection.port = Some(port);
            Ok(format!("Connected to {port_path}"))
        }
        Err(e) => Err(format!("Failed to connect: {e}")),
    }
}

#[tauri::command]
async fn disconnect_serial(state: State<'_, SerialState>) -> Result<String, String> {
    let mut connection = state.lock().await;
    connection.port = None;
    Ok("Disconnected".to_string())
}

#[tauri::command]
async fn send_serial_data(data: Vec<u8>, state: State<'_, SerialState>) -> Result<(), String> {
    let mut connection = state.lock().await;
    
    if let Some(ref mut port) = connection.port {
        port.write_all(&data)
            .map_err(|e| format!("Failed to send data: {e}"))?;
        port.flush()
            .map_err(|e| format!("Failed to flush data: {e}"))?;
        Ok(())
    } else {
        Err("Not connected to any serial port".to_string())
    }
}

#[tauri::command]
async fn read_serial_data(state: State<'_, SerialState>) -> Result<Vec<u8>, String> {
    // Try to acquire the lock with a timeout to avoid blocking other operations
    let connection_result = tokio::time::timeout(
        Duration::from_millis(50),
        state.lock()
    ).await;
    
    let mut connection = match connection_result {
        Ok(conn) => conn,
        Err(_) => return Ok(vec![]), // Return empty if we can't get lock quickly
    };
    
    if let Some(ref mut port) = connection.port {
        // Perform a non-blocking read
        let mut buffer = vec![0; 1024];
        match port.read(&mut buffer) {
            Ok(n) => {
                buffer.truncate(n);
                Ok(buffer)
            }
            Err(e) => {
                if e.kind() == std::io::ErrorKind::TimedOut {
                    Ok(vec![])
                } else {
                    Err(format!("Failed to read data: {e}"))
                }
            }
        }
    } else {
        Err("Not connected to any serial port".to_string())
    }
}

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    tauri::Builder::default()
        .plugin(tauri_plugin_opener::init())
        .manage(Arc::new(Mutex::new(SerialConnection { port: None })))
        .invoke_handler(tauri::generate_handler![
            list_serial_ports,
            connect_serial,
            disconnect_serial,
            send_serial_data,
            read_serial_data
        ])
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}