import xml.etree.ElementTree as ET

def analyze_svg(file_path):
    try:
        tree = ET.parse(file_path)
        root = tree.getroot()
        ns = {"svg": "http://www.w3.org/2000/svg"}
        
        print(f"File: {file_path}")
        print(f"ViewBox: {root.get('viewBox')}")
        print("Groups/Elements with IDs:")
        for elem in root.findall(".//svg:*[@id]", namespaces=ns):
            print(f"  - {elem.tag.split('}')[-1]} id={elem.get('id')}")
    except Exception as e:
        print(f"Error: {e}")

analyze_svg("/home/sannis/ESPSCar/art/style_drake_military/sprite_sheet.svg")
analyze_svg("/home/sannis/ESPSCar/art/style_origin_lux/sprite_sheet.svg")
