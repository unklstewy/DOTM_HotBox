#!/usr/bin/env bash
# =============================================================================
# prep_sdcard.sh — SC Terminal SD Card Deployment Tool
#
# Detects the SD card, rasterizes sprites on the host, builds/deploys the
# web portal, then syncs all firmware assets to the card.
#
# Usage:
#   ./tools/prep_sdcard.sh [options]
#
# Options:
#   --mount-point <path>  Override auto-detected mount point
#   --dry-run             Show what would be done, without writing
#   --force               Skip the confirmation prompt
#   --demo                Also copy the demo ship layout from ~/Pictures/
#   --build-web           Run 'npm run build' in web_portal/ before deploying
#   --skip-rasterize      Skip host-side SVG sprite rasterization
#
# SD card layout (FAT32, relative to mount root):
#   assets/
#     images/             ← splash screens, logos
#     themes/
#       drake/            ← SVG source + rasterized sprites/ subdir
#         sprites/        ← per-sprite RGB565 .bin + sprites_meta.json
#       origin/
#         sprites/
#   ships/                ← Ship JSON layout files
#   web/                  ← Web portal UI (index.html + assets/)
# =============================================================================

set -euo pipefail

# ── Colours ──────────────────────────────────────────────────────────────────
RED='\033[0;31m'
GRN='\033[0;32m'
YLW='\033[0;33m'
BLU='\033[0;34m'
CYN='\033[0;36m'
BOLD='\033[1m'
RST='\033[0m'

# ── Defaults ──────────────────────────────────────────────────────────────────
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

SDCARD_SOURCE="${PROJECT_ROOT}/sdcard"
IMAGES_SOURCE="${PROJECT_ROOT}/data/assets/images"
WEB_DIST="${PROJECT_ROOT}/web_portal/dist"
DEMO_JSON="${HOME}/Pictures/demo_controls.json"
RASTERIZE_PY="${SCRIPT_DIR}/rasterize_sprites.py"

MOUNT_POINT=""
DRY_RUN=false
FORCE=false
INCLUDE_DEMO=false
BUILD_WEB=false
SKIP_RASTERIZE=false

# ── Argument parsing ──────────────────────────────────────────────────────────
while [[ $# -gt 0 ]]; do
    case "$1" in
        --mount-point)     MOUNT_POINT="$2"; shift 2 ;;
        --dry-run)         DRY_RUN=true; shift ;;
        --force)           FORCE=true; shift ;;
        --demo)            INCLUDE_DEMO=true; shift ;;
        --build-web)       BUILD_WEB=true; shift ;;
        --skip-rasterize)  SKIP_RASTERIZE=true; shift ;;
        -h|--help)
            sed -n '3,25p' "$0" | sed 's/^# \?//'
            exit 0 ;;
        *) echo -e "${RED}Unknown option: $1${RST}"; exit 1 ;;
    esac
done

# ── Header ────────────────────────────────────────────────────────────────────
echo -e "${BOLD}${CYN}"
echo "  ┌──────────────────────────────────────────────┐"
echo "  │   SC Terminal — SD Card Deployment Tool      │"
echo "  └──────────────────────────────────────────────┘"
echo -e "${RST}"

