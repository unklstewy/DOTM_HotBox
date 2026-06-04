# Agent Instruction: Phase 4 - Web Management Portal

**Objective**: Introduce a browser-based dashboard for device configuration, utilizing `esp_http_server` and a lightweight Vanilla JS frontend.

## Context
A modern smart peripheral needs a web interface for robust configuration. The user wants to manage Button Layouts, Network Settings, SD Card files, Camera Settings, Audio Codecs, and general device configuration. This requires a robust RESTful API on the ESP32 and a responsive web application served from the device itself.

## Execution Steps

1. **Create the `sc_web` Component**
   - Create `components/sc_web/CMakeLists.txt` and `components/sc_web/include/sc_web.h`.
   - Implement `sc_web_init()` which instantiates an `esp_http_server`.
   - Add `esp_http_server` as a requirement in CMake.

2. **Develop the REST API**
   - Register HTTP URI handlers for standard CRUD operations against `sc_config`.
   - Examples: 
     - `GET /api/config` -> Returns current NVS configuration as JSON.
     - `POST /api/config` -> Accepts JSON, updates `sc_config`, and calls `sc_config_save()`.
     - `GET /api/network` -> Returns Wi-Fi connection status and IP address from `sc_network`.
     - `GET /api/files` -> Lists contents of `/sdcard/`.
   - Ensure the server correctly handles CORS (Cross-Origin Resource Sharing) headers so the frontend can be developed and tested locally on a PC before deployment.

3. **Develop the Frontend (Vue/Preact)**
   - In a new top-level `web/` directory, initialize a modern JS project using Vite (e.g., React/Preact or Vue).
   - Use TypeScript and a component-based architecture for "Dashboard", "Network", "Buttons", and "Storage" views.
   - Use `fetch()` to interact with the ESP32 REST API. Configure the local dev server proxy to point to the ESP32 IP to avoid CORS during development.
   - Provide a build script that bundles the app into static files (`dist/`).

4. **Serve the Frontend**
   - Since Phase 3 moved assets to the SD Card, map the HTTP server's `/` route to serve the built static files from the `/sdcard/www/` directory via the VFS.
   - Provide instructions or a Python script to deploy the `dist/` contents to the SD card.

5. **Verify**
   - Compile locally with `idf.py build`.
   - Ensure that the HTTP server successfully returns JSON payloads when queried.
