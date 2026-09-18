import json
import math
import pygame

SIZE = 30

with open("map.json") as f:
    data = json.load(f)

pygame.init()
screen = pygame.display.set_mode((800, 600))

def draw_hex(x, y, t):
    points = [
        (x + SIZE * math.cos(math.radians(60 * i - 30)),
         y + SIZE * math.sin(math.radians(60 * i - 30)))
        for i in range(6)
    ]

    colors = {
        0: (150, 220, 150),
        1: (220, 180, 100),
        2: (120, 120, 120),
        3: (100, 150, 220)
    }

    pygame.draw.polygon(screen, colors[t], points)
    pygame.draw.polygon(screen, (40,40,40), points, 1)

running = True
while running:
    for e in pygame.event.get():
        if e.type == pygame.QUIT:
            running = False

    screen.fill((255,255,255))

    for r, row in enumerate(data):
        for c, tile in enumerate(row):
            x = 50 + math.sqrt(3) * SIZE * (c + 0.5 * (r & 1))
            y = 50 + 1.5 * SIZE * r
            draw_hex(x, y, tile)

    pygame.display.flip()

pygame.quit()