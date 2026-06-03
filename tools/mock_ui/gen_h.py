import glob
import os

def generate():
    c_files = glob.glob("components/sc_ui/assets/sprites/*/*.c")
    
    with open("components/sc_ui/include/sc_ui_theme_sprites.h", "w") as f:
        f.write("#pragma once\n#include \"lvgl.h\"\n\n")
        
        for c_file in sorted(c_files):
            # Read the file to find the lv_image_dsc_t name
            with open(c_file, "r") as cf:
                content = cf.read()
                # Find 'const lv_image_dsc_t XYZ ='
                import re
                match = re.search(r"const lv_image_dsc_t\s+([a-zA-Z0-9_]+)\s*=", content)
                if match:
                    name = match.group(1)
                    f.write(f"extern const lv_image_dsc_t {name};\n")

if __name__ == "__main__":
    generate()
