#!/usr/bin/env python3
import sys
import os
import subprocess
import shutil
import json

SDKCONFIG_PATH = "sdkconfig"

TARGET_PROFILES = {
    "touch": {"profile": "CONFIG_HOTBOX_BUILD_TOUCH", "target": "esp32p4", "board": "reTerminal D1001"},
    "lite": {"profile": "CONFIG_HOTBOX_BUILD_LITE", "target": "esp32s3", "board": "ESP32-S3-DevKit"},
    "tiny": {"profile": "CONFIG_HOTBOX_BUILD_TINY", "target": "esp32c3", "board": "ESP32-C3-DevKit"}
}

def load_sdkconfig(path=SDKCONFIG_PATH):
    configs = {}
    if not os.path.exists(path):
        return configs
    with open(path, "r") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            if "=" in line:
                key, val = line.split("=", 1)
                configs[key.strip()] = val.strip().strip('"')
    return configs

def get_enabled_profiles(configs):
    enabled = []
    for name, info in TARGET_PROFILES.items():
        if configs.get(info["profile"]) == "y":
            enabled.append(name)
    return enabled

def merge_configs(profile_name, src_path, dst_path):
    # Read custom configs from src_path (the root sdkconfig)
    custom_lines = []
    if os.path.exists(src_path):
        with open(src_path, "r") as f:
            for line in f:
                stripped = line.strip()
                # Copy CONFIG_HOTBOX_ and CONFIG_SC_ lines
                if (stripped.startswith("CONFIG_HOTBOX_") or 
                    stripped.startswith("# CONFIG_HOTBOX_") or
                    stripped.startswith("CONFIG_SC_") or
                    stripped.startswith("# CONFIG_SC_")):
                    # Skip the build target selection flags and board name
                    if any(x in stripped for x in ["CONFIG_HOTBOX_BUILD_", "CONFIG_HOTBOX_DEVKIT_BOARD_NAME"]):
                        continue
                    custom_lines.append(line)
                    
    # Read dst_path (the target-specific sdkconfig)
    dst_lines = []
    if os.path.exists(dst_path):
        with open(dst_path, "r") as f:
            dst_lines = f.readlines()
            
    # Filter out any existing custom configs and target flags from dst_lines
    new_lines = []
    for line in dst_lines:
        stripped = line.strip()
        if (stripped.startswith("CONFIG_HOTBOX_") or 
            stripped.startswith("# CONFIG_HOTBOX_") or
            stripped.startswith("CONFIG_SC_") or
            stripped.startswith("# CONFIG_SC_") or
            stripped.startswith("CONFIG_IDF_TARGET")):
            continue
        new_lines.append(line)
        
    # Append target selection flags for this profile
    for name, info in TARGET_PROFILES.items():
        key = info["profile"]
        if name == profile_name:
            new_lines.append(f"{key}=y\n")
        else:
            new_lines.append(f"# {key} is not set\n")
            
    # Append target and board name
    target = TARGET_PROFILES[profile_name]["target"]
    board_name = TARGET_PROFILES[profile_name]["board"]
    new_lines.append(f'CONFIG_IDF_TARGET="{target}"\n')
    new_lines.append(f'CONFIG_HOTBOX_DEVKIT_BOARD_NAME="{board_name}"\n')
    
    # Append custom settings from src_path
    new_lines.extend(custom_lines)
    
    # Write back to dst_path
    with open(dst_path, "w") as f:
        f.writelines(new_lines)

def get_directory_target(build_dir):
    desc_path = os.path.join(build_dir, "project_description.json")
    if not os.path.exists(desc_path):
        return None
    try:
        with open(desc_path, "r") as f:
            data = json.load(f)
            return data.get("target")
    except Exception:
        return None

def run_cmd(args, check=True):
    print(f"Running: {' '.join(args)}")
    return subprocess.run(args, check=check)

