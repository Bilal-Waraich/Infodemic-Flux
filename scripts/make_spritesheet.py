from PIL import Image
import os

BASE    = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RAW_DIR = os.path.join(BASE, 'assets', 'sprites', 'raw')
OUT     = os.path.join(BASE, 'assets', 'sprites', 'agents.png')

TILE    = 16
ORDER   = [
    ('susceptible.png', 'BeliefState::SUSCEPTIBLE'),
    ('exposed.png',     'BeliefState::EXPOSED'),
    ('infected.png',    'BeliefState::INFECTED'),
    ('recovered.png',   'BeliefState::RECOVERED'),
    ('immune.png',      'BeliefState::IMMUNE'),
    ('bot.png',         'bot agents'),
]

sheet = Image.new('RGBA', (TILE * len(ORDER), TILE), (0, 0, 0, 0))

for idx, (filename, label) in enumerate(ORDER):
    path   = os.path.join(RAW_DIR, filename)
    sprite = Image.open(path).convert('RGBA')
    sprite = sprite.resize((TILE, TILE), Image.LANCZOS)
    sheet.paste(sprite, (idx * TILE, 0), sprite)

sheet.save(OUT)

result = Image.open(OUT)
print(f'Spritesheet created: assets/sprites/agents.png')
print(f'Dimensions: {result.size[0]}x{result.size[1]} pixels')
print(f'Tile order: susceptible | exposed | infected | recovered | immune | bot')
