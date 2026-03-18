from rembg import remove
from PIL import Image
import os

input_dir = "/Users/bilalwaraich/Desktop/Research/Infodemic-Flux/assets"
output_dir = "/Users/bilalwaraich/Desktop/Research/Infodemic-Flux/assets/sprites/raw"
os.makedirs(output_dir, exist_ok=True)

for filename in os.listdir(input_dir):
    # Only process files in the top-level assets directory (avoid processing the output_dir itself recursively or other dirs)
    if filename.endswith(".png") or filename.endswith(".jpg"):
        input_path = os.path.join(input_dir, filename)
        if os.path.isfile(input_path):
            output_path = os.path.join(output_dir, filename.replace(".jpg", ".png"))
            with open(input_path, "rb") as f:
                input_data = f.read()
            output_data = remove(input_data)
            with open(output_path, "wb") as f:
                f.write(output_data)
            print(f"Processed: {filename}")
