sdl = dependency('sdl2', version: '>=2')
ttf = dependency('SDL2_ttf', version: '>=2')
img = dependency('SDL2_image', version: '>=2')

exe = executable('text-to-stereogram',
    'main.cxx',
    dependencies: [sdl, ttf, img],
    install: true
)
