#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <vector>

#include <getopt.h>

#include <SDL.h>
#include <SDL_image.h>
#include <SDL_ttf.h>

SDL_Renderer * renderer = nullptr;
SDL_Surface * tilesurface = nullptr;
SDL_Surface * textsurface = nullptr;
SDL_Surface * windowsurface = nullptr;
SDL_Texture * texture = nullptr;
TTF_Font * font = nullptr;

void destroy_texture()
{
    SDL_DestroyTexture(texture);
}

void free_tilesurface()
{
    SDL_FreeSurface(tilesurface);
}

void free_windowsurface()
{
    SDL_FreeSurface(windowsurface);
}

void free_textsurface()
{
    SDL_FreeSurface(textsurface);
}

void destroy_renderer()
{
    SDL_DestroyRenderer(renderer);
}

void close_font()
{
    TTF_CloseFont(font);
}

void usage()
{
    std::cerr << "Usage: text-to-stereogram -f <font> -t <tile> [-w <width>] [-h <height>] [-o <output file>] [<string>]\n";
}

int main(int argc, char * const * argv)
{
    // Default options
    int w = 640;
    int h = 480;
    int s = 24;
    char const * fontname = nullptr;
    char const * tilename = nullptr;
    char const * outfname = nullptr;
    char const * text = "Hello, world!";

    // Parse command-line options
    {
        int c;
        while ((c = getopt(argc, argv, "w:h:f:s:t:o:")) != -1)
        {
            switch (c)
            {
                case 'w':
                    w = std::atoi(optarg);
                    break;
                case 'h':
                    h = std::atoi(optarg);
                    break;
                case 's':
                    s = std::atoi(optarg);
                    break;
                case 'f':
                    fontname = optarg;
                    break;
                case 't':
                    tilename = optarg;
                    break;
                case 'o':
                    outfname = optarg;
                    break;
                default:
                    usage();
                    return 1;
            }
        }
    }
    if ((fontname == nullptr) || (tilename == nullptr) || (w <= 0) || (h <= 0) || (s <= 0))
    {
        usage();
        return 1;
    }
    if (optind < argc)
        text = argv[optind];

    // Init SDL_ttf
    if (TTF_Init() != 0)
    {
        std::cerr << "Unable to initialise SDL_ttf: " << TTF_GetError() << std::endl;
        return 1;
    }
    std::atexit(TTF_Quit);
    if (!(font = TTF_OpenFont(fontname, s)))
    {
        std::cerr << "Unable to open font: " << TTF_GetError() << std::endl;
        return 1;
    }
    std::atexit(close_font);

    // Init SDL
    if (SDL_Init(SDL_INIT_VIDEO) != 0)
    {
        std::cerr << "Unable to initialise SDL: " << SDL_GetError() << std::endl;
        return 1;
    }
    std::atexit(SDL_Quit);
    auto window = SDL_CreateWindow(
            "text-to-stereogram",
            SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
            w, h, SDL_WINDOW_SHOWN);
    if (!window)
    {
        std::cerr << "Unable to create window: " << SDL_GetError() << std::endl;
        return 1;
    }
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    if (!renderer)
    {
        std::cerr << "Unable to create renderer: " << SDL_GetError() << std::endl;
        return 1;
    }
    std::atexit(destroy_renderer);

    // Render text
    //textsurface = TTF_RenderUTF8_Shaded(font, text, {255, 255, 255}, {0, 0, 0});
    textsurface = TTF_RenderUTF8_Solid(font, text, {255, 255, 255});
    if (!textsurface)
    {
        std::cerr << "Unable to render text surface: " << TTF_GetError() << std::endl;
        return 1;
    }
    std::atexit(free_textsurface);

    // Load tile image
    if (IMG_Init(0) != 0)
    {
        std::cerr << "Unable to initialise SDL_image: " << IMG_GetError() << std::endl;
        return 1;
    }
    std::atexit(IMG_Quit);
    tilesurface = IMG_Load(tilename);
    if (!tilesurface)
    {
        std::cerr << "Unable to load tile image: " << IMG_GetError() << std::endl;
        return 1;
    }
    std::atexit(free_tilesurface);

    // Create a surface the same size as the window
    windowsurface = SDL_CreateRGBSurface(0, w, h, 32, 0, 0, 0, 0);
    if (!windowsurface)
    {
        std::cerr << "Unable to create window-sized surface: " << SDL_GetError() << std::endl;
        return 1;
    }
    std::atexit(free_windowsurface);
    // We make assumptions later that we can treat windowsurface->pixels as uint32_t*
    if (windowsurface->format->BytesPerPixel != 4)
    {
        std::cerr << "Unsuitable format for window-sized surface: "
            << windowsurface->format->BytesPerPixel << " bytes per pixel" << std::endl;
        return 1;
    }

    // Convert tile image to windowsurface pixel format
    if ((tilesurface->format->format != windowsurface->format->format)
            || (tilesurface->format->format == SDL_PIXELFORMAT_UNKNOWN))
    {
        auto old = tilesurface;
        tilesurface = SDL_ConvertSurface(old, windowsurface->format, 0);
        if (!tilesurface)
        {
            std::cerr << "Unable to convert tile surface format: " << SDL_GetError() << std::endl;
            return 1;
        }
        SDL_FreeSurface(old);
    }

    // Check we have enough horizontal space. One tile width each side of the depth image.
    if ((w < (tilesurface->w * 2)) || (((w - (tilesurface->w * 2))) < textsurface->w))
    {
        std::cout << "Warning: Image not wide enough! Should be at least " << ((tilesurface->w * 2) + textsurface->w) << std::endl;
    }

    // Blit text to the image
    {
        SDL_Rect dst = {(w / 2) - (textsurface->w / 2), (h / 2) - (textsurface->h / 2), 0, 0};
        SDL_BlitSurface(textsurface, nullptr, windowsurface, &dst);
    }

    // Blit one strip of tile image to window-sized surface
    for (int y = 0; y < windowsurface->h; y += tilesurface->h)
    {
        SDL_Rect dst = {0, y, 0, 0};
        SDL_BlitSurface(tilesurface, nullptr, windowsurface, &dst);
    }

    // Depth disparity coefficient: we want to normalise the depth
    // range so that the nearest elements are half the tile width
    double c = (static_cast<double>(tilesurface->w) / 6.0) / 256.0;
    // Render the actual stereogram, row by row
    for (int y = 0; y < windowsurface->h; ++y)
    {
        // State: previous depthbuffer value, current repeating pattern, pattern length.
        // Copy current row of initial tile into pattern.
        uint32_t prev = 0;
        std::uint32_t * start =
            reinterpret_cast<std::uint32_t*>(
                    static_cast<std::uint8_t*>(windowsurface->pixels) + (windowsurface->pitch * y));
        std::uint32_t * end = start + tilesurface->w;
        std::vector<std::uint32_t> pattern(start, end);
        // Keep length as double, as we may be adjusting it by fractions of a pixel,
        // and don't want shallow slopes to get lost in rounding errors that never
        // end up altering the integer pattern length.
        double len = static_cast<double>(pattern.size());
        // Iterator to current pattern position
        auto pattern_it = pattern.begin();
        // Iterate over remainder of row
        for (int x = tilesurface->w; x < windowsurface->w; ++x)
        {
            // Grab one single colour component from current pixel
            std::uint32_t current = *(start + x);
            current &= windowsurface->format->Rmask;
            current >>= windowsurface->format->Rshift;
            current <<= windowsurface->format->Rloss;
            // Shorten or lengthen pattern accordingly
            if (current > prev)
            {
                // Current pixel is closer. Shorten the pattern.
                std::uint32_t disparity = current - prev;
                double d = static_cast<double>(disparity) * c;
                double newlen = len - d;
                disparity = static_cast<std::uint32_t>(pattern.size() - std::lround(newlen));
                // We may need to wrap around the end of the pattern buffer
                if (disparity > (pattern.end() - pattern_it))
                {
                    auto to_end = pattern.end() - pattern_it;
                    pattern.erase(pattern_it, pattern.end());
                    auto remaining = disparity - to_end;
                    unsigned offset = (pattern_it - pattern.begin()) - remaining;
                    pattern.erase(pattern.begin(), pattern.begin() + remaining);
                    while (offset >= pattern.size())
                        offset -= pattern.size();
                    pattern_it = pattern.begin() + offset;
                }
                else
                {
                    unsigned offset = pattern_it - pattern.begin();
                    pattern.erase(pattern_it, pattern_it + disparity);
                    while (offset >= pattern.size())
                        offset -= pattern.size();
                    pattern_it = pattern.begin() + offset;
                }
                len = newlen;
            }
            else if (current < prev)
            {
                // Current pixel is further away. Lengthen the pattern.
                std::uint32_t disparity = prev - current;
                double d = static_cast<double>(disparity) * c;
                double newlen = len + d;
                disparity = static_cast<std::uint32_t>(std::lround(newlen) - pattern.size());
                len = newlen;
                auto offset = (pattern_it - pattern.begin());
                // Insert pixels from 64 to 72 rows above in the tile.
                // This randomness helps alleviate artefacts resulting from
                // accidentally introducing additional repeating patterns
                // if depth keeps alternating between two values.
                int py = y - (64 + (std::rand() / ((RAND_MAX + 1u) / 8)));
                if (py < 0)
                    py += tilesurface->h;
                else
                    while (py >= tilesurface->h)
                        py -= tilesurface->h;
                std::uint32_t px = x;
                while (px >= static_cast<std::uint32_t>(tilesurface->w))
                    px -= tilesurface->w;
                // We may need to wrap around edge of tile
                // Insert pixels up to edge of tile
                std::uint32_t * p =
                    reinterpret_cast<std::uint32_t*>(
                            static_cast<std::uint8_t*>(tilesurface->pixels) + (tilesurface->pitch * py))
                    + px;
                pattern.insert(pattern_it, p, p + std::min(disparity, tilesurface->w - px));
                pattern_it = pattern.begin() + offset;
                if (disparity > (tilesurface->w - px))
                {
                    disparity -= (tilesurface->w - px);
                    p -= px;
                    pattern.insert(pattern_it + 1 + (tilesurface->w - px), p, p + disparity);
                }
                pattern_it = pattern.begin() + offset;
            }
            // Write current pattern pixel to surface
            *(start + x) = *pattern_it;
            prev = current;
            if (++pattern_it == pattern.end())
                pattern_it = pattern.begin();
        }
    }

    // Save image if desired
    if (outfname != nullptr)
    {
        if (IMG_SavePNG(windowsurface, outfname) != 0)
            std::cerr << "Unable to save PNG: " << IMG_GetError() << std::endl;
    }

    // Prepare image for presentation
    texture = SDL_CreateTextureFromSurface(renderer, windowsurface);
    if (!texture)
    {
        std::cerr << "Unable to create texture from surface: " << SDL_GetError() << std::endl;
        return 1;
    }
    std::atexit(destroy_texture);

    // Display image until quit
    bool quit = false;
    while (!quit)
    {
        SDL_Event e;
        while (SDL_PollEvent(&e))
        {
            switch (e.type)
            {
                case SDL_QUIT:
                    quit = true;
            }
        }
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, texture, nullptr, nullptr);
        SDL_RenderPresent(renderer);
        {
            using namespace std::chrono_literals;
            std::this_thread::sleep_for(100ms);
        }
    }

    return 0;
}
