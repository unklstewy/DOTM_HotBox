import { useState, useEffect, useRef } from 'preact/hooks';

interface Config {
  ship_id: string;
  console_id: string;
  terminal_index: number;
}

interface WifiNetwork {
  ssid: string;
  rssi: number;
  secure: boolean;
}

interface FileItem {
  name: string;
  is_dir: boolean;
  size: number;
}

export function App() {
  const [activeTab, setActiveTab] = useState<'status' | 'config' | 'wifi' | 'files' | 'settings'>('status');
  
  // Status State
  const [status, setStatus] = useState({
    online: true,
    ip: '192.168.4.1',
    mode: 'AP', // 'AP' or 'STA'
    ssid: 'SC_Terminal',
    uptime: 0,
    free_heap: 0,
    psram_free: 0
  });

  // Config State
  const [config, setConfig] = useState<Config>({
    ship_id: 'cutlass_black',
    console_id: 'pilot_mfd_left',
    terminal_index: 0
  });
  const [saveLoading, setSaveLoading] = useState(false);
  const [configSuccess, setConfigSuccess] = useState(false);

  // WiFi State
  const [wifiList, setWifiList] = useState<WifiNetwork[]>([]);
  const [wifiLoading, setWifiLoading] = useState(false);
  const [selectedSsid, setSelectedSsid] = useState('');
  const [wifiPassword, setWifiPassword] = useState('');
  const [wifiSuccess, setWifiSuccess] = useState(false);

  // File Manager State
  const [currentPath, setCurrentPath] = useState('');
  const [files, setFiles] = useState<FileItem[]>([]);
  const [filesLoading, setFilesLoading] = useState(false);
  const [uploadProgress, setUploadProgress] = useState<string | null>(null);
  const fileInputRef = useRef<HTMLInputElement>(null);
  const [dragActive, setDragActive] = useState(false);

  // Settings State
  const [backlight, setBacklight] = useState(80);
  const [rebooting, setRebooting] = useState(false);

  // Fetch Status and Config on mount
  useEffect(() => {
    fetchStatus();
    fetchConfig();
    fetchBacklight();
  }, []);

  // Poll status every 5 seconds
  useEffect(() => {
    const interval = setInterval(fetchStatus, 5000);
    return () => clearInterval(interval);
  }, []);

  // Load files when tab changes or path changes
  useEffect(() => {
    if (activeTab === 'files') {
      fetchFiles(currentPath);
    }
  }, [activeTab, currentPath]);

  const fetchStatus = async () => {
    try {
      const res = await fetch('/api/status');
      if (res.ok) {
        const data = await res.json();
        setStatus(data);
      }
    } catch (e) {
      console.error('Failed to fetch status', e);
    }
  };

  const fetchConfig = async () => {
    try {
      const res = await fetch('/api/config');
      if (res.ok) {
        const data = await res.json();
        setConfig(data);
      }
    } catch (e) {
      console.error('Failed to fetch config', e);
    }
  };

  const fetchBacklight = async () => {
    try {
      const res = await fetch('/api/system/backlight');
      if (res.ok) {
        const data = await res.json();
        setBacklight(data.brightness);
      }
    } catch (e) {
      console.error('Failed to fetch backlight', e);
    }
  };

  const handleConfigSubmit = async (e: Event) => {
    e.preventDefault();
    setSaveLoading(true);
    setConfigSuccess(false);
    try {
      const res = await fetch('/api/config', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(config)
      });
      if (res.ok) {
        setConfigSuccess(true);
        setTimeout(() => setConfigSuccess(false), 3000);
      }
    } catch (e) {
      alert('Failed to save config');
    } finally {
      setSaveLoading(false);
    }
  };

  const handleWifiScan = async () => {
    setWifiLoading(true);
    try {
      const res = await fetch('/api/wifi/scan');
      if (res.ok) {
        const data = await res.json();
        setWifiList(data);
      }
    } catch (e) {
      console.error('WiFi scan failed', e);
    } finally {
      setWifiLoading(false);
    }
  };

  const handleWifiConnect = async (e: Event) => {
    e.preventDefault();
    if (!selectedSsid) return;
    setWifiLoading(true);
    setWifiSuccess(false);
    try {
      const res = await fetch('/api/wifi/config', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ ssid: selectedSsid, password: wifiPassword })
      });
      if (res.ok) {
        setWifiSuccess(true);
        setWifiPassword('');
      }
    } catch (e) {
      alert('Failed to configure Wi-Fi');
    } finally {
      setWifiLoading(false);
    }
  };

  const fetchFiles = async (path: string) => {
    setFilesLoading(true);
    try {
      const cleanPath = path.startsWith('/') ? path : '/' + path;
      const res = await fetch(`/api/fs/list?path=${encodeURIComponent(cleanPath)}`);
      if (res.ok) {
        const data = await res.json();
        setFiles(data);
      }
    } catch (e) {
      console.error('Failed to fetch files', e);
    } finally {
      setFilesLoading(false);
    }
  };

  const handleFileUpload = async (filesToUpload: FileList | null) => {
    if (!filesToUpload || filesToUpload.length === 0) return;
    const file = filesToUpload[0];
    
    setUploadProgress(`Uploading ${file.name}...`);
    try {
      const cleanPath = currentPath.endsWith('/') ? currentPath : currentPath + '/';
      const targetPath = '/sdcard' + (cleanPath.startsWith('/') ? cleanPath : '/' + cleanPath) + file.name;
      
      const res = await fetch('/api/fs/upload', {
        method: 'POST',
        headers: {
          'X-File-Path': targetPath,
          'Content-Type': 'application/octet-stream'
        },
        body: file
      });
      
      if (res.ok) {
        setUploadProgress('Upload complete!');
        fetchFiles(currentPath);
        setTimeout(() => setUploadProgress(null), 2000);
      } else {
        setUploadProgress('Upload failed.');
        setTimeout(() => setUploadProgress(null), 3000);
      }
    } catch (e) {
      setUploadProgress('Upload failed.');
      setTimeout(() => setUploadProgress(null), 3000);
    }
  };

  const handleFileDelete = async (filename: string) => {
    if (!confirm(`Are you sure you want to delete ${filename}?`)) return;
    try {
      const cleanPath = currentPath.endsWith('/') ? currentPath : currentPath + '/';
      const targetPath = '/sdcard' + (cleanPath.startsWith('/') ? cleanPath : '/' + cleanPath) + filename;
      
      const res = await fetch('/api/fs/delete', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ path: targetPath })
      });
      
      if (res.ok) {
        fetchFiles(currentPath);
      } else {
        alert('Failed to delete file');
      }
    } catch (e) {
      alert('Failed to delete file');
    }
  };

  const handleBacklightChange = async (val: number) => {
    setBacklight(val);
    try {
      await fetch('/api/system/backlight', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ brightness: val })
      });
    } catch (e) {
      console.error('Failed to set backlight', e);
    }
  };

  const handleReboot = async () => {
    if (!confirm('Reboot SC Terminal?')) return;
    setRebooting(true);
    try {
      await fetch('/api/system/reboot', { method: 'POST' });
    } catch (e) {
      // Expect connection close
    }
    setTimeout(() => {
      window.location.reload();
    }, 5000);
  };

  const navigateToFolder = (dirName: string) => {
    if (dirName === '..') {
      const parts = currentPath.split('/').filter(Boolean);
      parts.pop();
      setCurrentPath('/' + parts.join('/'));
    } else {
      const cleanPath = currentPath.endsWith('/') ? currentPath : currentPath + '/';
      setCurrentPath(cleanPath + dirName);
    }
  };

  const formatSize = (bytes: number) => {
    if (bytes === 0) return '0 B';
    const k = 1024;
    const sizes = ['B', 'KB', 'MB', 'GB'];
    const i = Math.floor(Math.log(bytes) / Math.log(k));
    return parseFloat((bytes / Math.pow(k, i)).toFixed(2)) + ' ' + sizes[i];
  };

  const formatUptime = (seconds: number) => {
    const h = Math.floor(seconds / 3600);
    const m = Math.floor((seconds % 3600) / 60);
    const s = seconds % 60;
    return `${h}h ${m}m ${s}s`;
  };

  return (
    <>
      <header>
        <h1>SC Terminal // <span>Control Portal</span></h1>
        <div class="system-status">
          <div class={`status-badge ${status.online ? 'online' : 'offline'}`}>
            ● System: {status.online ? 'Online' : 'Offline'}
          </div>
          <div class={`status-badge ${status.mode === 'AP' ? 'ap' : 'online'}`}>
            Network: {status.mode} ({status.ssid})
          </div>
        </div>
      </header>

      {rebooting && (
        <div class="alert warning" style="font-size: 1.1rem; justify-content: center; padding: 20px;">
          <div class="spinner"></div>
          Rebooting SC Terminal... Please wait a few seconds.
        </div>
      )}

      {!rebooting && (
        <>
          <div class="nav-tabs">
            <button class={`nav-tab ${activeTab === 'status' ? 'active' : ''}`} onClick={() => setActiveTab('status')}>
              Telemetry
            </button>
            <button class={`nav-tab ${activeTab === 'config' ? 'active' : ''}`} onClick={() => setActiveTab('config')}>
              Ship Config
            </button>
            <button class={`nav-tab ${activeTab === 'wifi' ? 'active' : ''}`} onClick={() => setActiveTab('wifi')}>
              Wi-Fi Setup
            </button>
            <button class={`nav-tab ${activeTab === 'files' ? 'active' : ''}`} onClick={() => setActiveTab('files')}>
              File Manager
            </button>
            <button class={`nav-tab ${activeTab === 'settings' ? 'active' : ''}`} onClick={() => setActiveTab('settings')}>
              System
            </button>
          </div>

          <main style="flex-grow: 1;">
            {/* Status Tab */}
            {activeTab === 'status' && (
              <div class="grid-2">
                <div class="glass-panel">
                  <h2>System Telemetry</h2>
                  <table style="margin-top: 15px;">
                    <tbody>
                      <tr>
                        <td>IP Address</td>
                        <td style="color: var(--primary); font-family: monospace; font-weight: bold;">{status.ip}</td>
                      </tr>
                      <tr>
                        <td>Uptime</td>
                        <td>{formatUptime(status.uptime)}</td>
                      </tr>
                      <tr>
                        <td>Free Internal Heap</td>
                        <td>{formatSize(status.free_heap)}</td>
                      </tr>
                      <tr>
                        <td>Free PSRAM</td>
                        <td>{formatSize(status.psram_free)}</td>
                      </tr>
                    </tbody>
                  </table>
                </div>

                <div class="glass-panel">
                  <h2>Active Configuration</h2>
                  <table style="margin-top: 15px;">
                    <tbody>
                      <tr>
                        <td>Ship Model</td>
                        <td style="color: var(--primary); font-weight: bold; text-transform: uppercase;">{config.ship_id.replace('_', ' ')}</td>
                      </tr>
                      <tr>
                        <td>Console ID</td>
                        <td style="color: var(--primary); font-weight: bold;">{config.console_id}</td>
                      </tr>
                      <tr>
                        <td>Terminal Index</td>
                        <td>{config.terminal_index}</td>
                      </tr>
                    </tbody>
                  </table>
                  <button class="btn" style="margin-top: 25px; width: 100%;" onClick={() => setActiveTab('config')}>
                    Change Configuration
                  </button>
                </div>
              </div>
            )}

            {/* Config Tab */}
            {activeTab === 'config' && (
              <div class="glass-panel" style="max-width: 600px; margin: 0 auto;">
                <h2>Ship &amp; Console Layout</h2>
                <p style="margin-bottom: 25px;">Select which Star Citizen ship model and physical console layout should run on this terminal screen.</p>
                
                {configSuccess && (
                  <div class="alert success">
                    ✓ Configuration saved successfully!
                  </div>
                )}

                <form onSubmit={handleConfigSubmit}>
                  <div class="form-group">
                    <label>Active Ship</label>
                    <select value={config.ship_id} onChange={(e) => setConfig({ ...config, ship_id: (e.target as HTMLSelectElement).value })}>
                      <option value="cutlass_black">Drake Cutlass Black</option>
                      <option value="caterpillar">Drake Caterpillar</option>
                      <option value="corsair">Drake Corsair</option>
                      <option value="avenger_titan">Aegis Avenger Titan</option>
                    </select>
                  </div>

                  <div class="form-group">
                    <label>Console Selection</label>
                    <select value={config.console_id} onChange={(e) => setConfig({ ...config, console_id: (e.target as HTMLSelectElement).value })}>
                      <option value="pilot_mfd_left">Pilot MFD (Left)</option>
                      <option value="pilot_mfd_right">Pilot MFD (Right)</option>
                      <option value="copilot">Copilot Console</option>
                      <option value="engineering">Engineering Panel</option>
                    </select>
                  </div>

                  <div class="form-group">
                    <label>Terminal Index</label>
                    <input 
                      type="number" 
                      min="0" 
                      max="10" 
                      value={config.terminal_index} 
                      onInput={(e) => setConfig({ ...config, terminal_index: parseInt((e.target as HTMLInputElement).value) || 0 })} 
                    />
                  </div>

                  <button type="submit" class="btn" style="width: 100%; margin-top: 10px;" disabled={saveLoading}>
                    {saveLoading ? 'Saving...' : 'Save &amp; Apply'}
                  </button>
                </form>
              </div>
            )}

            {/* Wi-Fi Tab */}
            {activeTab === 'wifi' && (
              <div class="glass-panel" style="max-width: 600px; margin: 0 auto;">
                <h2>Wi-Fi Settings</h2>
                <p style="margin-bottom: 20px;">Configure the SC Terminal to connect to your home Wi-Fi network.</p>

                {wifiSuccess && (
                  <div class="alert success">
                    ✓ Wi-Fi saved! The device will reboot to apply connection.
                  </div>
                )}

                <div class="form-group">
                  <label>Available Networks</label>
                  <button type="button" class="btn accent" style="align-self: flex-start; margin-bottom: 10px;" onClick={handleWifiScan} disabled={wifiLoading}>
                    {wifiLoading ? 'Scanning...' : 'Scan Networks'}
                  </button>
                  
                  {wifiList.length > 0 && (
                    <div class="wifi-scan-list">
                      {wifiList.map((net) => (
                        <div 
                          class={`wifi-network ${selectedSsid === net.ssid ? 'selected' : ''}`}
                          onClick={() => setSelectedSsid(net.ssid)}
                        >
                          <span style="font-weight: bold;">{net.ssid}</span>
                          <span class="wifi-rssi">
                            📶 {net.rssi} dBm {net.secure ? '🔒' : ''}
                          </span>
                        </div>
                      ))}
                    </div>
                  )}
                </div>

                <form onSubmit={handleWifiConnect}>
                  <div class="form-group">
                    <label>SSID (Network Name)</label>
                    <input 
                      type="text" 
                      required 
                      value={selectedSsid} 
                      onInput={(e) => setSelectedSsid((e.target as HTMLInputElement).value)} 
                      placeholder="Select scanned network or enter manually" 
                    />
                  </div>

                  <div class="form-group">
                    <label>Password</label>
                    <input 
                      type="password" 
                      value={wifiPassword} 
                      onInput={(e) => setWifiPassword((e.target as HTMLInputElement).value)} 
                      placeholder="Enter Wi-Fi password" 
                    />
                  </div>

                  <button type="submit" class="btn" style="width: 100%; margin-top: 10px;" disabled={wifiLoading || !selectedSsid}>
                    Save &amp; Connect
                  </button>
                </form>
              </div>
            )}

            {/* File Manager Tab */}
            {activeTab === 'files' && (
              <div class="glass-panel">
                <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 15px;">
                  <h2>SD Card Explorer</h2>
                  <div style="font-family: monospace; font-size: 0.9rem; color: var(--primary);">
                    Path: /sdcard{currentPath || '/'}
                  </div>
                </div>

                {uploadProgress && (
                  <div class="alert info">
                    {uploadProgress}
                  </div>
                )}

                <div 
                  class={`drop-zone ${dragActive ? 'active' : ''}`}
                  onDragOver={(e) => { e.preventDefault(); setDragActive(true); }}
                  onDragLeave={() => setDragActive(false)}
                  onDrop={(e) => { e.preventDefault(); setDragActive(false); if (e.dataTransfer) handleFileUpload(e.dataTransfer.files); }}
                  onClick={() => fileInputRef.current?.click()}
                >
                  <div class="drop-zone-icon">⎗</div>
                  <p style="font-weight: bold; color: var(--primary);">Drag &amp; drop files here or click to upload</p>
                  <p style="font-size: 0.8rem;">Supports config JSON, layout binaries, themes, and graphics.</p>
                  <input 
                    type="file" 
                    ref={fileInputRef} 
                    style="display: none;" 
                    onChange={(e) => handleFileUpload((e.target as HTMLInputElement).files)} 
                  />
                </div>

                {filesLoading ? (
                  <div style="display: flex; justify-content: center; padding: 40px;">
                    <div class="spinner"></div>
                  </div>
                ) : (
                  <table>
                    <thead>
                      <tr>
                        <th>Name</th>
                        <th>Type</th>
                        <th>Size</th>
                        <th style="width: 80px;">Action</th>
                      </tr>
                    </thead>
                    <tbody>
                      {currentPath !== '' && currentPath !== '/' && (
                        <tr>
                          <td 
                            onClick={() => navigateToFolder('..')} 
                            style="cursor: pointer; color: var(--primary); font-weight: bold;"
                          >
                            📁 .. (Parent Directory)
                          </td>
                          <td>Folder</td>
                          <td>-</td>
                          <td></td>
                        </tr>
                      )}
                      
                      {files.map((file) => (
                        <tr>
                          {file.is_dir ? (
                            <td 
                              onClick={() => navigateToFolder(file.name)} 
                              style="cursor: pointer; color: var(--primary); font-weight: bold;"
                            >
                              📁 {file.name}
                            </td>
                          ) : (
                            <td>📄 {file.name}</td>
                          )}
                          <td>{file.is_dir ? 'Folder' : 'File'}</td>
                          <td>{file.is_dir ? '-' : formatSize(file.size)}</td>
                          <td>
                            {!file.is_dir && (
                              <button 
                                class="btn danger" 
                                style="padding: 4px 8px; font-size: 0.75rem;" 
                                onClick={() => handleFileDelete(file.name)}
                              >
                                Delete
                              </button>
                            )}
                          </td>
                        </tr>
                      ))}

                      {files.length === 0 && (
                        <tr>
                          <td colspan={4} style="text-align: center; color: var(--text-secondary); padding: 30px;">
                            No files or directories found in this folder.
                          </td>
                        </tr>
                      )}
                    </tbody>
                  </table>
                )}
              </div>
            )}

            {/* Settings Tab */}
            {activeTab === 'settings' && (
              <div class="glass-panel" style="max-width: 600px; margin: 0 auto;">
                <h2>System Control</h2>
                <p style="margin-bottom: 25px;">Manage low-level hardware parameters and power actions.</p>

                <div class="form-group" style="margin-bottom: 30px;">
                  <label>Display Backlight Brightness ({backlight}%)</label>
                  <div class="slider-container" style="margin-top: 10px;">
                    <span>🔆</span>
                    <input 
                      type="range" 
                      min="5" 
                      max="100" 
                      value={backlight} 
                      onChange={(e) => handleBacklightChange(parseInt((e.target as HTMLInputElement).value))} 
                    />
                    <span>☀</span>
                  </div>
                </div>

                <div style="border-top: 1px solid rgba(0, 240, 255, 0.1); padding-top: 25px; margin-top: 25px;">
                  <h3 style="font-family: 'Orbitron', sans-serif; font-size: 0.95rem; color: var(--accent); margin-bottom: 10px; text-transform: uppercase;">
                    ⚠ Danger Zone
                  </h3>
                  <p style="font-size: 0.85rem; margin-bottom: 20px;">Rebooting the device will disconnect active sessions and reload all settings and assets from flash and SD card.</p>
                  <button class="btn danger" style="width: 100%;" onClick={handleReboot}>
                    Reboot Terminal
                  </button>
                </div>
              </div>
            )}
          </main>
        </>
      )}

      <footer style="margin-top: 40px; padding: 20px 0; border-top: 1px solid rgba(0, 240, 255, 0.05); text-align: center; font-size: 0.8rem; color: var(--text-secondary); font-family: monospace;">
        SC_TERMINAL_SYSTEM // v1.0.0 // RE-TERMINAL-D1001-P4
      </footer>
    </>
  );
}