def build_profile(profile_name):
    target = TARGET_PROFILES[profile_name]["target"]
    build_dir = f"build_{profile_name}"
    sdkconfig_profile = f"sdkconfig.{profile_name}"
    
    print(f"\n=======================================================")
    print(f"  Building Profile: {profile_name.upper()} ({target})")
    print(f"  Build Directory:  {build_dir}")
    print(f"  Config File:      {sdkconfig_profile}")
    print("=======================================================\n")
    
    # 1. Check if we need to run set-target
    active_target = get_directory_target(build_dir)
    target_mismatch = active_target != target
    
    if not os.path.exists(sdkconfig_profile) or target_mismatch:
        # Create base config with target
        with open(sdkconfig_profile, "w") as f:
            f.write(f'CONFIG_IDF_TARGET="{target}"\n')
        # Run set-target to generate clean default configs for target
        run_cmd(["idf.py", "-DSDKCONFIG=" + sdkconfig_profile, "-B", build_dir, "set-target", target])
        
    # Merge latest root config settings into target sdkconfig
    merge_configs(profile_name, src_path=SDKCONFIG_PATH, dst_path=sdkconfig_profile)

    # 2. Compile target
    run_cmd(["idf.py", "-DSDKCONFIG=" + sdkconfig_profile, "-B", build_dir, "build"])
    
    # 3. Copy binaries to dist/
    dist_dir = "dist"
    profile_dist = os.path.join(dist_dir, profile_name)
    os.makedirs(profile_dist, exist_ok=True)
    
    shutil.copyfile(os.path.join(build_dir, "sc_terminal.bin"), os.path.join(profile_dist, "sc_terminal.bin"))
    shutil.copyfile(os.path.join(build_dir, "bootloader", "bootloader.bin"), os.path.join(profile_dist, "bootloader.bin"))
    shutil.copyfile(os.path.join(build_dir, "partition_table", "partition-table.bin"), os.path.join(profile_dist, "partition-table.bin"))
    
    storage_path = os.path.join(build_dir, "storage.bin")
    if os.path.exists(storage_path):
        shutil.copyfile(storage_path, os.path.join(profile_dist, "storage.bin"))
        
    print(f"[build.py] Binaries copied successfully to {profile_dist}/")

def main():
    if len(sys.argv) > 1:
        cmd = sys.argv[1]
    else:
        cmd = "build"
        
    configs = load_sdkconfig()
    
    if cmd == "menuconfig":
        # Run menuconfig on the root sdkconfig
        run_cmd(["idf.py", "menuconfig"])
        return
        
    if cmd == "build" or cmd == "all":
        enabled_profiles = get_enabled_profiles(configs)
        
        # If no profiles are enabled, default to the current IDF_TARGET in sdkconfig or esp32p4
        if not enabled_profiles:
            idf_target = configs.get("CONFIG_IDF_TARGET", "esp32p4")
            if idf_target == "esp32c3":
                enabled_profiles = ["tiny"]
            elif idf_target == "esp32s3":
                enabled_profiles = ["lite"]
            else:
                enabled_profiles = ["touch"]
                
        print(f"\n[build.py] Active Profiles to build: {', '.join(e.upper() for e in enabled_profiles)}")
        
        # Clean dist directory before building
        dist_dir = "dist"
        if os.path.exists(dist_dir):
            shutil.rmtree(dist_dir)
        os.makedirs(dist_dir, exist_ok=True)
        
        for profile in enabled_profiles:
            build_profile(profile)
            
        print("\n=======================================================")
        print("  All requested HotBox profiles compiled successfully!")
        print(f"  Binaries are stored in the '{dist_dir}' directory.")
        print("=======================================================\n")
        return
        
    if cmd == "clean":
        for name in TARGET_PROFILES.keys():
            build_dir = f"build_{name}"
            if os.path.exists(build_dir):
                sdkconfig_profile = f"sdkconfig.{name}"
                run_cmd(["idf.py", "-DSDKCONFIG=" + sdkconfig_profile, "-B", build_dir, "clean"])
                if os.path.exists(sdkconfig_profile):
                    os.remove(sdkconfig_profile)
        if os.path.exists("build"):
            run_cmd(["idf.py", "clean"])
        dist_dir = "dist"
        if os.path.exists(dist_dir):
            shutil.rmtree(dist_dir)
        return
        
    if cmd == "flash":
        enabled_profiles = get_enabled_profiles(configs)
        if len(enabled_profiles) > 1:
            print("\n[build.py] Multiple target profiles are enabled in menuconfig.")
            print("[build.py] Please flash a specific target by specifying the profile:")
            for name in enabled_profiles:
                print(f"  ./build.py flash-{name}")
            return
        elif len(enabled_profiles) == 1:
            profile = enabled_profiles[0]
            build_dir = f"build_{profile}"
            sdkconfig_profile = f"sdkconfig.{profile}"
            run_cmd(["idf.py", "-DSDKCONFIG=" + sdkconfig_profile, "-B", build_dir, "flash"])
        else:
            run_cmd(["idf.py", "flash"])
        return
        
    if cmd.startswith("flash-"):
        profile_name = cmd.split("-", 1)[1]
        if profile_name in TARGET_PROFILES:
            build_dir = f"build_{profile_name}"
            sdkconfig_profile = f"sdkconfig.{profile_name}"
            run_cmd(["idf.py", "-DSDKCONFIG=" + sdkconfig_profile, "-B", build_dir, "flash"])
        else:
            print(f"[build.py] Unknown flash profile: {profile_name}")
        return
        
    # Pass-through all other commands to idf.py
    run_cmd(["idf.py"] + sys.argv[1:])

if __name__ == "__main__":
    main()
