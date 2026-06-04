import { useState, useEffect, useRef } from 'preact/hooks';
import { DRAKE_SVG, ORIGIN_SVG } from './assets/theme_svgs';


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

interface Widget {
  id: string;
  label: string;
  description: string;
  icon: string;
  row: number;
  col: number;
  width: number;
  height: number;
  widget_type: string;
  hid?: {
    consumer_usage?: number;
    hold_ms?: number;
    gamepad_button?: number;
  };
  state?: {
    gamelink_event?: string | null;
    values?: Record<string, { label: string; color: string }>;
  };
}

interface ConsoleLayout {
  console_id: string;
  display_name: string;
  position: string;
  layout: string;
  actions: Widget[];
}

interface ShipConfig {
  ship_id: string;
  ship_name: string;
  manufacturer: string;
  consoles: ConsoleLayout[];
}

const WIDGET_TYPES = [
  { type: 'btn_momentary', name: 'Momentary Button', icon: '🔘', w: 1, h: 1 },
  { type: 'btn_latching', name: 'Latching Button', icon: '🔒', w: 1, h: 1 },
  { type: 'btn_danger', name: 'Danger Button', icon: '🚨', w: 1, h: 1 },
  { type: 'slider_h', name: 'Horizontal Slider', icon: '↔️', w: 2, h: 1 },
  { type: 'slider_v', name: 'Vertical Slider', icon: '↕️', w: 1, h: 2 },
  { type: 'axis_joystick', name: 'Joystick Axis', icon: '🕹️', w: 2, h: 2 },
  { type: 'axis_dpad', name: 'D-Pad Axis', icon: '➕', w: 2, h: 2 },
  { type: 'axis_haat', name: 'HAT Switch Axis', icon: '🧭', w: 2, h: 2 },
  { type: 'axis_throttle', name: 'Throttle Axis', icon: '📈', w: 1, h: 2 },
  { type: 'axis_yaw', name: 'Yaw Ring Axis', icon: '🔄', w: 2, h: 2 },
  { type: 'axis_rudder', name: 'Rudder Pedals', icon: '👣', w: 3, h: 1 },
  { type: 'knob', name: 'Rotary Knob', icon: '🌀', w: 1, h: 1 },
  { type: 'jog_wheel', name: 'Jog Wheel', icon: '🛞', w: 2, h: 2 }
];

const getWidgetIcon = (type: string) => {
  const found = WIDGET_TYPES.find(w => w.type === type);
  return found ? found.icon : '🔘';
};

