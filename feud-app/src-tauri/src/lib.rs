use serde::{Deserialize, Serialize};
use serialport::{available_ports, SerialPortType};
use std::sync::Mutex;
use std::time::Duration;
use tauri::State;
use std::io::Write;

#[derive(Serialize, Deserialize, Debug)]
struct SerialPortInfo {
    name: String,
    path: String,
}

struct SerialConnection {
    port: Option<Box<dyn serialport::SerialPort + Send>>,
}

type SerialState = Mutex<SerialConnection>;

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
        Err(e) => Err(format!("Failed to list serial ports: {}", e)),
    }
}

#[tauri::command]
fn connect_serial(port_path: String, state: State<SerialState>) -> Result<String, String> {
    let mut connection = state.lock().unwrap();
    
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
            Ok(format!("Connected to {}", port_path))
        }
        Err(e) => Err(format!("Failed to connect: {}", e)),
    }
}

#[tauri::command]
fn disconnect_serial(state: State<SerialState>) -> Result<String, String> {
    let mut connection = state.lock().unwrap();
    connection.port = None;
    Ok("Disconnected".to_string())
}

#[tauri::command]
fn send_serial_data(data: Vec<u8>, state: State<SerialState>) -> Result<(), String> {
    let mut connection = state.lock().unwrap();
    
    if let Some(ref mut port) = connection.port {
        port.write_all(&data)
            .map_err(|e| format!("Failed to send data: {}", e))?;
        Ok(())
    } else {
        Err("Not connected to any serial port".to_string())
    }
}

#[tauri::command]
fn read_serial_data(state: State<SerialState>) -> Result<Vec<u8>, String> {
    let mut connection = state.lock().unwrap();
    
    if let Some(ref mut port) = connection.port {
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
                    Err(format!("Failed to read data: {}", e))
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
        .manage(SerialState::new(SerialConnection { port: None }))
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