# ── Auto-detect mount point ───────────────────────────────────────────────────
auto_detect_mount() {
    echo -e "${BLU}[→] Auto-detecting SD card...${RST}" >&2

    local candidate=""

    # Strategy 1: removable block device with a mount point
    candidate=$(lsblk -J -o NAME,RM,TYPE,MOUNTPOINTS 2>/dev/null | \
        python3 -c "
import json, sys
data = json.load(sys.stdin)
for dev in data.get('blockdevices', []):
    if dev.get('rm') and dev.get('type') == 'disk':
        for part in dev.get('children', []):
            for mp in (part.get('mountpoints') or []):
                if mp:
                    print(mp)
                    sys.exit(0)
" 2>/dev/null || true)

    # Strategy 2: match the known SD card UUID / label (F095-9D7D)
    if [[ -z "$candidate" ]]; then
        candidate=$(lsblk -J -o NAME,RM,TYPE,MOUNTPOINTS,LABEL,UUID 2>/dev/null | \
            python3 -c "
import json, sys
data = json.load(sys.stdin)
TARGET = 'F0959D7D'
for dev in data.get('blockdevices', []):
    for part in (dev.get('children') or []):
        raw = ((part.get('uuid') or '') + (part.get('label') or '')).upper().replace('-','')
        if TARGET in raw:
            for mp in (part.get('mountpoints') or []):
                if mp:
                    print(mp)
                    sys.exit(0)
" 2>/dev/null || true)
    fi

    # Strategy 3: any volume auto-mounted under /run/media/$USER
    if [[ -z "$candidate" ]]; then
        local user_media="/run/media/${USER}"
        if [[ -d "$user_media" ]]; then
            for mp in "$user_media"/*/; do
                [[ -d "$mp" ]] && candidate="${mp%/}" && break
            done
        fi
    fi

    [[ -n "$candidate" ]] && echo "${candidate%/}"
}

if [[ -z "$MOUNT_POINT" ]]; then
    MOUNT_POINT="$(auto_detect_mount)"
fi

if [[ -z "$MOUNT_POINT" ]]; then
    echo -e "${RED}[✗] No SD card mount point detected.${RST}"
    echo "    Insert the card, wait for it to be auto-mounted, then retry."
    echo "    Or specify manually:  $0 --mount-point /run/media/${USER}/F095-9D7D"
    exit 1
fi

MOUNT_POINT="${MOUNT_POINT%/}"

if ! mountpoint -q "$MOUNT_POINT" && ! findmnt -n "$MOUNT_POINT" &>/dev/null; then
    if [[ ! -d "$MOUNT_POINT" ]] || [[ -z "$(ls -A "$MOUNT_POINT" 2>/dev/null)" ]]; then
        echo -e "${RED}[✗] Mount point '${MOUNT_POINT}' does not appear to be mounted.${RST}"
        exit 1
    fi
fi

echo -e "${GRN}[✓] SD card detected at: ${BOLD}${MOUNT_POINT}${RST}"

# ── Step 0a: Host-side sprite rasterization ───────────────────────────────────
if ! $SKIP_RASTERIZE; then
    echo -e "\n${BLU}[→] Step 0a: Rasterizing SVG sprites on host...${RST}"

    if ! python3 -c "import cairosvg, PIL" 2>/dev/null; then
        echo -e "${YLW}[!] Python deps missing. Run: pip install -r tools/requirements_atlas.txt${RST}"
        echo -e "${YLW}    Skipping rasterization — sprites on card may be stale.${RST}"
    elif $DRY_RUN; then
        echo -e "${YLW}    [dry-run] Would run: python3 ${RASTERIZE_PY} --all${RST}"
    else
        python3 "${RASTERIZE_PY}" --all
        echo -e "${GRN}[✓] Sprite rasterization complete.${RST}"
    fi
else
    echo -e "${YLW}[!] Skipping sprite rasterization (--skip-rasterize).${RST}"
fi

# ── Step 0b: Web portal build ─────────────────────────────────────────────────
if $BUILD_WEB; then
    echo -e "\n${BLU}[→] Step 0b: Building web portal...${RST}"
    if $DRY_RUN; then
        echo -e "${YLW}    [dry-run] Would run: npm run build in ${PROJECT_ROOT}/web_portal/${RST}"
    else
        (cd "${PROJECT_ROOT}/web_portal" && npm run build)
        echo -e "${GRN}[✓] Web portal built.${RST}"
    fi
else
    echo -e "${BLU}[→] Step 0b: Using existing web_portal/dist/ (pass --build-web to rebuild).${RST}"
fi

# ── Disk space check ──────────────────────────────────────────────────────────
AVAIL_KB=$(df -k "$MOUNT_POINT" | awk 'NR==2 {print $4}')
NEEDED_KB=$(du -sk "$SDCARD_SOURCE" "$IMAGES_SOURCE" "$WEB_DIST" 2>/dev/null | awk '{s+=$1} END {print s}')

echo -e "${BLU}[→] Available: $(numfmt --to=iec $((AVAIL_KB * 1024)))  Required: ~$(numfmt --to=iec $((NEEDED_KB * 1024)))${RST}"

if (( AVAIL_KB < NEEDED_KB )); then
    echo -e "${RED}[✗] Insufficient space on SD card.${RST}"
    exit 1
fi

# ── Confirm ───────────────────────────────────────────────────────────────────
echo ""
echo -e "${BOLD}Files to be deployed to ${MOUNT_POINT}:${RST}"
echo -e "  ${CYN}• ${SDCARD_SOURCE}/ships/                  →  ships/${RST}"
echo -e "  ${CYN}• ${SDCARD_SOURCE}/assets/themes/drake/    →  assets/themes/drake/${RST}"
echo -e "  ${CYN}• ${SDCARD_SOURCE}/assets/themes/origin/   →  assets/themes/origin/${RST}"
echo -e "  ${CYN}• ${IMAGES_SOURCE}/                        →  assets/images/${RST}"
echo -e "  ${CYN}• ${WEB_DIST}/                             →  web/${RST}"
if $INCLUDE_DEMO && [[ -f "$DEMO_JSON" ]]; then
    echo -e "  ${YLW}• ${DEMO_JSON}  →  ships/demo_controls.json${RST}"
fi
echo ""

if ! $FORCE && ! $DRY_RUN; then
    read -r -p "$(echo -e "${BOLD}Proceed? [y/N] ${RST}")" confirm
    case "$confirm" in
        [yY][eE][sS]|[yY]) ;;
        *) echo "Aborted."; exit 0 ;;
    esac
fi

if $DRY_RUN; then
    echo -e "${YLW}[DRY RUN] No files will be written.${RST}"
fi

# ── Sync helper ───────────────────────────────────────────────────────────────
do_rsync() {
    local src="$1"
    local dst="$2"
    local label="$3"

    echo -e "\n${BLU}[→] Syncing ${label}...${RST}"

    local rsync_opts=(-av --checksum --progress --human-readable)
    if $DRY_RUN; then
        rsync_opts+=(--dry-run)
    fi

    if rsync "${rsync_opts[@]}" "$src" "$dst"; then
        if ! $DRY_RUN; then
            echo -e "${GRN}[✓] ${label} synced.${RST}"
        fi
    else
        echo -e "${RED}[✗] rsync failed for ${label}.${RST}"
        return 1
    fi
}

do_copy() {
    local src="$1"
    local dst="$2"
    local label="$3"

    echo -e "\n${BLU}[→] Copying ${label}...${RST}"
    if $DRY_RUN; then
        echo "    [dry-run] cp \"$src\" \"$dst\""
    else
        mkdir -p "$(dirname "$dst")"
        cp -v "$src" "$dst"
        echo -e "${GRN}[✓] ${label} copied.${RST}"
    fi
}

# ── Create directory structure ────────────────────────────────────────────────
if ! $DRY_RUN; then
    echo -e "\n${BLU}[→] Creating directory structure on card...${RST}"
    mkdir -p \
        "${MOUNT_POINT}/ships" \
        "${MOUNT_POINT}/assets/images" \
        "${MOUNT_POINT}/assets/themes/drake/sprites" \
        "${MOUNT_POINT}/assets/themes/origin/sprites" \
        "${MOUNT_POINT}/web/assets"
fi

# ── Deploy ────────────────────────────────────────────────────────────────────

# 1. Ship JSON layouts
do_rsync \
    "${SDCARD_SOURCE}/ships/" \
    "${MOUNT_POINT}/ships/" \
    "Ship layouts (ships/*.json)"

# 2. Drake theme (SVG + rasterized sprites/ subdir)
do_rsync \
    "${SDCARD_SOURCE}/assets/themes/drake/" \
    "${MOUNT_POINT}/assets/themes/drake/" \
    "Drake Military theme"

# 3. Origin theme
do_rsync \
    "${SDCARD_SOURCE}/assets/themes/origin/" \
    "${MOUNT_POINT}/assets/themes/origin/" \
    "Origin Lux theme"

# 4. Splash / branding images
do_rsync \
    "${IMAGES_SOURCE}/" \
    "${MOUNT_POINT}/assets/images/" \
    "Branding images (PNG)"

# 5. Web portal UI
if [[ -d "${WEB_DIST}" ]]; then
    do_rsync \
        "${WEB_DIST}/" \
        "${MOUNT_POINT}/web/" \
        "Web portal UI (web/)"
else
    echo -e "${YLW}[!] ${WEB_DIST} not found — skipping web UI deploy.${RST}"
    echo -e "${YLW}    Run with --build-web or manually: cd web_portal && npm run build${RST}"
fi

# 6. Optional: demo ship layout
if $INCLUDE_DEMO; then
    if [[ -f "$DEMO_JSON" ]]; then
        do_copy \
            "$DEMO_JSON" \
            "${MOUNT_POINT}/ships/demo_controls.json" \
            "Demo ship layout"
    else
        echo -e "${YLW}[!] --demo requested but ${DEMO_JSON} not found — skipping.${RST}"
    fi
fi

# ── Flush writes ──────────────────────────────────────────────────────────────
if ! $DRY_RUN; then
    echo -e "\n${BLU}[→] Flushing write cache (sync)...${RST}"
    sync
    echo -e "${GRN}[✓] Sync complete.${RST}"
fi

# ── Summary ───────────────────────────────────────────────────────────────────
echo ""
echo -e "${BOLD}${GRN}═══════════════════════════════════════════════════${RST}"
if $DRY_RUN; then
    echo -e "${BOLD}${YLW}  Dry run complete — no files were written.${RST}"
else
    echo -e "${BOLD}${GRN}  SD card is ready. Safe to eject.${RST}"
fi
echo -e "${BOLD}${GRN}═══════════════════════════════════════════════════${RST}"
echo ""

if command -v tree &>/dev/null && ! $DRY_RUN; then
    echo -e "${CYN}Card contents:${RST}"
    tree -h --du "${MOUNT_POINT}" -L 4 --noreport 2>/dev/null || true
    echo ""
fi
