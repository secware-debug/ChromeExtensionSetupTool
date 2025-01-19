
# Chrome Extension Install/Uninstall Tool

This repository contains a Chrome extension management tool implemented in three programming languages: **Rust**, **C++**, and **JavaScript**. Each implementation provides functionality to install and uninstall Chrome extensions in developer mode.
**Note**: This tool is intended solely for testing and learning purposes. It must not be used for any unethical or malicious activities.

---

## Table of Contents

- [Overview](#overview)
- [Features](#features)
- [Usage](#usage)
  - [JavaScript Project](#javascript-project)
  - [Rust Project](#rust-project)
  - [C++ Project](#c-project)
- [Technical Details](#technical-details)
- [Folder Structure](#folder-structure)

---

## Overview

This tool allows you to:
- Install a Chrome extension by extracting its `.zip` package into the appropriate browser directories.
- Uninstall a Chrome extension with its name by removing its files and updating browser preferences.

Each implementation supports the following key operations:
1. Read and modify Chrome user profile directories.
2. Handle file operations like extracting `.zip` files and modifying JSON configuration files.
3. Use cryptographic hashing (e.g., SHA256 and HMAC) for securing extension metadata.

---

## Features

- **Multi-language Support**:
  - Rust: Command-line interface for efficient operations.
  - C++: GUI-based application with a tray icon for easy interaction.
  - JavaScript: Node.js-based tool for quick execution in scripts or terminals.

- **Extension Management**:
  - Install extensions from `.zip` files.
  - Uninstall extensions by name.
  - Modify Chrome preferences (`Preferences` and `Secure Preferences` files).

---

## Usage

### JavaScript Project

1. **Setup**:
   - Navigate to the `JS Project` directory.
   - Install dependencies:
     ```bash
     npm install unzipper
     ```

2. **Execution**:
   - **Install Extension**:
     ```bash
     node main.js -i <path_to_extension_zip>
     ```
     Replace `<path_to_extension_zip>` with the full path to the `.zip` file containing the Chrome extension.

   - **Uninstall Extension**:
     ```bash
     node main.js -u
     ```

### Rust Project

1. **Setup**:
   - Navigate to the `Rust Project` directory.
   - Build the project:
     ```bash
     cargo build
     ```

2. **Execution**:
   - **Install Extension**:
     ```bash
     target/debug/rust_project install <path_to_extension_zip>
     ```
     Replace `<path_to_extension_zip>` with the path to the extension `.zip` file.

   - **Uninstall Extension**:
     ```bash
     target/debug/rust_project uninstall <extension_name>
     ```

### C++ Project

1. **Setup**:
   - Open the `VS Project` in Visual Studio 2022.
   - Build the solution to generate the executable.

2. **Execution**:
   - Place an extension's zip file to same directory to the compiled executable and run the application.
   - Use the tray icon to `Install` or `Uninstall` the extension.

---

## Technical Details

### Workflow for Installation:
1. Extract the `.zip` file of the Chrome extension.
2. Parse the `manifest.json` file to retrieve the extension name.
3. Copy extracted files to the appropriate Chrome profile directory.
4. Update `Preferences` and `Secure Preferences` files to register the extension.
5. Secure the preferences with HMAC-SHA256 for Chrome integrity checks.

### Workflow for Uninstallation:
1. Identify the extension directory and remove its files.
2. Update `Preferences` and `Secure Preferences` files to deregister the extension.
3. Recalculate and secure HMAC-SHA256 for modified preferences.

### Cryptographic Operations:
- **SHA256**:
  - Used to generate a unique `extension ID` from the file path.
- **HMAC-SHA256**:
  - Used to secure modified preferences and ensure integrity.

---

## Folder Structure

```plaintext
.
├── JS Project/               # JavaScript implementation
├── Rust Project/            # Rust implementation
├── VS Project/              # C++ implementation (Visual Studio 2022)
├── extension_changesearchengine.zip  # Test Chrome extension
├── .gitignore
├── README.md                # Project documentation
```

---

## License

This project is open-source and distributed under the MIT License.

---

## Acknowledgments

Special thanks to the contributors for implementing this tool in multiple languages and ensuring its functionality across different environments.
