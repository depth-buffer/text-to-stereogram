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
SDL_Surface * gradientsurface = nullptr;
SDL_Surface * offsetsurface = nullptr;
SDL_Surface * rearrsurface = nullptr;
SDL_Surface * depthsurface = nullptr;
SDL_Surface * tilesurface = nullptr;
SDL_Surface * windowsurface = nullptr;
SDL_Texture * texture = nullptr;
TTF_Font * font = nullptr;

void free_rearrsurface()
{
    SDL_FreeSurface(rearrsurface);
}

void free_offsetsurface()
{
    SDL_FreeSurface(offsetsurface);
}

void free_gradientsurface()
{
    SDL_FreeSurface(gradientsurface);
}

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

void free_depthsurface()
{
    SDL_FreeSurface(depthsurface);
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
    std::cerr << "Usage: text-to-stereogram -t <tile> [-c] [-w <width>] [-h <height>] [-o <output file>] [-f <font> [-s <size> -d <depth>] <string>] [-m <depth map>] [-l <pattern length divisor>]\n";
    std::cerr << "Specify -f and <string> to render text, -m to render geometry.\n";
}

void draw(SDL_Surface * srcsurface, bool init, int row, bool cross, double l)
{
    SDL_SetSurfaceBlendMode(windowsurface, SDL_BLENDMODE_NONE);
    SDL_SetSurfaceBlendMode(srcsurface, SDL_BLENDMODE_NONE);

    if (init)
    {
        std::srand(42);
        // Blit depth map to the image
        {
            SDL_Rect dst = {((windowsurface->w / 2) - (depthsurface->w / 2)) + (srcsurface->w / 2), (windowsurface->h / 2) - (depthsurface->h / 2), 0, 0};
            SDL_BlitSurface(depthsurface, nullptr, windowsurface, &dst);
        }
    }

    if (row < 0)
    {
        // We're doing all rows
        // Blit one strip of tile image to window-sized surface
        for (int y = 0; y < windowsurface->h; y += srcsurface->h)
        {
            SDL_Rect dst = {0, y, 0, 0};
            SDL_BlitSurface(srcsurface, nullptr, windowsurface, &dst);
        }
    }
    else
    {
        // Blit just the current row of the tile image to the surface
        int y = row;
        while (y >= srcsurface->h)
            y -= srcsurface->h;
        SDL_Rect src = {0, y, srcsurface->w, 1};
        SDL_Rect dst = {0, row, 0, 0};
        SDL_BlitSurface(srcsurface, &src, windowsurface, &dst);
    }

    // Depth disparity coefficient: we want to normalise the depth range so
    // that the pattern doesn't get down to one pixel or anything ridiculous
    // (unless that's what the user claims they want).
    // With a monochrome depthmap we get 256 discrete depth steps; calculate a
    // coefficient which determines how much of the 0..255 range each pixel of
    // pattern length increase/decrease represents, whilst also limiting how
    // short the pattern can get as a proportion of original tile width.
    double c = (static_cast<double>(srcsurface->w) / l) / 256.0;
    // Render the actual stereogram, row by row
    int y = 0, ylimit = windowsurface->h;
    if (row >= 0)
    {
        y = row;
        ylimit = row + 1;
    }
    for (; y < ylimit; ++y)
    {
        // State: previous depthbuffer value, current repeating pattern, pattern length.
        // Copy current row of initial tile into pattern.
        uint32_t prev = 0;
        std::uint32_t * start =
            reinterpret_cast<std::uint32_t*>(
                    static_cast<std::uint8_t*>(windowsurface->pixels) + (windowsurface->pitch * y));
        std::uint32_t * end = start + srcsurface->w;
        std::vector<std::uint32_t> pattern(start, end);
        // Keep length as double, as we may be adjusting it by fractions of a pixel,
        // and don't want shallow slopes to get lost in rounding errors that never
        // end up altering the integer pattern length.
        double len = static_cast<double>(pattern.size());
        // Iterator to current pattern position
        auto pattern_it = pattern.begin();
        // Iterate over remainder of row
        for (int x = srcsurface->w; x < windowsurface->w; ++x)
        {
            // Grab one single colour component from current pixel
            std::uint32_t current = *(start + x);
            current &= windowsurface->format->Rmask;
            current >>= windowsurface->format->Rshift;
            current <<= windowsurface->format->Rloss;
            // Shorten or lengthen pattern accordingly.
            // In wall-eyed mode: shorten when pixels get nearer; lengthen for further.
            // In cross-eyed mode: lengthen when pixels get further; shorten for nearer.
            // NB: The comparisons look the wrong way round because we assume inverted
            // depth maps, i.e. 0 is the far plane, 255 near.
            if (cross ? (current < prev) : (current > prev))
            {
                // Shorten the pattern.
                std::uint32_t disparity = cross ? (prev - current) : (current - prev);
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
            else if (cross ? (current > prev) : (current < prev))
            {
                // Lengthen the pattern.
                std::uint32_t disparity = cross ? (current - prev) : (prev - current);
                double d = static_cast<double>(disparity) * c;
                double newlen = len + d;
                disparity = static_cast<std::uint32_t>(std::lround(newlen) - pattern.size());
                len = newlen;
                auto offset = (pattern_it - pattern.begin());
                // Insert pixels from 1 to 5 rows above in the tile.
                // This randomness helps alleviate artefacts resulting from
                // accidentally introducing additional repeating patterns
                // if depth keeps alternating between two values.
                int py = y - ((std::rand() / ((RAND_MAX + 1u) / 5)) + 1);
                if (py < 0)
                    py += srcsurface->h;
                else
                    while (py >= srcsurface->h)
                        py -= srcsurface->h;
                std::uint32_t px = x;
                while (px >= static_cast<std::uint32_t>(srcsurface->w))
                    px -= srcsurface->w;
                // We may need to wrap around edge of tile
                // Insert pixels up to edge of tile
                std::uint32_t * p =
                    reinterpret_cast<std::uint32_t*>(
                            static_cast<std::uint8_t*>(srcsurface->pixels) + (srcsurface->pitch * py))
                    + px;
                pattern.insert(pattern_it, p, p + std::min(disparity, srcsurface->w - px));
                pattern_it = pattern.begin() + offset;
                if (disparity > (srcsurface->w - px))
                {
                    disparity -= (srcsurface->w - px);
                    p -= px;
                    pattern.insert(pattern_it + 1 + (srcsurface->w - px), p, p + disparity);
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
}

int main(int argc, char * argv[])
{
    // Default options
    int w = 640;
    int h = 480;
    int s = 24;
    int d = 60;
    double l = 2.0;
    char const * fontname = nullptr;
    char const * tilename = nullptr;
    char const * outfname = nullptr;
    char const * depthname = nullptr;
    char const * text = "Hello, world!";
    bool cross = false;

    // Parse command-line options
    {
        int c;
        while ((c = getopt(argc, argv, "w:h:f:s:t:o:m:cd:l:")) != -1)
        {
            switch (c)
            {
                case 'w':
                    // Output width
                    w = std::atoi(optarg);
                    break;
                case 'h':
                    // Output height
                    h = std::atoi(optarg);
                    break;
                case 's':
                    // Font size
                    s = std::atoi(optarg);
                    break;
                case 'f':
                    // Font filename
                    fontname = optarg;
                    break;
                case 't':
                    // Input tile image filename
                    tilename = optarg;
                    break;
                case 'o':
                    // Output image filename
                    outfname = optarg;
                    break;
                case 'm':
                    // Input depthbuffer image filename
                    depthname = optarg;
                    break;
                case 'c':
                    // Generate cross-eyed autostereogram
                    // (as opposed to wall-eyed)
                    cross = true;
                    break;
                case 'd':
                    // Depth offset of text 0 (far) .. 255 (near)
                    d = std::atoi(optarg);
                    break;
                case 'l':
                    // Pattern length divisor: at the far plane, pattern length
                    // will be the full width of the input tile; at the near
                    // plane, it will be tile width divided by this.
                    l = std::atof(optarg);
                    break;
                default:
                    // Unrecognised
                    usage();
                    return 1;
            }
        }
    }
    if (((fontname == nullptr) && (depthname == nullptr)) || (tilename == nullptr) || (w <= 0) || (h <= 0) || (s <= 0))
    {
        usage();
        return 1;
    }
    if ((fontname != nullptr) && ((d <= 0) || (d > 255)))
    {
        std::cerr << "Depth value must be between 0 and 256" << std::endl;
        return 1;
    }
    if (l <= 1.0)
    {
        std::cerr << "Pattern length divisor must be greater than 1.0" << std::endl;
        return 1;
    }
    if (optind < argc)
    {
        if (depthname != nullptr)
        {
            std::cerr << "Please specify just a string & font pair, or a depth map, not both" << std::endl;
            return 1;
        }
        text = argv[optind];
    }

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

    // Init SDL_image
    if (IMG_Init(0) != 0)
    {
        std::cerr << "Unable to initialise SDL_image: " << IMG_GetError() << std::endl;
        return 1;
    }
    std::atexit(IMG_Quit);

    if (depthname == nullptr)
    {
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
        // Render text
        std::uint8_t depth = static_cast<std::uint8_t>(d);
        depthsurface = TTF_RenderUTF8_Solid(font, text, {depth, depth, depth});
        if (!depthsurface)
        {
            std::cerr << "Unable to render text surface: " << TTF_GetError() << std::endl;
            return 1;
        }
    }
    else
    {
        // Load custom depth map
        depthsurface = IMG_Load(depthname);
        if (!depthsurface)
        {
            std::cerr << "Unable to load depth map image: " << IMG_GetError() << std::endl;
            return 1;
        }
    }
    std::atexit(free_depthsurface);

    // Load tile image
    tilesurface = IMG_Load(tilename);
    if (!tilesurface)
    {
        std::cerr << "Unable to load tile image: " << IMG_GetError() << std::endl;
        return 1;
    }
    std::atexit(free_tilesurface);
    if ((tilesurface->w > 65536) || (tilesurface->h > 65536))
    {
        std::cerr << "Tile image too big; max. dimensions 65536*65536" << std::endl;
        return 1;
    }

    // Create a surface the same size as the window
    windowsurface = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB32);
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

    // Render a simple x/y gradient grid.
    // Split x & y coordinates over two colour components each, so we can have
    // max. values of 256*256 = 65536, supporting up to 65536*65536 tiles.
    gradientsurface = SDL_CreateRGBSurfaceWithFormat(0, tilesurface->w, tilesurface->h, 32, SDL_PIXELFORMAT_ARGB32);
    if (!gradientsurface)
    {
        std::cerr << "Unable to create offset surface: " << SDL_GetError() << std::endl;
        return 1;
    }
    std::atexit(free_gradientsurface);
    for (int y = 0; y < gradientsurface->h; ++y)
    {
        std::uint32_t * pixel =
            reinterpret_cast<std::uint32_t*>(
                    static_cast<std::uint8_t*>(gradientsurface->pixels) + (gradientsurface->pitch * y));
        for (int x = 0; x < gradientsurface->w; ++x, ++pixel)
        {
            int a = x >> 8;
            int r = x - (a << 8);
            int g = y >> 8;
            int b = y - (g << 8);
            *(pixel) = SDL_MapRGBA(gradientsurface->format, r, g, b, a);
        }
    }

    // We make assumptions later that the image will be at least as wide & tall as the tile
    if ((w < tilesurface->w) || (h < tilesurface->h))
    {
        std::cerr << "Image must be at least as big as the tile in both dimensions" << std::endl;
        return 1;
    }

    // Check we have enough horizontal space. One tile width each side of the depth image.
    if ((w < (tilesurface->w * 2)) || (((w - (tilesurface->w * 2))) < depthsurface->w))
    {
        std::cout << "Warning: Image not wide enough! Should be at least " << ((tilesurface->w * 2) + depthsurface->w) << std::endl;
    }

    draw(gradientsurface, true, -1, cross);

    // Duplicate the original tile again, as a precursor to making the rearranged tile
    rearrsurface = SDL_DuplicateSurface(tilesurface);
    if (!rearrsurface)
    {
        std::cerr << "Unable to duplicate tile surface again: " << SDL_GetError() << std::endl;
        return 1;
    }
    std::atexit(free_rearrsurface);

    // Second pass: create a unique tile per row, reverse-scrambled so that it
    // should look its least distorted in the centre of the final image.
    offsetsurface = SDL_DuplicateSurface(windowsurface);
    if (!offsetsurface)
    {
        std::cerr << "Unable to copy offset map: " << SDL_GetError() << std::endl;
        return 1;
    }
    std::atexit(free_offsetsurface);
    SDL_FillRect(windowsurface, nullptr, SDL_MapRGB(windowsurface->format, 0, 0, 0));
    {
        bool init = true;
        for (int row = 0; row < offsetsurface->h; ++row)
        {
            // Sample offsets from tile-width region in the centre of the
            // current row, to create a new tile which should line up with the
            // original image:
            //   - Start with the original tile
            //   - R & G components in the sampled offsets tell us the X and Y
            //     coordinates within the tile that will end up at that point
            //   - Loop over tile, copying each pixel to the given X & Y coordinates
            //   - When reconstructed and sampled in that order... it should reassemble
            //     into something resembling the original image, in the centre!
            SDL_BlitSurface(tilesurface, nullptr, rearrsurface, nullptr);
            int i = row;
            while (i >= tilesurface->h)
                i -= tilesurface->h;
            std::uint32_t* src = reinterpret_cast<std::uint32_t*>(
                    static_cast<std::uint8_t*>(tilesurface->pixels) + (tilesurface->pitch * i));
            std::uint32_t* off = reinterpret_cast<std::uint32_t*>(
                    static_cast<std::uint8_t*>(offsetsurface->pixels) + (offsetsurface->pitch * row))
                    + ((w / 2) - (tilesurface->w / 2));
            for (int x = 0; x < tilesurface->w; ++x, ++off)
            {
                // Grab X & Y offsets from A/R & G/B colour components
                std::uint32_t a = *off;
                a &= offsetsurface->format->Amask;
                a >>= offsetsurface->format->Ashift;
                a <<= offsetsurface->format->Aloss;
                std::uint32_t r = *off;
                r &= offsetsurface->format->Rmask;
                r >>= offsetsurface->format->Rshift;
                r <<= offsetsurface->format->Rloss;
                std::uint32_t xo = r + (a << 8);
                std::uint32_t g = *off;
                g &= offsetsurface->format->Gmask;
                g >>= offsetsurface->format->Gshift;
                g <<= offsetsurface->format->Gloss;
                std::uint32_t b = *off;
                b &= offsetsurface->format->Bmask;
                b >>= offsetsurface->format->Bshift;
                b <<= offsetsurface->format->Bloss;
                std::uint32_t yo = b + (g << 8);
                // Copy from coordinates in original tile to offset pixel in rearranged tile
                std::uint32_t* dst = reinterpret_cast<std::uint32_t*>(
                        static_cast<std::uint8_t*>(rearrsurface->pixels) + (rearrsurface->pitch * yo))
                        + xo;
                *dst = *(src + x);
            }
            // Render the row
            draw(rearrsurface, init, row, cross);
            init = false;
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