export function App() {
  const [activeTab, setActiveTab] = useState<'status' | 'config' | 'wifi' | 'files' | 'settings'>('status');
  
  // Theme and Vector SVG State
  const [editorThemeOverride, setEditorThemeOverride] = useState<'drake' | 'origin' | null>(null);
  const [svgContent, setSvgContent] = useState<string>('');
  const [spriteRects, setSpriteRects] = useState<Record<string, { x: number, y: number, w: number, h: number }>>({});

  
  // Status State
  const [status, setStatus] = useState({
    online: true,
    ip: '192.168.4.1',
    mode: 'AP',
    ssid: 'SC_Terminal',
    uptime: 0,
    free_heap: 0,
    psram_free: 0
  });

  // Config State (Active Device Configuration)
  const [config, setConfig] = useState<Config>({
    ship_id: 'cutlass_black',
    console_id: 'pilot_mfd_left',
    terminal_index: 0
  });
  const [saveLoading, setSaveLoading] = useState(false);
  const [configSuccess, setConfigSuccess] = useState(false);

  // WYSIWYG Editor State
  const [editingShipId, setEditingShipId] = useState<string>('cutlass_black');
  const [shipConfig, setShipConfig] = useState<ShipConfig | null>(null);
  const [activeConsoleId, setActiveConsoleId] = useState<string>('');
  const [selectedWidgetIdx, setSelectedWidgetIdx] = useState<number | null>(null);
  const [editorLoading, setEditorLoading] = useState(false);
  const [newStateValKey, setNewStateValKey] = useState('');

  // Dynamic Ship List & Templates States
  const [shipsList, setShipsList] = useState<{ id: string, name: string }[]>([]);
  const [shipTemplates, setShipTemplates] = useState<ShipConfig[]>([]);
  const [showAddShipModal, setShowAddShipModal] = useState(false);
  const [activeTargetConsoles, setActiveTargetConsoles] = useState<{ id: string, name: string }[]>([]);

  // Add Ship Form States
  const [newShipId, setNewShipId] = useState('');
  const [newShipName, setNewShipName] = useState('');
  const [newShipMfr, setNewShipMfr] = useState('');
  const [newShipTemplateId, setNewShipTemplateId] = useState('custom_empty');

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
  const importFileRef = useRef<HTMLInputElement>(null);
  const [dragActive, setDragActive] = useState(false);

  // Settings State
  const [backlight, setBacklight] = useState(80);
  const [rebooting, setRebooting] = useState(false);

  // Fetch initial telemetry, device config and ships list
  useEffect(() => {
    fetchStatus();
    fetchConfig();
    fetchBacklight();
    fetchShipsList();
    fetchShipTemplates();
  }, []);

  // Sync active console targets whenever config.ship_id changes
  useEffect(() => {
    const loadActiveTargetConsoles = async () => {
      try {
        const res = await fetch(`/api/fs/read?path=${encodeURIComponent(`/ships/${config.ship_id}.json`)}`);
        if (res.ok) {
          const data = await res.json();
          if (data.consoles) {
            setActiveTargetConsoles(data.consoles.map((c: any) => ({ id: c.console_id, name: c.display_name })));
            if (!data.consoles.some((c: any) => c.console_id === config.console_id) && data.consoles.length > 0) {
              setConfig(prev => ({ ...prev, console_id: data.consoles[0].console_id }));
            }
          }
        }
      } catch (e) {
        console.warn('Failed to load active target consoles, using fallbacks', e);
        setActiveTargetConsoles([
          { id: 'pilot_mfd_left', name: 'Pilot MFD (Left)' },
          { id: 'pilot_mfd_right', name: 'Pilot MFD (Right)' },
          { id: 'copilot', name: 'Copilot Console' },
          { id: 'engineering', name: 'Engineering Panel' }
        ]);
      }
    };
    if (config.ship_id) {
      loadActiveTargetConsoles();
    }
  }, [config.ship_id]);

  // Poll status every 5 seconds
  useEffect(() => {
    const interval = setInterval(fetchStatus, 5000);
    return () => clearInterval(interval);
  }, []);

  // Sync Layout Editor whenever editingShipId changes
  useEffect(() => {
    fetchShipConfig(editingShipId);
  }, [editingShipId]);

  // Load files when tab changes or path changes
  useEffect(() => {
    if (activeTab === 'files') {
      fetchFiles(currentPath);
    }
  }, [activeTab, currentPath]);

  const getActiveTheme = () => {
    if (editorThemeOverride) return editorThemeOverride;
    const mfr = (shipConfig?.manufacturer || '').toLowerCase();
    if (mfr.includes('origin')) return 'origin';
    return 'drake';
  };
  const activeTheme = getActiveTheme();

  const parseAndSetRects = (svgText: string) => {
    const rects: Record<string, { x: number, y: number, w: number, h: number }> = {};
    const parser = new DOMParser();
    const doc = parser.parseFromString(svgText, 'image/svg+xml');
    const groups = doc.querySelectorAll('g[id^="sprite-"]');
    groups.forEach(g => {
      const id = g.getAttribute('id') || '';
      const rectStr = g.getAttribute('data-rect') || '';
      if (id && rectStr) {
        const parts = rectStr.split(',').map(Number);
        if (parts.length === 4) {
          rects[id] = { x: parts[0], y: parts[1], w: parts[2], h: parts[3] };
        }
      }
    });
    setSpriteRects(rects);
  };

  useEffect(() => {
    const loadSvg = async () => {
      try {
        const res = await fetch(`/api/fs/read?path=${encodeURIComponent(`/assets/themes/${activeTheme}/sprite_sheet.svg`)}`);
        if (res.ok) {
          const text = await res.text();
          if (text && text.includes('<svg')) {
            setSvgContent(text);
            parseAndSetRects(text);
            return;
          }
        }
      } catch (e) {
        console.warn('Failed to fetch SVG sprite sheet from server, falling back to local asset', e);
      }
      // Fallback
      const fallbackText = activeTheme === 'origin' ? ORIGIN_SVG : DRAKE_SVG;
      setSvgContent(fallbackText);
      parseAndSetRects(fallbackText);
    };
    loadSvg();
  }, [activeTheme, shipConfig?.manufacturer]);

  function WidgetSprite({ type, active }: { type: string, active?: boolean }) {
    const renderSprite = (spriteId: string, style?: any) => {
      const rect = spriteRects[spriteId];
      if (!rect) {
        return <div style={{ width: '100%', height: '100%', border: '1px dashed rgba(0, 240, 255, 0.2)', borderRadius: '4px' }} />;
      }
      return (
        <svg
          viewBox={`${rect.x} ${rect.y} ${rect.w} ${rect.h}`}
          width="100%"
          height="100%"
          style={{ display: 'block', pointerEvents: 'none', ...style }}
        >
          <use href={`#${spriteId}`} />
        </svg>
      );
    };

    switch (type) {
      case 'btn_momentary':
        return renderSprite(active ? 'sprite-btn_momentary_active' : 'sprite-btn_momentary_idle');
      case 'btn_latching':
        return renderSprite(active ? 'sprite-btn_latching_on' : 'sprite-btn_latching_off');
      case 'btn_danger':
        return renderSprite('sprite-btn_danger');
      case 'slider_h':
        return (
          <div style="position: relative; width: 100%; height: 100%; display: flex; align-items: center; justify-content: center;">
            {renderSprite('sprite-slider_track_h')}
            <div style="position: absolute; width: 30%; height: 80%; left: 35%; top: 10%;">
              {renderSprite('sprite-slider_thumb')}
            </div>
          </div>
        );
      case 'slider_v':
        return (
          <div style="position: relative; width: 100%; height: 100%; display: flex; align-items: center; justify-content: center;">
            {renderSprite('sprite-slider_track_v')}
            <div style="position: absolute; width: 80%; height: 30%; top: 35%; left: 10%;">
              {renderSprite('sprite-slider_thumb')}
            </div>
          </div>
        );
      case 'axis_joystick':
        return (
          <div style="position: relative; width: 100%; height: 100%; display: flex; align-items: center; justify-content: center;">
            {renderSprite('sprite-axis_joystick_base')}
            <div style="position: absolute; width: 35%; height: 35%; left: 32.5%; top: 32.5%;">
              {renderSprite('sprite-axis_joystick_thumb')}
            </div>
          </div>
        );
      case 'axis_dpad':
        return renderSprite('sprite-axis_dpad_base');
      case 'axis_haat':
        return (
          <div style="position: relative; width: 100%; height: 100%; display: flex; align-items: center; justify-content: center;">
            {renderSprite('sprite-axis_haat_base')}
            <div style="position: absolute; width: 24%; height: 24%; left: 38%; top: 38%;">
              {renderSprite('sprite-axis_haat_cursor')}
            </div>
          </div>
        );
      case 'axis_throttle':
        return (
          <div style="position: relative; width: 100%; height: 100%; display: flex; align-items: center; justify-content: center;">
            {renderSprite('sprite-axis_throttle_track')}
            <div style="position: absolute; width: 80%; height: 30%; left: 10%; top: 35%;">
              {renderSprite('sprite-axis_throttle_grip')}
            </div>
          </div>
        );
      case 'axis_yaw':
        return (
          <div style="position: relative; width: 100%; height: 100%; display: flex; align-items: center; justify-content: center;">
            {renderSprite('sprite-axis_yaw_ring')}
            <div style="position: absolute; width: 15%; height: 70%; left: 42.5%; top: 15%;">
              {renderSprite('sprite-axis_yaw_needle')}
            </div>
          </div>
        );
      case 'axis_rudder':
        return (
          <div style="position: relative; width: 100%; height: 100%; display: flex; align-items: center; justify-content: center;">
            {renderSprite('sprite-axis_rudder_track')}
            <div style="position: absolute; width: 25%; height: 80%; left: 37.5%; top: 10%;">
              {renderSprite('sprite-axis_rudder_pedal')}
            </div>
          </div>
        );
      case 'knob':
        return (
          <div style="position: relative; width: 100%; height: 100%; display: flex; align-items: center; justify-content: center;">
            {renderSprite('sprite-knob_ring')}
            <div style="position: absolute; width: 70%; height: 70%; left: 15%; top: 15%;">
              {renderSprite('sprite-knob_cap')}
            </div>
          </div>
        );
      case 'jog_wheel':
        return renderSprite('sprite-jog_wheel_f0');
      default:
        return renderSprite('sprite-btn_momentary_idle');
    }
  }

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
        // Default layout editor to active device ship
        setEditingShipId(data.ship_id);
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

  const fetchShipsList = async (templatesToMerge?: ShipConfig[]) => {
    const templates = templatesToMerge || shipTemplates;
    const listMap = new Map<string, { id: string; name: string }>();

    // First populate from templates (defaults)
    templates.forEach(t => {
      listMap.set(t.ship_id, { id: t.ship_id, name: t.ship_name });
    });

    try {
      const res = await fetch('/api/fs/list?path=%2Fships');
      if (res.ok) {
        const fileItems = await res.json();
        fileItems
          .filter((item: any) => !item.is_dir && item.name.endsWith('.json') && item.name !== 'ship_templates.json')
          .forEach((item: any) => {
            const id = item.name.replace('.json', '');
            const name = id.split('_').map((s: string) => s.charAt(0).toUpperCase() + s.slice(1)).join(' ');
            listMap.set(id, { id, name });
          });
      }
    } catch (e) {
      console.warn('Failed to read ships from filesystem API, using only templates/fallbacks', e);
    }

    // Convert Map back to array
    const mergedList = Array.from(listMap.values());
    // Sort alphabetically by name
    mergedList.sort((a, b) => a.name.localeCompare(b.name));
    setShipsList(mergedList);
  };


  const fetchShipTemplates = async () => {
    let loadedTemplates: ShipConfig[] = [];
    try {
      const res = await fetch('/api/fs/read?path=%2Fships%2Fship_templates.json');
      if (res.ok) {
        const data = await res.json();
        if (data.templates) {
          loadedTemplates = data.templates;
        }
      }
    } catch (e) {
      console.warn('Using fallback templates library', e);
    }

    if (loadedTemplates.length === 0) {
      loadedTemplates = [
        {
          ship_id: 'misc_prospector',
          ship_name: 'MISC Prospector',
          manufacturer: 'Musashi Industrial & Starflight Concern',
          consoles: [
            {
              console_id: 'mining_operations',
              display_name: 'Mining Operations Console',
              position: 'pilot',
              layout: 'grid_6x6',
              actions: [
                {
                  id: 'mining_laser_power',
                  label: 'LASER PWR',
                  description: 'Toggle Mining Laser Power',
                  icon: 'btn_latching',
                  row: 0,
                  col: 0,
                  width: 1,
                  height: 1,
                  widget_type: 'btn_latching',
                  hid: { consumer_usage: 0, hold_ms: 0, gamepad_button: 1 },
                  state: {
                    gamelink_event: 'mining_laser_active',
                    values: {
                      active: { label: 'LASER ON', color: '#FF8800' },
                      idle: { label: 'LASER OFF', color: '#00FF88' }
                    }
                  }
                }
              ]
            }
          ]
        },
        {
          ship_id: 'rsi_scorpius',
          ship_name: 'RSI Scorpius',
          manufacturer: 'Roberts Space Industries',
          consoles: [
            {
              console_id: 'turret_control',
              display_name: 'Copilot Turret Console',
              position: 'copilot',
              layout: 'grid_6x6',
              actions: [
                {
                  id: 'turret_power',
                  label: 'TURRET ON',
                  description: 'Power up weapon turret systems',
                  icon: 'btn_latching',
                  row: 0,
                  col: 0,
                  width: 1,
                  height: 1,
                  widget_type: 'btn_latching',
                  hid: { consumer_usage: 0, hold_ms: 0, gamepad_button: 1 }
                }
              ]
            }
          ]
        }
      ];
    }
    setShipTemplates(loadedTemplates);
    fetchShipsList(loadedTemplates);
  };


  const handleAddNewShipSubmit = async (e: Event) => {
    e.preventDefault();
    if (!newShipId.trim()) return;
    const cleanId = newShipId.trim().toLowerCase().replace(/[^a-z0-9_]/g, '_');
    
    if (shipsList.some(s => s.id === cleanId)) {
      alert('A ship layout with this ID already exists.');
      return;
    }

    let newConfig: ShipConfig;

    if (newShipTemplateId === 'custom_empty') {
      newConfig = {
        ship_id: cleanId,
        ship_name: newShipName.trim() || cleanId.toUpperCase(),
        manufacturer: newShipMfr.trim() || 'Custom Manufacturer',
        consoles: [
          {
            console_id: 'pilot_mfd',
            display_name: 'Pilot MFD',
            position: 'pilot',
            layout: 'grid_4x5',
            actions: []
          }
        ]
      };
    } else {
      const template = shipTemplates.find(t => t.ship_id === newShipTemplateId);
      if (!template) {
        alert('Selected template not found.');
        return;
      }
      newConfig = {
        ...template,
        ship_id: cleanId,
        ship_name: newShipName.trim() || template.ship_name,
        manufacturer: newShipMfr.trim() || template.manufacturer
      };
    }

    setSaveLoading(true);
    try {
      const targetPath = `/sdcard/ships/${cleanId}.json`;
      const res = await fetch('/api/fs/upload', {
        method: 'POST',
        headers: {
          'X-File-Path': targetPath,
          'Content-Type': 'application/octet-stream'
        },
        body: JSON.stringify(newConfig, null, 4)
      });
      if (res.ok) {
        alert(`Successfully created and saved ship layout: ${cleanId}`);
        await fetchShipsList();
        setEditingShipId(cleanId);
        setShipConfig(newConfig);
        if (newConfig.consoles.length > 0) {
          setActiveConsoleId(newConfig.consoles[0].console_id);
        }
        setShowAddShipModal(false);
        setNewShipId('');
        setNewShipName('');
        setNewShipMfr('');
        setNewShipTemplateId('custom_empty');
      } else {
        alert('Failed to save the new ship layout file to the device.');
      }
    } catch (e) {
      alert('Error creating ship: ' + e);
    } finally {
      setSaveLoading(false);
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

  // WYSIWYG Load Layout from SD Card or Fallback
  const fetchShipConfig = async (shipId: string) => {
    setEditorLoading(true);
    setSelectedWidgetIdx(null);
    try {
      const res = await fetch(`/api/fs/read?path=${encodeURIComponent(`/ships/${shipId}.json`)}`);
      if (res.ok) {
        const data = await res.json();
        setShipConfig(data);
        if (data.consoles && data.consoles.length > 0) {
          setActiveConsoleId(data.consoles[0].console_id);
        } else {
          setActiveConsoleId('');
        }
        setEditorLoading(false);
        return;
      }
    } catch (e) {
      console.warn('Failed to load ship config from SD card, checking templates', e);
    }

    // Try finding template in templates
    const template = shipTemplates.find(t => t.ship_id === shipId);
    if (template) {
      setShipConfig(JSON.parse(JSON.stringify(template)));
      if (template.consoles && template.consoles.length > 0) {
        setActiveConsoleId(template.consoles[0].console_id);
      } else {
        setActiveConsoleId('');
      }
    } else {
      // Create empty fallback
      const mockConfig: ShipConfig = {
        ship_id: shipId,
        ship_name: shipId.split('_').map((s: string) => s.charAt(0).toUpperCase() + s.slice(1)).join(' '),
        manufacturer: shipId.startsWith('drake') ? 'Drake Interplanetary' : 'Aegis Dynamics',
        consoles: [
          {
            console_id: 'pilot_mfd_left',
            display_name: 'Pilot MFD — Left',
            position: 'pilot',
            layout: 'grid_4x5',
            actions: []
          }
        ]
      };
      setShipConfig(mockConfig);
      setActiveConsoleId(mockConfig.consoles[0].console_id);
    }
    setEditorLoading(false);
  };


  // Drag and Drop Handlers
  const handlePaletteDragStart = (e: DragEvent, widgetType: string) => {
    e.dataTransfer?.setData('widget_type', widgetType);
    e.dataTransfer?.setData('drag_source', 'palette');
  };

  const handleWidgetDragStart = (e: DragEvent, idx: number) => {
    e.dataTransfer?.setData('widget_index', idx.toString());
    e.dataTransfer?.setData('drag_source', 'grid');
  };

  const handleWidgetDrop = (e: DragEvent, row: number, col: number) => {
    e.preventDefault();
    if (!shipConfig || !activeConsoleId) return;
    
    const source = e.dataTransfer?.getData('drag_source');
    const updatedConfig = { ...shipConfig };
    const consoleIndex = updatedConfig.consoles.findIndex(c => c.console_id === activeConsoleId);
    if (consoleIndex === -1) return;
    
    const targetConsole = updatedConfig.consoles[consoleIndex];
    
    if (source === 'palette') {
      const widgetType = e.dataTransfer?.getData('widget_type');
      const widgetDef = WIDGET_TYPES.find(w => w.type === widgetType);
      if (!widgetType || !widgetDef) return;

      const gamepadBtn = getNextAvailableGamepadButton(targetConsole);
      const newWidget: Widget = {
        id: `${widgetType}_${Date.now().toString().slice(-4)}`,
        label: widgetDef.name.split(' ').map(s => s[0]).join('').slice(0, 8).toUpperCase(),
        description: widgetDef.name,
        icon: widgetType,
        row,
        col,
        width: widgetDef.w,
        height: widgetDef.h,
        widget_type: widgetType,
        hid: { consumer_usage: 0, hold_ms: 0, gamepad_button: gamepadBtn },
        state: { gamelink_event: null }
      };
      
      targetConsole.actions.push(newWidget);
      setShipConfig(updatedConfig);
      setSelectedWidgetIdx(targetConsole.actions.length - 1);
    } else if (source === 'grid') {
      const idxStr = e.dataTransfer?.getData('widget_index');
      if (idxStr === undefined) return;
      const idx = parseInt(idxStr);
      
      if (targetConsole.actions[idx]) {
        targetConsole.actions[idx].row = row;
        targetConsole.actions[idx].col = col;
        setShipConfig(updatedConfig);
        setSelectedWidgetIdx(idx);
      }
    }
  };

  const getNextAvailableGamepadButton = (consoleLayout: ConsoleLayout) => {
    const usedButtons = new Set(
      consoleLayout.actions
        .map(a => a.hid?.gamepad_button)
        .filter(b => b !== undefined && b > 0)
    );
    for (let i = 1; i <= 32; i++) {
      if (!usedButtons.has(i)) return i;
    }
    return 1;
  };

  const handleUpdateWidgetProperty = (property: string, value: any) => {
    if (!shipConfig || !activeConsoleId || selectedWidgetIdx === null) return;
    
    const updatedConfig = { ...shipConfig };
    const consoleIndex = updatedConfig.consoles.findIndex(c => c.console_id === activeConsoleId);
    if (consoleIndex === -1) return;

    const widget = updatedConfig.consoles[consoleIndex].actions[selectedWidgetIdx];
    if (!widget) return;

    if (property.startsWith('hid.')) {
      const hidProp = property.split('.')[1];
      if (!widget.hid) widget.hid = {};
      (widget.hid as any)[hidProp] = value;
    } else if (property.startsWith('state.')) {
      const stateProp = property.split('.')[1];
      if (!widget.state) widget.state = { gamelink_event: null };
      (widget.state as any)[stateProp] = value;
    } else {
      (widget as any)[property] = value;
    }

    setShipConfig(updatedConfig);
  };

  const handleRemoveWidget = () => {
    if (!shipConfig || !activeConsoleId || selectedWidgetIdx === null) return;
    if (!confirm('Remove this control element?')) return;

    const updatedConfig = { ...shipConfig };
    const consoleIndex = updatedConfig.consoles.findIndex(c => c.console_id === activeConsoleId);
    if (consoleIndex === -1) return;

    updatedConfig.consoles[consoleIndex].actions.splice(selectedWidgetIdx, 1);
    setShipConfig(updatedConfig);
    setSelectedWidgetIdx(null);
  };

  // Add state value to game link settings
  const handleAddStateValue = () => {
    if (!shipConfig || !activeConsoleId || selectedWidgetIdx === null || !newStateValKey.trim()) return;

    const updatedConfig = { ...shipConfig };
    const consoleIndex = updatedConfig.consoles.findIndex(c => c.console_id === activeConsoleId);
    const widget = updatedConfig.consoles[consoleIndex].actions[selectedWidgetIdx];
    
    if (!widget.state) widget.state = { gamelink_event: null };
    if (!widget.state.values) widget.state.values = {};
    
    widget.state.values[newStateValKey.trim().toLowerCase()] = {
      label: newStateValKey.toUpperCase(),
      color: '#00F0FF'
    };

    setShipConfig(updatedConfig);
    setNewStateValKey('');
  };

  const handleRemoveStateValue = (valKey: string) => {
    if (!shipConfig || !activeConsoleId || selectedWidgetIdx === null) return;
    
    const updatedConfig = { ...shipConfig };
    const consoleIndex = updatedConfig.consoles.findIndex(c => c.console_id === activeConsoleId);
    const widget = updatedConfig.consoles[consoleIndex].actions[selectedWidgetIdx];

    if (widget.state?.values && widget.state.values[valKey]) {
      delete widget.state.values[valKey];
      setShipConfig(updatedConfig);
    }
  };

  // Console management
  const handleAddConsole = () => {
    const name = prompt('Enter Console Display Name:', 'Auxiliary Console');
    if (!name) return;
    const id = name.toLowerCase().replace(/[^a-z0-9]/g, '_');
    
    if (shipConfig?.consoles.some(c => c.console_id === id)) {
      alert('A console with that ID already exists.');
      return;
    }

    const updatedConfig = { ...shipConfig } as ShipConfig;
    const newConsole: ConsoleLayout = {
      console_id: id,
      display_name: name,
      position: 'pilot',
      layout: 'grid_4x5',
      actions: []
    };

    updatedConfig.consoles.push(newConsole);
    setShipConfig(updatedConfig);
    setActiveConsoleId(id);
    setSelectedWidgetIdx(null);
  };

  const handleDeleteConsole = () => {
    if (!shipConfig || !activeConsoleId) return;
    if (shipConfig.consoles.length <= 1) {
      alert('A ship layout must contain at least one console.');
      return;
    }
    if (!confirm('Are you sure you want to delete this entire console layout? This action cannot be undone.')) return;

    const updatedConfig = { ...shipConfig };
    updatedConfig.consoles = updatedConfig.consoles.filter(c => c.console_id !== activeConsoleId);
    
    setShipConfig(updatedConfig);
    setActiveConsoleId(updatedConfig.consoles[0].console_id);
    setSelectedWidgetIdx(null);
  };

  // Save to SD Card via upload endpoint
  const handleSaveLayoutToDevice = async () => {
    if (!shipConfig) return;
    setSaveLoading(true);
    try {
      const targetPath = `/sdcard/ships/${editingShipId}.json`;
      const res = await fetch('/api/fs/upload', {
        method: 'POST',
        headers: {
          'X-File-Path': targetPath,
          'Content-Type': 'application/octet-stream'
        },
        body: JSON.stringify(shipConfig, null, 4)
      });
      if (res.ok) {
        alert(`Successfully uploaded and saved layout configuration to: ${targetPath}`);
      } else {
        alert('Failed to upload layout to the target device. Check network logs.');
      }
    } catch (e) {
      alert(`Network error saving layout: ${e}`);
    } finally {
      setSaveLoading(false);
    }
  };

  const handleExportLayoutFile = () => {
    if (!shipConfig) return;
    const blob = new Blob([JSON.stringify(shipConfig, null, 4)], { type: 'application/json' });
    const url = URL.createObjectURL(blob);
    const link = document.createElement('a');
    link.href = url;
    link.download = `${editingShipId}.json`;
    link.click();
    URL.revokeObjectURL(url);
  };

  const handleImportLayoutFile = (e: Event) => {
    const file = (e.target as HTMLInputElement).files?.[0];
    if (!file) return;
    
    const reader = new FileReader();
    reader.onload = (evt) => {
      try {
        const parsed = JSON.parse(evt.target?.result as string);
        if (!parsed.ship_id || !parsed.consoles) {
          alert('Incorrect JSON format: missing ship_id or consoles field.');
          return;
        }
        setShipConfig(parsed);
        setEditingShipId(parsed.ship_id);
        if (parsed.consoles.length > 0) {
          setActiveConsoleId(parsed.consoles[0].console_id);
        }
        setSelectedWidgetIdx(null);
        alert('Successfully imported layout config file!');
      } catch (err) {
        alert('Invalid JSON file format.');
      }
    };
    reader.readAsText(file);
  };

  const parseLayoutGrid = (layoutStr: string): [number, number] => {
    if (!layoutStr || !layoutStr.startsWith('grid_')) return [4, 5];
    const match = layoutStr.match(/grid_(\d+)x(\d+)/);
    if (!match) return [4, 5];
    return [parseInt(match[1]) || 4, parseInt(match[2]) || 5];
  };

  const getWidgetDisplayName = (type: string) => {
    const found = WIDGET_TYPES.find(w => w.type === type);
    return found ? found.name : type;
  };

  // WiFi Handlers
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

  // File Manager Handlers
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

  // Backlight Handlers
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

  // Editor Calculations
  const activeConsole = shipConfig?.consoles.find(c => c.console_id === activeConsoleId);
  const [gridCols, gridRows] = activeConsole ? parseLayoutGrid(activeConsole.layout) : [4, 5];
  const selectedWidget = activeConsole?.actions[selectedWidgetIdx !== null ? selectedWidgetIdx : -1];

  // Build grid drop zone boxes
  const gridCells = [];
  for (let r = 0; r < gridRows; r++) {
    for (let c = 0; c < gridCols; c++) {
      gridCells.push(
        <div 
          class="grid-cell-drop"
          onDragOver={(e) => e.preventDefault()}
          onDrop={(e) => handleWidgetDrop(e, r, c)}
          style={`grid-row: ${r + 1}; grid-column: ${c + 1};`}
        >
          <span class="cell-coord">{r},{c}</span>
        </div>
      );
    }
  }

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

          <main style="flex-grow: 1; display: flex; flex-direction: column;">
            {/* Telemetry Tab */}
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
                    Configure Layouts &amp; Run
                  </button>
                </div>
              </div>
            )}

            {/* Config Tab (Drag & Drop Console Builder Dashboard) */}
            {activeTab === 'config' && (
              <div style="display: flex; flex-direction: column; gap: 20px; width: 100%;">
                
                {configSuccess && (
                  <div class="alert success" style="margin-bottom: 0;">
                    ✓ Active terminal target configuration updated successfully!
                  </div>
                )}
                
                {/* 1. Terminal Active Settings & Device Save */}
                <div class="glass-panel" style="display: flex; flex-wrap: wrap; justify-content: space-between; align-items: center; gap: 20px;">
                  <div>
                    <h2 style="margin-bottom: 5px;">Active Terminal Target</h2>
                    <p style="font-size: 0.85rem;">Set which layout coordinates the physical screen renders.</p>
                  </div>
                  <form onSubmit={handleConfigSubmit} style="display: flex; flex-wrap: wrap; align-items: center; gap: 15px;">
                    <div style="display: flex; flex-direction: column; gap: 4px;">
                      <label style="font-size: 0.7rem;">Active Ship</label>
                      <select value={config.ship_id} onChange={(e) => setConfig({ ...config, ship_id: (e.target as HTMLSelectElement).value })} style="padding: 6px 10px; font-size: 0.85rem; width: 180px;">
                        {shipsList.map(s => (
                          <option value={s.id}>{s.name}</option>
                        ))}
                      </select>
                    </div>
                    <div style="display: flex; flex-direction: column; gap: 4px;">
                      <label style="font-size: 0.7rem;">Selected Console</label>
                      <select value={config.console_id} onChange={(e) => setConfig({ ...config, console_id: (e.target as HTMLSelectElement).value })} style="padding: 6px 10px; font-size: 0.85rem; width: 180px;">
                        {activeTargetConsoles.map(c => (
                          <option value={c.id}>{c.name}</option>
                        ))}
                      </select>
                    </div>
                    <div style="display: flex; flex-direction: column; gap: 4px;">
                      <label style="font-size: 0.7rem;">Index</label>
                      <input 
                        type="number" 
                        min="0" 
                        max="10" 
                        value={config.terminal_index} 
                        onInput={(e) => setConfig({ ...config, terminal_index: parseInt((e.target as HTMLInputElement).value) || 0 })} 
                        style="padding: 6px 10px; font-size: 0.85rem; width: 60px;"
                      />
                    </div>
                    <button type="submit" class="btn" style="padding: 8px 16px; font-size: 0.8rem; margin-top: 18px;" disabled={saveLoading}>
                      {saveLoading ? 'Setting...' : 'Set Active Run Target'}
                    </button>
                  </form>
                </div>

                {/* 2. Visual WYSIWYG Layout Editor */}
                <div class="glass-panel" style="display: flex; flex-direction: column; gap: 15px;">
                  <div style="display: flex; flex-wrap: wrap; justify-content: space-between; align-items: center; border-bottom: 1px solid var(--border-color); padding-bottom: 15px;">
                    <div>
                      <h2>Console Layout WYSIWYG Editor</h2>
                      <div style="display: flex; align-items: center; gap: 10px; margin-top: 8px; flex-wrap: wrap;">
                        <span style="font-size: 0.85rem; color: var(--text-secondary);">Editing Ship Layout:</span>
                        <select value={editingShipId} onChange={(e) => setEditingShipId((e.target as HTMLSelectElement).value)} style="padding: 4px 8px; width: 170px; font-size: 0.85rem; height: auto;">
                          {shipsList.map(s => (
                            <option value={s.id}>{s.name}</option>
                          ))}
                        </select>
                        <span style="font-size: 0.85rem; color: var(--text-secondary); margin-left: 10px;">Active Theme:</span>
                        <select 
                          value={editorThemeOverride || 'auto'} 
                          onChange={(e) => {
                            const val = (e.target as HTMLSelectElement).value;
                            setEditorThemeOverride(val === 'auto' ? null : val as 'drake' | 'origin');
                          }} 
                          style="padding: 4px 8px; width: 130px; font-size: 0.85rem; height: auto;"
                        >
                          <option value="auto">Auto ({getActiveTheme()})</option>
                          <option value="drake">Drake (Military)</option>
                          <option value="origin">Origin (Lux)</option>
                        </select>

                      </div>
                    </div>
                    
                    {/* Action Tools */}
                    <div style="display: flex; gap: 10px;">
                      <button class="btn" onClick={handleSaveLayoutToDevice} disabled={saveLoading || !shipConfig} style="padding: 8px 16px; font-size: 0.8rem;">
                        💾 Save to Device
                      </button>
                      <button class="btn accent" onClick={handleExportLayoutFile} disabled={!shipConfig} style="padding: 8px 16px; font-size: 0.8rem;">
                        📤 Export JSON
                      </button>
                      <button class="btn" onClick={() => importFileRef.current?.click()} style="padding: 8px 16px; font-size: 0.8rem;">
                        📥 Import JSON
                      </button>
                      <input 
                        type="file" 
                        ref={importFileRef} 
                        style="display: none;" 
                        accept=".json"
                        onChange={handleImportLayoutFile} 
                      />
                    </div>
                  </div>

                  {editorLoading ? (
                    <div style="display: flex; flex-direction: column; align-items: center; padding: 60px 0; gap: 15px;">
                      <div class="spinner"></div>
                      <p>Loading layout JSON coordinates...</p>
                    </div>
                  ) : shipConfig ? (
                    <div class="editor-workspace">
                      
                      {/* Left Sidebar: Controls Palette & Properties Inspector */}
                      <div class="editor-sidebar-container">
                        
                        {/* Selector Tab for Sidebar */}
                        <div class="properties-inspector" style="margin-bottom: 20px;">
                          <h3 style="color: var(--primary); font-family: 'Orbitron', sans-serif; font-size: 0.85rem; margin-bottom: 12px; text-transform: uppercase;">
                            {selectedWidget ? 'Widget Properties' : 'Controls Toolbox'}
                          </h3>
                          
                          {selectedWidget ? (
                            /* Properties Inspector Panel */
                            <div style="display: flex; flex-direction: column; gap: 14px;">
                              <div style="background: rgba(255,255,255,0.02); padding: 10px; border-radius: 6px; border: 1px solid var(--border-color);">
                                <span style="font-size: 0.75rem; color: var(--text-secondary); text-transform: uppercase;">Type</span>
                                <div style="font-weight: bold; color: var(--primary); font-size: 0.9rem; margin-top: 2px;">
                                  {getWidgetIcon(selectedWidget.widget_type)} {getWidgetDisplayName(selectedWidget.widget_type)}
                                </div>
                              </div>

                              <div class="form-group" style="margin-bottom: 0;">
                                <label style="font-size: 0.7rem;">Action ID</label>
                                <input 
                                  type="text" 
                                  value={selectedWidget.id} 
                                  onInput={(e) => handleUpdateWidgetProperty('id', (e.target as HTMLInputElement).value)} 
                                  style="padding: 6px 10px; font-size: 0.85rem;"
                                />
                              </div>

                              <div class="form-group" style="margin-bottom: 0;">
                                <label style="font-size: 0.7rem;">Display Label</label>
                                <input 
                                  type="text" 
                                  value={selectedWidget.label} 
                                  onInput={(e) => handleUpdateWidgetProperty('label', (e.target as HTMLInputElement).value)} 
                                  style="padding: 6px 10px; font-size: 0.85rem;"
                                />
                              </div>

                              <div class="form-group" style="margin-bottom: 0;">
                                <label style="font-size: 0.7rem;">Description</label>
                                <input 
                                  type="text" 
                                  value={selectedWidget.description} 
                                  onInput={(e) => handleUpdateWidgetProperty('description', (e.target as HTMLInputElement).value)} 
                                  style="padding: 6px 10px; font-size: 0.85rem;"
                                />
                              </div>

                              <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 10px;">
                                <div class="form-group" style="margin-bottom: 0;">
                                  <label style="font-size: 0.7rem;">Col Span (W)</label>
                                  <input 
                                    type="number" 
                                    min="1" 
                                    max={gridCols} 
                                    value={selectedWidget.width} 
                                    onInput={(e) => handleUpdateWidgetProperty('width', parseInt((e.target as HTMLInputElement).value) || 1)} 
                                    style="padding: 6px 10px; font-size: 0.85rem;"
                                  />
                                </div>
                                <div class="form-group" style="margin-bottom: 0;">
                                  <label style="font-size: 0.7rem;">Row Span (H)</label>
                                  <input 
                                    type="number" 
                                    min="1" 
                                    max={gridRows} 
                                    value={selectedWidget.height} 
                                    onInput={(e) => handleUpdateWidgetProperty('height', parseInt((e.target as HTMLInputElement).value) || 1)} 
                                    style="padding: 6px 10px; font-size: 0.85rem;"
                                  />
                                </div>
                              </div>

                              {/* HID Coordinates */}
                              <div style="border-top: 1px solid rgba(0, 240, 255, 0.1); padding-top: 10px; display: flex; flex-direction: column; gap: 10px;">
                                <span style="font-family: 'Orbitron', sans-serif; font-size: 0.75rem; color: var(--accent); font-weight: bold; text-transform: uppercase;">
                                  HID USB Mapping
                                </span>
                                
                                <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 10px;">
                                  <div class="form-group" style="margin-bottom: 0;">
                                    <label style="font-size: 0.65rem;">Gamepad Btn (1-32)</label>
                                    <input 
                                      type="number" 
                                      min="0" 
                                      max="32" 
                                      value={selectedWidget.hid?.gamepad_button || 0} 
                                      onInput={(e) => handleUpdateWidgetProperty('hid.gamepad_button', parseInt((e.target as HTMLInputElement).value) || 0)} 
                                      style="padding: 6px 10px; font-size: 0.85rem;"
                                    />
                                  </div>
                                  <div class="form-group" style="margin-bottom: 0;">
                                    <label style="font-size: 0.65rem;">Consumer Code</label>
                                    <input 
                                      type="number" 
                                      min="0" 
                                      value={selectedWidget.hid?.consumer_usage || 0} 
                                      onInput={(e) => handleUpdateWidgetProperty('hid.consumer_usage', parseInt((e.target as HTMLInputElement).value) || 0)} 
                                      style="padding: 6px 10px; font-size: 0.85rem;"
                                    />
                                  </div>
                                </div>
                                <div class="form-group" style="margin-bottom: 0;">
                                  <label style="font-size: 0.65rem;">Hold Pulse ms</label>
                                  <input 
                                    type="number" 
                                    min="0" 
                                    value={selectedWidget.hid?.hold_ms || 0} 
                                    onInput={(e) => handleUpdateWidgetProperty('hid.hold_ms', parseInt((e.target as HTMLInputElement).value) || 0)} 
                                    style="padding: 6px 10px; font-size: 0.85rem;"
                                  />
                                </div>
                              </div>

                              {/* GameLink State Event Linker */}
                              <div style="border-top: 1px solid rgba(0, 240, 255, 0.1); padding-top: 10px; display: flex; flex-direction: column; gap: 10px;">
                                <span style="font-family: 'Orbitron', sans-serif; font-size: 0.75rem; color: var(--accent); font-weight: bold; text-transform: uppercase;">
                                  GameLink Feedback
                                </span>
                                <div class="form-group" style="margin-bottom: 0;">
                                  <label style="font-size: 0.65rem;">State Changed Event</label>
                                  <input 
                                    type="text" 
                                    placeholder="e.g. gear_state_changed"
                                    value={selectedWidget.state?.gamelink_event || ''} 
                                    onInput={(e) => handleUpdateWidgetProperty('state.gamelink_event', (e.target as HTMLInputElement).value || null)} 
                                    style="padding: 6px 10px; font-size: 0.85rem;"
                                  />
                                </div>

                                {/* States Values Config */}
                                {selectedWidget.state?.gamelink_event && (
                                  <div style="display: flex; flex-direction: column; gap: 8px;">
                                    <label style="font-size: 0.65rem; color: var(--primary);">State Values Mapping</label>
                                    
                                    {selectedWidget.state.values && Object.keys(selectedWidget.state.values).length > 0 && (
                                      <div style="display: flex; flex-direction: column; gap: 4px; background: rgba(0,0,0,0.2); padding: 6px; border-radius: 4px; max-height: 120px; overflow-y: auto;">
                                        {Object.entries(selectedWidget.state.values).map(([k, val]: [string, any]) => (
                                          <div style="display: flex; justify-content: space-between; align-items: center; font-size: 0.75rem; padding: 4px; border-bottom: 1px solid rgba(255,255,255,0.03);">
                                            <span style="font-family: monospace; font-weight: bold; color: var(--accent);">{k}</span>
                                            <div style="display: flex; align-items: center; gap: 6px;">
                                              <span style={`color: ${val.color}; font-size: 0.7rem;`}>{val.label}</span>
                                              <button 
                                                type="button" 
                                                onClick={() => handleRemoveStateValue(k)} 
                                                style="background:transparent; border:none; color:var(--error); cursor:pointer; font-size:0.75rem; padding: 0 2px;"
                                              >
                                                ❌
                                              </button>
                                            </div>
                                          </div>
                                        ))}
                                      </div>
                                    )}
                                    
                                    <div style="display: flex; gap: 6px; align-items: center;">
                                      <input 
                                        type="text" 
                                        placeholder="Add value key" 
                                        value={newStateValKey} 
                                        onInput={(e) => setNewStateValKey((e.target as HTMLInputElement).value)}
                                        style="padding: 4px 8px; font-size: 0.75rem; flex-grow: 1;"
                                      />
                                      <button type="button" class="btn accent" onClick={handleAddStateValue} style="padding: 4px 8px; font-size: 0.7rem;">
                                        Add
                                      </button>
                                    </div>
                                  </div>
                                )}
                              </div>

                              <div style="display: flex; gap: 10px; margin-top: 10px;">
                                <button type="button" class="btn danger" onClick={handleRemoveWidget} style="flex-grow: 1; padding: 8px; font-size: 0.8rem;">
                                  🗑️ Delete Control
                                </button>
                                <button type="button" class="btn" onClick={() => setSelectedWidgetIdx(null)} style="padding: 8px; font-size: 0.8rem;">
                                  Done
                                </button>
                              </div>
                            </div>
                          ) : (
                            /* Drag & Drop Palette Toolbox */
                            <div class="widget-palette">
                              <p style="font-size: 0.8rem; margin-bottom: 12px; line-height: 1.4;">Drag controls from this toolbox and drop them onto the console grid mapping canvas.</p>
                              <div style="display: flex; flex-direction: column; gap: 8px; max-height: 520px; overflow-y: auto; padding-right: 5px;">
                                {WIDGET_TYPES.map(w => (
                                  <div
                                    class="palette-item"
                                    draggable
                                    onDragStart={(e) => handlePaletteDragStart(e, w.type)}
                                  >
                                    <div style="width: 48px; height: 32px; flex-shrink: 0; display: flex; align-items: center; justify-content: center;">
                                      <WidgetSprite type={w.type} />
                                    </div>
                                    <div style="text-align: left;">
                                      <div style="font-weight: bold; font-size: 0.85rem; color: var(--text-primary);">{w.name}</div>
                                      <div style="font-size: 0.7rem; color: var(--text-secondary);">Default dimensions: {w.w}x{w.h}</div>
                                    </div>
                                  </div>
                                ))}
                              </div>

                            </div>
                          )}
                        </div>

                      </div>

                      {/* Right Panel: Active Console Layout Grid */}
                      <div class="editor-canvas-container" onClick={() => setSelectedWidgetIdx(null)}>
                        
                        {/* Tab Headers representing console files */}
                        <div class="editor-console-tabs">
                          <div style="display: flex; gap: 6px; overflow-x: auto; flex-grow: 1;">
                            {shipConfig.consoles.map(c => (
                              <button
                                class={`editor-console-tab ${activeConsoleId === c.console_id ? 'active' : ''}`}
                                onClick={(e) => { e.stopPropagation(); setActiveConsoleId(c.console_id); setSelectedWidgetIdx(null); }}
                              >
                                🖥️ {c.display_name}
                              </button>
                            ))}
                          </div>
                          
                          <button class="btn accent" onClick={(e) => { e.stopPropagation(); handleAddConsole(); }} style="padding: 6px 10px; font-size: 0.75rem; white-space: nowrap;">
                            ✚ New Console
                          </button>
                        </div>

                        {activeConsole ? (
                          <div style="display: flex; flex-direction: column; gap: 15px; margin-top: 5px;">
                            
                            {/* Console Configuration Properties */}
                            <div style="display: flex; flex-wrap: wrap; gap: 20px; background: rgba(255,255,255,0.01); padding: 12px; border-radius: 6px; border: 1px solid rgba(0, 240, 255, 0.08); align-items: center; justify-content: space-between;">
                              <div style="display: flex; gap: 15px; flex-wrap: wrap; align-items: center;">
                                <div style="display: flex; flex-direction: column; gap: 2px;">
                                  <label style="font-size: 0.65rem;">Console Name</label>
                                  <input 
                                    type="text" 
                                    value={activeConsole.display_name} 
                                    onInput={(e) => {
                                      const updated = { ...shipConfig };
                                      const idx = updated.consoles.findIndex(c => c.console_id === activeConsoleId);
                                      if (idx !== -1) {
                                        updated.consoles[idx].display_name = (e.target as HTMLInputElement).value;
                                        setShipConfig(updated);
                                      }
                                    }} 
                                    style="padding: 4px 8px; font-size: 0.8rem; width: 180px; height: auto;"
                                  />
                                </div>
                                <div style="display: flex; flex-direction: column; gap: 2px;">
                                  <label style="font-size: 0.65rem;">Layout Grid Structure</label>
                                  <select 
                                    value={activeConsole.layout} 
                                    onChange={(e) => {
                                      const updated = { ...shipConfig };
                                      const idx = updated.consoles.findIndex(c => c.console_id === activeConsoleId);
                                      if (idx !== -1) {
                                        updated.consoles[idx].layout = (e.target as HTMLSelectElement).value;
                                        setShipConfig(updated);
                                      }
                                    }} 
                                    style="padding: 4px 8px; font-size: 0.8rem; width: 140px; height: auto;"
                                  >
                                    <option value="grid_4x5">4 Columns x 5 Rows</option>
                                    <option value="grid_6x6">6 Columns x 6 Rows</option>
                                    <option value="grid_8x5">8 Columns x 5 Rows</option>
                                    <option value="grid_8x8">8 Columns x 8 Rows</option>
                                    <option value="grid_10x5">10 Columns x 5 Rows</option>
                                    <option value="grid_12x6">12 Columns x 6 Rows</option>
                                  </select>
                                </div>
                              </div>
                              
                              <button class="btn danger" onClick={(e) => { e.stopPropagation(); handleDeleteConsole(); }} style="padding: 6px 12px; font-size: 0.75rem;">
                                🗑️ Delete Console
                              </button>
                            </div>

                            {/* Main Drag/Drop Grid Layout Area */}
                            <div style="background: rgba(0,0,0,0.4); border-radius: 8px; border: 1px solid var(--border-color); padding: 15px; position: relative;">
                              <div style="font-family: monospace; font-size: 0.75rem; color: var(--primary); margin-bottom: 8px; text-transform: uppercase;">
                                Grid: {gridCols} Cols x {gridRows} Rows (D1001 LCD LCD boundaries layout)
                              </div>
                              
                              <div 
                                class="editor-grid" 
                                style={`grid-template-columns: repeat(${gridCols}, 1fr); grid-template-rows: repeat(${gridRows}, 80px);`}
                              >
                                {/* Background helper cell drops */}
                                {gridCells}

                                {/* Placed controls */}
                                {activeConsole.actions.map((act, index) => {
                                  const isSelected = selectedWidgetIdx === index;
                                  const isButton = act.widget_type.startsWith('btn_');
                                  return (
                                    <div
                                      class={`placed-widget ${act.widget_type || 'btn_momentary'} ${isSelected ? 'selected' : ''}`}
                                      style={`grid-row: ${act.row + 1} / span ${act.height || 1}; grid-column: ${act.col + 1} / span ${act.width || 1};`}
                                      onClick={(e) => { e.stopPropagation(); setSelectedWidgetIdx(index); }}
                                      draggable
                                      onDragStart={(e) => handleWidgetDragStart(e, index)}
                                    >
                                      <WidgetSprite type={act.widget_type} active={isSelected} />
                                      <div class={`widget-label ${isButton ? 'button-label' : 'caption-label'}`}>
                                        {act.label || act.id}
                                      </div>
                                      <div class="widget-dim">{act.width}×{act.height}</div>
                                    </div>
                                  );
                                })}

                              </div>
                            </div>
                            
                            <p style="font-size: 0.75rem; color: var(--text-secondary); text-align: center;">💡 Tip: You can drag placed widget items around the canvas grid directly to reposition them.</p>

                          </div>
                        ) : (
                          <div style="padding: 40px; text-align: center; color: var(--text-secondary);">
                            No consoles defined. Click "New Console" to start.
                          </div>
                        )}

                      </div>

                    </div>
                  ) : (
                    <div style="padding: 30px; text-align: center;">
                      Failed to fetch or generate ship config layouts.
                    </div>
                  )}

                </div>

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

      {showAddShipModal && (
        <div class="modal-overlay" onClick={() => setShowAddShipModal(false)}>
          <div class="modal-content glass-panel" onClick={(e) => e.stopPropagation()}>
            <h2 style="color: var(--primary); font-family: 'Orbitron', sans-serif; border-bottom: 1px solid rgba(0, 240, 255, 0.15); padding-bottom: 10px; margin-bottom: 20px; font-size: 1.2rem;">
              ✚ Create New Ship Layout
            </h2>
            <form onSubmit={handleAddNewShipSubmit} style="display: flex; flex-direction: column; gap: 15px;">
              <div class="form-group" style="margin-bottom: 0;">
                <label style="font-size: 0.75rem;">Unique Ship ID (lowercase, no spaces, e.g. misc_prospector)</label>
                <input 
                  type="text" 
                  required 
                  placeholder="e.g. anvil_arrow"
                  value={newShipId} 
                  onInput={(e) => setNewShipId((e.target as HTMLInputElement).value)} 
                  style="padding: 6px 10px; font-size: 0.85rem;"
                />
              </div>
              <div class="form-group" style="margin-bottom: 0;">
                <label style="font-size: 0.75rem;">Display Name (e.g. Anvil Arrow)</label>
                <input 
                  type="text" 
                  required 
                  placeholder="e.g. Anvil Arrow"
                  value={newShipName} 
                  onInput={(e) => setNewShipName((e.target as HTMLInputElement).value)} 
                  style="padding: 6px 10px; font-size: 0.85rem;"
                />
              </div>
              <div class="form-group" style="margin-bottom: 0;">
                <label style="font-size: 0.75rem;">Manufacturer (e.g. Anvil Aerospace)</label>
                <input 
                  type="text" 
                  required 
                  placeholder="e.g. Anvil Aerospace"
                  value={newShipMfr} 
                  onInput={(e) => setNewShipMfr((e.target as HTMLInputElement).value)} 
                  style="padding: 6px 10px; font-size: 0.85rem;"
                />
              </div>
              <div class="form-group" style="margin-bottom: 0;">
                <label style="font-size: 0.75rem;">Layout Template Preset</label>
                <select 
                  value={newShipTemplateId} 
                  onChange={(e) => setNewShipTemplateId((e.target as HTMLSelectElement).value)}
                  style="padding: 6px 10px; font-size: 0.85rem;"
                >
                  <option value="custom_empty">Custom Empty Layout (grid_4x5)</option>
                  {shipTemplates.map(t => (
                    <option value={t.ship_id}>{t.manufacturer} — {t.ship_name} Preset</option>
                  ))}
                </select>
              </div>
              <div style="display: flex; gap: 15px; margin-top: 15px; justify-content: flex-end;">
                <button type="button" class="btn danger" onClick={() => setShowAddShipModal(false)} style="padding: 8px 16px; font-size: 0.8rem;">
                  Cancel
                </button>
                <button type="submit" class="btn accent" style="padding: 8px 24px; font-size: 0.8rem;" disabled={saveLoading}>
                  Create Ship Layout
                </button>
              </div>
            </form>
          </div>
        </div>
      )}

      <footer style="margin-top: 40px; padding: 20px 0; border-top: 1px solid rgba(0, 240, 255, 0.05); text-align: center; font-size: 0.8rem; color: var(--text-secondary); font-family: monospace;">
        SC_TERMINAL_SYSTEM // v1.0.0 // RE-TERMINAL-D1001-P4
      </footer>
      <div style={{ display: 'none' }} dangerouslySetInnerHTML={{ __html: svgContent }} />
    </>
  );
}

