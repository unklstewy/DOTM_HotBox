import xml.etree.ElementTree as ET

def analyze_svg(file_path):
    try:
        tree = ET.parse(file_path)
        root = tree.getroot()
        ns = {"svg": "http://www.w3.org/2000/svg"}
        
        print(f"File: {file_path}")
        print("Groups with classes:")
        for elem in root.findall(".//svg:g[@class]", namespaces=ns):
            print(f"  - g class=\"{elem.get('class')}\"")
        for elem in root.findall(".//svg:use", namespaces=ns):
            print(f"  - use href=\"{elem.get('{http://www.w3.org/1999/xlink}href') or elem.get('href')}\"")
    except Exception as e:
        print(f"Error: {e}")

analyze_svg("/home/sannis/ESPSCar/art/style_drake_military/sprite_sheet.svg")
analyze_svg("/home/sannis/ESPSCar/art/style_origin_lux/sprite_sheet.svg")
